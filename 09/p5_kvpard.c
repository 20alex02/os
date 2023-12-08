#define _POSIX_C_SOURCE 200809L

/* Vaším úkolem je podobně jako v ‹p3› naprogramovat jednoduchý
 * server, který bude poslouchat na unixovém socketu a s klienty
 * bude komunikovat podobným, ale ještě o něco jednodušším
 * protokolem.
 *
 * Struktura protokolu je velmi jednoduchá: klient bude odesílat
 * jednobajtové klíče. Ke každému klíči, který server přečte, odešle
 * odpověď, která bude obsahovat čtyřbajtovou délku hodnoty, která
 * klíči náleží, následovanou samotnou hodnotou. Není-li klíč
 * přítomen, odešle hodnotu 0xffffffff. Nejvýznamnější bajt je vždy
 * odesílán jako první.
 *
 * V této verzi je potřeba, aby byl server schopen souběžně
 * komunikovat s větším počtem klientů – tento minimální počet je
 * zadán parametrem (můžete předpokládat, že nebude větší než 63).
 *
 * Parametry podprogramu ‹kvsd› budou:
 *
 *  • ‹sock_fd› je popisovač socketu, který je svázán s adresou, ale
 *    jinak není nijak nastaven, a na kterém bude server poslouchat,
 *  • ‹data› je ukazatel na kořen binárního vyhledávacího stromu,
 *    který obsahuje klíče a hodnoty, které jim odpovídají, a podle
 *    kterého bude klientům odpovídat,
 *  • ‹parallel› je počet klientů, které musí být server schopen
 *    obsloužit souběžně,
 *  • ‹count› je celkový počet klientů, které podprogram ‹kvsd›
 *    obslouží (poté, co se připojí ‹count›-tý klient, další spojení
 *    již nebude přijímat, ale dokončí komunikaci s již připojenými
 *    klienty).
 *
 * Můžete předpokládat, že klient, který odeslal požadavek bude
 * odpověď číst bez zbytečné prodlevy, a komunikace s ostatními
 * klienty tak nebude čekáním na odeslání dat negativně ovlivněna.
 *
 * Podprogram ‹kvsd› vrátí hodnotu 0 v případě, že bylo úspěšně
 * obslouženo ‹count› klientů, -1 jinak. Neexistence klíče není
 * fatální chybou.
 *
 * Nápověda: Tento příklad se Vám pravděpodobně bude řešit lépe,
 * vyřešíte-li nejprve příklad ‹p1›. */

struct kv_tree
{
    char key;
    const char *data;
    int data_len;
    struct kv_tree *left, *right;
};

int kvsd( int sock_fd, const struct kv_tree *root, int parallel, int count );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <err.h>        /* err */
#include <sys/wait.h>   /* waitpid */
#include <sys/socket.h> /* AF_UNIX */
#include <sys/un.h>     /* sockaddr_un */
#include <unistd.h>     /* read, write, close, unlink */
#include <errno.h>      /* errno */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strlen, memcmp */
#include <sched.h>      /* sched_yield */

static const struct sockaddr_un server_addr =
{ .sun_family = AF_UNIX, .sun_path = "zt.p5_kvpard" };

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

static int fork_server( struct kv_tree *root, int parallel,
                        int count )
{
    int sock_fd;

    if ( ( sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 2, "creating server socket" );

    unlink_if_exists( server_addr.sun_path );

    if ( bind( sock_fd, ( const struct sockaddr * ) &server_addr,
               sizeof( server_addr ) ) == -1 )
        err( 2, "binding server socket to %s", server_addr.sun_path );

    pid_t pid = fork();
    alarm( 5 ); /* die after 5s if we get stuck */

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 ) /* child → server */
    {
        int rv = kvsd( sock_fd, root, parallel, count );
        close( sock_fd );
        exit( -rv );
    }

    close_or_warn( sock_fd, "server socket" );
    return pid;
}

static void reap_server( int pid )
{
    int status;
    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "collecting server" );
    assert( WIFEXITED( status ) );
    assert( WEXITSTATUS( status ) == 0 );
}

static int mk_client()
{
    int sock_fd;

    if ( ( sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 2, "creating client socket" );

    for ( int i = 0; i < 25; ++i )
        if ( connect( sock_fd, ( const struct sockaddr* ) &server_addr, sizeof( server_addr ) ) == 0 )
            break;
        else
        {
            if ( errno != ECONNREFUSED )
                err( 2, "connecting client socket" );
            sched_yield();
        }

    return sock_fd;
}

static void check_solution( int client_fd, int n,
                            const char *request,
                            const char **expect,
                            const int* expect_sz )
{
    char buffer[ 128 ];

    for ( int i = 0; i < n; i++ )
    {
        assert( write( client_fd, request + i, 1 ) != -1 );

        int nread, ntotal = 0, nexp = expect_sz[ i ];
        while ( ( nread = read( client_fd, buffer + ntotal,
                                nexp - ntotal ) ) > 0 )
            ntotal += nread;

        if ( nread == -1 )
            err( 2, "client read" );

        assert( ntotal == nexp );
        assert( memcmp( buffer, expect[ i ], nexp ) == 0 );
    }
}

int main()
{
    struct kv_tree
        key_1 = { .key = 'e', .data = "blinded", .data_len = 7,
                  .left = NULL, .right = NULL },
        key_2 = { .key = 'f', .data = "the", .data_len = 3,
                  .left = &key_1, .right = NULL },
        key_3 = { .key = 'r', .data = "devastation", .data_len = 11,
                  .left = NULL, .right = NULL },
        key_r = { .key = 'o', .data = "up", .data_len = 2,
                  .left = &key_2, .right = &key_3 };

    const char keys_1[] = "r",
               keys_2[] = "oe",
               keys_3[] = "fsx";

    const char
        *values_1[] = { ( "\x00\x00\x00\x0B" "devastation" ) },
        *values_2[] = { ( "\x00\x00\x00\x02" "up" ),
                        ( "\x00\x00\x00\x07" "blinded" ) },
        *values_3[] = { ( "\x00\x00\x00\x03" "the" ),
                        ( "\xff\xff\xff\xff" ),
                        ( "\xff\xff\xff\xff" ) };

    int valsizes_1[] = { 15 },
        valsizes_2[] = { 6, 11 },
        valsizes_3[] = { 7, 4, 4 };

    int pid = fork_server( &key_r, 3, 5 );

    int c1 = mk_client();
    check_solution( c1, 1, keys_1, values_1, valsizes_1 );

    int c2 = mk_client();
    check_solution( c1, 2, keys_2, values_2, valsizes_2 );
    check_solution( c2, 2, keys_2, values_2, valsizes_2 );

    int c3 = mk_client();
    check_solution( c1, 3, keys_3, values_3, valsizes_3 );
    check_solution( c2, 3, keys_3, values_3, valsizes_3 );
    check_solution( c3, 3, keys_3, values_3, valsizes_3 );

    close_or_warn( c1, "closing client 1" );

    int c4 = mk_client();
    check_solution( c4, 1, keys_1, values_1, valsizes_1 );
    check_solution( c4, 2, keys_2, values_2, valsizes_2 );
    check_solution( c4, 3, keys_3, values_3, valsizes_3 );

    close_or_warn( c4, "closing client 4" );

    int c5 = mk_client();

    close_or_warn( c2, "closing client 2" );
    close_or_warn( c3, "closing client 3" );
    close_or_warn( c5, "closing client 5" );

    reap_server( pid );

    return 0;
}
