#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <ctype.h>      /* isdigit */
#include <errno.h>      /* errno */
#include <stdbool.h>    /* true, false */
#include <stdlib.h>     /* exit */
#include <stdio.h>      /* snprintf */
#include <string.h>     /* strlen */
#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <sys/wait.h>   /* waitpid */
#include <unistd.h>     /* read, write */
#include <err.h>        /* err */
#include <stdbool.h>
/* V tomto cvičení bude Vaším úkolem naprogramovat klient protokolu
 * LMTP (Local Mail Transfer Protocol, RFC 2033). Implementujte
 * proceduru ‹lmtp_send›, která pomocí LMTP odešle e-mail, a která
 * bude mít tyto parametry:
 *
 *  • ‹addr›   – adresa unixového socketu na které běží server,
 *  • ‹sender› – nulou ukončený řetězec, který obsahuje adresu
 *    odesílatele,
 *  • ‹recip›  – podobně, ale s adresou adresáta,
 *  • ‹data›   – obsah zprávy (včetně hlaviček; nebude obsahovat
 *    řádek se samostatně stojící tečkou).
 *
 * Návratová hodnota bude:
 *
 *  •  0 – vše proběhlo v pořádku a mail byl doručen,
 *  •  1 – adresát neexistuje,
 *  •  2 – došlo k jiné chybě doručení,
 *  • -1 – došlo k systémové chybě na straně klienta.¹
 *
 * LMTP je jeden z mnoha řádkově orientovaných internetových
 * protokolů. Na rozdíl od konvence POSIX-ových systémů používají
 * protokoly specifikované v RFC jako ukončení řádku sekvenci bajtů
 * ‹0x0d 0x0a› (CRLF). Takto ukončené řádky musíte jak odesílat, tak
 * akceptovat v odpovědích serveru.
 *
 * Základní příkazy protokolu LMTP jsou tyto:
 *
 *  • ‹LHLO jméno› – pozdrav; jako jméno použijte ‹pb152cv›,
 *  • ‹MAIL FROM:<adresa>› – oznámí serveru, že hodláme odesílat
 *    mail a zároveň adresu jeho odesílatele,
 *  • ‹RCPT TO:<adresa>› – předá serveru adresu, na kterou má zprávu
 *    doručit (lze použít vícekrát, ale my budeme odesílat zprávy
 *    vždy pouze jednomu adresátovi),
 *  • ‹DATA› – iniciuje přenos samotné zprávy, která je ukončena
 *    řádkem, který obsahuje pouze tečku,
 *  • ‹QUIT› – ukončí sezení, server příkaz potvrdí a uzavře
 *    spojení.
 *
 * Na každý klientem odeslaný příkaz dostanete jednořádkovou²
 * odpověď, která začíná číselným kódem. Kód v rozsahu 200–299
 * znamená úspěšné provedení příkazu. Na navázání spojení reaguje
 * server kódem 220, na který musíte vyčkat než odešlete příkaz
 * ‹LHLO›. Chybové kódy jsou v rozsahu 400–599 (4xx pro dočasné a
 * 5xx pro permanentní chyby).
 *
 * ¹ Zahrnuje mimo jiné chybnou adresu serveru. Při výsledku -1
 *   nechť je ‹errno› nastavené na odpovídající hodnotu.
 * ² Protokol umožňuje serveru odeslat více odpovědí na jeden
 *   příkaz, tuto situaci ale nemusíte řešit, protože normálně
 *   nastává pouze pro zprávy s více než jedním adresátem. */

int get_response(int sock_fd) {
    char buffer[5];
    if (read(sock_fd, buffer, 4) != 4) {
        return -1;
    }
    return strtol(buffer, NULL, 10);
}

bool check_response(int sock_fd) {
    int resp = get_response(sock_fd);
    if (resp >= 400 && resp < 600) {
        warn("error");
        return false;
    }
    return true;
}

int sock_write(int sock_fd, const char *format_string, const char *data) {
    char *buffer = malloc(sizeof(char) * (strlen(format_string) + strlen(data)));
    if (buffer == NULL) {
        warn("malloc");
        return -1;
    }
    int buff_size = sizeof(buffer);
    memset(buffer, 0, buff_size);
    snprintf(buffer, buff_size, format_string, data);
    int msg_len = strlen(buffer);
    if (write(sock_fd, buffer, msg_len) != msg_len) {
        free(buffer);
        warn("write");
        return -1;
    }
    free(buffer);
    if (!check_response(sock_fd)) {
        return 2;
    }
    return 0;
}

int lmtp_send(const char *addr, const char *sender,
              const char *recip, const char *message) {
    int rv = -1;
    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 ) {
        warn("creating a unix socket");
        return rv;
    }
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    if ( strlen(addr) >= sizeof sa.sun_path - 1 ) {
        warn( "socket address too long, maximum is %zu",
              sizeof sa.sun_path );
        goto error;
    }

    snprintf( sa.sun_path, sizeof sa.sun_path, "%s", addr );
    if ( connect( sock_fd, ( struct sockaddr * ) &sa,
                  sizeof sa ) == -1 ) {
        warn( "connecting to %s", sa.sun_path );
        goto error;
    }
    if (get_response(sock_fd) != 220) {
        warn("connection failed");
        rv = 2;
        goto error;
    }
    if ((rv = sock_write(sock_fd, "LHLO pb152cv\r\n", NULL)) != 0) {
        goto error;
    }
    if ((rv = sock_write(sock_fd, "MAIL FROM:<%s>\r\n", sender)) != 0) {
        goto error;
    }
    if ((rv = sock_write(sock_fd, "RCPT TO:<%s>\r\n", recip)) != 0) {
        goto error;
    }
    if ((rv = sock_write(sock_fd, "DATA\r\n%s\r\n.\r\n", message)) != 0) {
        goto error;
    }
    rv = sock_write(sock_fd, "QUIT\r\n", NULL);
  error:
    if ( close( sock_fd ) == -1 ) {
        warn( "closing %s", sa.sun_path );
    }
    return rv;
}

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

static int _read_until( int fd, char *buf, int buf_len, const char *str )
{
    int i = 0, len = strlen( str );

    while ( true )
    {
        int ret = read( fd, buf + i++, 1 );
        if ( ( i > len - 1 && strncmp( buf + i - len, str, 2 ) == 0 )
                || ret == 0 || i == buf_len )
            break;
        if ( ret == -1 )
            err( 2, "read" );
    }

    return i;
}

static void _write( int fd, const char *buf, int len )
{
    if ( write( fd, buf, len ) == -1 )
        err( 2, "write" );
}

static void receive_mail( int client_fd, int counter )
{
    char buffer[ 2048 ];
    char  ok[] = "220 success\r\n";
    char nok[] = "550 failure\r\n";
    int len = sizeof( ok ) - 1;

    _write( client_fd, counter-- == 0 ? nok : ok, len );

    _read_until( client_fd, buffer, sizeof( buffer ), "\r\n" );
    assert( strncmp( buffer, "LHLO pb152cv\r\n", 13 ) == 0 );
    _write( client_fd, counter-- == 0 ? nok : ok, len );

    bool from = false, to = false;

    for ( int i = 0; i < 2 && counter > 0; ++i )
    {
        _read_until( client_fd, buffer, sizeof( buffer ), "\r\n" );

        if ( strncmp( buffer, "MAIL FROM:", 10 ) == 0 )
        {
            from = true;
            _write( client_fd, counter == 0 ? nok : ok, len );
        }
        else if ( strncmp( buffer, "RCPT TO:", 8 ) == 0 )
        {
            to = true;
            _write( client_fd, counter == 1 ? nok : ok, len );
        }
        else
            assert( false ); // invalid command
    }

    if ( counter > 0 )
        assert( from && to );

    counter -= 2;

    _read_until( client_fd, buffer, sizeof( buffer ), "\r\n" );

    if ( strncmp( buffer, "DATA\r\n", 6 ) == 0 )
    {
        _write( client_fd, counter-- == 0 ? nok : ok, len );

        int msg_len = _read_until( client_fd, buffer,
                                   sizeof( buffer ), "\r\n.\r\n" );
        assert( strncmp( buffer + msg_len - 5, "\r\n.\r\n", 5 ) == 0 );
        _write( client_fd, counter-- == 0 ? nok : ok, len );
        _read_until( client_fd, buffer, sizeof( buffer ), "\r\n" );
    }

    if ( strncmp( buffer, "QUIT\r\n", 6 ) == 0 )
        _write( client_fd, counter-- == 0 ? nok : ok, len );
}

static int fork_server( int counter )
{
    struct sockaddr_un addr =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.p2_socket"
    };

    int sock_fd, client_fd;

    unlink_if_exists( addr.sun_path );

    if ( ( sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 2, "creating socket" );

    if ( bind( sock_fd, ( struct sockaddr * ) &addr,
               sizeof( addr ) ) == -1 )
        err( 2, "binding socket to %s", addr.sun_path );

    if ( listen( sock_fd, 1 ) == -1 )
        err( 2, "listening on %s", addr.sun_path );

    pid_t child_pid = fork();

    if ( child_pid == -1 )
        err( 2, "fork" );

    if ( child_pid > 0 )
    {
        close_or_warn( sock_fd, addr.sun_path );
        return child_pid;
    }

    alarm( 5 ); /* die if the parent gets stuck */

    if ( ( client_fd = accept( sock_fd, NULL, NULL ) ) == -1 )
        err( 2, "accepting a connection on %s", addr.sun_path );

    receive_mail( client_fd, counter );

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

int main( int argc, char **argv )
{
    if ( argc == 5 )
        return lmtp_send( argv[ 1 ], argv[ 2 ], argv[ 3 ], argv[ 4 ] );

    const char *socket = "zt.p2_socket";
    pid_t server_pid;

    server_pid = fork_server( 7 ); // no fail
    assert( lmtp_send( socket, "alice@fi.cz", "bob@fi.cz", "hello" ) == 0 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( 3 ); // fails at RCPT TO
    assert( lmtp_send( socket, "alice@fi.cz", "charlie@fi.cz", "42!" ) == 1 );
    assert( reap( server_pid ) == 0 );

    server_pid = fork_server( 1 ); // fails at LHLO
    assert( lmtp_send( socket, "charlie@fi.cz", "bob@fi.cz", "world" ) == 2 );
    assert( reap( server_pid ) == 0 );

    assert( lmtp_send( "/dev/null", "alice@fi.cz", "bob@fi.cz", ":*" ) == -1 );
}
