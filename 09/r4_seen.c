#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <err.h>            /* err */
#include <errno.h>          /* errno */
#include <stdint.h>         /* uint32_t, int64_t */
#include <stdlib.h>         /* exit */
#include <unistd.h>         /* read, write, close, unlink, fork, alarm */
#include <sys/socket.h>     /* socket, AF_* */
#include <sys/un.h>         /* struct sockaddr_un */
#include <arpa/inet.h>      /* ntohl */

/* Napište podprogram ‹seen_server›, který přijme dva parametry:
 *
 *  1. ‹sock_fd› je popisovač socketu, který je svázán s adresou
 *     a je nastaven do režimu poslouchání (‹listen›),
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *  3. ‹par› je minimální počet klientů, kteří se mohou připojit
 *     souběžně.
 *
 * a který implementuje tento jednoduchý protokol:
 *
 *  1. po připojení klient ihned odešle jednobajtový identifikátor,
 *     o kterém si server poznačí, že byl viděn – na tuto zprávu
 *     server nijak neodpovídá,
 *  2. na každý další bajt, který klient odešle, odpoví
 *     jednobajtovou zprávou – buď 0 nebo 1, podle toho, jestli daný
 *     bajt již viděl nebo nikoliv.
 *
 * Server musí být schopen komunikovat s několika klienty najednou.
 * Můžete předpokládat, že klienti budou odpovědi číst bez zbytečné
 * prodlevy.
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */

int seen_server( int sock_fd, int count, int par );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* kill, SIGTERM */
#include <time.h>           /* nanosleep */
#include <sched.h>          /* sched_yield */

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

static pid_t fork_server( int sock_fd, int clients )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.r4_socket" };

    if ( bind( sock_fd, (const struct sockaddr *) &addr,
               sizeof addr ) == -1 )
        err( 2, "bind" );

    if ( listen( sock_fd, 3 ) == -1 )
        err( 2, "listen" );

    pid_t pid = fork();

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        alarm( 3 );
        exit( seen_server( sock_fd, clients, 2 ) ? 1 : 0 );
    }

    return pid;
}

static int client_connect( int retries, char id )
{
    int res = -1;
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );

    if ( fd == -1 )
        err( 2, "socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.r4_socket" };

    while ( ( res = connect( fd, (const struct sockaddr *) &addr,
                             sizeof addr ) ) == -1 &&
             errno == ECONNREFUSED &&
             retries --> 0 )
    {
        sched_yield();
    }

    if ( res == -1 )
        err( 2, "connect" );

    if ( send( fd, &id, 1, 0 ) == -1 )
        err( 2, "sending id" );

    return fd;
}

static int client_check( int fd, char id )
{
    if ( send( fd, &id, 1, 0 ) != 1 )
        return -3;

    char result;

    if ( recv( fd, &result, 1, 0 ) != 1 )
        return -1;

    return result;
}

int main( void )
{
    unlink_if_exists( "zt.r4_socket" );

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 2, "socket" );

    pid_t pid = fork_server( sock_fd, 5 );

    close_or_warn( sock_fd, "server socket in client" );

    int c1 = client_connect( 5, 3 );
    int c2 = client_connect( 0, 2 );

    assert( client_check( c2, 2 ) == 1 );
    assert( client_check( c1, 3 ) == 1 );
    assert( client_check( c1, 2 ) == 1 );
    assert( client_check( c2, 3 ) == 1 );

    close_or_warn( c1, "client 1" );

    assert( client_check( c2, 1 ) == 0 );

    int c3 = client_connect( 0, 1 );
    assert( client_check( c3, 0 ) == 0 );
    assert( client_check( c2, 1 ) == 1 );

    close_or_warn( c3, "client 3" );

    assert( client_check( c2, 1 ) == 1 );

    close_or_warn( c2, "client 2" );

    int c4 = client_connect( 0, 2 );
    int c5 = client_connect( 0, 5 );

    assert( client_check( c5, 5 ) == 1 );
    assert( client_check( c4, 5 ) == 1 );
    assert( client_check( c3, 5 ) == 1 );

    close_or_warn( c5, "client 5" );
    sched_yield();

    assert( client_check( c4, 5 ) == 1 );
    assert( client_check( c3, 5 ) == 1 );

    close_or_warn( c4, "client 4" );

    assert( reap( pid ) == 0 );

    unlink_if_exists( "zt.r4_socket" );
    return 0;
}
