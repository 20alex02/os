#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <errno.h>      /* errno */

/* Uvažme proudový protokol z předchozích dvou příprav. Vaším úkolem
 * bude naprogramovat klient, který stáhne všechny zadané klíče a
 * jim odpovídající hodnoty uloží do souborů v zadané složce – název
 * souboru bude vždy jméno klíče. Seznam souborů ke stažení je zadán
 * jako zřetězený seznam.
 *
 * Pro připomenutí, požadavek klienta sestává z nulou ukončeného
 * klíče, odpověď serveru pak z čtyřbajtové velikosti hodnoty
 * (v bajtech) a pak samotné hodnoty. Nejvýznamnější bajt velikosti
 * je vždy odeslán jako první. Není-li klíč přítomen, server odešle
 * hodnotu 0xffffffff.
 *
 * Návratová hodnota bude:
 *
 *  • 0 je-li vše v pořádku (všechny hodnoty byly staženy a úspěšně
 *    uloženy),
 *  • -1 došlo-li k chybě komunikace, chybě při ukládání dat, nebo
 *    jiné fatální chybě,
 *  • -2 nebyl-li některý požadovaný klíč na serveru přítomen
 *    (nepřítomnost klíče ale nebrání stažení všech ostatních).
 *
 * Existuje-li v zadané složce soubor se stejným jménem, jaký by měl
 * podprogram ‹kvget› vytvořit, považujeme to za fatální chybu
 * (dotčený soubor se přitom nesmí nijak změnit). */

struct key_list
{
    const char *key;
    struct key_list *next;
};

int kvget( int server_fd, int dir_fd, struct key_list *keys );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <unistd.h>         /* read, write, unlink, fork, alarm */
#include <stdlib.h>         /* NULL, exit */
#include <stdint.h>         /* uint32_t */
#include <string.h>         /* memcmp */
#include <sys/socket.h>     /* socketpair */
#include <sys/types.h>      /* pid_t */
#include <sys/wait.h>       /* waitpid */
#include <arpa/inet.h>      /* htonl */
#include <fcntl.h>          /* open */
#include <err.h>            /* err */

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

    return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
}

static void write_or_die( int fd, const void *buffer, int nbytes )
{
    int bytes_written = write( fd, buffer, nbytes );

    if ( bytes_written == -1 )
        err( 1, "writing %d bytes", nbytes );

    if ( bytes_written != nbytes )
        errx( 1, "unexpected short write: %d/%d written",
              bytes_written, nbytes );
}

static int read_exactly( int fd, char *buf, int size )
{
    int remains = size;
    int bytes;
    while ( remains > 0
        && ( bytes = read( fd, buf + size - remains, remains ) ) > 0 )
    {
        remains -= bytes;
    }
    if ( bytes == -1 ) err( 2, "read" );

    return remains;
}

static int check_file( int dir, const char *file,
                       const char *expected, int len )
{
    char buffer[ 4096 ];

    int fd = openat( dir, file, O_RDONLY );
    if ( fd == -1 )
        return -1;

    int bytes;
    if ( ( bytes = read( fd, buffer, 4096 ) ) == -1 )
        err( 2, "reading %s", file );

    close_or_warn( fd, file );
    return bytes == len ? memcmp( expected, buffer, len ) : -1;
}

int ch_to_idx( char c )
{
    return c == 'a' ? 0
         : c == 'b' ? 1
         : c == 'c' ? 2
         : -1;
}

static void server( int fd, int *present )
{
    char buf[ 8 ];
    /* Server skončí, jakmile klient požádal o každý ze tří zadaných
     * klíčů. */
    int visited = 0;
    while ( visited != 0x7 )
    {
        assert( read_exactly( fd, buf, 8 ) == 0 );
        assert( buf[ 7 ] == '\0' );
        assert( strncmp( buf, "zt.p6_", 6 ) == 0 );
        int idx = ch_to_idx( buf[ 6 ] );
        assert( idx != -1 );

        if ( present[ idx ] )
        {
            uint32_t size = htonl( 19 );
            write_or_die( fd, &size, 4 );
            write_or_die( fd, "contents of ", 12 );
            write_or_die( fd, buf, 7 );
        }
        else
        {
            uint32_t size = 0xffffffff;
            write_or_die( fd, &size, 4 );
        }

        visited |= 1 << idx;
    }
}

static int fork_server( int close_fd, int *client_fd, int *present )
{
    int fds[ 2 ];
    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) == -1 )
        err( 1, "socketpair" );

    int server_fd = fds[ 0 ];
    *client_fd = fds[ 1 ];

    alarm( 5 );
    int pid = fork();

    if ( pid == -1 )
        err( 1, "fork" );

    if ( pid > 0 )
    {
        close_or_warn( server_fd, "server end of the socket" );
        return pid;
    }

    close_or_warn( close_fd, "fd to be closed" );
    close_or_warn( *client_fd, "client end of the socket" );
    server( server_fd, present );
    close_or_warn( server_fd, "server end of the socket" );
    exit( 0 );
}

int main( void )
{
    int client_fd;
    pid_t server_pid;
    int dir = open( ".", O_RDONLY );
    if ( dir == -1 ) err( 2, "opening working directory" );

    struct key_list keys[] =
    {
        { .key = "zt.p6_a" },
        { .key = "zt.p6_b" },
        { .key = "zt.p6_c" },
    };

    for ( int i = 0; i < 3; ++i )
    {
        unlink_if_exists( keys[ i ].key );
        keys[ i ].next = i < 2 ? keys + i + 1 : NULL;
    }

    /* Case 1 */

    int all[ 3 ] = { 1, 1, 1 };
    server_pid = fork_server( dir, &client_fd, all );
    assert( kvget( client_fd, dir, keys ) == 0 );
    assert( check_file( dir, "zt.p6_a", "contents of zt.p6_a", 19 ) == 0 );
    assert( check_file( dir, "zt.p6_b", "contents of zt.p6_b", 19 ) == 0 );
    assert( check_file( dir, "zt.p6_c", "contents of zt.p6_c", 19 ) == 0 );
    close_or_warn( client_fd, "client side of socket" );
    assert( reap( server_pid ) == 0 );

    /* Case 2 */

    /* Nejprve nutno smazat vytvořené soubory. */
    for ( int i = 0; i < 3; ++i )
        unlink_if_exists( keys[ i ].key );

    int third_missing[ 3 ] = { 1, 1, 0 };
    server_pid = fork_server( dir, &client_fd, third_missing );
    assert( kvget( client_fd, dir, keys ) == -2 );
    assert( check_file( dir, "zt.p6_a", "contents of zt.p6_a", 19 ) == 0 );
    assert( check_file( dir, "zt.p6_b", "contents of zt.p6_b", 19 ) == 0 );
    /* Klíč není na serveru, takže by soubor neměl být vytvořen. */
    assert( check_file( dir, "zt.p6_c", "", 0 ) == -1 );
    close_or_warn( client_fd, "client side of socket" );
    assert( reap( server_pid ) == 0 );

    for ( int i = 0; i < 3; ++i )
        unlink_if_exists( keys[ i ].key );

    close_or_warn( dir, "." );
    return 0;
}
