#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <err.h>            /* err */
#include <errno.h>          /* errno */
#include <stdint.h>         /* uint32_t, int64_t */
#include <stdlib.h>         /* exit */
#include <string.h>         /* memcmp */
#include <unistd.h>         /* read, write, unlink, fork, … */
#include <sys/socket.h>     /* socket, AF_* */
#include <sys/un.h>         /* struct sockaddr_un */
#include <arpa/inet.h>      /* ntohl */

/* Naprogramujte proudově orientovaný server, který bude přijímat
 * data od všech klientů, a každou přijatou zprávu přepošle všem
 * (včetně původního odesílatele). Každá zpráva sestává z jednoho
 * libovolně dlouhého řádku ukončeného znakem ‹\n›.
 *
 * Klienti se mohou průběžně připojovat a odpojovat. Server musí
 * reagovat i v situaci, kdy někteří klienti delší dobu data
 * nepřijímají. Čtou-li ovšem data všichni klienti, server musí být
 * schopen libovolně dlouhého provozu v konečně velké paměti.
 * Po navázání spojení potvrdí server klientu připravenost odesláním
 * zprávy ‹"broadcast server ready\n"›.
 *
 * Předchází-li příjem nějaké celistvé zprávy serverem připojení
 * nového klienta, tato zpráva mu již nebude doručena. Naopak, je-li
 * připojení předcházeno doručením znaku konce řádku na server, nově
 * připojený klient obdrží celou příslušnou zprávu.
 *
 * Podprogram ‹broadcast_server› bude tento server realizovat.
 * Vstupem budou:
 *
 *  • ‹sock_fd› je popisovač socketu, který je svázán s adresou a je
 *    nastaven do režimu naslouchání (‹listen›) – tento popisovač je
 *    „vlastněn“ volajícím – podprogram ‹broadcast_server› jej
 *    nebude zavírat, a to ani v případě chyby,
 *  • ‹count› je počet spojení, které ‹broadcast_server› nejvýše
 *    přijme – další klienty již nebude akceptovat, a jakmile se
 *    poslední zbývající klient odpojí, ‹broadcast_server› vrátí
 *    nulu,
 *  • ‹par› je minimální počet souběžných připojení, které musí být
 *    server schopen obsluhovat.
 *
 * Narazí-li ‹broadcast_server› na systémovou chybu, ukončí svou
 * činnost a volajícímu vrátí -1. Výsledek ‹EPIPE› při zápisu za
 * chybu v tomto kontextu nepovažujeme – klienti se mohou odpojit
 * kdykoliv. */

int *fds;
int fd_count;

void *bcast(void *arg) {
    return NULL
}

int broadcast_server(int sock_fd, int count, int par) {
    return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* kill, SIGTERM */
#include <time.h>           /* nanosleep */
#include <sched.h>          /* sched_yield */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
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

static pid_t fork_server(int sock_fd, int clients) {
    pid_t pid = fork();

    if (pid == -1)
        err(2, "fork");

    if (pid == 0) {
        alarm(3);
        exit(broadcast_server(sock_fd, clients, 3) ? 1 : 0);
    }

    close_or_warn(sock_fd, "server socket in client");
    return pid;
}

static const struct sockaddr_un test_addr =
        {
                .sun_family = AF_UNIX,
                .sun_path = "zt.a_socket"
        };

static int client_connect() {
    char banner[] = "broadcast server ready\n";
    char buffer[sizeof banner];

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd == -1)
        err(2, "socket");

    if (connect(fd, (const struct sockaddr *) &test_addr,
                sizeof test_addr) == -1)
        return -1;

    if (recv(fd, buffer, sizeof banner - 1, MSG_WAITALL) == -1)
        return -1;

    if (memcmp(buffer, banner, sizeof banner - 1) != 0)
        return -1;

    return fd;
}

static int client_bcast(int fd, char msg) {
    char buf[2] = {msg, '\n'};
    return send(fd, buf, 2, 0) == -1 ? -1 : 0;
}

static int client_check(int fd, char msg) {
    char result[2];

    if (recv(fd, result, 2, MSG_WAITALL) != 2)
        return -1;

    return result[0] == msg && result[1] == '\n' ? 0 : -1;
}

int main(void) {
    unlink_if_exists(test_addr.sun_path);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sock_fd == -1)
        err(2, "socket");

    if (bind(sock_fd, (const struct sockaddr *) &test_addr,
             sizeof test_addr) == -1)
        err(2, "bind");

    if (listen(sock_fd, 3) == -1)
        err(2, "listen");

    pid_t pid = fork_server(sock_fd, 5);
    sched_yield();

    int c1 = client_connect();
    int c2 = client_connect();

    assert(client_bcast(c1, 'x') == 0);
    assert(client_check(c2, 'x') == 0);
    assert(client_check(c1, 'x') == 0);

    assert(client_bcast(c2, 'y') == 0);
    assert(client_check(c1, 'y') == 0);
    assert(client_check(c2, 'y') == 0);

    close_or_warn(c1, "c1"); /* active: c2 */

    assert(client_bcast(c2, 'z') == 0);
    assert(client_check(c2, 'z') == 0);

    int c3 = client_connect(); /* active: c2, c3 */

    assert(client_bcast(c3, '3') == 0);
    assert(client_check(c3, '3') == 0);
    assert(client_check(c2, '3') == 0);

    assert(client_bcast(c2, '2') == 0);
    assert(client_check(c3, '2') == 0);
    assert(client_check(c2, '2') == 0);

    close_or_warn(c2, "c2"); /* active: c3 */

    int c4 = client_connect(); /* active: c3, c4 */
    int c5 = client_connect(); /* active: c3, c4, c5 */

    assert(client_bcast(c5, '5') == 0);
    assert(client_check(c4, '5') == 0);
    assert(client_check(c3, '5') == 0);
    assert(client_check(c5, '5') == 0);

    assert(client_bcast(c3, '3') == 0);
    assert(client_check(c5, '3') == 0);
    assert(client_check(c4, '3') == 0);
    assert(client_check(c3, '3') == 0);

    assert(client_bcast(c3, 'x') == 0);
    assert(client_check(c4, 'x') == 0);
    assert(client_check(c3, 'x') == 0);
    assert(client_check(c5, 'x') == 0);

    close_or_warn(c5, "c5"); /* active: c3, c4 */
    sched_yield();

    assert(client_bcast(c4, '4') == 0);
    assert(client_check(c4, '4') == 0);
    assert(client_check(c3, '4') == 0);

    assert(client_bcast(c3, '3') == 0);
    assert(client_check(c3, '3') == 0);
    assert(client_check(c4, '3') == 0);

    close_or_warn(c4, "c4"); /* active: c3 */
    close_or_warn(c3, "c3"); /* active: -  */

    assert(reap(pid) == 0);

    unlink_if_exists(test_addr.sun_path);
    return 0;
}
