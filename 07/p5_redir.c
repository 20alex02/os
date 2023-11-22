#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <err.h>            /* err */
#include <netinet/in.h>     /* struct sockaddr_in6, in_port_t */
#include <sys/socket.h>     /* socket, AF_* */
#include <fcntl.h>          /* open */
#include <unistd.h>         /* read, write, close, unlink, fork, alarm */
#include <string.h>         /* memcmp */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* Naprogramujte jednoduchý klient proudového protokolu. Protistrana
 * ihned po připojení odešle jednu ze dvou možných odpovědí:
 *
 *  • jsou-li první 4 bajty ‹"data"›, zbytek odpovědi zapište do
 *    předaného výstupního souboru,
 *  • jsou-li první 4 bajty ‹"rdir"›, zbytek odpovědi je číslo
 *    portu (zadané jako dva bajty, významnější z nich první), na
 *    který se klient připojí a celý proces opakuje.
 *
 * Na vstupu dostanete adresu socketu rodiny AF_INET6, ke které
 * provedete první připojení. Odpověď ‹rdir› mění pouze číslo portu
 * – hostitel zůstává původní.
 *
 * Klient je implementován procedurou ‹redir_fetch›, které výsledkem
 * bude 0 v případě úspěchu (data byla uložena do souboru), -1
 * v případě systémového selhání nebo -2 detekuje-li cyklické
 * přesměrování. */

#define BUF_SIZE 1024
struct ports {
    in_port_t *ports;
    int capacity;
    int len;
};

bool realloc_double(struct ports *ports) {
    ports->capacity *= 2;
    in_port_t *tmp = realloc(ports->ports, ports->capacity * sizeof(in_port_t));
    if (!tmp) {
        return false;
    }
    ports->ports = tmp;
    return true;
}

bool visited(struct ports ports, in_port_t port) {
    for (int i = 0; i < ports.len; ++i) {
        if (ports.ports[i] == port) {
            return true;
        }
    }
    return false;
}

int redir_fetch_rec(const struct sockaddr_in6 *address, int out_fd, struct ports *ports) {
    if (visited(*ports, address->sin6_port)) {
        return -2;
    }
    if (ports->len + 1 > ports->capacity && !realloc_double(ports)) {
        return -1;
    }
    ports->ports[ports->len++] = address->sin6_port;

    int rv = -1;
    int sockfd = socket(AF_INET6, SOCK_STREAM, 0);
    if (sockfd == -1) {
        goto out;
    }

    if (connect(sockfd, (struct sockaddr *) address, sizeof(struct sockaddr_in6)) == -1) {
        goto out;
    }

    char buffer[BUF_SIZE];
    ssize_t nbytes = recv(sockfd, buffer, 4, 0);
    if (nbytes != 4) {
        goto out;
    }

    if (strncmp(buffer, "data", 4) == 0) {
        while ((nbytes = recv(sockfd, buffer, BUF_SIZE, 0)) > 0) {
            if (write(out_fd, buffer, nbytes) != nbytes) {
                goto out;
            }
        }
        if (nbytes == -1) {
            goto out;
        }
    } else if (strncmp(buffer, "rdir", 4) == 0) {
        if (recv(sockfd, buffer, 2, 0) < 0) {
            goto out;
        }

        uint16_t new_port;
        memcpy(&new_port, buffer, 2);
        new_port = ntohs(new_port);

        struct sockaddr_in6 new_address = *address;
        new_address.sin6_port = htons(new_port);

        close(sockfd);
        return redir_fetch_rec(&new_address, out_fd, ports);
    }
    rv = 0;
    out:
    close(sockfd);
    return rv;
}

int redir_fetch(const struct sockaddr_in6 *address, int out_fd) {
    struct ports ports;
    ports.len = 0;
    ports.capacity = 10;
    ports.ports = malloc(ports.capacity * sizeof(in_port_t));
    if (!ports.ports) {
        return -1;
    }
    int rv = redir_fetch_rec(address, out_fd, &ports);
    free(ports.ports);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* kill, SIGTERM */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
}

struct testserver {
    pid_t pid;
    struct sockaddr_in6 addr;
};

static int prep_socket(struct sockaddr_in6 *saddr) {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == -1)
        err(2, "socket");

    const socklen_t sin6len = sizeof(struct sockaddr_in6);

    memset(saddr, 0, sin6len);
    saddr->sin6_family = AF_INET6;
    saddr->sin6_addr.s6_addr[15] = 1; /* bind to [::1] */

    if (bind(fd, (struct sockaddr *) saddr, sin6len) == -1)
        err(2, "bind");

    if (listen(fd, 1) == -1)
        err(2, "listen");

    socklen_t socklen = sin6len;
    if (getsockname(fd, (struct sockaddr *) saddr, &socklen) == -1 ||
        socklen != sin6len ||
        saddr->sin6_port == 0)
        err(2, "getsockname");

    return fd;
}

static pid_t spawn_server(int sock_fd, const char *data, int len) {
    pid_t pid = fork();
    if (pid == -1)
        err(2, "fork");

    if (pid > 0)
        return pid;

    /* server */

    /* Ukončit server nejpozději po dvou sekundách, což je čas více než
     * dostatečný na běh přiložených testů. Zejména ve sdíleném prostředí
     * serveru Aisa je velice důležité servery důsledně ukončovat, aby
     * nedošlo k nedostatku volných TCP portů. */
    alarm(2);

    int cfd;
    while ((cfd = accept(sock_fd, NULL, NULL)) != -1) {
        if (write(cfd, data, len) < len)
            err(2, "server write");
        close_or_warn(cfd, "client fd in server");
    }
    err(2, "accept");
}

static pid_t spawn_rdir_at(in_port_t to, int sock_fd) {
    char msg[6] = {'r', 'd', 'i', 'r'};
    const unsigned char *port_rep = (const unsigned char *) &to;
    msg[4] = port_rep[0];
    msg[5] = port_rep[1];

    pid_t pid = spawn_server(sock_fd, msg, 6);

    close_or_warn(sock_fd, "server socket in client");
    return pid;
}

static struct testserver spawn_rdir(in_port_t to) {
    struct testserver s;
    int sock_fd = prep_socket(&s.addr);
    s.pid = spawn_rdir_at(to, sock_fd);
    return s;
}

static int kill_and_reap(pid_t pid) {
    if (kill(pid, SIGTERM) == -1)
        err(2, "kill");

    int status;

    if (waitpid(pid, &status, 0) == -1)
        err(2, "wait");

    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM)
        return 0;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    else
        return -1;
}

static int check_read(int fd, const char *data, int len) {
    char buf[512];
    int bytes_read = read(fd, buf, 512);
    if (bytes_read == len)
        return memcmp(buf, data, len);
    return 1;
}

int main(void) {
    int out_fd_w = open("zt.p5_out", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out_fd_w == -1)
        err(2, "open zt.p5_out write");

    int out_fd_r = open("zt.p5_out", O_RDONLY);
    if (out_fd_r == -1)
        err(2, "open zt.p5_out read");

    struct testserver server_data;
    int serv_fd = prep_socket(&server_data.addr);
    server_data.pid = spawn_server(serv_fd, "datalorem\0ipsum", 15);

    struct testserver server_rdir1 = spawn_rdir(server_data.addr.sin6_port);
    struct testserver server_rdir2 = spawn_rdir(server_rdir1.addr.sin6_port);

    assert(redir_fetch(&server_data.addr, out_fd_w) == 0);
    assert(check_read(out_fd_r, "lorem\0ipsum", 11) == 0);

    assert(redir_fetch(&server_rdir1.addr, out_fd_w) == 0);
    assert(check_read(out_fd_r, "lorem\0ipsum", 11) == 0);

    assert(redir_fetch(&server_rdir2.addr, out_fd_w) == 0);
    assert(check_read(out_fd_r, "lorem\0ipsum", 11) == 0);

    /* zasmyčkování */
    assert(kill_and_reap(server_data.pid) == 0);

    server_data.pid = spawn_rdir_at(server_rdir2.addr.sin6_port, serv_fd);

    assert(redir_fetch(&server_data.addr, out_fd_w) == -2);
    assert(redir_fetch(&server_rdir1.addr, out_fd_w) == -2);
    assert(redir_fetch(&server_rdir2.addr, out_fd_w) == -2);

    assert(check_read(out_fd_r, "", 0) == 0);

    close_or_warn(out_fd_w, "zt.p5_out write");
    close_or_warn(out_fd_r, "zt.p5_out read");
    unlink_if_exists("zt.p5_out");

    assert(kill_and_reap(server_data.pid) == 0);
    assert(kill_and_reap(server_rdir1.pid) == 0);
    assert(kill_and_reap(server_rdir2.pid) == 0);

    return 0;
}
