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

/* Napište podprogram ‹parallel_server›, který přijme dva parametry:
 *
 *  1. ‹sock_fd› je popisovač socketu, který je svázán s adresou,
 *     ale jinak není nijak nastaven,
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *
 * a který implementuje jednoduchý protokol – pokaždé, když klient
 * odešle jednobajtovou zprávu ‹\x05›, server odpoví počtem souběžně
 * připojených klientů, uloženém do čtyřbajtového slova (bez
 * znaménka, nejvýznamnější bajt první). Jediná jiná povolená akce
 * klienta je ukončení spojení.
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */

int parallel_server( int sock_fd, int count );

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
                                .sun_path = "zt.p4_socket" };

    if ( bind( sock_fd, (const struct sockaddr *) &addr, sizeof addr ) == -1 )
        err( 2, "bind" );

    pid_t pid = fork();
    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        alarm( 3 );
        exit( parallel_server( sock_fd, clients ) ? 1 : 0 );
    }

    return pid;
}

static int client_connect( int retries )
{
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( fd == -1 )
        err( 2, "socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p4_socket" };

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
        err( 2, "connect" );

    return fd;
}

static int64_t client_read( int fd )
{
    char req = 0x05;
    if ( write( fd, &req, 1 ) != 1 )
        return -3;

    uint32_t msg;
    int total = 0;
    while ( total < 4 )
    {
        int bytes_read = read( fd, total + ( unsigned char * ) &msg, 4 - total );
        if ( bytes_read <= 0 )
            return -1;

        total += bytes_read;
    }

    return ntohl( msg );
}

int main( void )
{
    unlink_if_exists( "zt.p4_socket" );

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 2, "socket" );

    pid_t pid = fork_server( sock_fd, 5 );

    close_or_warn( sock_fd, "server socket in client" );

    int c1 = client_connect( 5 );
    int c2 = client_connect( 0 );

    assert( client_read( c2 ) == 2 );
    assert( client_read( c1 ) == 2 );

    close_or_warn( c1, "client 1" );

    assert( client_read( c2 ) == 1 );

    int c3 = client_connect( 0 );
    assert( client_read( c2 ) == 2 );
    assert( client_read( c3 ) == 2 );

    close_or_warn( c3, "client 3" );
    sched_yield();

    assert( client_read( c2 ) == 1 );

    close_or_warn( c2, "client 2" );

    int c4 = client_connect( 0 );
    int c5 = client_connect( 0 );
    sched_yield();

    assert( client_read( c5 ) == 2 );
    assert( client_read( c4 ) == 2 );

    close_or_warn( c5, "client 5" );
    sched_yield();

    assert( client_read( c4 ) == 1 );

    close_or_warn( c4, "client 4" );

    assert( reap( pid ) == 0 );

    unlink_if_exists( "zt.p4_socket" );
    return 0;
}
