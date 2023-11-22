#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <stdlib.h>         /* exit, NULL */
#include <errno.h>          /* errno */
#include <sys/socket.h>     /* socket, connect */
#include <sys/un.h>         /* sockaddr_un */
#include <err.h>            /* err, warn */
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* V této přípravě je Vaším úkolem naprogramovat jednoduchý server
 * implementující protokol echo v proudově orientované verzi.
 * Tento server od klienta přijme data a stejná mu odpoví. Bude tak
 * protějškem ke klientu z ‹p1_echoc›.
 *
 * Naprogramujte proceduru ‹echo_server›, která přijímá jediný
 * parametr: adresu unxiového socketu, který má vytvořit a na němž
 * má poslouchat.
 *
 * Procedura ‹echo_server› vytváří server, který by měl běžet, dokud
 * nebude zvenčí indikováno, že má skončit. Toho zde docílíme
 * zabitím procesu (např. signálem jako v testech níže).
 * Pro zjednodušení «není» třeba tento případ v kódu zachycovat
 * (např. zpracováním signálů), jelikož to je nad rámec tohoto
 * kurzu.
 *
 * Na proceduru ‹echo_server› tedy pohlížíme jako na kompletní
 * program a po zavolání neočekáváme, že se z ní program vrátí.
 *
 * Jestliže nastane chyba při vytváření serveru, ukončí se «program»
 * s návratovým kódem ‹2›. Pokud vše proběhne v pořádku, server
 * poslouchá na zadaném socketu a zpracovává spojení.
 *
 * Nastane-li chyba během komunikace s některým klientem, ukončí se
 * toto spojení a program pokračuje čekáním na dalšího klienta
 * (můžete v tomto případě vypsat nějaké varování na standardní
 * chybový výstup). Není-li možné pokračovat, program skončí
 * s chybou (rovněž návratový kód ‹2›). */

void echo_server( const char *sock_path ) {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        err( 2, "creating a unix socket" );
    }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    if (strlen(sock_path) >= sizeof sa.sun_path - 1) {
        errx( 2, "socket address too long, maximum is %zu",
              sizeof sa.sun_path );
    }
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path);
    if ( unlink( sa.sun_path ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", sa.sun_path );
    if ( bind( sock_fd, ( struct sockaddr * ) &sa, sizeof sa ) )
        err( 2, "binding a unix socket to %s", sa.sun_path );
    if ( listen( sock_fd, 5 ) )
        err( 2, "listen on %s", sa.sun_path );
    int client_fd, bytes;
    char buffer[ 64 ];
    while ( ( client_fd = accept( sock_fd, NULL, NULL ) ) >= 0 ) {
        while ( ( bytes = read( client_fd, buffer, 64 ) ) > 0 ) {
            if (write(client_fd, buffer, bytes) != bytes) {
                err(2, "write");
            }
        }
        if ( bytes == -1 ) {
            warn( "reading from client" );
        }
        if ( close( client_fd ) == -1 ) {
            warn( "closing connection" );
        }
    }
    err( 2, "accept" );
}

/* Jelikož implementujete kompletní server, může být užitečné jej
 * testovat interaktivně – můžete ho spustit jako
 *     ./p5_echod cesta_k_socketu
 * v takovém případě se přiložené testy přeskočí. Viz též začátek
 * procedury ‹main›. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <string.h>         /* memcmp */
#include <stdbool.h>        /* bool */

#include <unistd.h>         /* fork, unlink, close */
#include <fcntl.h>          /* open */

#include <sys/wait.h>       /* wait */
#include <sys/types.h>      /* pid_t */
#include <time.h>           /* nanosleep */
#include <signal.h>         /* kill, SIGTERM */

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", file );
}

static void close_or_die( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        err( 2, "err: closing %s", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "warn: closing %s", name );
}

static pid_t fork_server( const char *sock_path )
{
    pid_t pid = fork();

    if ( pid == -1 ) err( 2, "fork" );

    /* Rodič se hned vrátí a pokračuje dále v ‹main›. */
    if ( pid > 0 )
        return pid;

    /* V potomku poslouchá server. */
    echo_server( sock_path );

    /* ‹echo_server› se vrátilo z funkce místo ukončení programu. */
    assert( false );
}

static void message( const char *msg, int size, pid_t server )
{
    int sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock == -1 ) err( 2, "socket" );

    struct sockaddr_un addr = { .sun_family = AF_UNIX,
                                .sun_path = "zt.p5_socket", };

    /* Jelikož není garance, že socket byl v druhém procesu už
     * vytvořen a server na něm poslouchá, je třeba se dotazovat
     * opakovaně. */
    while ( 1 )
    {
        if ( connect( sock, ( struct sockaddr* )&addr,
                      sizeof( addr ) ) == 0 )
            break;
        else if ( errno != ECONNREFUSED && errno != ENOENT )
            err( 2, "client: connect" );
        else if ( waitpid( server, 0, WNOHANG ) == server )
            errx( 1, "client: server died" );

        /* Před dalším pokusem chvíli počkat (100 ms). */
        struct timespec ts = { .tv_nsec = 100000000 };
        if ( nanosleep( &ts, NULL ) == -1 ) warn( "client: nsleep" );
    }

    int out = openat( AT_FDCWD, "zt.p5_test_out",
                      O_CREAT | O_TRUNC | O_WRONLY, 0666 );
    if ( out == -1 ) err( 2, "openat" );

    if ( write( sock, msg, size ) == -1 )
        err( 2, "client: write(socket)" );

    char buf[ 4096 ];
    int bytes;
    while ( size > 0
        && ( bytes = read( sock, buf, sizeof( buf ) ) ) > 0 )
    {
        size -= bytes;
        if ( write( out, buf, bytes ) == -1 )
            err( 2, "client: write(out)" );
    }

    close_or_warn( out, "client: out" );
    close_or_die( sock, "client: socket" );
}

static bool check_response( const char *str, int len )
{
    int fd = openat( AT_FDCWD, "zt.p5_test_out", O_RDONLY );
    if ( fd == -1 ) err( 2, "opening zt.p5_test_out" );

    int bytes;
    char buf[ 4096 ];
    int size = 0;

    while ( ( bytes = read( fd, buf + size, sizeof( buf ) - size )
            ) > 0 )
    {
        size += bytes;
    }

    if ( bytes == -1 ) err( 2, "read" );

    close_or_warn( fd, "zt.p5_test_out" );

    if ( size != len )
        return false;

    return memcmp( str, buf, len ) == 0;
}

static int reap( pid_t pid )
{
    int status;

    if ( waitpid( pid, &status, 0 ) == -1 ) err( 2, "waitpid" );

    return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
}

int main( int argc, char **argv )
{
    if ( argc == 2 )
    {
        echo_server( argv[ 1 ] );
        return 3;
    }

    /* Spustí ‹echo_server› ve vedlejším procesu. */
    pid_t server_pid = fork_server( "zt.p5_socket" );

    message( "hello world", 11, server_pid );
    assert( check_response( "hello world", 11 ) );

    message( "ahoj2", 5, server_pid );
    assert( check_response( "ahoj2", 5 ) );

    message( "nejneobhospodářovávatelnějšími", 36, server_pid );
    assert( check_response( "nejneobhospodářovávatelnějšími", 36 ) );

    char str[] = "3fc9b689459d738f8c88a3a48aa9e33542016b7a4";
    message( str, sizeof( str ), server_pid );
    assert( check_response( str, sizeof( str ) ) );

    char bin_data[] = { 0xAB, 0x45, 0x00, 0xE3, 0xAA, 0x59 };
    message( bin_data, 6, server_pid );
    assert( check_response( bin_data, 6 ) );

    /* Ukončíme serverový proces signálem a sklidíme jeho ostatky. */
    if ( kill( server_pid, SIGTERM ) == -1 ) err( 2, "kill" );
    reap( server_pid );

    /* Chyba při otevření socketu. Kontrola, že proces skončil
     * s patřičnou návratovou hodnotou. */
    server_pid = fork_server( "/dev/null" );
    assert( reap( server_pid ) == 2 );

    /* Po úspěšných testech smazat dočasný soubor a přebývající
     * soket. */
    unlink_if_exists( "zt.p5_test_out" );
    unlink_if_exists( "zt.p5_socket" );
    return 0;
}
