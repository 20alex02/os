#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* close, write */
#include <sys/socket.h> /* socketpair, recv */
#include <string.h>     /* memset */
#include <err.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <errno.h>

#define BUF_SIZE 1024

/* V tomto příkladu budete opět programovat trojici podprogramů:
 *
 *  1. ‹meter_start› nastartuje asynchronní počítání bajtů,
 *  2. ‹meter_read› přečte aktuální stav počítadla,
 *  3. ‹meter_cleanup› vyčká na konec přenosu, uvolní zdroje a vrátí
 *     finální hodnotu počítadla. */

/* Oproti první přípravě tohoto týdne bude přenos dat probíhat mezi
 * dvěma popisovači obousměrně. Podprogram ‹meter_start› bude mít 3
 * parametry:
 *
 *  1. dvojici popisovačů ‹fd_1› a ‹fd_2› připojených proudových
 *     socketů, mezi kterými bude obousměrně kopírovat (přeposílat)
 *     data,
 *  2. ‹max_delay› který určuje, jaký může být nejvýše rozdíl mezi
 *     ustálenou hodnotou ‹meter_read› a počtem bajtů skutečně
 *     zapsaných (odeslaných) do socketů ‹fd_1› a ‹fd_2›, a zároveň
 *     o kolik bajtů může být opožděný přenos mezi ‹fd_1› a ‹fd_2›.
 *
 * Podprogram ‹meter_start› vrátí volajícímu ukazatel ‹handle›,
 * který volající později předá podprogramu ‹meter_cleanup›,
 * případně ‹meter_read›. Samotné přeposílání dat bude provádět
 * asynchronně.
 *
 * Při uzavření spojení na libovolném z popisovačů ‹fd_1› nebo
 * ‹fd_2› uzavře i zbývající spojení. Můžete předpokládat, že
 * zápisy do ‹fd_1› a ‹fd_2› budou i v blokujícím režimu provedeny
 * obratem (nehrozí tedy uváznutí při zápisu, ani hladovění). */

struct handle {
    struct pollfd fds[2];
    atomic_int bytes;
    pthread_t tid;
    int rv;
};

void *meter(void *data) {
    struct handle *handle = data;
//    printf("tid: %lu bytes: %d\n", handle->tid, handle->bytes);

    uint8_t buffer[BUF_SIZE];
    ssize_t bytes_read;
    while (1) {
//        printf("tid: %lu calling poll\n", handle->tid);
        if (poll(handle->fds, 2, -1) == -1) {
            handle->rv = -1;
//            printf("tid: %lu poll failed\n", handle->tid);
            return NULL;
        }
        for (int i = 0; i < 2; i++) {
            if (handle->fds[i].revents & (POLLIN)) {
                bytes_read = read(handle->fds[i].fd, buffer, BUF_SIZE);
                if (bytes_read == -1) {
                    if (errno != ECONNRESET) {
                        handle->rv = -1;
                    } else {
                        handle->rv = 0;
                    }
//                    perror("read");
//                    printf("tid: %lu read failed\n", handle->tid);
                    return NULL;
                }
                if (bytes_read == 0) {
                    handle->rv = 0;
//                    printf("tid: %lu connection closed\n", handle->tid);
                    return NULL;
                }
                if (write(handle->fds[1 - i].fd, buffer, bytes_read) == -1) {
//                    perror("write");
                    handle->rv = -1;
//                    printf("tid: %lu write failed\n", handle->tid);
                    return NULL;
                }
                handle->bytes += bytes_read;
//                printf("tid: %lu bytes transfered: %d\n", handle->tid, handle->bytes);
            }
        }
    }
}

void *meter_start(int fd_1, int fd_2, int max_delay) {
    struct handle *handle = malloc(sizeof(struct handle));
    if (!handle) {
        return NULL;
    }
    handle->fds[0].fd = fd_1;
    handle->fds[0].events = POLLIN;
    handle->fds[1].fd = fd_2;
    handle->fds[1].events = POLLIN;
    handle->bytes = 0;
    // todo delay
    if (pthread_create(&handle->tid, NULL, meter, handle) != 0) {
        free(handle);
        return NULL;
    }
    return handle;
}

/* Podprogram ‹meter_read› přečte aktuální hodnotu počítadla.
 * Neprobíhají-li souběžně žádné zápisy a celkový počet skutečně
 * zapsaných bajtů je ⟦n⟧, musí se výsledek opakovaného volání
 * ‹meter_read› ustálit na hodnotě ⟦k⟧, ⟦n - 2d ≤ k ≤ n⟧, kde ⟦d⟧ je
 * parametr ‹max_delay›, který byl předán podprogramu ‹meter_start›.
 * */

int meter_read(void *handle) {
    return ((struct handle *) handle)->bytes;
}

/* Podprogram ‹meter_cleanup› obdrží ukazatel ‹handle›, který byl
 * vrácen podprogramem ‹meter_start›, a uvolní veškeré zdroje s ním
 * spojené. Návratová hodnota ≥ 0 zaručuje, že všechna data byla
 * úspěšně přeposlána a zároveň určuje, kolik bajtů bylo přeneseno.
 * Jinak je výsledkem -1. */

int meter_cleanup(void *handle) {
    if (handle == NULL) {
        return -1;
    }
    struct handle *h = handle;
    if (pthread_join(h->tid, NULL) != 0 || h->rv != 0) {
        close(h->fds[0].fd);
        close(h->fds[1].fd);
//        printf("join failed rv: %d bytes: %d\n", h->rv, h->bytes);
        free(h);
        return -1;
    }
    int rv = h->bytes;
    if (close(h->fds[0].fd) == -1) rv = -1;
    if (close(h->fds[1].fd) == -1) rv = -1;
    free(h);
//    printf("rv: %d\n", rv);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sched.h>      /* sched_yield */
#include <signal.h>     /* signal, SIGPIPE, SIG_IGN */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
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

static int drain(int fd, ssize_t bytes) {
    char buf[512];

    if (recv(fd, buf, bytes, 0) == -1)
        return -1;
    else
        return 0;
}

int main(void) {
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        err(2, "signal");

    int fds_a[2], fds_b[2];
    void *handle;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds_a) == -1)
        err(1, "creating a socketpair");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds_b) == -1)
        err(1, "creating a socketpair");

    handle = meter_start(fds_a[0], fds_b[0], 10);
    fill(fds_a[1], 15);
    fill(fds_b[1], 18);

    while (meter_read(handle) < 13)
        sched_yield();

    assert(meter_read(handle) <= 33);

    for (int i = 0; i < 20; ++i) {
        fill(fds_a[1], 15);
        assert(drain(fds_b[1], 15) == 0);
    }

    for (int i = 0; i < 20; ++i) {
        fill(fds_a[1], 18);
        fill(fds_b[1], 18);
        assert(drain(fds_b[1], 18) == 0);
        assert(drain(fds_a[1], 18) == 0);
    }

    while (meter_read(handle) < 13 + 20 * 15 + 20 * 36)
        sched_yield();

    assert(meter_read(handle) <= 33 + 20 * 15 + 20 * 36);
    close_or_warn(fds_a[1], "socket");
    close_or_warn(fds_b[1], "socket");
    printf("cleanup: %d\n", meter_cleanup(handle));
//    assert(meter_cleanup(handle) >= 13 + 20 * 15 + 20 * 36);

    return 0;
}
