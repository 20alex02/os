#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* exit */
#include <unistd.h>     /* write, read, close, … */
#include <poll.h>       /* poll */
#include <sys/socket.h> /* socket, AF_*, SOCK_* */
#include <sys/un.h>     /* sockaddr_un */
#include <errno.h>
#include <assert.h>
#include <err.h>

/* Napište podprogram ‹accept_clients›, který bude podle potřeby a
 * možnosti přijímat nová spojení a tato nová spojení poznamená do
 * předaného pole struktur ‹pollfd›. Při volání ‹accept_clients› je
 * zaručeno, že jedno volání ‹accept› na popisovači ‹sock_fd› se
 * vrátí bez prodlevy. Parametry:
 *
 *  • ‹sock_fd› je popisovač poslouchajícího socketu,
 *  • ‹poll_fds› je pole struktur ‹pollfd›,
 *  • ‹nfds› je počet položek pole ‹poll_fds›,
 *  • ‹events› je hodnota, na kterou si přejeme nastavit položku
 *    ‹events› ve struktuře přidělené případnému novému spojení.
 *
 * Výsledkem je počet přijatých spojení (nula nebo jedna) v případě
 * úspěchu a -1 v případě neúspěchu. Nevyužité položky v ‹poll_fds›
 * mají nastavenu hodnotu ‹fd› na -1 – toto nastavování provádí
 * volající ve vlastní režii. */

int accept_clients(int sock_fd, struct pollfd *poll_fds,
                   int nfds, int events) {
    struct pollfd *pfd = NULL;
    for (int i = 0; i < nfds; ++i) {
        if (poll_fds[i].fd == -1) {
            pfd = poll_fds + i;
        }
    }
    if (!pfd) {
        return 0;
    }
    int client_fd = accept(sock_fd, NULL, NULL);
    if (client_fd == -1) {
        return -1;
    }
    pfd->fd = client_fd;
    pfd->events = (short) events;
    return 1;
}

int main(void) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);;
    char buffer[1];

    struct sockaddr_un sun = {.sun_family = AF_UNIX,
            .sun_path = "zt.p1_sock"};
    struct sockaddr *sa = (struct sockaddr *) &sun;

    if (unlink(sun.sun_path) == -1 && errno != ENOENT)
        err(1, "unlinking %s", sun.sun_path);

    if (bind(sock_fd, sa, sizeof sun))
        err(1, "binding a unix socket to %s", sun.sun_path);

    if (listen(sock_fd, 5))
        err(1, "listen on %s", sun.sun_path);

    int fd[3], ready_fd = -1, ready_count = 0;

    for (int i = 0; i < 3; ++i)
        if ((fd[i] = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
            err(1, "creating a unix socket");

    if (connect(fd[0], sa, sizeof sun) == -1)
        err(1, "connecting to %s", sun.sun_path);

    if (write(fd[0], "x", 1) == -1)
        err(1, "writing to socket");

    struct pollfd poll_fds[3] =
            {
                    {.fd = sock_fd, .events = POLLIN},
                    {.fd = -1},
                    {.fd = -1}
            };

    assert(accept_clients(sock_fd, poll_fds, 3, POLLIN) == 1);
    assert(poll_fds[0].fd == sock_fd);
    assert(poll_fds[1].fd != -1 || poll_fds[2].fd != -1);
    assert(poll(poll_fds, 3, -1) == 1);

    for (int i = 1; i < 3; ++i)
        if (poll_fds[i].revents & POLLIN) {
            ready_fd = poll_fds[i].fd;
            assert(read(ready_fd, buffer, 1) == 1);
            assert(buffer[0] == 'x');
        }

    assert(ready_fd != -1);

    for (int i = 1; i < 3; ++i) {
        if (connect(fd[i], sa, sizeof sun) == -1)
            err(1, "connecting to %s", sun.sun_path);
        if (write(fd[i], "y", 1) == -1)
            err(1, "writing to socket");
    }

    assert(accept_clients(sock_fd, poll_fds, 3, POLLIN) == 1);
    assert(accept_clients(sock_fd, poll_fds, 3, POLLIN) == 0);

    for (int i = 1; i < 3; ++i)
        if (poll_fds[i].fd == ready_fd)
            poll_fds[i].fd = -1;

    close(ready_fd);

    assert(accept_clients(sock_fd, poll_fds, 3, POLLIN) == 1);
    assert(poll(poll_fds, 3, -1) == 2);

    for (int i = 1; i < 3; ++i)
        if (poll_fds[i].revents & POLLIN) {
            ++ready_count;
            assert(read(poll_fds[i].fd, buffer, 1) == 1);
            assert(buffer[0] == 'y');
        }

    assert(ready_count == 2);

    for (int i = 0; i < 3; ++i)
        close(fd[i]);

    close(sock_fd);
    return 0;
}
