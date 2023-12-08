#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdio.h>

#define BLOCK_SIZE 1024

/* Vaším úkolem bude opět napsat dvojici podprogramů, ‹mcount_start›
 * a ‹mcount_cleanup›, která bude pracovat velmi podobně jako ta
 * z předchozího příkladu. Rozdílem je, že nový proces bude pracovat
 * s větším počtem popisovačů souběžně. */

/* Podprogram ‹mcount_start›:
 *
 *  1. má parametry ‹fds› – pole popisovačů otevřených souborů, rour
 *     nebo proudových socketů, a jeho velikost ‹fd_count›,
 *  2. vytvoří nový proces, který bude z popisovačů souběžně číst
 *     data a počítat kolik přečetl bajtů,
 *  3. v tomto novém procesu se podprogram ‹mcount_start› «nevrátí»
 *     – nový proces bude ukončen jakmile všechny popisovače narazí
 *     na konec souboru (resp. jakmile jsou spojení na všech
 *     popisovačích protistranou ukončena),
 *  4. v původním (rodičovském) procesu je výsledkem ‹mcount_start›
 *     ukazatel, který později volající předá podprogramu
 *     ‹mcount_cleanup› (nebo nulový ukazatel v případě chyby).
 *
 * Proces-potomek musí číst data z popisovačů souběžně v tom smyslu,
 * že neaktivní popisovač nesmí blokovat zápis do těch ostatních. */

struct handle {
    pid_t pid;
    int pipe_fd[2];
};

int drain_poll(struct pollfd *pfd, int pfd_size, int *nbytes, int *fd_count) {
    if (poll(pfd, pfd_size, -1) == -1) {
        return -1;
    }

    uint8_t buffer[BLOCK_SIZE];
    ssize_t bytes;
    for (int i = 0; i < pfd_size; ++i) {
        if ((pfd[i].revents & POLLIN) == 0) {
            continue;
        }
        if ((bytes = read(pfd[i].fd, buffer, sizeof buffer)) == -1) {
            return -1;
        }
        *nbytes += (int) bytes;
        if (bytes != sizeof buffer) {
            // should i close it?
//            close(pfd[i].fd);
            pfd[i].fd = -1;
            --*fd_count;
        }
    }
    return 0;
}


void *mcount_start(int fd_count, const int *fds) {
    struct handle *handle = malloc(sizeof(struct handle));
    if (handle == NULL) {
        return NULL;
    }
//    if (fd == -1) {
//        handle->pid = -1;
//        return handle;
//    }

    if (pipe(handle->pipe_fd) == -1) {
        free(handle);
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(handle->pipe_fd[0]);
        close(handle->pipe_fd[1]);
        free(handle);
        return NULL;
    }

    if (pid == 0) {
        if (close(handle->pipe_fd[0]) == -1) {
            exit(EXIT_FAILURE);
        }
        struct pollfd pfds[fd_count];
        for (int i = 0; i < fd_count; ++i) {
            pfds[i].fd = fds[i];
            pfds[i].events = POLLIN;
        }
        int total = 0;
        int active_fd = fd_count;
        while (active_fd > 0) {
            if (drain_poll(pfds, fd_count, &total, &active_fd) == -1) {
                close(handle->pipe_fd[1]);
                exit(EXIT_FAILURE);
            }
        }
        if (write(handle->pipe_fd[1], &total, sizeof(total)) == -1) {
            close(handle->pipe_fd[1]);
            exit(EXIT_FAILURE);
        }
        if (close(handle->pipe_fd[1]) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    close(handle->pipe_fd[1]);
    handle->pid = pid;
    return handle;
}


/* Podprogram ‹mcount_cleanup› obdrží ukazatel ‹handle›, který byl
 * vrácen podprogramem ‹mcount_start›, a vrátí počet bajtů, které
 * byly celkem do popisovačů ‹fds› zapsané. Zároveň uvolní veškeré
 * zdroje spojené s ‹handle›. Skončil-li proces-potomek chybou, nebo
 * není možné získat výsledek z jiného důvodu, výsledkem bude -1. */

int mcount_cleanup(void *handle) {
    if (handle == NULL) {
        return -1;
    }
    struct handle *h = handle;
    pid_t pid = h->pid;
    if (pid == -1) {
        free(handle);
        return -1;
    }
    int nbytes;
    int status;

    if (read(h->pipe_fd[0], &nbytes, sizeof(int)) == -1) {
        close(h->pipe_fd[0]);
        free(handle);
        return -1;
    }

    close(h->pipe_fd[0]);
    free(handle);

    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? nbytes : -1;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <err.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void mk_pipe(int *fd_r, int *fd_w) {
    int p[2];
    if (pipe(p) == -1)
        err(2, "pipe");
    *fd_r = p[0];
    *fd_w = p[1];
}

static void mk_pipes(int n, int *fds_r, int *fds_w) {
    for (int i = 0; i < n; ++i)
        mk_pipe(fds_r + i, fds_w + i);
}

static void close_fds(int n, const int *fds, const char *desc) {
    for (int i = 0; i < n; ++i)
        close_or_warn(fds[i], desc);
}

static void fill(int fd, ssize_t bytes) {
    char buf[512];
    memset(buf, 'x', 512);

    ssize_t nwrote;
    while ((nwrote = write(fd, buf,
                           bytes > 512 ? 512 : bytes)) > 0)
        bytes -= nwrote;

    assert(nwrote != -1);
    assert(bytes == 0);
}

static int fork_solution(int n, int *fds_r, int *fds_w,
                         int fd_count, int expected_res) {
    int sync[2];

    if (pipe(sync) == -1)
        err(2, "sync pipe");

    alarm(5); /* if we get stuck */

    pid_t pid = fork();
    if (pid == -1)
        err(2, "fork");

    if (pid == 0)   /* child -> verifies solution */
    {
        close_or_warn(sync[0], "sync pipe: read end (fork_solution)");
        close_fds(n, fds_w, "input pipes: write ends (fork_solution)");

        void *handle = mcount_start(fd_count, fds_r);
        assert(handle != NULL);

        close_fds(n, fds_r, "input pipes: read ends (fork_solution)");
        if (write(sync[1], "a", 1) == -1)
            err(2, "sync write");
        close_or_warn(sync[1], "sync pipe: write end (fork_solution)");

        int ntot = mcount_cleanup(handle);
        if (ntot != expected_res)
            errx(1, "expected_res = %d, ntot = %d", expected_res, ntot);
        exit(0);
    }

    /* parent -> sends data to the solution */

    close_or_warn(sync[1], "sync pipe: write end (tests)");
    char c;
    assert(read(sync[0], &c, 1) != -1); /* blocks until counter_start is called */
    close_or_warn(sync[0], "sync pipe: read end (tests)");

    return pid;
}

static int reap_solution(pid_t pid) {
    int status;
    if (waitpid(pid, &status, 0) == -1)
        err(2, "waitpid");
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void) {
    int fds_r[3], fds_w[3];

    mk_pipes(3, fds_r, fds_w);
    pid_t pid = fork_solution(3, fds_r, fds_w, 3, 15);
    close_fds(3, fds_r, "input pipes: read ends (tests)");
    fill(fds_w[0], 5);
    fill(fds_w[1], 5);
    fill(fds_w[2], 5);
    close_fds(3, fds_w, "input pipes: write ends (tests)");
    assert(reap_solution(pid));

    mk_pipes(3, fds_r, fds_w);
    pid = fork_solution(3, fds_r, fds_w, 3, 10000);
    close_fds(3, fds_r, "input pipes: read ends (tests)");
    fill(fds_w[0], 3333);
    fill(fds_w[1], 6666);
    fill(fds_w[2], 1);
    close_fds(3, fds_w, "input pipes: write ends (tests)");
    assert(reap_solution(pid));

    mk_pipes(3, fds_r, fds_w);
    pid = fork_solution(3, fds_r, fds_w, 3, 20);
    close_fds(3, fds_r, "input pipes: read ends (tests)");
    fill(fds_w[0], 19);
    fill(fds_w[1], 1);
    close_fds(3, fds_w, "input pipes: write ends (tests)");
    assert(reap_solution(pid));

    return 0;
}
