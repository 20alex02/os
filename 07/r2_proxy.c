#define _POSIX_C_SOURCE 200809L
#include <errno.h>          /* errno */
#include <string.h>         /* strlen, strcmp */
#include <sys/socket.h>     /* socket, connect, AF_UNIX */
#include <sys/un.h>         /* struct sockaddr_un */
#include <err.h>            /* err, warn, warnx */

/* Naprogramujte proceduru ‹proxy›, která obdrží popisovač
 * datagramového socketu a která pro každý přijatý datagram provede
 * následovné:
 *
 *  1. z datagramu přečte prvních ‹sizeof sockaddr_un› bajtů,
 *  2. zbytek datagramu přepošle na takto získanou adresu.
 *
 * Předpokládejte, že datagramy budou mít celkovou délku nejvýše
 * 4KiB. V případě chyby vypište varování, nebo není-li možné
 * v programu pokračovat, tento ukončete s chybou. */

void proxy( int sock_fd );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <stdlib.h>     /* exit, NULL */
#include <unistd.h>     /* close, read, write, unlink, fork, … */
#include <sys/stat.h>   /* stat, struct stat */
#include <time.h>       /* nanosleep */
#include <sys/wait.h>   /* waitpid, W* */
#include <signal.h>     /* kill, SIGTERM */

static void unlink_if_exists( const char* file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", file );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

struct sockaddr_un make_addr( const char* socket_addr )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy( addr.sun_path, socket_addr );
    return addr;
}

int fork_proxy()
{
    int sock_fd;
    struct sockaddr_un addr = make_addr( "zt.r5_proxy" );
    unlink_if_exists( addr.sun_path );

    if ( ( sock_fd = socket( AF_UNIX, SOCK_DGRAM, 0 ) ) == -1 )
        err( 2, "creating socket" );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr,
               sizeof( addr ) ) == -1 )
        err( 2, "binding socket to %s", addr.sun_path );

    pid_t pid = fork();
    alarm( 5 ); /* die after 5s if we get stuck */

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 ) /* child → server */
    {
        proxy( sock_fd );
        exit( 1 );
    }

    close_or_warn( sock_fd, "server socket" );
    return pid;
}

int make_client( const char* socket_addr )
{
    unlink_if_exists( socket_addr );

    int sock_fd = socket( AF_UNIX, SOCK_DGRAM, 0 );

    if ( sock_fd == -1 )
        err( 1, "opening socket" );

    struct sockaddr_un addr_client = make_addr( socket_addr );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr_client,
               sizeof( addr_client ) ) == -1 )
        err( 2, "binding socket to %s", addr_client.sun_path );

    return sock_fd;
}

size_t send_or_die( int fd, const char *what, int nbytes,
                    struct sockaddr_un *addr )
{
    int sent = sendto( fd, what, nbytes, 0,
                       ( struct sockaddr * ) addr,
                       sizeof( struct sockaddr_un ) );
    if ( sent == -1 )
        err( 1, "sendto" );

    return sent;
}

int main( void )
{
    int status;
    pid_t pid = fork_proxy();

    const int nbytes = 1024;
    const int expect_bytes = nbytes - sizeof( struct sockaddr_un );
    char to_send[ nbytes ], buffer[ nbytes ];
    char *to_forward = to_send + sizeof( struct sockaddr_un );

    memset( to_send, 0, nbytes );
    to_send[ 800 ] = 3;

    struct sockaddr_un c2_addr = make_addr( "zt.r5_socket_2" ),
                       c3_addr = make_addr( "zt.r5_socket_3" ),
                       s_addr  = make_addr( "zt.r5_proxy" );

    int c1_fd = make_client( "zt.r5_socket_1" ),
        c2_fd = make_client( "zt.r5_socket_2" ),
        c3_fd = make_client( "zt.r5_socket_3" );

    memcpy( to_send, &c2_addr, sizeof c2_addr );
    strcpy( to_send + sizeof c2_addr, "hello" );

    send_or_die( c1_fd, to_send, nbytes, &s_addr );
    assert( recv( c2_fd, buffer, nbytes, 0 ) == expect_bytes );
    assert( memcmp( buffer, to_forward, expect_bytes ) == 0 );

    memcpy( to_send, &c3_addr, sizeof c3_addr );
    strcpy( to_send + sizeof c3_addr, "lorem ipsum" );

    send_or_die( c2_fd, to_send, nbytes, &s_addr );
    assert( recv( c3_fd, buffer, nbytes, 0 ) == expect_bytes );
    assert( memcmp( buffer, to_forward, expect_bytes ) == 0 );

    close_or_warn( c1_fd, "client 1 socket" );
    close_or_warn( c2_fd, "client 2 socket" );
    close_or_warn( c3_fd, "client 3 socket" );

    if ( kill( pid, SIGTERM ) == -1 ||
         waitpid( pid, &status, 0 ) == -1 )
        err( 1, "terminating child process" );

    assert( WIFSIGNALED( status ) );
    assert( WTERMSIG( status ) == SIGTERM );

    return 0;
}
