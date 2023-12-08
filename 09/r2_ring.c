#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <err.h>            /* err */
#include <errno.h>          /* errno */
#include <stdint.h>         /* uint32_t, int64_t */
#include <stdlib.h>         /* exit */
#include <string.h>         /* memcmp */
#include <unistd.h>         /* read, write, close, unlink, … */
#include <sys/socket.h>     /* socket, AF_* */
#include <sys/un.h>         /* struct sockaddr_un */
#include <arpa/inet.h>      /* ntohl */

/* Napište podprogram ‹ring_server›, který přijme dva parametry:
 *
 *  1. ‹sock_fd› je popisovač proudového socketu, který je svázán
 *     s adresou a je nastaven do režimu poslouchání,
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *  3. ‹initial› je počáteční hodnota stavu,
 *  4. ‹size› je velikost stavu.
 *
 * Krom stavu si server pamatuje aktuální pozici v rámci stavu,
 * která začíná na nule a při každém úspěšném připojení se posune
 * o jedna doprava, nebo dojde-li na konec stavu, posune se zpátky
 * na začátek.
 *
 * Server každému připojenému klientu sdělí aktuální stav (blok
 * velikosti ‹size›) a poté od něj přečte jeden bajt, kterým přepíše
 * aktuální pozici ve stavu. Po ukončení této výměny ukončí
 * s klientem spojení a je připraven obsloužit dalšího.
 *
 * Za předpokladu, že ‹size > 1› je bajt odeslaný prvním připojeným
 * klientem zapsán na pozici (index) 1.
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */

int ring_server( int sock_fd, int count,
                 const char *initial, int size );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* kill, SIGTERM */
#include <time.h>           /* nanosleep */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink" );
}

static int reap( pid_t pid )
{
    int status;

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "wait" );

    if ( WIFEXITED( status ) )
        return WEXITSTATUS( status );
    else
        return -1;
}

static pid_t fork_server( int sock_fd, int clients,
                          const char *initial, int size )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.r2_socket" };

    if ( bind( sock_fd, (const struct sockaddr *) &addr, sizeof addr ) == -1 )
        err( 2, "bind" );

    if ( listen( sock_fd, 4 ) == -1 )
        return -1;

    pid_t pid = fork();

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        alarm( 3 );
        exit( ring_server( sock_fd, clients, initial, size ) ? 1 : 0 );
    }

    return pid;
}

static int read_and_write( int retries, const char *expect, int len, char new )
{
    char buffer[ len ];
    int rv = -1;
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );

    if ( fd == -1 )
        err( 2, "socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.r2_socket" };

    int connect_rv = -1;
    while ( ( connect_rv = connect( fd, (const struct sockaddr *) &addr, sizeof addr ) ) == -1 &&
            errno == ECONNREFUSED &&
            retries --> 0 )
    {
        /* wait 100 ms */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        if ( nanosleep( &ts, NULL ) == -1 )
            err( 2, "nanosleep" );
    }

    if ( connect_rv == -1 )
    {
        if ( errno == ECONNREFUSED )
        {
            rv = -3;
            goto finish;
        }
        err( 2, "connect" );
    }

    if ( recv( fd, buffer, len, MSG_WAITALL ) == -1 )
    {
        if ( errno == ECONNRESET )
        {
            rv = -3;
            goto finish;
        }
        else
            err( 1, "recv" );
    }

    if ( memcmp( buffer, expect, len ) )
        goto finish;

    if ( send( fd, &new, 1, 0 ) == -1 )
        err( 2, "send" );

    if ( recv( fd, &new, 1, 0 ) != 0 )
        goto finish;

    return 0;
finish:
    close_or_warn( fd, "client socket" );
    return rv;
}

int main( void )
{
    unlink_if_exists( "zt.r2_socket" );

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 2, "socket" );

    pid_t pid = fork_server( sock_fd, 4, "foo", 3 );

    close_or_warn( sock_fd, "server socket in client" );

    assert( read_and_write( 5, "foo", 3, 'x' ) == 0 );
    assert( read_and_write( 0, "fxo", 3, 'y' ) == 0 );
    assert( read_and_write( 0, "fxy", 3, 'z' ) == 0 );
    assert( read_and_write( 0, "zxy", 3, '-' ) == 0 );
    assert( read_and_write( 0, NULL,  0, 0 ) == -3 );

    assert( reap( pid ) == 0 );

    unlink_if_exists( "zt.r2_socket" );
    return 0;
}
