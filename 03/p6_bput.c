#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <unistd.h>         /* read, write, fork, close */
#include <stdint.h>         /* uint8_t */
#include <err.h>

#define BLOCK_SIZE 512

/* V této přípravě je Vaším úkolem naprogramovat klient pro
 * jednoduchý spojovaný protokol pro přenos bloků dat. Po připojení
 * klienta server potvrdí připravenost přijímat data jednobajtovou
 * zprávou ‹0x01›.
 *
 * Klient tak může odeslat první 512bajtový blok, načež vyčká na
 * potvrzení od serveru, který odpoví opět jednobajtovou zprávou –
 * ‹0x01› v případě úspěchu a ‹0x02› v případě, že data nemohla být
 * zapsána. Klient vždy před odesláním dalšího bloku vyčká na
 * potvrzení. Konec přenosu klient oznámí zavřením spojení.
 *
 * Procedura ‹bput› obdrží popisovač spojovaného socketu a popisovač
 * souboru, ze kterého bude data načítat. Není-li velikost předaného
 * souboru dělitelná 512, procedura se ihned vrátí s chybou -1 (aniž
 * by jakkoliv komunikovala s protistranou).
 *
 * Aby mohla být data ze souboru efektivně odstraněna, budeme bloky
 * odesílat od konce souboru a každý blok, kterého příjem byl
 * protistranou potvrzen, ze souboru odstraníme voláním ‹ftruncate›.
 *
 * Návratová hodnota ‹bput› bude:
 *
 *  • 0 v případě úspěchu – server potvrdil všechny zprávy a soubor
 *    s daty k odeslání je prázdný,
 *  • -3 je-li předán nesprávný datový soubor,
 *  • -2 v případě, že byla komunikace ukončena kvůli selhání na
 *    straně serveru (server indikoval, že data nebyl schopen
 *    uložit),
 *  • -1 v případě, že nastala chyba komunikace nebo systémová chyba
 *    na straně klienta.
 *
 * Do výstupního parametru ‹block_count› pak uloží počet bloků,
 * které byly úspěšně odeslány a potvrzeny, bez ohledu na celkový
 * výsledek. Popisovač spojení vždy před návratem uzavře, popisovač
 * souboru nikoliv. */

int bput(int sock_fd, int file_fd, int *block_count) {
    int rv = -1;
    off_t file_size = lseek(file_fd, 0, SEEK_END);
    if (file_size % BLOCK_SIZE != 0) {
        goto error;
    }
    *block_count = 0;
    uint8_t resp;
    if (read(sock_fd, &resp, 1) == -1) {
        goto error;
    }
    if (resp == 0x02) {
        rv = -2;
        goto error;
    }
    char buffer[BLOCK_SIZE];
    for (int i = BLOCK_SIZE; i <= file_size; i += BLOCK_SIZE) {
        if (lseek(file_fd, file_size - i, SEEK_SET) == -1 ||
            read(file_fd, buffer, BLOCK_SIZE) == -1 ||
            write(sock_fd, buffer, BLOCK_SIZE) == -1 ||
            read(sock_fd, &resp, 1) == -1) {
            goto error;
        }
        if (resp == 0x02) {
            rv = -2;
            goto error;
        }
        if (ftruncate(file_fd, file_size - i) == -1) {
            goto error;
        }
        ++(*block_count);
    }
    rv = 0;
    error:
    close(sock_fd);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>     /* exit */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
}

static int file_size(int fd) {
    int size = lseek(fd, 0, SEEK_END);

    if (size == -1)
        err(1, "seek");

    return size;
}

static int file_rewind(int fd) {
    int size = lseek(fd, 0, SEEK_SET);

    if (size == -1)
        err(1, "seek");

    return size;
}

static void copy_fd(int sock_fd, int file_fd, int bytes) {
    while (bytes) {
        char buffer[32];
        int nbytes = bytes > 32 ? 32 : bytes;
        int bytes_read = read(sock_fd, buffer, nbytes);

        if (bytes_read == -1)
            err(1, "reading data from client");

        if (bytes_read == 0)
            err(1, "client unexpectedly closed the connection");

        int bytes_written = write(file_fd, buffer, bytes_read);

        if (bytes_written == -1)
            err(1, "server writing to file");
        if (bytes_written != bytes_read)
            errx(1, "short write on the server");

        bytes -= bytes_read;
    }
}

void write_or_die(int fd, const uint8_t *buffer, int nbytes) {
    int bytes_written = write(fd, buffer, nbytes);

    if (bytes_written == -1)
        err(1, "writing %d bytes", nbytes);

    if (bytes_written != nbytes)
        errx(1, "unexpected short write: %d/%d written",
             bytes_written, nbytes);
}

static int reap(pid_t pid) {
    int status;

    if (waitpid(pid, &status, 0) == -1)
        err(2, "wait");

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return -1;
}

static int create_file(const char *name) {
    unlink_if_exists(name);
    int fd;

    if ((fd = open(name,
                   O_CREAT | O_TRUNC | O_RDWR, 0666)) == -1)
        err(2, "creating %s", name);

    return fd;
}

static int fork_server(int *client_fd, int *file_fd,
                       void( *server )(int, int)) {
    const char *file_name = "zt.p6_server.bin";
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        err(1, "socketpair");

    int server_fd = fds[0];
    *client_fd = fds[1];
    *file_fd = create_file(file_name);

    int server_pid = fork();

    if (server_pid == -1)
        err(1, "fork");

    if (server_pid == 0) {
        close_or_warn(*client_fd, "client end of the socket");
        server(server_fd, *file_fd);
        close_or_warn(*file_fd, file_name);
        close_or_warn(server_fd, "server end of the socket");
        exit(0);
    }

    close_or_warn(server_fd, "server end of the socket");
    return server_pid;
}

static void server_1(int sock_fd, int file_fd) {
    uint8_t status = 1;

    write_or_die(sock_fd, &status, 1);
    copy_fd(sock_fd, file_fd, 512);
    write_or_die(sock_fd, &status, 1);
    copy_fd(sock_fd, file_fd, 512);
    write_or_die(sock_fd, &status, 1);

    assert(read(sock_fd, &status, 1) == 0);
}

static void server_2(int sock_fd, int file_fd) {
    uint8_t status = 1;

    write_or_die(sock_fd, &status, 1);
    copy_fd(sock_fd, file_fd, 512);
    write_or_die(sock_fd, &status, 1);
    copy_fd(sock_fd, file_fd, 512);

    status = 2;
    write_or_die(sock_fd, &status, 1);
    assert(read(sock_fd, &status, 1) == 0);
}

static void server_3(int sock_fd, int file_fd) {
    (void) file_fd;
    uint8_t status = 1;
    assert(read(sock_fd, &status, 1) == 0);
}

int main() {
    const char *cfile_name = "zt.p6_client.bin";
    uint8_t buffer[2048] = {0};
    int client_fd, server_pid, block_count;
    int cfile_fd, sfile_fd, null_fd_wr, null_fd_rd;
    int bytes_read;

    buffer[13] = 1;

    cfile_fd = create_file(cfile_name);
    write_or_die(cfile_fd, buffer, 1024);
    file_rewind(cfile_fd);

    null_fd_rd = open("/dev/null", O_RDONLY);
    null_fd_wr = open("/dev/null", O_WRONLY);

    if (null_fd_wr == -1 || null_fd_rd == -1)
        err(1, "opening /dev/null");

    /* case 1 */
    server_pid = fork_server(&client_fd, &sfile_fd, server_1);
    assert(bput(client_fd, cfile_fd, &block_count) == 0);
    assert(block_count == 2);
    assert(reap(server_pid) == 0);

    /* check and reset */
    bytes_read = pread(sfile_fd, buffer, 2048, 0);
    assert(bytes_read == 1024);
    assert(buffer[525] == 1);
    assert(file_size(cfile_fd) == 0);
    close_or_warn(sfile_fd, "server data file");

    /* case 2 */
    write_or_die(cfile_fd, buffer, 1024);
    file_rewind(cfile_fd);

    server_pid = fork_server(&client_fd, &sfile_fd, server_2);
    assert(bput(client_fd, cfile_fd, &block_count) == -2);
    assert(block_count == 1);
    assert(reap(server_pid) == 0);
    assert(file_size(cfile_fd) == 512);
    close_or_warn(sfile_fd, "server data file");

    /* case 3 & 4 */
    assert(bput(null_fd_wr, cfile_fd, &block_count) == -1);

    server_pid = fork_server(&client_fd, &sfile_fd, server_3);
    write_or_die(cfile_fd, buffer, 13);
    assert(bput(client_fd, cfile_fd, &block_count) == -1);
    assert(reap(server_pid) == 0);
    close_or_warn(sfile_fd, "server data file");

    close_or_warn(null_fd_rd, "/dev/null");
    close_or_warn(cfile_fd, cfile_name);
    return 0;
}
