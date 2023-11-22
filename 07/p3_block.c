#define _POSIX_C_SOURCE 200809L

#include <err.h>            /* err, warn */
#include <assert.h>         /* assert */
#include <sys/un.h>         /* sockaddr_un */
#include <sys/socket.h>     /* send, recv */

/* Napište proceduru ‹block›, která obdrží:
 *
 *  • dva popisovače otevřených «datagramových» socketů ‹in_fd› a
 *    ‹out_fd›,
 *  • ukazatel na kořen binárního vyhledávacího stromu, který
 *    obsahuje adresy odesílatelů, které bude blokovat, a konečně
 *  • ‹count› je počet datagramů, které procedura nejvýše zpracuje.
 *
 * Je-li zdrojová adresa datagramu přijatá na popisovači ‹in› ve
 * stromě přítomna, tento datagram je bez náhrady zahozen. V opačném
 * případě je beze změny přeposlán na popisovač ‹out›.
 *
 * Výsledkem je
 *
 *  • 0, bylo-li bez chyb zpracováno (přeposláno nebo zamítnuto)
 *    ‹count› datagramů,
 *  • -1 došlo-li k nenapravitelné systémové chybě nebo
 *  • -2 v případě, že se některý datagram, který měl být přeposlán,
 *    nepodaří odeslat.
 *
 * Procedura bude pracovat výhradně s adresami rodiny ‹AF_UNIX›.
 *
 * Vstupní binární vyhledávací strom je řazen vzestupně vzhledem
 * k ‹strcmp› na atributu ‹sun_path› unixových adres.
 *
 * Za maximální velikost datagramu považujte 2¹⁶ - 1  bajtů. */

struct address_map
{
    struct sockaddr_un source;
    struct address_map *left, *right;
};

int block( int in_fd, int out_fd,
           struct address_map *to_block, int count );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <errno.h>          /* errno */
#include <sys/socket.h>     /* socket, bind, send, recv, socketpair */
#include <stdlib.h>         /* NULL, exit */
#include <stdio.h>          /* snprintf */
#include <string.h>         /* strlen */
#include <unistd.h>         /* alarm, fork */
#include <sys/wait.h>       /* wait */
#include <poll.h>           /* poll */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink %s", file );
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

static void bind_unix( int sock_fd, struct sockaddr_un *addr )
{
    unlink_if_exists( addr->sun_path );
    if ( bind( sock_fd, ( struct sockaddr* )addr,
               sizeof( struct sockaddr_un ) ) == -1 )
        err( 2, "binding %s", addr->sun_path );
}

static void send_from_to( const char *str, struct sockaddr_un *addr,
                          struct sockaddr_un *to )
{
    int sock_fd = socket_or_die();

    if ( addr )
        bind_unix( sock_fd, addr );

    if ( sendto( sock_fd, str, strlen( str ), 0,
                 ( struct sockaddr* )to, sizeof( *to ) ) == -1 )
        err( 2, "sending \"%s\" to %s", str, to->sun_path );

    close_or_warn( sock_fd, "socket" );
}

static int check_recv( int fd, const char *str )
{
    char buf[ 256 ] = { 0 };

    int bytes = recv( fd, buf, sizeof buf, 0 );
    if ( bytes == -1 )
        err( 2, "recv for \"%s\"", str );

    if ( bytes != ( int )strlen( str ) )
        return 0;

    return strncmp( str, buf, strlen( str ) ) == 0;
}

static int check_no_recv( int fd )
{
    /* Počká se nejdéle 1 sekundu a pokud do té doby žádný
     * datagram nepřijde, považuje se to za úspěch. */
    struct pollfd polled = { .fd = fd, .events = POLLIN };
    int rv = poll( &polled, 1, 1000 );
    if ( rv == -1 ) err( 2, "poll" );

    if ( rv > 0 )
    {
        char buf[ 256 ] = { 0 };
        int bytes = recv( fd, buf, sizeof buf, 0 );

        if ( bytes == -1 )
            err( 2, "recv after poll() > 0" );

        printf( "received unexpected \"%*s\" of size %d\n", bytes,
                buf, bytes );
    }
    return rv == 0;
}

static pid_t fork_block( struct sockaddr_un *in_addr, int out_fd,
                         struct address_map *to_block, int count,
                         int close_fd )
{
    int in_fd = socket_or_die();
    bind_unix( in_fd, in_addr );

    pid_t pid = fork();
    alarm( 10 );  /* Die after 10s if anything gets stuck. */

    if ( pid == -1 ) err( 2, "fork" );
    if ( pid > 0 )
    {
        close_or_warn( in_fd, "input socket" );
        close_or_warn( out_fd, "output socket" );
        return pid;
    }

    close_or_warn( close_fd, "child given fd to close" );
    int rv = block( in_fd, out_fd, to_block, count );
    close_or_warn( in_fd, "input socket" );
    close_or_warn( out_fd, "output socket" );
    exit( rv );
}

static int reap( pid_t pid )
{
    int status;

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "wait" );

    return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
}

int main( void )
{
    struct sockaddr_un in_addr = make_unix_addr( "zt.p3_in" );
    struct sockaddr_un allowed = make_unix_addr( "zt.p3_allowed" );

    struct address_map blocked[ 5 ] =
    {
        { .source = make_unix_addr( "zt.p3_a_ll" ) },
        { .source = make_unix_addr( "zt.p3_b_l" ) },
        { .source = make_unix_addr( "zt.p3_c_lr" ) },
        { .source = make_unix_addr( "zt.p3_d_root" ) },
        { .source = make_unix_addr( "zt.p3_e_r" ) },
    };

    blocked[ 3 ].left = blocked + 1;
    blocked[ 3 ].right = blocked + 4;
    blocked[ 1 ].left = blocked;
    blocked[ 1 ].right = blocked + 2;

    int fds[ 2 ];
    if ( socketpair( AF_UNIX, SOCK_DGRAM, 0, fds ) == -1 )
        err( 2, "socketpair" );

    int check_fd = fds[ 0 ];
    pid_t pid = fork_block( &in_addr, fds[ 1 ], blocked + 3, 7,
                            check_fd );

    send_from_to( "blocked", &blocked[ 0 ].source, &in_addr );
    assert( check_no_recv( check_fd ) );

    send_from_to( "hello", &allowed, &in_addr );
    assert( check_recv( check_fd, "hello" ) );

    send_from_to( "blocked", &blocked[ 1 ].source, &in_addr );
    assert( check_no_recv( check_fd ) );

    send_from_to( "world", &allowed, &in_addr );
    assert( check_recv( check_fd, "world" ) );

    send_from_to( "blocked", &blocked[ 2 ].source, &in_addr );
    send_from_to( "blocked", &blocked[ 3 ].source, &in_addr );
    send_from_to( "blocked", &blocked[ 4 ].source, &in_addr );
    assert( check_no_recv( check_fd ) );

    assert( reap( pid ) == 0 );

    close_or_warn( check_fd, "other end of output socket" );

    for ( int i = 0; i < 5; ++i )
        unlink_if_exists( blocked[ i ].source.sun_path );
    unlink_if_exists( in_addr.sun_path );
    unlink_if_exists( allowed.sun_path );
    return 0;
}
