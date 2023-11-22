#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <string.h>         /* strlen */
#include <errno.h>          /* errno */
#include <err.h>            /* err, warn */

#include <sys/socket.h>     /* socket, bind */
#include <sys/un.h>         /* sockaddr_un */
#include <unistd.h>         /* fork, unlink, close */

#include <stdio.h>
#include <stdlib.h>

/* V této úloze je Vaším úkolem naprogramovat klient pro velmi
 * jednoduchý protokol echo, konkrétně jeho proudově orientovanou
 * verzi.¹ Server tohoto protokolu od klienta přijímá data a obratem
 * je odesílá nezměněná zpět.
 *
 * Implementujte proceduru ‹check_echo›, která obdrží adresu
 * unxiového socketu, data která má odeslat a jejich délku, a
 * následně pomocí této zprávy ověří, že na předané adrese běží
 * korektní server² protokolu echo. Můžete předpokládat, že
 * testovací zpráva, která bude proceduře ‹check_echo› předána, je
 * menší, než implicitní kapacita vyrovnávací paměti socketu.
 *
 * Návratová hodnota nechť je:
 *
 *  • 0 je-li ověření úspěšné,
 *  • 1 je-li zjištěno, že server se nechová korektně,
 *  • 2 dojde-li k systémové chybě.
 *
 * ¹ Protokol echo je přes svoji jednoduchost standardizovaný,
 *   konkrétně v RFC 862.
 * ² Server považujeme za korektní i v případě, že odpoví pomocí
 *   křišťálové koule – Vámi odeslaná data nemusí vůbec přečíst,
 *   bude-li jeho odpověď i tak správná. */

int check_echo(const char* sock_path, const char* msg, int size) {
    int rv = 2;
    char *buffer = NULL;
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        return rv;
    }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    if (strlen(sock_path) >= sizeof sa.sun_path - 1) {
        goto error;
    }
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path);
    if (connect(sock_fd, (struct sockaddr *) &sa,sizeof sa) == -1) {
        goto error;
    }
    int nbytes;
    int offset = 0;
    while((nbytes = write(sock_fd, &msg[offset], size - offset)) > 0) {
        offset = nbytes;
    }
    if (nbytes == -1) {
        goto error;
    }
    offset = 0;
    if ((buffer = malloc((size + 1) * sizeof(char))) == NULL) {
        goto error;
    }
    memset(buffer, 0, size + 1);
    while ((nbytes = read(sock_fd, &buffer[offset], size - offset)) > 0) {
        offset = nbytes;
    }
    if (nbytes == -1) {
        goto error;
    }
    rv = strcmp(buffer, msg) == 0 ? 0 : 1;
  error:
    free(buffer);
    if (close(sock_fd) == -1) {
        warn("closing %s", sock_path);
    }
    return rv;
}

/* Přesto, že implementujete znovupoužitelný podprogram, může být
 * užitečné jej testovat interaktivně – tento program můžete spustit
 * jako ‹./p1_echoc cesta_k_socketu zpráva› – v takovém případě se
 * přiložené testy přeskočí. Viz též začátek procedury ‹main›. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <stdlib.h>         /* exit, NULL */
#include <poll.h>           /* poll */
#include <sys/wait.h>       /* wait */
#include <sys/types.h>      /* pid_t */

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

static pid_t fork_server( const char* msg, int size )
{
    struct sockaddr_un addr =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.p1_socket"
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

    if ( ( client_fd = accept( sock_fd, NULL, NULL ) ) == -1 )
        err( 2, "accepting a connection on %s", addr.sun_path );

    if ( write( client_fd, msg, size ) == -1 )
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

int main( int argc, char** argv )
{
    if ( argc == 3 )
        return check_echo( argv[ 1 ], argv[ 2 ], strlen( argv[ 2 ] ) );

    const char *socket = "zt.p1_socket";
    pid_t server_pid;

    server_pid = fork_server( "hello", 5 );
    assert( check_echo( socket, "hello", 5 ) == 0 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( "world", 5 );
    assert( check_echo( socket, "world", 5 ) == 0 );
    assert( reap( server_pid ) == 0 );

    char str[] = "3fc9b689459d738f8c88a3a48aa9e33542016b7a4";
    server_pid = fork_server( str, strlen( str ) );
    assert( check_echo( socket, str, strlen( str ) ) == 0 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( "wrlod", 5 );
    assert( check_echo( socket, "world", 5 ) == 1 );
    assert( reap( server_pid ) == 0 );

    char data_s = 's', data_c = 'c';
    server_pid = fork_server( &data_s, 1 );
    assert( check_echo( socket, &data_c, 1 ) == 1 );
    assert( reap( server_pid ) == 0 );

    assert( check_echo( "/dev/null", "ahoj", 4 ) == 2 );

    unlink_if_exists( socket );
    return 0;
}
