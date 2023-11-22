#define _POSIX_C_SOURCE 200809L

#include <err.h>            /* err, warn, warnx */
#include <stdlib.h>         /* exit, NULL */
#include <unistd.h>         /* close, read, write, unlink, fork, alarm */
#include <errno.h>          /* errno */
#include <string.h>         /* strlen, strcmp */
#include <assert.h>         /* assert */
#include <sys/stat.h>       /* stat, struct stat */
#include <sys/socket.h>     /* socket, connect, AF_UNIX */
#include <sys/un.h>         /* struct sockaddr_un */

/* Tato příprava doplňuje předchozí ‹p4_newsc› – Vaším úkolem bude
 * tentokrát naprogramovat odpovídající server. Krom tam popsaného
 * protokolu implementujte také nahrávání dat – jsou-li první 4
 * bajty odeslané klientem ‹0xff 0xff 0xff 0xff›, následuje
 * libovolný počet nenulových bajtů, které server připojí na konec
 * své sekvence. První nulový bajt ukončí zpracování, načež server
 * uzavře spojení. Procedura ‹news_server› bude mít jediný parametr
 * – adresu unixového socketu, na které má poslouchat.
 *
 * Protože se jedná o uzavřený program, procedura ‹news_server› se
 * nikdy nevrátí. Nastane-li chyba během komunikace s klientem,
 * ukončí spojení a vyčká na dalšího klienta. Není-li toto možné,
 * program ukončí s chybou. */

void news_server( const char *addr );

/* Přestože implementujete znovupoužitelný podprogram, může být
 * užitečné jej testovat interaktivně – tento program můžete spustit
 * jako ‹./p6_newsd cesta_k_socketu› – v takovém případě se přiložené
 * testy přeskočí. Viz též začátek procedury ‹main›. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>       /* waitpid */
#include <signal.h>         /* kill, SIGTERM */
#include <time.h>           /* nanosleep */
#include <stdbool.h>        /* bool */

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

static pid_t fork_server()
{
    const char *addr = "zt.p6_socket";
    unlink_if_exists( addr );
    pid_t pid = fork();

    if ( pid == -1 ) err( 2, "fork" );

    /* V potomku poslouchá server. */
    if ( pid == 0 )
    {
        /* Proces se serverem se ukončí za 5 sekund; déle by testování jednoho
         * serveru nemělo trvat a při selhaných testech rodič zemře dřív, než
         * může potomka ukončit. */
        alarm( 5 );
        news_server( addr );
        /* news_server by se neměl vrátit a smí skončit leda s chybovým kódem,
         * protože není způsob, jak jej čistě ukončit. */
        exit( 0 );
    }

    /* Rodič čeká, než vznikne socket */
    int tries = 10;
    struct stat buf;

    while ( tries --> 0 && stat( addr, &buf ) == -1 )
    {
        /* Počkat 100 ms. */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        if ( nanosleep( &ts, NULL ) == -1 )
            err( 1, "nanosleep" );
    }

    if ( tries == 0 || errno != ENOENT)
        err( 1, "stat %s", addr );

    return pid;
}

static void kill_server( pid_t pid )
{
    if ( kill( pid, SIGTERM ) == -1 ) err( 1, "kill" );
    if ( waitpid( pid, NULL, WNOHANG ) == -1 ) err( 1, "waitpid" );
}

static int connect_to_server()
{
    int sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock == -1 ) err( 1, "opening socket" );

    struct sockaddr_un saddr = { .sun_family = AF_UNIX, .sun_path = "zt.p6_socket" };
    if ( connect( sock, (const struct sockaddr *) &saddr, sizeof saddr ) == -1 )
        err( 1, "connecting to socket" );

    return sock;
}

static void append_span( const char *msg, size_t len )
{
    int fd = connect_to_server();

    if ( write( fd, "\xff\xff\xff\xff", 4 ) == -1 )
        err( 1, "sending header" );
    if ( write( fd, msg, len ) == -1 )
        err( 1, "sending data" );

    close_or_warn( fd, "client socket" );
}

static void append( const char *msg )
{
    append_span( msg, strlen( msg ) + 1 );
}

static bool read_compare( int fd, const char *msg )
{
    const size_t max_size = 127;
    char buf[max_size + 1];
    size_t msg_length = strlen( msg );
    assert( msg_length < max_size ); // 1 byte for \0 and 1 to detect long responses

    size_t read_total = 0;
    ssize_t bytes_read;

    while ( read_total < max_size
            && ( bytes_read = read( fd, buf + read_total, max_size - read_total ) ) > 0 )
        read_total += bytes_read;

    if ( bytes_read == -1 )
        err( 1, "reading response" );

    if ( read_total != msg_length )
        return false;

    buf[read_total] = '\0';

    return strcmp( buf, msg ) == 0;
}

static bool check_response( const char header[4], const char *str )
{
    int fd = connect_to_server();

    if ( write( fd, header, 4 ) == -1 )
        err( 1, "sending header" );

    bool res = read_compare( fd, str );

    close_or_warn( fd, "client socket" );
    return res;
}

int main( int argc, char **argv )
{
    if ( argc == 2 )
    {
        news_server( argv[1] );
        return 2;
    }

    pid_t pid = fork_server();

    assert( check_response( "\0\0\0\0", "" ) );

    append( "foo" );
    assert( check_response( "\0\0\0\0", "foo" ) );
    assert( check_response( "\0\0\0\1", "oo" ) );
    assert( check_response( "\1\0\0\0", "" ) );

    append( "" );
    assert( check_response( "\0\0\0\0", "foo" ) );

    append( "bar" );
    assert( check_response( "\0\0\0\0", "foobar" ) );

    append_span( "some\0thing\0", 11 );
    assert( check_response( "\0\0\0\xa", "" ) );
    assert( check_response( "\0\0\0\xc", "" ) );
    assert( check_response( "\0\0\0\0", "foobarsome" ) );

    assert( check_response( "\xff\xff\xff\xfe", "" ) );

    kill_server( pid );
    unlink_if_exists( "zt.p6_socket" );

    return 0;
}

