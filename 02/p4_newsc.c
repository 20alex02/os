#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <stdlib.h>         /* exit, NULL */
#include <string.h>         /* strlen, strcmp */
#include <unistd.h>         /* read, write, lseek, close, unlink, fork, alarm */
#include <fcntl.h>          /* open, O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC */
#include <sys/socket.h>     /* socket, bind, listen, accept, AF_UNIX */
#include <sys/un.h>         /* struct sockaddr_un */
#include <errno.h>          /* errno */
#include <err.h>            /* err, warn, errx */

/* V této přípravě je Vaším úkolem implementovat klient, který bude
 * synchronizovat data ze serveru, a to tak, že přenese vždy jen ta
 * data, která ještě nemá uložena lokálně.
 *
 * Server má uloženu sekvenci bajtů, která se může postupně
 * prodlužovat, nemůže se ale ani zkrátit, ani se již uložené bajty
 * nemohou měnit.
 *
 * Po navázání spojení klient odešle číslo ‹n› (zakódované do 4
 * bajtů, nejvýznamnější bajt první – tzn. v pořadí big endian).
 * Server obratem odešle všechny své uložené bajty počínaje tím na
 * indexu ‹n› a ukončí spojení. Hodnota ‹0xffff'ffff› je vyhrazená
 * pro nahrávání dat na server (klient v tomto příkladu tuto hodnotu
 * používat nebude).
 *
 * Naprogramujte proceduru ‹news_update›, která bude mít tyto dva
 * parametry:
 *
 *  • ‹state_fd› – popisovač souboru, který obsahuje již známá data,
 *     a do kterého budou nová data připsána, tzn. po ukončení
 *     ‹news_update› bude soubor obsahovat přesnou kopii dat ze
 *     serveru,
 *  • ‹addr› – adresa unixového socketu, na které server poslouchá.
 *
 * O popisovači ‹state_fd› nepředpokládejte nic jiného, než že se
 * jedná o obyčejný soubor otevřený pro zápis (zejména může ukazovat
 * na libovolnou pozici).
 *
 * Návratová hodnota 0 indikuje, že soubor ‹state› byl úspěšně
 * doplněn. Nastane-li libovolná chyba, procedura vrátí hodnotu -1 a
 * obsah souboru ‹state› nezmění. */

int news_update( int state_fd, const char *addr );

/* Přestože implementujete znovupoužitelný podprogram, může být
 * užitečné jej testovat interaktivně – tento program můžete spustit
 * jako ‹./p4_newsc cesta_k_souboru_s_daty cesta_k_socketu› – v takovém
 * případě se přiložené testy přeskočí. Viz též začátek procedury ‹main›. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* wait */
#include <sys/types.h>      /* pid_t */

const char *test_file_path = "zt.p4_state";
const char *test_socket_path = "zt.p4_socket";

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void unlink_if_exists( const char* file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", file );
}

static pid_t fork_server( const char hdr[4], const char *msg )
{
    struct sockaddr_un addr =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.p4_socket"
    };

    int sock_fd, client_fd;
    pid_t child_pid;

    unlink_if_exists( addr.sun_path );

    if ( ( sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 2, "creating socket" );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr,
               sizeof( addr ) ) == -1 )
        err( 2, "binding socket to %s", addr.sun_path );

    if ( listen( sock_fd, 1 ) == -1 )
        err( 2, "listening on %s", addr.sun_path );

    child_pid = fork();

    if ( child_pid == -1 )
        err( 2, "fork" );

    if ( child_pid > 0 )
    {
        close_or_warn( sock_fd, addr.sun_path );
        return child_pid;
    }

    // Terminate the server after 5 seconds in case it doesn't exit naturally
    alarm( 5 );

    if ( ( client_fd = accept( sock_fd, NULL, NULL ) ) == -1 )
        err( 2, "accepting a connection on %s", addr.sun_path );

    // Check header
    char buf[5];
    ssize_t bytes_read = read( client_fd, buf, 5 );
    if ( bytes_read == - 1 )
        err( 2, "server receiving header" );
    if ( bytes_read != 4 )
        errx( 1, "server received header of wrong size %zd", bytes_read );
    if ( memcmp( hdr, buf, 4 ) != 0 )
        errx( 1, "server received unexpected header %02x %02x %02x %02x",
                buf[0], buf[1], buf[2], buf[3] );

    if ( write( client_fd, msg, strlen( msg ) ) == -1 )
        err( 2, "server writing to %s", addr.sun_path );

    close_or_warn( client_fd, "server side of the connection" );
    close_or_warn( sock_fd, "server socket" );

    exit( 0 );
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

static int test_file_contains( const char *contents )
{
    int fd = open( test_file_path, O_RDONLY );
    if ( fd == -1 )
        err( 1, "opening %s for reading", test_file_path );
    char buf[256];
    ssize_t bytes_read = read( fd, buf, 255 );
    if ( bytes_read == -1 )
        err( 1, "reading from %s", test_file_path );
    close_or_warn( fd, test_file_path );
    buf[bytes_read] = '\0';
    return strcmp( buf, contents ) == 0;
}

int main( int argc, char** argv )
{
    if ( argc == 3 )
    {
        int fd = open( argv[1], O_WRONLY | O_CREAT, 0644 );
        if ( fd == -1 )
            err( 1, "opening %s for writing", argv[1]  );
        return news_update( fd, argv[2] );
    }

    int fd = open( test_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
    if ( fd == -1 )
        err( 1, "truncating %s", test_file_path );

    pid_t pid = fork_server( "\0\0\0\0", "foo" );
    assert(( news_update( fd, test_socket_path ) == 0 ));
    assert( reap( pid ) == 0 );
    assert( test_file_contains( "foo" ) );

    pid = fork_server( "\0\0\0\3", "bar" );
    assert(( news_update( fd, test_socket_path ) == 0 ));
    assert( reap( pid ) == 0 );
    assert( test_file_contains( "foobar" ) );

    if ( lseek( fd, 2, SEEK_SET ) == -1 )
        err( 1, "seek in %s", test_file_path );

    pid = fork_server( "\0\0\0\6", "baz" );
    assert(( news_update( fd, test_socket_path ) == 0 ));
    assert( reap( pid ) == 0 );
    assert( test_file_contains( "foobarbaz" ) );

    unlink_if_exists( test_socket_path );
    close_or_warn( fd, test_file_path );
    return 0;
}
