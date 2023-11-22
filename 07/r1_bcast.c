#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <sys/socket.h>     /* socket, bind, send, recv */
#include <sys/un.h>         /* sockaddr_un */
#include <unistd.h>         /* unlink, close */

/* Naprogramujte proceduru ‹bcast›, která obdrží:
 *
 *  • popisovač datagramového socketu,
 *  • zřetězený seznam adres.
 *
 * Jejím úkolem bude každý přijatý datagram přeposlat na všechny
 * adresy v seznamu, s výjimkou původního odesílatele.
 *
 * Jako maximální možnou délku datagramu považujte pro tento
 * úkol 4 KiB.
 *
 * V případě že nastane nějaká chyba, vypište pouze varování
 * a pokračujte, je-li to možné. */

struct address_list
{
    struct sockaddr_un address;
    struct address_list *next;
};

void bcast( int sock_fd, struct address_list *addresses );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <stdio.h>          /* snprintf */
#include <stdlib.h>         /* exit, NULL */
#include <string.h>         /* memcmp */
#include <unistd.h>         /* alarm, fork */
#include <sys/wait.h>       /* wait */
#include <poll.h>           /* poll */
#include <signal.h>         /* kill, SIGTERM */
#include <err.h>            /* err, warn */

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

static struct sockaddr_un make_unix_addr( const char *path )
{
    struct sockaddr_un addr = { .sun_family = AF_UNIX, };
    if ( snprintf( addr.sun_path, sizeof addr.sun_path, "%s",
                   path ) >= ( int )sizeof addr.sun_path )
        err( 2, "path too long %s", path );
    return addr;
}

static int socket_or_die()
{
    int sock_fd = socket( AF_UNIX, SOCK_DGRAM, 0 );
    if ( sock_fd == -1 ) err( 2, "creating socket" );
    return sock_fd;
}

static int create_peer( struct sockaddr_un *addr )
{
    int sock_fd = socket_or_die();

    unlink_if_exists( addr->sun_path );

    if ( bind( sock_fd, ( struct sockaddr* )addr,
               sizeof( *addr ) ) == -1 )
        err( 2, "binding %s", addr->sun_path );

    return sock_fd;
}


static pid_t fork_bcast( int sock_fd, struct address_list *addresses )
{
    pid_t pid = fork();
    alarm( 5 ); /* die after 5s if anything gets stuck */

    if ( pid == -1 ) err( 2, "fork" );
    if ( pid > 0 )
    {
        close_or_warn( sock_fd, "socket" );
        return pid;
    }

    bcast( sock_fd, addresses );
    exit( 1 );
}

int main( void )
{
    struct sockaddr_un addr = make_unix_addr( "zt.r2_socket_bcast" );

    struct address_list linked[ 3 ] =
    {
        { .address = make_unix_addr( "zt.r2_socket_a" ) },
        { .address = make_unix_addr( "zt.r2_socket_b" ) },
        { .address = make_unix_addr( "zt.r2_socket_c" ) },
    };

    int bcast_fd = create_peer( &addr );
    const char *msg = "hello world";

    for ( int i = 0; i < 2; ++i )
        linked[ i ].next = &linked[ i + 1 ];

    pid_t pid = fork_bcast( bcast_fd, linked );

    char buf[ 4096 ];
    int bytes, ready, status;
    int sock_fds[ 3 ];
    struct pollfd pfd[ 3 ];

    for ( int i = 0; i < 3; ++i )
        sock_fds[ i ] = create_peer( &linked[ i ].address );

    if ( sendto( sock_fds[ 2 ], msg, strlen( msg ), 0,
                 ( struct sockaddr * ) &addr, sizeof addr ) == -1 )
        err( 2, "sending to %s", addr.sun_path );

    for ( int i = 0; i < 3; ++i )
    {
        pfd[ i ].fd = sock_fds[ i ];
        pfd[ i ].events = POLLIN;

        if ( i == 2 )
            continue;

        if ( ( bytes = recv( sock_fds[ i ], buf,
                             sizeof buf, 0 ) ) == -1 )
            err( 2, "recv on client %d", i );

        assert( bytes == ( int ) strlen( msg ) );
        assert( memcmp( msg, buf, strlen( msg ) ) == 0 );
    }

    if ( ( ready = poll( pfd, 3, 1000 ) ) == -1 )
        err( 2, "poll" );

    assert( ready == 0 );

    if ( kill( pid, SIGTERM ) == -1 ||
         waitpid( pid, &status, 0 ) == -1 )
        err( 1, "terminating child process" );

    assert( WIFSIGNALED( status ) );
    assert( WTERMSIG( status ) == SIGTERM );

    unlink_if_exists( addr.sun_path );

    for ( int i = 0; i < 3; ++i )
        unlink_if_exists( linked[ i ].address.sun_path );

    return 0;
}
