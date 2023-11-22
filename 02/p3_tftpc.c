#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>

/* Vaším úkolem je tentokrát naprogramovat klient protokolu TFTP
 * (Trivial File Transfer Protocol, RFC 1350). Jedná se o paketově
 * (datagramově) orientovaný protokol pro přenos souborů.
 *
 * Procedura ‹tftp_put› pomocí protokolu TFTP na server uloží dodaný
 * soubor. Parametry:
 *
 *  • ‹addr› – adresa unixového socketu na které běží server,
 *  • ‹name› – nulou ukončený název souboru,
 *  • ‹data› – ukazatel na data souboru, který má být uložen,
 *  • ‹size› – velikost souboru.
 *
 * Nahrání souboru probíhá v protokolu TFTP takto:
 *
 *  1. klient odešle paket ‹WRQ›, který obsahuje:
 *   
 *     ◦ kód příkazu – bajty ‹0x00 0x02›,
 *     ◦ název souboru ukončený nulovým bajtem,
 *     ◦ řetězec ‹"octet"›, ukončen dalším nulovým bajtem,
 *   
 *  2. server odpoví paketem ‹ACK› nebo ‹ERROR› (viz níže),
 *  3. je-li odpověď ‹ACK›, klient pokračuje odesláním paketu typu
 *     ‹DATA›, který obsahuje:
 *     
 *     ◦ kód příkazu – bajty ‹0x00 0x03›,
 *     ◦ dvoubajtové sekvenční číslo (první ‹DATA› paket má číslo 1,
 *       každý další pak o jedna větší než předchozí),
 *     ◦ 0–512 bajtů dat (každý paket krom posledního má 512 bajtů
 *       dat, poslední paket zbytek – může být i nula),
 *   
 *  4. server potvrdí příjem datového paketu opět paketem ‹ACK›,
 *     který zároveň slouží jako výzva k odeslání dalšího datového
 *     paketu,
 *  5. odpověď ‹ACK› na poslední ‹DATA› paket signalizuje, že přenos
 *     skončil a klient může spojení uzavřít.
 *
 * Pakety, které odesílá server, vypadají takto:
 *
 *  • ‹ACK› – začíná bajty ‹0x00 0x04›, další dva bajty určují
 *    pořadové číslo paketu, na který navazují (0 pro paket ‹WRQ›, 1
 *    a výše pro pakety ‹DATA›),
 *  • ‹ERROR› – začíná bajty ‹0x00 0x05›, dále dvoubajtový kód
 *    chyby, řetězec popisující chybu a nulový bajt.
 *
 * Všechny dvoubajtové číselné hodnoty jsou odesílány v pořadí vyšší
 * bajt, nižší bajt (tzv. „big endian“).
 *
 * Procedura ‹tftp_put› vrátí:
 *
 *  • 0 – přenos byl úspěšný,
 *  • kladné číslo – přenos skončil chybou na straně serveru,
 *    návratový kód odpovídá kódu chyby z paketu ‹ERROR›,
 *  • -3 – server ukončil přenos s chybou 0 (paketem ‹ERROR›),
 *  • -2 – neočekávaný paket od serveru (špatný kód příkazu, špatné
 *    sekvenční číslo v paketu typu ‹ACK›, špatná velikost paketu),
 *  • -1 – nastala systémová chyba na straně klienta. */

int tfpt_put( const char *addr, const char *name,
              const void *data, size_t size );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

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

int receive_file( int client_fd, const char *name, const char *response,
                  bool should_exit )
{
    char buf[ 2048 ];
    int name_len = strlen( name );

    struct sockaddr_un _src_addr;
    struct sockaddr *src_addr = ( struct sockaddr * ) &_src_addr;
    socklen_t addr_len = sizeof( _src_addr );

    ssize_t received = recvfrom( client_fd, buf, sizeof( buf ), 0,
                                 src_addr, &addr_len );
    if ( received == -1 )
        return -1;

    assert( received == name_len + 9 );
    assert( buf[ 0 ] == 0x00 && buf[ 1 ] == 0x02 );
    assert( strncmp( buf + 2, name, name_len + 1 ) == 0 );
    assert( strncmp( buf + 3 + name_len, "octet\0", 6 ) == 0 );

    if ( sendto( client_fd, response, 4, 0, src_addr, addr_len ) == -1 )
        return -1;

    if ( should_exit )
        return 0;

    received = recv( client_fd, buf, sizeof( buf ), 0 );
    if ( received == -1 )
        return -1;

    assert( received >= 2 );
    assert( buf[ 0 ] == 0x00 && buf[ 1 ] == 0x03 );

    if ( sendto( client_fd, "\x00\x04\x00\x01", 4, 0, src_addr, addr_len ) == -1 )
        return -1;

    return 0;
}

int fork_server( const char *name, const char *response, bool should_exit )
{
    struct sockaddr_un addr =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.p3_socket"
    };

    int sock_fd;

    unlink_if_exists( addr.sun_path );

    if ( ( sock_fd = socket( AF_UNIX, SOCK_DGRAM, 0 ) ) == -1 )
        err( 2, "creating socket" );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr,
               sizeof( addr ) ) == -1 )
        err( 2, "binding socket to %s", addr.sun_path );


    pid_t child_pid = fork();

    if ( child_pid == -1 )
        err( 2, "fork" );

    if ( child_pid > 0 )
    {
        close_or_warn( sock_fd, addr.sun_path );
        return child_pid;
    }

    if ( receive_file( sock_fd, name, response, should_exit ) == -1 )
        err( 2, "receive file" );

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
int main( int argc, char **argv )
{
    if ( argc == 4 )
        return tfpt_put( argv[ 1 ], argv[ 2 ], argv[ 3 ],
                         strlen( argv[ 3 ] ) );

    const char *socket = "zt.p3_socket";
    pid_t server_pid;

    server_pid = fork_server( "foo.txt", "\x00\x04\x00\x00", false );
    assert( tfpt_put( socket, "foo.txt", "hello", 5 ) == 0 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( "bar.txt", "\x00\x04\x00\x00", false );
    assert( tfpt_put( socket, "bar.txt", "he\x00he", 5 ) == 0 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( "baz.txt", "\x00\x04\x00\x08", true );
    assert( tfpt_put( socket, "baz.txt", "world", 5 ) == -2 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( "qux.txt", "\x00\x05\x00\x00", true );
    assert( tfpt_put( socket, "qux.txt", "hello", 5 ) == -3 );
    assert( reap( server_pid ) == 0 );

    assert( tfpt_put( "/dev/null", "quux.txt", "nope", 4 ) == -1 );
}

