#define _POSIX_C_SOURCE 200809L

#include <stdio.h>      /* write, read */
#include <fcntl.h>      /* openat, O_* */
#include <string.h>     /* strlen */
#include <unistd.h>     /* close, open */
#include <arpa/inet.h>  /* htonl, ntohl */
#include <errno.h>      /* errno */
#include <stdlib.h>     /* exit */
#include <err.h>
#include <assert.h>
#include <limits.h>
#include <stdbool.h>

#define BLOCK_SIZE 128

/* Vaším úkolem je tentokrát naprogramovat jednoduchý server, který
 * bude komunikovat prostřednictvím proudového socketu s jedním
 * klientem. Struktura protokolu je velmi jednoduchá: klient bude
 * odesílat „klíče“ ukončené nulovým bajtem. Ke každému klíči, který
 * server přečte, odešle odpověď, která bude obsahovat čtyřbajtovou
 * délku hodnoty, která klíči náleží, následovanou samotnou
 * hodnotou. Není-li klíč přítomen, odešle hodnotu 0xffffffff.
 * Nejvýznamnější bajt je vždy odesílán jako první.
 *
 * Krom nulového bajtu (který slouží jako oddělovač) nebudou klíče
 * obsahovat ani znak lomítka ‹/›. Klíče a hodnoty jsou uloženy
 * v souborovém systému, ve složce předané podprogramu ‹kvsd›
 * popisovačem ‹root_fd›. Klíč je název souboru, hodnota je pak jeho
 * obsah.
 *
 * Podprogram ‹kvsd› vrátí hodnotu -1 v případě fatální chyby, jinak
 * hodnotu 0. Neexistence klíče není fatální chybou (viz výše), jiné
 * chyby při otevírání souboru ale ano. */

bool realloc_double(char **string, int *capacity) {
    *capacity *= 2;
    char *tmp = realloc(*string, *capacity * sizeof(char));
    if (!tmp) {
        return false;
    }
    *string = tmp;
    return true;
}

//int get_delim(const char *buffer, int start, int end, char delim) {
//    for (; start < end; ++start) {
//        if (buffer[start] == delim) {
//            return start;
//        }
//    }
//    return -1;
//}

int get_key_names(int client_fd, char **buffer, int *buff_size) {
    int offset = 0;
    int nread;
    while ((nread = read(client_fd, *buffer + offset, BLOCK_SIZE)) > 0) {
        offset += nread;
        if (*buff_size - offset < BLOCK_SIZE && !realloc_double(buffer, buff_size)) {
            return -1;
        }
    }
    if (nread == -1) {
        return -1;
    }
    return offset;
}

int read_key(int fd, char **buffer, int *capacity) {
    int nread;
    int offset = 0;
    while ((nread = read(fd, *buffer + offset, BLOCK_SIZE)) > 0) {
        offset += nread;
        if (*capacity - offset < BLOCK_SIZE && !realloc_double(buffer, capacity)) {
            return -1;
        }
    }
    if (nread == -1) {
        return -1;
    }
    return offset;
}

int get_key_count(const char *key_names, int len) {
    int counter = 0;
    for (int i = 0; i < len; ++i) {
        if (key_names[i] == '\0') {
            ++counter;
        }
    }
    return counter;
}

int kvsd(int root_fd, int client_fd) {
    int rv = -1;
    const uint32_t NOT_FOUND = 0xffffffff;
    int key_names_capacity = BLOCK_SIZE;
    char *key_names = malloc(key_names_capacity * sizeof(char));
    int key_names_len;
    int key_count;
    int key_names_offset = 0;

    int capacity = BLOCK_SIZE;
    char *key_content = malloc(capacity * sizeof(char));
    if (!key_content) {
        return -1;
    }
    int32_t key_len;
    uint32_t key_len_net;

    int key_fd;
    while ((key_names_len = get_key_names(client_fd, &key_names, &key_names_capacity)) > 0) {
        key_count = get_key_count(key_names, key_names_len);
        for (int key = 0; key < key_count; ++key) {
            key_fd = openat(root_fd, &key_names[key_names_offset], O_RDONLY);
            if (key_fd == -1) {
                if (errno == ENOENT && write(client_fd, &NOT_FOUND, sizeof(NOT_FOUND)) != -1) {
                    continue;
                }
                goto error;
            }
            if ((key_len = read_key(key_fd, &key_content, &capacity)) == -1) {
                goto error;
            }
            close(key_fd);
        }
        if (key_fd == -3) {
            if (write(client_fd, &NOT_FOUND, sizeof(NOT_FOUND)) == -1) {
                goto error;
            }
            continue;
        }
        if ((key_len = read_key(key_fd, &key, &capacity)) == -1) {
            goto error;
        }
        key_len_net = htonl(key_len);
        if (write(client_fd, &key_len_net, sizeof(key_len_net)) == -1) {
            goto error;
        }
        if (write(client_fd, key, key_len) == -1) {
            goto error;
        }
        close(key_fd);
//        key_fd = -1;
    }
    if (key_fd == -1) {
        goto error;
    }
    rv = 0;
    error:
    free(key);
    free(key_names);
    if (key_fd > 0) close(key_fd);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/stat.h>       /* mkdir */
#include <sys/socket.h>     /* socketpair */
#include <sys/wait.h>       /* waitpid */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(int dir_fd, const char *file) {
    if (unlinkat(dir_fd, file, 0) == -1 && errno != ENOENT)
        err(2, "unlink %s", file);
}

static void mkdir_or_die(const char *dirname) {
    if (mkdir(dirname, 0755) == -1 && errno != EEXIST)
        err(2, "mkdir");
}

static void prepare_file(int dir_fd, const char *name,
                         const char *content) {
    unlink_if_exists(dir_fd, name);
    int fd = openat(dir_fd, name, O_CREAT | O_WRONLY, 0755);
    if (fd == -1)
        err(2, "creating input file");
    if (write(fd, content, strlen(content)) == -1)
        err(2, "writing input file");
    close(fd);
}

static int fork_client(const char *to_write, int w_bytes,
                       const char *expect, int e_bytes,
                       int *client_fd) {
    int fds[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1)
        err(1, "socketpair");

    pid_t pid = fork();
//    alarm(5);

    if (pid == -1)
        err(2, "fork");

    if (pid == 0) // child → client
    {
        close(fds[0]);

        if (write(fds[1], to_write, w_bytes) == -1)
            err(2, "client write");

        char buf[e_bytes];
        int total = 0, nread;

        while ((nread = read(fds[1], buf + total,
                             e_bytes - total)) > 0)
            total += nread;

        if (nread == -1)
            err(2, "client read");

        assert(total == e_bytes);
        assert(memcmp(buf, expect, e_bytes) == 0);

        close(fds[1]);
        exit(0);
    }

    close(fds[1]);
    *client_fd = fds[0];
    return pid;
}

static int reap(pid_t pid) {
    int status;

    if (waitpid(pid, &status, 0) == -1)
        err(2, "waiting for %d", pid);

    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return -1;
}

int main() {
    const char *dir_path = "zt.p4_kvsd";

    mkdir_or_die(dir_path);
    int dir_fd = open(dir_path, O_DIRECTORY);
    if (dir_fd == -1) err(2, "opening directory");

    prepare_file(dir_fd, "key1", "a");
    prepare_file(dir_fd, "key2", "bb");
    prepare_file(dir_fd, "key3", "ccc");
    prepare_file(dir_fd, "empty", "");

    int pid, sock_fd;

    pid = fork_client("key1\0", 5, "\0\0\0\01a", 5, &sock_fd);
    assert(kvsd(dir_fd, sock_fd) == 0);
    assert(reap(pid) == 0);
    close_or_warn(sock_fd, "server side of the socket");

    pid = fork_client("key2\0key3\0", 10,
                      "\0\0\0\02bb\0\0\0\03ccc", 13, &sock_fd);
    assert(kvsd(dir_fd, sock_fd) == 0);
    assert(reap(pid) == 0);
    close_or_warn(sock_fd, "server side of the socket");

    pid = fork_client("empty\0", 6, "\0\0\0\0", 4, &sock_fd);
    assert(kvsd(dir_fd, sock_fd) == 0);
    assert(reap(pid) == 0);
    close_or_warn(sock_fd, "server side of the socket");

    pid = fork_client("nope\0", 5, "\xff\xff\xff\xff", 4, &sock_fd);
    assert(kvsd(dir_fd, sock_fd) == 0);
    assert(reap(pid) == 0);
    close_or_warn(sock_fd, "server side of the socket");

    close_or_warn(dir_fd, dir_path);
    return 0;
}
