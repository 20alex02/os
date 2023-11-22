#define _POSIX_C_SOURCE 200809L
#include <errno.h>          /* errno */
#include <string.h>         /* strlen, strcmp */
#include <sys/socket.h>     /* socket, connect, AF_UNIX */
#include <sys/un.h>         /* struct sockaddr_un */
#include <err.h>            /* err, warn, warnx */

/* Naprogramujte proceduru ‹router›, která bude přeposílat pakety
 * podle tabulky, která je zadaná binárním vyhledávacím stromem.
 * Pro každý příchozí datagram:
 *
 *  1. vyhledejte odesílatele ve stromě (adresa ‹source› musí být
 *     stejná jako adresa odesílatele datagramu; strom je seřazen
 *     podle ‹source›),¹
 *  2. je-li odpovídající záznam ve stromě přítomen, přepošlete
 *     datagram na adresu ‹destination› uvedenou v tomto záznamu.
 *
 * Předpokládejte, že maximální velikost datagramu bude 512 bajtů.
 *
 * Za normálních okolností se procedura ‹router› nikdy nevrátí.
 * Nastane-li při běhu procedury ‹router› fatální chyba, program
 * ukončete s vhodným chybovým hlášením (rozmyslete si ale, které
 * chyby lze považovat za fatální). Při chybách, které umožňují
 * pokračování v běhu, vypište pouze varování na standardní chybový
 * výstup.
 *
 * ¹ Ve smyslu vyhledávacího stromu – adresy v levém podstromě jsou
 *   lexikograficky menší, a v pravém podstromě lexikograficky
 *   větší, než je adresa v kořenu. */

struct address_map
{
    struct sockaddr_un source, destination;
    struct address_map *left, *right;
};

void router( int sock_fd, struct address_map *root );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <stdlib.h>     /* exit, NULL */
#include <unistd.h>     /* close, read, write, unlink, fork, alarm */
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

int fork_server( struct address_map *root )
{
    int sock_fd;
    struct sockaddr_un addr = make_addr( "zt.r4_socket" );
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
        router( sock_fd, root );
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

    struct sockaddr_un addr_server = make_addr( "zt.r4_socket" );
    struct sockaddr_un addr_client = make_addr( socket_addr );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr_client,
               sizeof( addr_client ) ) == -1 )
        err( 2, "binding socket to %s", addr_client.sun_path );

    if ( connect( sock_fd, (const struct sockaddr *) &addr_server,
                  sizeof addr_server ) == -1 )
        err( 1, "connecting to socket" );

    return sock_fd;
}

size_t write_or_die( int fd, const char *what )
{
    int wrote = write( fd, what, strlen( what ) );
    if ( wrote == -1 )
        err( 1, "write" );
    return wrote;
}

int main( void )
{
    struct address_map c1 =
    {
        .source      = make_addr( "zt.r4_socket_1"),
        .destination = make_addr( "zt.r4_socket_2" ),
        .left = NULL, .right = NULL
    };

    struct address_map c3 =
    {
        .source      = make_addr( "zt.r4_socket_3"),
        .destination = make_addr( "zt.r4_socket_4" ),
        .left = NULL, .right = NULL
    };

    struct address_map c4 =
    {
        .source      = make_addr( "zt.r4_socket_4"),
        .destination = make_addr( "zt.r4_socket_5" ),
        .left = &c3, .right = NULL
    };

    struct address_map c2 =
    {
        .source      = make_addr( "zt.r4_socket_2"),
        .destination = make_addr( "zt.r4_socket_3" ),
        .left = &c1, .right = &c4
    };

    int status;
    pid_t pid = fork_server( &c2 );

    const size_t buffer_size = 512;
    char buffer[ buffer_size ];
    memset( buffer, 0, buffer_size );

    int client_1 = make_client( "zt.r4_socket_1" );
    int client_2 = make_client( "zt.r4_socket_2" );
    int client_3 = make_client( "zt.r4_socket_3" );
    int client_4 = make_client( "zt.r4_socket_4" );
    int client_5 = make_client( "zt.r4_socket_5" );

    int nwrote;
    nwrote = write_or_die( client_1, "hello" );
    assert( recv( client_2, buffer, buffer_size, 0 ) == nwrote );
    assert( memcmp( buffer, "hello", 5 ) == 0 );

    nwrote = write_or_die( client_3, "world" );
    assert( recv( client_4, buffer, buffer_size, 0 ) == nwrote );
    assert( memcmp( buffer, "world", 5 ) == 0 );

    nwrote = write_or_die( client_4, "lorem" );
    assert( recv( client_5, buffer, buffer_size, 0 ) == nwrote );
    assert( memcmp( buffer, "lorem", 5 ) == 0 );

    write_or_die( client_5, "shouldn't die" );

    nwrote = write_or_die( client_2, "dolor" );
    assert( recv( client_3, buffer, buffer_size, 0 ) == nwrote );
    assert( memcmp( buffer, "dolor", 5 ) == 0 );

    close( client_5 );
    write_or_die( client_4, "shouldn't die either" );

    nwrote = write_or_die( client_1, "hello" );
    assert( recv( client_2, buffer, buffer_size, 0 ) == nwrote );
    assert( memcmp( buffer, "hello", 5 ) == 0 );

    close( client_1 );
    close( client_2 );
    close( client_3 );
    close( client_4 );

    if ( kill( pid, SIGTERM ) == -1 ||
         waitpid( pid, &status, 0 ) == -1 )
        err( 1, "terminating child process" );

    assert( WIFSIGNALED( status ) );
    assert( WTERMSIG( status ) == SIGTERM );

    return 0;
}
