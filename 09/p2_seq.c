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

/* Napište podprogram ‹sequence_server›, který přijme dva parametry:
 *
 *  1. ‹sock_fd› je popisovač socketu, který je svázán s adresou,
 *     ale jinak není nijak nastaven,
 *  2. ‹count› je maximální počet připojení (po jeho dosažení se
 *     podprogram vrátí),
 *
 * a který každému připojenému klientu sdělí jeho pořadové číslo
 * jako čtyřbajtové slovo (bez znaménka, nejvýznamnější bajt první)
 * a poté s tímto klientem ukončí spojení. Pořadová čísla začínají
 * od nuly.
 *
 * Návratová hodnota 0 znamená, že bylo úspěšně obslouženo ‹count›
 * klientů, -1 znamená systémovou chybu. */

int sequence_server( int sock_fd, int count );

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

static pid_t fork_server( int sock_fd, int clients )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p2_socket" };

    if ( bind( sock_fd, (const struct sockaddr *) &addr, sizeof addr ) == -1 )
        err( 2, "bind" );

    pid_t pid = fork();
    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        alarm( 3 );
        exit( sequence_server( sock_fd, clients ) ? 1 : 0 );
    }

    return pid;
}

static int64_t connect_and_read_seq( int retries )
{
    int rv = -1;
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( fd == -1 )
        err( 2, "socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p2_socket" };

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

    uint32_t msg;
    int total = 0;
    while ( total < 4 )
    {
        int bytes_read = read( fd, total + ( unsigned char * ) &msg, 4 - total );
        if ( bytes_read <= 0 )
        {
            rv = bytes_read == 0 ? -2 :
                 errno == ECONNRESET ? -3 : -1;
            goto finish;
        }

        total += bytes_read;
    }

    char extra;
    if ( read( fd, &extra, 1 ) == 1 )
    {
        rv = -2;
        goto finish;
    }

    rv = ntohl( msg );

finish:
    close_or_warn( fd, "client socket" );
    return rv;
}

int main( void )
{
    unlink_if_exists( "zt.p2_socket" );

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 2, "socket" );

    pid_t pid = fork_server( sock_fd, 3 );

    close_or_warn( sock_fd, "server socket in client" );

    assert( connect_and_read_seq( 5 ) == 0 );
    assert( connect_and_read_seq( 0 ) == 1 );
    assert( connect_and_read_seq( 0 ) == 2 );
    assert( connect_and_read_seq( 0 ) == -3 ); /* connection has been closed */

    assert( reap( pid ) == 0 );

    unlink_if_exists( "zt.p2_socket" );
    return 0;
}
