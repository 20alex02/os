#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <errno.h>      /* errno */
#include <stdlib.h>     /* exit */
#include <stdint.h>     /* uint64_t */
#include <unistd.h>     /* read, write, close, unlink, fork, alarm */
#include <sys/socket.h> /* socket, bind, connect, recv */
#include <sys/wait.h>   /* waitpid */

/* Vaším úkolem v této přípravě bude naprogramovat jednoduchý
 * server, který bude klientům poskytovat výpočetní službu – samotný
 * výpočet bude delegován na externí podprogram, který bude
 * podprogramu ‹compute_server› předán jako funkční ukazatel.
 * Parametry ‹compute_server› budou:
 *
 *  • ‹sock_fd› popisovač socketu svázaného s adresou a
 *  • ‹count› – celkový počet připojení, které má server obsloužit
 *    (poté, co se poslední klient odpojí, podprogram skončí),
 *  • ‹compute› – ukazatel na funkci, která přijme a poté vrátí
 *    64bitové číslo bez znaménka – server musí reagovat na
 *    požadavky klientů i v případě, že výpočet ‹compute› trvá
 *    dlouho.
 *
 * Klient bude odesílat jediný typ požadavku, totiž 64bitové číslo
 * ‹n› (nejvýznamnějším bajtem napřed) a server odpoví podobně, ale
 * odpovědí bude výsledek funkce ‹compute›. Je nutné, aby na sebe
 * klienti nemuseli vzájemně čekat.
 *
 * Podprogram ‹compute_server› vrátí nulu v případě, že se mu
 * podařilo obsloužit ‹count› klientů, -1 jinak. Před návratem musí
 * uvolnit veškeré zdroje (s výjimkou popisovače ‹sock_fd›, který
 * vlastní volající). */

int compute_server( int sock_fd, int count,
                    uint64_t (*compute)( uint64_t ) );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <string.h>     /* memcmp */
#include <fcntl.h>      /* fcntl, F_GETFL, F_SETFL, O_NONBLOCK */
#include <sys/un.h>     /* struct sockaddr_un */
#include <time.h>       /* nanosleep */
#include <sched.h>      /* sched_yield */

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink %s", file );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
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


static uint64_t sleepy_square( uint64_t x )
{
    /* Simulace dlouhého výpočtu: počkat 10·x ms */
    uint64_t timeout = 100000 * x / 16;
    const uint64_t one_second = 1000000000;
    struct timespec ts = { .tv_sec = timeout / one_second,
                           .tv_nsec = timeout % one_second };

    for ( unsigned i = 0; i < x / 16; ++i )
    {
        if ( nanosleep( &ts, NULL ) == -1 )
            err( 2, "nanosleep" );

        sched_yield();
    }

    return x * x;
}

static int client_start( char input[ 8 ] )
{
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( fd == -1 )
        err( 2, "client socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p6_socket" };

    int connect_rv = -1;
    int retries = 5;
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
        err( 2, "connect" );


    if ( write( fd, input, 8 ) != 8 )
        err( 2, "client write" );

    return fd;
}

static int client_finish( int fd, char expected[ 8 ] )
{
    uint8_t buf[ 8 ];

    int total = recv( fd, buf, 8, MSG_WAITALL );

    if ( total != 8 )
        return -1;

    close_or_warn( fd, "client" );
    return memcmp( buf, expected, 8 );
}

static int client_is_busy( int fd )
{
    int flags = fcntl( fd, F_GETFL );
    if ( flags == -1 )
        err( 2, "fcntl get" );

    if ( fcntl( fd, F_SETFL, flags | O_NONBLOCK ) == -1 )
        err( 2, "fcntl set" );

    char dummy;
    ssize_t would_recv = recv( fd, &dummy, 1, 0 );
    int e = errno;

    if ( fcntl( fd, F_SETFL, flags ) == -1 )
        err( 2, "fcntl reset" );

    return would_recv == -1 && ( e == EAGAIN || e == EWOULDBLOCK );
}

int main( void )
{
    unlink_if_exists( "zt.p6_socket" );

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 2, "server socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p6_socket" };
    if ( bind( sock_fd, ( struct sockaddr * ) &addr, sizeof addr ) == -1 )
        err( 2, "bind" );

    pid_t pid = fork();
    if ( pid == -1 )
        return -1;

    if ( pid == 0 )
    {
        alarm( 10 ); /* ukončit server po 10 sekundách */
        int rv = compute_server( sock_fd, 3, sleepy_square );
        close_or_warn( sock_fd, "sock_fd in server" );
        exit( !!rv );
    }

    close_or_warn( sock_fd, "sock_fd in client" );

    int client1 = client_start( "\0\0\0\0\0\0\1\0" ); /* 256, pomalý výpočet */
    int client2 = client_start( "\0\0\0\0\0\0\0\20" ); /* 16 */
    int client3 = client_start( "\0\0\0\0\0\0\0\4" ); /* 4 */

    assert( client_finish( client2, "\0\0\0\0\0\0\1\0" ) == 0 ); /* 256 */
    assert( client_finish( client3, "\0\0\0\0\0\0\0\20" ) == 0 ); /* 16 */
    assert( client_is_busy( client1 ) ); /* pomalý výpočet neskončil */
    assert( client_finish( client1, "\0\0\0\0\0\1\0\0" ) == 0 ); /* 256² = 65536 */

    assert( reap( pid ) == 0 );

    unlink_if_exists( "zt.p6_socket" );

    return 0;
}
