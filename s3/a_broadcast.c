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
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <poll.h>

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

#define BLOCK_SIZE 128
#define WELCOME_MSG_LEN 23

const char *welcome_msg = "broadcast server ready\n";

enum status {
    NOT_CONNECTED,
    CONNECTED,
    OK,
    ERR
};

struct client {
    int idx;
    int *clients;
    int clients_len;
    int status;
    pthread_t tid;
};

struct buf {
    char *data;
    int len;
    int capacity;
    int msg_len;
};

bool realloc_double(struct buf *buf) {
    buf->capacity *= 2;
    char *tmp = realloc(buf->data, buf->capacity * sizeof(char));
    if (!tmp) {
        return false;
    }
    buf->data = tmp;
    return true;
}

bool bcast(struct client *client, struct buf *buf) {
//    int *current_clients = malloc(client->clients_len * sizeof(int));
//    if (!current_clients) {
//        return false;
//    }
//    for (int i = 0; i < client->clients_len; ++i) {
//        current_clients[i] = client->clients[i];
//    }
//
//    // Copy the clients array
//    memcpy(current_clients, client->clients, client->clients_len * sizeof(int));


    // TODO send copy of clients fd so newly connected clients wont receive old messages
    for (int i = 0; i < client->clients_len; ++i) {
        if (client->clients[i] == -1) {
            continue;
        }
        if (write(client->clients[i], buf->data, buf->msg_len) == -1 && errno != EPIPE) {
            return false;
        }
    }
    memmove(buf->data, buf->data + buf->msg_len, buf->len - buf->msg_len);
    return true;
}

bool reap_thread(struct client *clients, int len, bool wait) {
    bool rv = true;
    int status;
    for (int client = 0; client < len; ++client) {
        status = clients[client].status;
        if (status == OK || status == ERR || (status == CONNECTED && wait)) {
            if (pthread_join(clients[client].tid, NULL) != 0) {
                rv = false;
            }
            clients[client].status = NOT_CONNECTED;
        }
    }
    return rv;
}

void *client_thread(void *arg) {
    int status = ERR;
    struct client *client = arg;
    struct buf buf;
    buf.capacity = BLOCK_SIZE;
    buf.len = 0;
    buf.data = malloc(buf.capacity * sizeof(char));
    if (!buf.data) {
        goto out;
    }
    if (write(client->clients[client->idx], welcome_msg, WELCOME_MSG_LEN) == -1) {
        if (errno == EPIPE) {
            client->status = OK;
        }
        goto out;
    }
    ssize_t nread;
    char *newline;
    while ((nread = read(client->clients[client->idx], buf.data + buf.len, BLOCK_SIZE)) > 0) {
        buf.len += (int) nread;
        if ((newline = memchr(buf.data + buf.len - nread, '\n', nread))) {
            buf.msg_len = newline - buf.data + 1;
            if (!bcast(client, &buf)) {
                goto out;
            }
        }
        if (buf.capacity - buf.len < BLOCK_SIZE && !realloc_double(&buf)) {
            goto out;
        }
    }
    if (nread == -1) {
        goto out;
    }
    status = OK;

    out:

    free(buf.data);
    if (close(client->clients[client->idx]) == -1) status = ERR;
    client->clients[client->idx] = -1;
    client->status = status;
    return NULL;
}

int broadcast_server(int sock_fd, int count, int par) {
    int rv = -1;
    struct client *clients = malloc(par * sizeof(struct client));
    atomic_int *client_fds = malloc(par * sizeof(atomic_int));
    if (!clients || !client_fds) {
        goto out;
    }
    for (int client = 0; client < par; ++client) {
        clients[client].idx = client;
        clients[client].clients_len = par;
        clients[client].clients = client_fds;
        clients[client].status = NOT_CONNECTED;
    }

    for (int conn = 0; conn < count; ++conn) {

    }

    rv = 0;
    out:
    // join all remaining threads
    // memory cleanup
    // close files
    return rv;
}

//bool init_data(struct pollfd **pfds, struct buf **bufs, int len) {
//    *pfds = malloc(len * sizeof(struct pollfd));
//    *bufs = malloc(len * sizeof(struct buf));
//    if (!*pfds || !*bufs) {
//        return false;
//    }
//    for (int i = 0; i < len; ++i) {
//        *pfds[i].fd = -1;
//        pfds[i].events = POLLIN;
//        bufs->data = NULL;
//        bufs->len = 0;
//        bufs->capacity = BLOCK_SIZE;
//    }
//    for (int i = 0; i < len; ++i) {
//        bufs[i].data = malloc(bufs[i].capacity * sizeof(char));
//        if (!bufs[i].data) {
//            goto out;
//        }
//    }
//}

int broadcast_server_2(int sock_fd, int count, int par) {
    int rv = -1;
    struct pollfd *pfds = malloc(par * sizeof(struct pollfd));
    struct buf *bufs = malloc(par * sizeof(struct buf));
    if (!pfds || !bufs) {
        goto out;
    }
    for (int i = 0; i < par; ++i) {
        pfds[i].fd = -1;
        pfds[i].events = POLLIN;
        bufs->data = NULL;
        bufs->len = 0;
        bufs->capacity = BLOCK_SIZE;
    }
    for (int i = 0; i < par; ++i) {
        bufs[i].data = malloc(bufs[i].capacity * sizeof(char));
        if (!bufs[i].data) {
            goto out;
        }
    }

    int connected = 0;
    int pfd;
    for (int conn = 0; conn < count; ++conn) {
        for (pfd = 0; pfd < par; ++pfd) {
            if (pfds[pfd].fd == -1) break;
        }
        if ((pfds[pfd].fd = accept(sock_fd, NULL, NULL)) == -1) {
            goto out;
        }
        if (++connected == par) {

        }
    }

    rv = 0;
    out:
    if (pfds) {
        for (int i = 0; i < par; ++i) {
            if (pfds[i].fd != -1 && close(pfds[i].fd) == -1) rv = -1;
        }
        free(pfds);
    }
    if (bufs) {
        for (int i = 0; i < par; ++i) {
            free(bufs->data);
        }
        free(bufs);
    }
    return rv;
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
