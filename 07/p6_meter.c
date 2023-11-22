#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>         /* exit */
#include <assert.h>         /* assert */
#include <unistd.h>         /* read, write, fork, close */
#include <err.h>            /* err, warn, warnx */
#include <stdio.h>          /* dprintf */
#include <poll.h>
#include <stdbool.h>

/* Naprogramujte proceduru ‹connection_meter›, která obdrží dva
 * popisovače, ‹fd_1› a ‹fd_2› a výstupní parametr ‹bytes›. Jejím
 * úkolem bude přeposílat data z jednoho popisovače na druhý a
 * naopak, podle potřeby.¹ Zároveň bude do parametru ‹bytes›
 * přičítat každý přenesený bajt (bez ohledu na směr přenosu).
 *
 * Data musí být přenášena bez zbytečné prodlevy (zejména nesmí být
 * blokován některý ze směrů přenosu jen kvůli tomu, že v tom druhém
 * žádná komunikace neprobíhá). Popisovače budou proceduře předány
 * v neblokujícím režimu.
 *
 * Procedura skončí jakmile jsou obě spojení ukončena, s výsledkem 0
 * proběhlo-li vše bez problémů, jinak s výsledkem -1. */

int connection_meter(int fd_1, int fd_2, int *count) {
    *count = 0;
    struct pollfd fds[2];
    fds[0].fd = fd_1;
    fds[0].events = POLLIN;
    fds[1].fd = fd_2;
    fds[1].events = POLLIN;

    char buffer[1024];
    bool closed[2] = {false, false};
    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret == -1) {
            return -1;
        }

        for (int i = 0; i < 2; i++) {
            if (fds[i].revents & (POLLIN)) {
                ssize_t bytes_read = read(fds[i].fd, buffer, sizeof(buffer));
                if (bytes_read == -1) {
                    return -1;
                }
                if (bytes_read == 0) {
                    if (closed[1 - i]) {
                        return 0;
                    }
                    closed[i] = true;
                    continue;
                }
                ssize_t bytes_written = write(fds[1 - i].fd, buffer, bytes_read);
                if (bytes_written == -1) {
                    return -1;
                }
                *count += (int) bytes_written;
            }
        }
    }
}

/* ¹ Doporučujeme opět nahlédnout do třetí a čtvrté kapitoly. Můžete
 *   tam nalézt i kód, který Vám poslouží jako dobrý startovní bod
 *   řešení. Dobře si ale rozmyslete, jak se vypořádat se zápisy. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/socket.h>     /* socketpair */
#include <sys/wait.h>       /* waitpid */
#include <string.h>         /* strcmp, strlen */
#include <fcntl.h>          /* fcntl, F_*, O_* */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
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

pid_t spawn_meter(int *fd_1, int *fd_2, int expect) {
    int sock_1[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_1) == -1)
        err(1, "socketpair 1");

    int sock_2[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_2) == -1)
        err(1, "socketpair 2");

    pid_t pid = fork();
    if (pid == -1)
        err(1, "fork");

    if (pid > 0) {
        close_or_warn(sock_1[1], "child's socket 1 in parent");
        close_or_warn(sock_2[1], "child's socket 2 in parent");

        *fd_1 = sock_1[0];
        *fd_2 = sock_2[0];

        return pid;
    }

    alarm(1);

    close_or_warn(sock_1[0], "parent's socket 1 in child");
    close_or_warn(sock_2[0], "parent's socket 2 in child");

    if (fcntl(sock_1[1], F_SETFL, O_NONBLOCK) == -1)
        err(1, "setting nonblock on sock_1");
    if (fcntl(sock_2[1], F_SETFL, O_NONBLOCK) == -1)
        err(1, "setting nonblock on sock_2");

    int count = 0;
    int rv = connection_meter(sock_1[1], sock_2[1], &count);

    close_or_warn(sock_1[1], "child's socket 1");
    close_or_warn(sock_2[1], "child's socket 2");

    dprintf(2, "count = %d, expect = %d\n", count, expect);
    assert(count == expect);

    exit(rv);
}

static int push(int fd, const char *msg) {
    if (write(fd, msg, strlen(msg)) == -1)
        return warn("push write"), 0;
    return 1;
}

static int pull(int fd, const char *msg) {
    char buf[256];
    int len = strlen(msg);
    int read_total = 0;
    int bytes_read;
    while (read_total < len &&
           (bytes_read = read(fd, buf + read_total,
                              sizeof buf - read_total - 1)) > 0) {
        read_total += bytes_read;
    }

    if (bytes_read == -1)
        return warn("pull read"), 0;

    buf[read_total] = '\0';
    if (strcmp(msg, buf) != 0)
        return warnx("pull unexpected message: %s", buf), 0;

    return 1;
}

static int transfer(int fd_in, int fd_out, const char *msg) {
    return push(fd_in, msg) && pull(fd_out, msg);
}

int main(void) {
    int fd_1, fd_2;

    pid_t pid = spawn_meter(&fd_1, &fd_2, 0);
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    pid = spawn_meter(&fd_1, &fd_2, 43);
    assert(transfer(fd_1, fd_2, "only one is speaking"));
    assert(transfer(fd_1, fd_2, "and it is the first one"));
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    pid = spawn_meter(&fd_1, &fd_2, 60);
    assert(transfer(fd_2, fd_1, "only one is speaking again"));
    assert(transfer(fd_2, fd_1, "and it is the second one this time"));
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    pid = spawn_meter(&fd_1, &fd_2, 48);
    assert(transfer(fd_1, fd_2, "sockets are wonderful"));
    assert(transfer(fd_2, fd_1, "poll is great"));
    assert(transfer(fd_1, fd_2, "select less so"));
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    pid = spawn_meter(&fd_1, &fd_2, 88);
    assert(transfer(fd_2, fd_1, "hello from the other side"));
    assert(transfer(fd_1, fd_2, "order should not matter"));
    assert(transfer(fd_2, fd_1, "wooooo!"));
    assert(transfer(fd_2, fd_1, "and another one in this direction"));
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    pid = spawn_meter(&fd_1, &fd_2, 52);
    assert(push(fd_1, "lets talk "));
    assert(push(fd_1, "over each other."));
    assert(push(fd_2, "okay great idea."));
    assert(push(fd_1, "okay!"));
    assert(push(fd_2, "well?"));
    assert(pull(fd_2, "lets talk over each other.okay!"));
    assert(pull(fd_1, "okay great idea.well?"));
    close_or_warn(fd_1, "parent's socket 1");
    close_or_warn(fd_2, "parent's socket 2");
    assert(reap(pid) == 0);

    return 0;
}
