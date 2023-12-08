#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#define BUF_SIZE 1024

/* Vaším úkolem bude napsat trojici podprogramů:
 *
 *  1. ‹counter_start›, který nastartuje asynchronní počítání bajtů
 *     na zadaném popisovači ‹fd›,
 *  2. ‹counter_read›, který umožní volajícímu kdykoliv přečíst
 *     aktuální hodnotu počítadla,
 *  3. ‹counter_cleanup›, který vyčká na ukončení spojení na
 *     popisovači a vrátí celkový výsledný počet přečtených bajtů.
 *
 * Podobně jako v minulém týdnu budou podprogramy rodiny ‹counter›
 * propojeny ukazatelem ‹handle›, který je výsledkem podprogramu
 * ‹counter_start›. */

struct handle {
    int fd;
    atomic_int bytes;
    pthread_t tid;
    int rv;
};

void *counter(void *data) {
    struct handle *handle = data;
    uint8_t buffer[BUF_SIZE];
    ssize_t nbytes;
    while ((nbytes = read(handle->fd, buffer, BUF_SIZE)) > 0) {
        handle->bytes += nbytes;
    }
    if (nbytes == -1) {
        handle->rv = -1;
    }
    handle->rv = 0;
    return NULL;
}

void *counter_start(int fd, int max_delay) {
    struct handle *handle = malloc(sizeof(struct handle));
    if (!handle) {
        return NULL;
    }
    handle->fd = fd;
    handle->bytes = 0;
    // todo delay
    if (pthread_create(&handle->tid, NULL, counter, handle) != 0) {
        free(handle);
        return NULL;
    }
    return handle;
}

/* Podprogram ‹counter_read›, kdykoliv je zavolán, vrátí aktuální
 * hodnotu počítadla bajtů. Předchází-li spuštění ‹counter_read›
 * zápis ⟦n⟧ bajtů,¹ musí návratová hodnota opakovaného volání
 * ‹counter_read› (bez jakéhokoliv dalšího vnějšího zásahu) dříve
 * nebo později nabýt hodnoty ⟦k⟧ takové, že ⟦n - d ≤ k ≤ n⟧, kde
 * ⟦d⟧ je parametr ‹max_delay› se kterým bylo zavoláno
 * ‹counter_start›. */

int counter_read(void *handle) {
    return ((struct handle *) handle)->bytes;
}

/* Podprogram ‹counter_cleanup› obdrží ukazatel ‹handle›, který byl
 * vrácen podprogramem ‹counter_start›, a vrátí počet bajtů, které
 * byly do popisovače ‹fd› zapsané. Zároveň uvolní veškeré zdroje
 * spojené s ‹handle›. Není-li možné tento počet zjistit, protože
 * nastala nějaká systémová chyba, výsledkem bude -1. */

int counter_cleanup(void *handle) {
    if (handle == NULL) {
        return -1;
    }
    struct handle *h = handle;
    if (pthread_join(h->tid, NULL) != 0) {
        close(h->fd);
        free(h);
        return -1;
    }
    if (h->rv != 0) {
        close(h->fd);
        free(h);
        return -1;
    }
    int rv = h->bytes;
    if (close(h->fd) == -1) rv = -1;
    free(h);
    return rv;
}

/* ¹ Předchází ve formálním smyslu, tzn. jsou-li příslušná volání
 *   ‹write› nebo ‹send› v relaci předcházení se spuštěním
 *   podprogramu ‹counter_read›. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sched.h>      /* sched_yield */
#include <signal.h>     /* signal, SIGPIPE, SIG_IGN */

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

int main(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    int fd_r, fd_w;
    void *handle;

    mk_pipe(&fd_r, &fd_w);
    handle = counter_start(fd_r, 10);
    fill(fd_w, 15);

    while (counter_read(handle) < 5)
        sched_yield();

    assert(counter_read(handle) <= 15);
    fill(fd_w, 5);
    close_or_warn(fd_w, "write end of the pipe");
    assert(counter_cleanup(handle) == 20);

    mk_pipe(&fd_r, &fd_w);
    handle = counter_start(fd_r, 1000);
    fill(fd_w, 6000);

    while (counter_read(handle) < 5000)
        sched_yield();

    assert(counter_read(handle) <= 6000);
    close_or_warn(fd_w, "write end of the pipe");
    assert(counter_cleanup(handle) == 6000);
    return 0;
}
