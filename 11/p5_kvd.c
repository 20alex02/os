#define _POSIX_C_SOURCE 200809L

#include <sys/wait.h>   /* waitpid */
#include <sys/socket.h> /* AF_UNIX */
#include <sys/un.h>     /* sockaddr_un */
#include <unistd.h>     /* read, write, close, unlink */
#include <errno.h>      /* errno */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strlen, memcmp */
#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>

#define MSG_SIZE 4
#define BLOCK_SIZE 1024

/* Vaším úkolem je naprogramovat jednoduchý server, který bude
 * poslouchat na unixovém socketu a s klienty bude komunikovat
 * jednoduchým protokolem.
 *
 * Struktura protokolu je velmi jednoduchá: klient bude odesílat
 * „klíče“ ukončené nulovým bajtem. Ke každému klíči, který server
 * přečte, odešle odpověď, která bude obsahovat čtyřbajtovou délku
 * hodnoty, která klíči náleží, následovanou samotnou hodnotou.
 * Není-li klíč přítomen, odešle hodnotu 0xffffffff. Nejvýznamnější
 * bajt je vždy odesílán jako první.
 *
 * V této verzi je potřeba, aby byl server schopen souběžně
 * komunikovat s větším počtem klientů. O reakční době klientů nic
 * nepředpokládejte.
 *
 * Parametry podprogramu ‹kv_server› budou:
 *
 *  • ‹sock_fd› je popisovač socketu, který je svázán s adresou a
 *    nastaven do režimu poslouchání,
 *  • ‹data› je ukazatel na kořen binárního vyhledávacího stromu,
 *    který obsahuje klíče a hodnoty, které jim odpovídají, a podle
 *    kterého bude klientům odpovídat,
 *  • ‹count› je celkový počet klientů, které podprogram ‹kv_server›
 *    obslouží (poté, co se připojí ‹count›-tý klient, další spojení
 *    již nebude přijímat, ale dokončí komunikaci s již připojenými
 *    klienty).
 *
 * Podprogram ‹kv_server› vrátí hodnotu 0 v případě, že bylo úspěšně
 * obslouženo ‹count› klientů, -1 jinak. Neexistence klíče není
 * fatální chybou. */

struct client {
    int fd;
    int rv;
    pthread_t tid;
    bool started;
};

struct kv_tree {
    const char *key;
    const char *data;
    int data_len;
    const struct kv_tree *left, *right;
};

const struct kv_tree *server_data;
const uint32_t MESSAGE = 0xffffffff;

const char *search(const struct kv_tree *root, const char *key, uint32_t *data_len) {
    if (root == NULL) {
        *data_len = 0;
        return NULL;
    }

    int cmp = strcmp(key, root->key);
    if (cmp == 0) {
        *data_len = root->data_len;
        return root->data;
    } else if (cmp < 0) {
        return search(root->left, key, data_len);
    } else {
        return search(root->right, key, data_len);
    }
}

bool realloc_double(char **buf, int *capacity) {
    *capacity *= 2;
    char *tmp = realloc(*buf, *capacity);
    if (!tmp) {
        return false;
    }
    *buf = tmp;
    return true;
}

void *client_thread(void *handle) {
    int rv = -1;
    struct client *client = handle;
    int capacity = BLOCK_SIZE;
    char *buffer = malloc(capacity);
    if (!buffer) {
        client->rv = -1;
        return NULL;
    }
    ssize_t nbytes, offset;
    uint32_t data_len, data_len_net;
    while (true) {
        offset = 0;
        while ((nbytes = read(client->fd, buffer + offset, BLOCK_SIZE)) > 0) {
            if (strchr(buffer + offset, '\0')) {
                break;
            }
            offset += nbytes;
            if (capacity - offset < BLOCK_SIZE && !realloc_double(&buffer, &capacity)) {
                goto err;
            }
        }
//        printf("tid: %lu nbytes: %zd, key: %s\n", client->tid, nbytes, buffer);
        if (nbytes == 0) {
            break;
        }
        if (nbytes == -1) {
            goto err;
        }
        const char *data = search(server_data, buffer, &data_len);
        if (data) {
//            printf("tid: %lu data: %s, data_len: %d\n", client->tid, data, data_len);
            data_len_net = htonl(data_len);
            if (write(client->fd, &data_len_net, MSG_SIZE) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
            if (write(client->fd, data, data_len) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
        } else {
//            printf("tid: %lu key not found\n", client->tid);
            if (write(client->fd, &MESSAGE, MSG_SIZE) == -1) {
//                printf("tid: %lu write failed: %zd\n", client->tid, nbytes);
                goto err;
            }
        }
//        printf("tid: %lu communication done\n", client->tid);
    }
    rv = 0;
    err:
    client->rv = rv;
    free(buffer);
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


int kv_server(int sock_fd, const struct kv_tree *root, int count) {
    int rv = -1;
    server_data = root;
    struct client *clients = malloc(count * sizeof(struct client));
    if (!clients) {
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

//    printf("calling reap\n");
    return reap_threads(clients, count);
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sched.h>      /* sched_yield */
#include <signal.h>     /* signal, SIG_IGN, SIGPIPE */

static const struct sockaddr_un server_addr =
        {
                .sun_family = AF_UNIX,
                .sun_path = "zt.p5_kvd"
        };

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlinking %s", file);
}

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static int fork_server(struct kv_tree *root, int count) {
    int sock_fd;

    if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        err(2, "creating server socket");

    unlink_if_exists(server_addr.sun_path);

    if (bind(sock_fd, (const struct sockaddr *) &server_addr,
             sizeof(server_addr)) == -1)
        err(2, "binding server socket to %s", server_addr.sun_path);

    if (listen(sock_fd, 1) == -1)
        err(2, "listening on %s", server_addr.sun_path);

    pid_t pid = fork();
    alarm(5); /* die after 5s if we get stuck */

    if (pid == -1)
        err(2, "fork");

    if (pid == 0) /* child → server */
    {
        int rv = kv_server(sock_fd, root, count);
        close(sock_fd);
        exit(-rv);
    }

    close_or_warn(sock_fd, "server socket");
    return pid;
}

static void reap_server(int pid) {
    int status;
    if (waitpid(pid, &status, 0) == -1)
        err(2, "collecting server");
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

static int mk_client() {
    int sock_fd;

    if ((sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
        err(2, "creating client socket");

    for (int i = 0; i < 25; ++i)
        if (connect(sock_fd, (const struct sockaddr *) &server_addr, sizeof(server_addr)) == 0)
            break;
        else {
            if (errno != ECONNREFUSED)
                err(2, "connecting client socket");
            sched_yield();
        }

    return sock_fd;
}

static void check_solution(int client_fd, int n,
                           const char **request,
                           const char **expect,
                           const int *expect_sz) {
    char buffer[128];

    for (int i = 0; i < n; i++) {
        int nexp = expect_sz[i];

        assert(write(client_fd, request[i],
                     strlen(request[i]) + 1) != -1);

        int nread = recv(client_fd, buffer, nexp, MSG_WAITALL);

        if (nread == -1)
            err(2, "client read");

        assert(nread == nexp);
        assert(memcmp(buffer, expect[i], nexp) == 0);
    }
}

int main() {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    struct kv_tree
            key_1 = {.key = "blind", .data = "yourself", .data_len = 8,
            .left = NULL, .right = NULL},
            key_2 = {.key = "just", .data = "close", .data_len = 5,
            .left = &key_1, .right = NULL},
            key_3 = {.key = "your", .data = "eyes", .data_len = 4,
            .left = NULL, .right = NULL},
            key_r = {.key = "to", .data = "truth", .data_len = 5,
            .left = &key_2, .right = &key_3};

    const char
            *keys_1[] = {"just"},
            *keys_2[] = {"your", "blind"},
            *keys_3[] = {"to", "lorem", "ipsum"};

    const char
            *values_1[] = {("\x00\x00\x00\x05" "close")},
            *values_2[] = {("\x00\x00\x00\x04" "eyes"),
                           ("\x00\x00\x00\x08" "yourself")},
            *values_3[] = {("\x00\x00\x00\x05" "truth"),
                           ("\xff\xff\xff\xff"),
                           ("\xff\xff\xff\xff")};

    int valsizes_1[] = {9},
            valsizes_2[] = {8, 12},
            valsizes_3[] = {9, 4, 4};

    int pid = fork_server(&key_r, 5);

    int c1 = mk_client();
    check_solution(c1, 1, keys_1, values_1, valsizes_1);
//    printf("<<<DONE 1>>>\n");

    int c2 = mk_client();
    check_solution(c1, 2, keys_2, values_2, valsizes_2);
    check_solution(c2, 2, keys_2, values_2, valsizes_2);
//    printf("<<<DONE 2>>>\n");

    int c3 = mk_client();
    check_solution(c1, 3, keys_3, values_3, valsizes_3);
    check_solution(c2, 3, keys_3, values_3, valsizes_3);
    check_solution(c3, 3, keys_3, values_3, valsizes_3);
//    printf("<<<DONE 3>>>\n");

    close_or_warn(c1, "closing client 1");

    int c4 = mk_client();
    check_solution(c4, 1, keys_1, values_1, valsizes_1);
    check_solution(c4, 2, keys_2, values_2, valsizes_2);
    check_solution(c4, 3, keys_3, values_3, valsizes_3);
//    printf("<<<DONE 4>>>\n");

    close_or_warn(c4, "closing client 4");

    int c5 = mk_client();

    close_or_warn(c2, "closing client 2");
    close_or_warn(c3, "closing client 3");
    close_or_warn(c5, "closing client 5");
//    printf("<<<DONE 5>>>\n");

    reap_server(pid);

    return 0;
}
