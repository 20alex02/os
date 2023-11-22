#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sched.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>

#define BLOCK_SIZE 128

/* Uvažme proudový protokol z předchozí přípravy. Vaším úkolem bude
 * naprogramovat klient, který ověří, zda je hodnota klíče na
 * serveru rovna nějaké očekávané. Protože hodnoty mohou být velké,
 * je očekávaná hodnota klientu předána jako soubor (resp.
 * popisovač, který odkazuje na obyčejný soubor).
 *
 * Pro připomenutí, požadavek klienta sestává z nulou ukončeného
 * klíče (budeme odesílat pouze jeden), odpověď serveru pak
 * z čtyřbajtové velikosti hodnoty a pak samotné hodnoty.
 * Nejvýznamnější bajt velikosti je odeslán jako první.
 *
 * Návratová hodnota bude:
 *
 *  • 0 je-li vše v pořádku (hodnota odpovídá očekávání),
 *  • -1 došlo-li k chybě komunikace nebo jiné fatální chybě,
 *  • -2 nebyl-li požadovaný klíč na serveru přítomen,
 *  • -3 jestli byla hodnota přijata, ale neodpovídá očekávání. */

bool realloc_double(uint8_t **string, int *capacity) {
    *capacity *= 2;
    uint8_t *tmp = realloc(*string, *capacity);
    if (!tmp) {
        return false;
    }
    *string = tmp;
    return true;
}

int kvchk(int server_fd, const char *key, int data_fd) {
    int rv = -1;
    uint8_t *buffer = NULL;
    uint8_t *data_buf = NULL;
    if (write(server_fd, key, strlen(key)) == -1) {
        return -1;
    }
    int capacity = BLOCK_SIZE;
    if (!(buffer = malloc(capacity))) {
        goto error;
    }
    int32_t offset = 0;
    ssize_t nread;
    while ((nread = read(data_fd, buffer + offset, BLOCK_SIZE)) > 0) {
        offset += nread;
        if (capacity - offset < BLOCK_SIZE && !realloc_double(&buffer, &capacity)) {
            goto error;
        }
    }
    if (nread == -1) {
        goto error;
    }
    if (!(data_buf = malloc(offset + 1))) {
        goto error;
    }
    int a;
    if ((a = read(data_fd, data_buf, offset + 1)) != offset) {
        return -1;
    }
    rv = memcmp(buffer, data_buf, offset) == 0 ? 0 : -3;
    error:
    free(buffer);
    free(data_buf);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */



static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
}

static void write_or_die(int fd, const uint8_t *buffer, int nbytes) {
    int bytes_written = write(fd, buffer, nbytes);

    if (bytes_written == -1 && errno == EPIPE)
        return;

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

static int fork_server(int *client_fd, void( *server )(int), int close_fd) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        err(1, "socketpair");

    int server_fd = fds[0];
    *client_fd = fds[1];

    int server_pid = fork();

    if (server_pid == -1)
        err(1, "fork");

    if (server_pid == 0) {
        close_or_warn(close_fd, "client state file");
        close_or_warn(*client_fd, "client end of the socket");
        server(server_fd);
        close_or_warn(server_fd, "server end of the socket");
        exit(0);
    }

    close_or_warn(server_fd, "server end of the socket");
    return server_pid;
}

static void expect(int fd, const char *str) {
    char buffer[128];

    do {
        int len = strlen(str) > 128 ? 128 : strlen(str) + 1;
        int bytes = read(fd, buffer, len);

        if (bytes == -1)
            err(1, "reading data from client");

        assert(memcmp(str, buffer, bytes) == 0);
        str += bytes;
    } while (str[-1] != 0);
}

static void server_1(int fd) {
    expect(fd, "key_1");
    uint8_t buffer[512] = {0};
    uint32_t size = htonl(512);
    write_or_die(fd, (uint8_t *) &size, 4);
    write_or_die(fd, buffer, 512);
    assert(read(fd, buffer, 1) == 0);
}

static void server_2(int fd) {
    expect(fd, "key_2");
    uint8_t buffer[300] = {0, 0, 0, 0, 7, 3};
    uint32_t size = htonl(296);
    memcpy(buffer, &size, 4);

    for (int i = 0; i < 300; i += 3) {
        write_or_die(fd, buffer + i, 3);
        sched_yield();
    }

    assert(read(fd, buffer, 1) == 0);
}

static void server_3(int fd) {
    char buffer;
    read(fd, &buffer, 1); /* just wait for close */
}

static int file_rewind(int fd) {
    int size = lseek(fd, 0, SEEK_SET);

    if (size == -1)
        err(1, "seek");

    return size;
}

int main() {
    const char *expect_name = "zt.p5_expect.bin";
    uint8_t buffer[2048] = {0};
    int client_fd, server_pid;
    int file_fd, null_fd_wr, null_fd_rd;

    signal(SIGPIPE, SIG_IGN);

    file_fd = create_file(expect_name);
    null_fd_rd = open("/dev/null", O_RDONLY);
    null_fd_wr = open("/dev/null", O_WRONLY);

    if (null_fd_wr == -1 || null_fd_rd == -1)
        err(1, "opening /dev/null");

    /* case 1 */
    write_or_die(file_fd, buffer, 512);
    file_rewind(file_fd);
    server_pid = fork_server(&client_fd, server_1, file_fd);
    assert(kvchk(client_fd, "key_1", file_fd) == 0);
    close_or_warn(client_fd, "client side of the socket");
    assert(reap(server_pid) == 0);

    if (ftruncate(file_fd, 0) != 0)
        err(1, "truncating %s", expect_name);

    /* case 2 */
    buffer[0] = 7;
    buffer[1] = 3;
    file_rewind(file_fd);
    write_or_die(file_fd, buffer, 296);
    file_rewind(file_fd);
    server_pid = fork_server(&client_fd, server_2, file_fd);
    assert(kvchk(client_fd, "key_2", file_fd) == 0);
    close_or_warn(client_fd, "client side of the socket");
    assert(reap(server_pid) == 0);

    if (ftruncate(file_fd, 0) != 0)
        err(1, "truncating %s", expect_name);

    /* case 3 */
    file_rewind(file_fd);
    write_or_die(file_fd, buffer, 305);
    file_rewind(file_fd);
    server_pid = fork_server(&client_fd, server_2, file_fd);
    assert(kvchk(client_fd, "key_2", file_fd) == -2);
    close_or_warn(client_fd, "client side of the socket");
    assert(reap(server_pid) == 0);

    if (ftruncate(file_fd, 0) != 0)
        err(1, "truncating %s", expect_name);

    /* case 4 */
    file_rewind(file_fd);
    write_or_die(file_fd, buffer, 200);
    file_rewind(file_fd);
    server_pid = fork_server(&client_fd, server_2, file_fd);
    assert(kvchk(client_fd, "key_2", file_fd) == -2);
    close_or_warn(client_fd, "client side of the socket");
    assert(reap(server_pid) == 0);

    /* case 5 */
    server_pid = fork_server(&client_fd, server_3, file_fd);
    assert(kvchk(client_fd, "foo", null_fd_wr) == -1);
    close_or_warn(client_fd, "client side of the socket");
    assert(reap(server_pid) == 0);

    /* case 6 */
    assert(kvchk(null_fd_wr, "foo", file_fd) == -1);

    close_or_warn(file_fd, expect_name);
    close_or_warn(null_fd_rd, "/dev/null");
    close_or_warn(null_fd_wr, "/dev/null");
    unlink_if_exists(expect_name);

    return 0;
}
