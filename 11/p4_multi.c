#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <err.h>            /* err */
#include <errno.h>          /* errno */
#include <stdint.h>         /* uint32_t, int64_t */
#include <stdlib.h>         /* exit */
#include <unistd.h>         /* read, write, close, unlink, … */
#include <sys/socket.h>     /* socket, AF_* */
#include <sys/un.h>         /* struct sockaddr_un */
#include <arpa/inet.h>      /* ntohl */
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

/* Napište podprogram ‹multi_server›, který bude pracovat podobně
 * jako ‹memo_server› z dřívější přípravy, s tím rozdílem, že bude
 * mít několik paměťových buněk. Přijme tyto parametry:
 *
 *  1. ‹sock_fd› je popisovač proudového socketu, který je svázán
 *     s adresou a je nastaven do režimu poslouchání,
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *  3. ‹initial› je ukazatel na počáteční obsah paměti,
 *  4. ‹size› je počet 32bitových buněk paměti.
 *
 * Server implementuje následující protokol:
 *
 *  • klient může kdykoliv odeslat šestibajtovou zprávu,
 *  • první dva bajty určují index buňky, se kterou chce klient
 *    pracovat (významnější bajt je odeslán první),
 *  • je-li zbytek zprávy ‹0xffff'ffff›, server odpoví aktuální
 *    hodnotou požadované buňky, nebo ‹0xffff'ffff› je-li požadovaný
 *    index větší nebo roven ‹size›,
 *  • jinak je zbytek zprávy nová hodnota, kterou server do vybrané
 *    buňky uloží, a klientu tuto akci potvrdí odesláním nové
 *    hodnoty, nebo zprávou ‹0xffff'ffff› je-li požadovaný index
 *    neplatný,
 *  • uzavření spojení iniciuje vždy klient.
 *
 * Hodnoty buněk jsou odesílány nejvýznamnějším bajtem napřed.
 * Předchází-li potvrzení změny stavu nějakému dotazu na tentýž
 * index, odpověď na tento dotaz musí korespondovat takto potvrzené
 * změně (tzn. buď je odpověď takto potvrzený stav, nebo nějaký
 * novější).
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */

#define RECV_SIZE 6
#define SEND_SIZE 4

struct client {
    int fd;
    int rv;
    pthread_t tid;
    bool started;
};

uint32_t *server_data;
int data_size;
const uint32_t MESSAGE = 0xffffffff;

void *client_thread(void *handle) {
    int rv = -1;
    struct client *client = handle;
    uint8_t buffer[RECV_SIZE];
    uint32_t data;
    uint16_t cell;
    ssize_t nbytes, offset;
    while (1) {
        offset = 0;
        while (offset < RECV_SIZE && (nbytes = read(client->fd, buffer + offset, RECV_SIZE - offset)) > 0) {
            offset += nbytes;
        }
//        printf("tid: %lu nbytes: %zd, offset: %zd\n", client->tid, nbytes, offset);
        if (nbytes == 0) {
            break;
        }
        if (nbytes == -1) {
            goto err;
        }
//        cell = buffer[1] | (buffer[0] << 8);
        cell = ntohs(*((uint16_t *) buffer));
//        data = buffer[5] | (buffer[4] << 8) | (buffer[3] << 16) | (buffer[2] << 24);
        data = ntohl(*((uint32_t *) (buffer + 2)));
//        printf("tid: %lu cell: %hu, data: %u\n", client->tid, cell, data);
        if (cell >= data_size) {
//            printf("tid: %lu invalid cell: %d\n", client->tid, cell);
            if (write(client->fd, &MESSAGE, SEND_SIZE) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
            continue;
        }
        if (data != MESSAGE) {
//            printf("tid: %lu updating cell[%d]: %d\n", client->tid, cell, data);
            server_data[cell] = data;
//            printf("tid: %lu cell[%d] updated\n", client->tid, cell);
        }

//        printf("tid: %lu sending cell[%d]: %d\n", client->tid, cell, server_data[cell]);
        data = htonl(server_data[cell]);
        if (write(client->fd, &data, sizeof(uint32_t)) == -1) {
//            printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
            goto err;
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

int multi_server(int sock_fd, int count, const uint32_t *initial, int size) {
    int rv = -1;
    server_data = malloc(size * sizeof(uint32_t));
    if (!server_data) {
        return rv;
    }
    memcpy(server_data, initial, size * sizeof(uint32_t));
    data_size = size;
//    for (int i = 0; i < size; ++i) {
//        printf("server: %d, initail: %d\n", server_data[i], initial[i]);
//    }

    struct client *clients = malloc(count * sizeof(struct client));
    if (!clients) {
        free(server_data);
        server_data = NULL;
        return rv;
    }
    for (int client = 0; client < count; ++client) {
        clients[client].started = false;
        clients[client].fd = -1;
    }
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

    free(server_data);
    server_data = NULL;
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

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return -1;
}

static pid_t fork_server(int sock_fd, int clients,
                         uint32_t *init, int size) {
    pid_t pid = fork();

    if (pid == -1)
        err(2, "fork");

    if (pid == 0) {
        alarm(3);
        exit(multi_server(sock_fd, clients, init, size) ? 1 : 0);
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

struct client_msg {
    uint16_t idx;
    uint32_t val;
} __attribute__(( packed ));

static int client_set(int fd, uint16_t idx, uint32_t val) {
    struct client_msg msg =
            {
                    .idx = htons(idx),
                    .val = htonl(val)
            };

    uint32_t reply;

    if (send(fd, &msg, sizeof msg, 0) == -1) {
//        printf("client_set: send failed\n");
        return -1;
    }

    if (recv(fd, &reply, 4, MSG_WAITALL) != 4 ||
        reply != msg.val) {
//        printf("client_set: recv failed, reply: %d, expected: %d\n", reply, msg.val);
        return -1;
    }

    return 0;
}

static int client_get(int fd, uint16_t idx) {
    struct client_msg msg =
            {
                    .idx = htons(idx),
                    .val = -1
            };

    uint32_t reply;

    if (send(fd, &msg, sizeof msg, 0) == -1)
        return -1;

    if (recv(fd, &reply, sizeof reply, MSG_WAITALL) == -1)
        return -1;

    return ntohl(reply);
}

int main(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    char buffer[4];
    uint32_t init[2] = {33, 44};

    unlink_if_exists(test_addr.sun_path);

    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sock_fd == -1)
        err(2, "socket");

    if (bind(sock_fd, (const struct sockaddr *) &test_addr,
             sizeof test_addr) == -1)
        err(2, "bind");

    if (listen(sock_fd, 3) == -1)
        err(2, "listen");

    pid_t pid = fork_server(sock_fd, 3, init, 2);

    int c1 = client_connect();
    int c2 = client_connect();

    assert(client_get(c1, 0) == 33);
    assert(client_get(c2, 1) == 44);
    assert(client_set(c1, 0, 77) == 0);
    assert(client_get(c2, 0) == 77);
    assert(client_get(c1, 0) == 77);
    assert(client_get(c1, 1) == 44);

    int c3 = client_connect();

    assert(send(c3, "\0", 1, 0) == 1);
    assert(client_set(c2, 1, 1077) == 0);
    assert(client_get(c1, 1) == 1077);

    assert(send(c3, "\0", 1, 0) == 1);
    assert(client_set(c1, 1, 7077) == 0);
    assert(client_get(c2, 1) == 7077);

    assert(send(c3, "\0\2", 2, 0) == 2);
    assert(client_set(c1, 1, 1300 * 7077) == 0);
    assert(client_get(c2, 1) == 1300 * 7077);

    close_or_warn(c1, "c1");

    assert(send(c3, "\1\0", 2, 0) == 2);
    assert(recv(c3, buffer, 1, 0) == 1);
    assert(client_get(c2, 0) == 2 * 65536 + 256);
    assert(client_get(c2, 1) == 1300 * 7077);

    close_or_warn(c2, "c2");
    close_or_warn(c3, "c3");

    assert(reap(pid) == 0);

    unlink_if_exists(test_addr.sun_path);
    return 0;
}
