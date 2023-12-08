#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* exit */
#include <unistd.h>     /* write, read, close, … */
#include <poll.h>       /* poll */
#include <sys/socket.h> /* socket, AF_*, SOCK_* */
#include <sys/un.h>     /* sockaddr_un */
#include <errno.h>
#include <assert.h>
#include <err.h>

/* Napište podprogram ‹poll_clients›, který bude spravovat pole
 * struktur ‹pollfd› pro souběžný server. Pro účely tohoto příkladu
 * budeme uvažovat server, který neudržuje žádný stav specifický pro
 * jednotlivé klienty.¹
 *
 * Podprogram ‹poll_clients› vrátí popisovač, na kterém jsou
 * k dispozici data k přečtení, nebo na kterém bylo ukončeno spojení
 * (v obou případech je zaručeno, že ‹read› nebude blokovat).
 * Zároveň bude ‹poll_clients› automaticky přijímat nová připojení a
 * uklízet popisovače, na kterých byla komunikace volajícím
 * ukončena.
 *
 * Parametry:
 *
 *  1. ‹sock_fd› je popisovač socketu, na kterém server poslouchá, a
 *     na kterém bude ‹poll_clients› automaticky volat ‹accept›
 *     kdykoliv je to potřeba,
 *  2. ‹poll_fds› je vstupně-výstupní parametr, který obsahuje
 *     ukazatel na začátek pole struktur ‹pollfd›, a které
 *     podprogram ‹poll_clients› spravuje,
 *  3. ‹fd_count› je vstupně-výstupní parametr, který obsahuje
 *     velikost pole ‹poll_fds›,
 *  4. ‹close_fd› je -1, nebo číslo popisovače, na kterém bylo
 *     ukončeno spojení a je tedy potřeba jej uklidit.
 *
 * Je-li ‹*poll_fds› nulový ukazatel, bude zároveň platit, že
 * ‹*fd_count› je 0. Podprogram ‹poll_clients› v takové situaci
 * alokuje nové pole a do ‹*fd_count› uloží jeho velikost.
 *
 * Návratová hodnota je číslo popisovače připraveného ke čtení, nebo
 * -1 v případě, že nastala systémová chyba. Pole ‹poll_fds›
 * v takovém případě podprogram ‹poll_client› «nebude» dealokovat
 * (uvolní ale jakékoliv lokální zdroje). */

int poll_clients( int sock_fd, struct pollfd **poll_fds,
                  int *fd_count, int close_fd );

/* ¹ Tato podmínka je v praxi příliš omezující, protože neumožňuje
 *   rozumně zpracovat vícebajtové zprávy. Zkuste si rozmyslet, jak
 *   byste ‹poll_clients› upravili, aby tímto nedostatkem netrpělo.
 */

int main( void )
{
    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );;
    char buffer[ 1 ];

    struct sockaddr_un sun = { .sun_family = AF_UNIX,
                               .sun_path = "zt.p1_sock" };
    struct sockaddr *sa = ( struct sockaddr * ) &sun;

    if ( unlink( sun.sun_path ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", sun.sun_path );

    if ( bind( sock_fd, sa, sizeof sun ) )
        err( 1, "binding a unix socket to %s", sun.sun_path );

    if ( listen( sock_fd, 5 ) )
        err( 1, "listen on %s", sun.sun_path );

    int fd[ 3 ], ready_fd;

    for ( int i = 0; i < 3; ++ i )
        if ( ( fd[ i ] = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
            err( 1, "creating a unix socket" );

    if ( connect( fd[ 0 ], sa, sizeof sun ) == -1 )
        err( 1, "connecting to %s", sun.sun_path );

    if ( write( fd[ 0 ], "x", 1 ) == -1 )
        err( 1, "writing to socket" );

    struct pollfd *poll_fds = NULL;
    int fd_count = 0;

    ready_fd = poll_clients( sock_fd, &poll_fds, &fd_count, -1 );
    assert( ready_fd >= 0 );
    assert( read( ready_fd, buffer, 1 ) == 1 );
    assert( buffer[ 0 ] == 'x' );

    for ( int i = 1; i < 3; ++i )
    {
        if ( connect( fd[ i ], sa, sizeof sun ) == -1 )
            err( 1, "connecting to %s", sun.sun_path );
        if ( write( fd[ i ], "y", 1 ) == -1 )
            err( 1, "writing to socket" );
    }

    ready_fd = poll_clients( sock_fd, &poll_fds, &fd_count, ready_fd );
    assert( ready_fd >= 0 );
    assert( read( ready_fd, buffer, 1 ) == 1 );
    assert( buffer[ 0 ] == 'y' );
    assert( read( fd[ 0 ], buffer, 1 ) == 0 );
    ready_fd = poll_clients( sock_fd, &poll_fds, &fd_count, ready_fd );
    assert( ready_fd >= 0 );
    assert( read( ready_fd, buffer, 1 ) == 1 );
    assert( buffer[ 0 ] == 'y' );
    assert( close( ready_fd ) != -1 );
    assert( read( fd[ 1 ], buffer, 1 ) == 0 );
    assert( read( fd[ 2 ], buffer, 1 ) == 0 );

    for ( int i = 0; i < 3; ++i )
        close( fd[ i ] );

    free( poll_fds );
    close( sock_fd );
    return 0;
}
