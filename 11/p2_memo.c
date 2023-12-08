#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <err.h>            /* err */
#include <errno.h>          /* errno */
#include <stdlib.h>         /* exit */
#include <unistd.h>         /* read, write, close, unlink, … */
#include <sys/socket.h>     /* socket, AF_* */
#include <sys/un.h>         /* struct sockaddr_un */
#include <arpa/inet.h>      /* ntohl */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdbool.h>

#define RECV_SIZE 4

/* Napište podprogram ‹memo_server›, který přijme 3 parametry:
 *
 *  1. ‹sock_fd› je popisovač proudového socketu, který je svázán
 *     s adresou a je nastaven do režimu poslouchání,
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *  3. ‹initial› je počáteční hodnota stavu.
 *
 * Server implementuje následující protokol:
 *
 *  • klient může kdykoliv odeslat čtyřbajtovou zprávu,
 *  • je-li zpráva od klienta ‹0xffff'ffff›, server odpoví svým
 *    aktuálním stavem,
 *  • jinak nastaví stav na hodnotu, kterou od klienta obdržel a
 *    změnu klientu potvrdí zprávou ‹0xffff'ffff›,
 *  • uzavření spojení iniciuje vždy klient.
 *
 * Veškeré zprávy jsou odesílány nejvýznamnějším bajtem napřed.
 * Předchází-li potvrzení změny stavu nějakému dotazu, odpověď na
 * tento dotaz musí korespondovat takto potvrzené změně (tzn. buď je
 * odpověď takto potvrzený stav, nebo nějaký novější).
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */


struct client {
    int fd;
    atomic_int rv;
    pthread_t tid;
    bool started;
};

atomic_uint_least32_t state;
const uint32_t MESSAGE = 0xffffffff;

void *client_thread(void *data) {
    int rv = -1;
    struct client *client = data;
    uint8_t buffer[RECV_SIZE];
    uint32_t msg;
    ssize_t nbytes, offset;
    while (1) {
        offset = 0;
        while (offset < 4 && (nbytes = read(client->fd, buffer + offset, RECV_SIZE - offset)) > 0) {
            offset += nbytes;
        }
//        printf("tid: %lu nbytes: %zd, offset: %zd\n", client->tid, nbytes, offset);
        if (nbytes == 0) {
            break;
        }
        if (nbytes == -1) {
            perror("read failed");
            goto err;
        }
        msg = buffer[3] | (buffer[2] << 8) | (buffer[1] << 16) | (buffer[0] << 24);
        if (msg != MESSAGE) {
            state = msg;
            if (write(client->fd, &MESSAGE, RECV_SIZE) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
        } else {
            msg = htonl(state);
            if (write(client->fd, &msg, RECV_SIZE) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
        }
//        printf("tid: %lu communication done\n", client->tid);
    }
    rv = 0;
    err:
    client->rv = rv;
    return NULL;
}

static int reap_threads(struct client *clients, int count) {
    int rv = 0;
    for (int client = 0; client < count; ++client) {
        if (!clients[client].started) {
            if (clients[client].fd != -1 && close(clients[client].fd) == -1) {
                rv = -1;
            }
//            printf("thread hasnt started\n");
            continue;
        }
//        printf("withing for thread\n");
        if (pthread_join(clients[client].tid, NULL) != 0) {
//            printf("withing for thread failed\n");
            rv = -1;
        }
//        printf("closing fd\n");
        if (clients[client].fd != -1 && close(clients[client].fd) == -1) {
//            printf("closing failed\n");
            rv = -1;
        }
//        printf("client rv: %d\n", clients[client].rv);
        if (clients[client].rv != 0) {
            rv = -1;
        }
    }
    free(clients);
//    printf("reap rv: %d\n", rv);
    return rv;
}

int memo_server(int sock_fd, int count, uint32_t initial) {
    int rv = -1;
    struct client *clients = malloc(count * sizeof(struct client));
    if (!clients) {
        return rv;
    }
    for (int client = 0; client < count; ++client) {
        clients[client].started = false;
        clients[client].fd = -1;
    }
    state = initial;
    for (int client = 0; client < count; ++client) {
        if ((clients[client].fd = accept(sock_fd, NULL, NULL)) == -1) {
            break;
        }
//        printf("client connected\n");
        if (pthread_create(&clients[client].tid, NULL, client_thread, &clients[client]) != 0) {
            break;
        }
        clients[client].started = true;
//        printf("thread %d started, tid: %lu\n", client, clients[client].tid);
    }
//    printf("calling reap\n");
    return reap_threads(clients, count);
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* signal, SIG_IGN, SIGPIPE */

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

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return -1;
    }
}

static pid_t fork_server(int sock_fd, int clients, uint32_t init) {
    pid_t pid = fork();

    if (pid == -1)
        err(2, "fork");

    if (pid == 0) {
        alarm(3);
        exit(memo_server(sock_fd, clients, init) ? 1 : 0);
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
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (fd == -1)
        err(2, "socket");

    if (connect(fd, (const struct sockaddr *) &test_addr,
                sizeof test_addr) == -1)
        return -1;

    return fd;
}

static int client_set(int fd, uint32_t val) {
    val = htonl(val);

    if (send(fd, &val, 4, 0) == -1)
        return -1;

    if (recv(fd, &val, 4, MSG_WAITALL) != 4 ||
        val != 0xffffffff) {
        printf("val: %d\n", val);
        return -1;
    }

    return 0;
}

static int client_get(int fd) {
    uint32_t msg = 0xffffffff;

    if (send(fd, &msg, 4, 0) == -1)
        return -1;

    if (recv(fd, &msg, 4, MSG_WAITALL) != 4)
        return -1;

    return ntohl(msg);
}

int main(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    char buffer[4];
    unlink_if_exists(test_addr.sun_path);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sock_fd == -1)
        err(2, "socket");

    if (bind(sock_fd, (const struct sockaddr *) &test_addr,
             sizeof test_addr) == -1)
        err(2, "bind");

    if (listen(sock_fd, 3) == -1)
        err(2, "listen");

    pid_t pid = fork_server(sock_fd, 3, 33);

    int c1 = client_connect();
    int c2 = client_connect();
    assert(client_get(c1) == 33);
    assert(client_get(c2) == 33);
    assert(client_set(c1, 77) == 0);
    assert(client_get(c2) == 77);
    assert(client_get(c1) == 77);

    int c3 = client_connect();

    assert(send(c3, "\0", 1, 0) == 1);
    assert(client_set(c2, 1077) == 0);
    assert(client_get(c1) == 1077);

    assert(send(c3, "\0", 1, 0) == 1);
    assert(client_set(c1, 7077) == 0);
    assert(client_get(c2) == 7077);

    assert(send(c3, "\1", 1, 0) == 1);
    assert(client_set(c1, 1300 * 7077) == 0);
    assert(client_get(c2) == 1300 * 7077);

    close_or_warn(c1, "c1");

    assert(send(c3, "\0", 1, 0) == 1);
    assert(recv(c3, buffer, 1, 0) == 1);
    assert(client_get(c2) == 256);

    close_or_warn(c2, "c2");
    close_or_warn(c3, "c3");

    assert(reap(pid) == 0);

    unlink_if_exists(test_addr.sun_path);
    return 0;
}
