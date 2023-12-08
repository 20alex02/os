#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdint.h>

#define BLOCK_SIZE 128

/* Vaším úkolem bude napsat dvojici podprogramů, ‹counter_start› a
 * ‹counter_cleanup›, které realizují asynchronní počítání bajtů na
 * předaném popisovači. */

/* Podprogram ‹counter_start›:
 *
 *  1. má jediný parametr ‹fd› – popisovač otevřeného souboru, roury nebo
 *     proudového socketu,
 *  2. vytvoří nový proces, který bude z popisovače ‹fd› číst data a
 *     počítat kolik přečetl bajtů,
 *  3. v tomto novém procesu se podprogram ‹counter_start› «nevrátí»
 *     – nový proces bude ukončen jakmile narazí na konec souboru
 *     (resp. jakmile je spojení na popisovači ‹fd› protistranou
 *     ukončeno),
 *  4. v původním (rodičovském) procesu je výsledkem ‹counter_start›
 *     ukazatel, který později volající předá podprogramu
 *     ‹counter_cleanup› (nebo nulový ukazatel v případě chyby). */

struct handle {
    pid_t pid;
    int pipe_fd[2];
};

void *counter_start(int fd) {
    struct handle *handle = malloc(sizeof(struct handle));
    if (handle == NULL) {
        return NULL;
    }
    if (fd == -1) {
        handle->pid = -1;
        return handle;
    }

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

        uint8_t buffer[BLOCK_SIZE];
        ssize_t nbytes;
        int total = 0;
        while ((nbytes = read(fd, buffer, BLOCK_SIZE)) > 0) {
            total += (int) nbytes;
        }
        if (nbytes < 0 ||
            write(handle->pipe_fd[1], &total, sizeof(total)) == -1 ||
            close(handle->pipe_fd[1]) == -1) {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    close(handle->pipe_fd[1]);
    handle->pid = pid;
    return handle;
}

/* Podprogram ‹counter_cleanup› obdrží ukazatel ‹handle›, který byl
 * vrácen podprogramem ‹counter_start›, a vrátí počet bajtů, které
 * byly do popisovače ‹fd› zapsané. Zároveň uvolní veškeré zdroje
 * spojené s ‹handle›. Skončil-li proces-potomek chybou, nebo není
 * možné získat výsledek z jiného důvodu, výsledkem bude -1. */

int counter_cleanup(void *handle) {
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

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <err.h>
#include <string.h>
#include <assert.h>

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

static void fill(int fd, ssize_t bytes) {
    char buf[512];
    memset(buf, 'x', 512);

    ssize_t nwrote;
    while ((nwrote = write(fd, buf, bytes > 512 ? 512 : bytes)) > 0)
        bytes -= nwrote;

    assert(nwrote != -1);
    assert(bytes == 0);
}

static int fork_solution(int fd_r, int fd_w, int expected_res) {

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
        close_or_warn(fd_w, "input pipe: write end (fork_solution)");

        void *handle = counter_start(fd_r);
        assert(handle != NULL);

        close_or_warn(fd_r, "input pipe: read end (fork_solution)");
        if (write(sync[1], "a", 1) == -1)
            err(2, "sync write");
        close_or_warn(sync[1], "sync pipe: write end (fork_solution)");

        int ntot = counter_cleanup(handle);
        if (ntot != expected_res)
            errx(1, "assertion counter_cleanup( handle ) == expected_res failed!\n"
                    "counter_cleanup( handle ) = %d\n"
                    "expected_res = %d", ntot, expected_res);
        exit(0);
    }

    /* parent -> sends data to the solution */

    close_or_warn(sync[1], "sync pipe: write end (tests)");
    char c;
    assert(read(sync[0], &c, 1) == 1); /* blocks until counter_start is called */
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
    int fd_r, fd_w;

    mk_pipe(&fd_r, &fd_w);
    pid_t pid = fork_solution(fd_r, fd_w, 15);
    close_or_warn(fd_r, "input pipe: read end (tests)");
    fill(fd_w, 15);
    close_or_warn(fd_w, "input pipe: write end (tests)");
    assert(reap_solution(pid));

    mk_pipe(&fd_r, &fd_w);
    pid = fork_solution(fd_r, fd_w, 6000);
    close_or_warn(fd_r, "input pipe: read end (tests)");
    fill(fd_w, 6000);
    close_or_warn(fd_w, "input pipe: write end (tests)");
    assert(reap_solution(pid));

    mk_pipe(&fd_r, &fd_w);
    pid = fork_solution(fd_r, fd_w, 0);
    close_or_warn(fd_r, "input pipe: read end (tests)");
    close_or_warn(fd_w, "input pipe: write end (tests)");
    assert(reap_solution(pid));

    void *handle = counter_start(-1); // Bad file descriptor
    assert(handle != NULL);
    assert(counter_cleanup(handle) == -1);
    return -1;
}
