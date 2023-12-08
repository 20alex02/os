#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>

/* Vaším úkolem je naprogramovat jednoduchý server, který bude
 * poslouchat na unixovém socketu a s klienty bude komunikovat
 * jednoduchým protokolem.
 *
 * Struktura protokolu je velmi jednoduchá: klient bude odesílat
 * „klíče“ ukončené nulovým bajtem. Ke každému klíči, který server
 * přečte, odešle odpověď, která bude obsahovat čtyřbajtovou délku
 * hodnoty, která klíči náleží, následovanou samotnou hodnotou.
 * Není-li klíč přítomen, odešle hodnotu 0xffffffff. Nejvýznamnější
 * bajt je vždy odesílán jako první.
 *
 * Parametry podprogramu ‹kvsd› budou:
 *
 *  • ‹sock_fd› je popisovač socketu, který je svázán s adresou, ale
 *    jinak není nijak nastaven, a na kterém bude server poslouchat,
 *  • ‹data› je ukazatel na kořen binárního vyhledávacího stromu,
 *    který obsahuje klíče a hodnoty, které jim odpovídají, a podle
 *    kterého bude klientům odpovídat,
 *  • ‹count› je celkový počet klientů, které podprogram ‹kvsd›
 *    obslouží (poté, co se připojí ‹count›-tý klient, další spojení
 *    již nebude přijímat, ale dokončí komunikaci s již připojenými
 *    klienty).
 *
 * V této verzi je dostačující, aby server klienty vyřizoval
 * sekvenčně – klient vyčká, než se předchozí klient odpojí.
 *
 * Podprogram ‹kvsd› vrátí hodnotu 0 v případě, že bylo úspěšně
 * obslouženo ‹count› klientů, -1 jinak. Neexistence klíče není
 * fatální chybou. */

struct kv_tree
{
    const char *key; /* zero-terminated */
    const char *data;
    int data_len;
    struct kv_tree *left, *right;
};

int kvsd( int sock_fd, const struct kv_tree *root, int count );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <err.h>        /* err */
#include <sys/wait.h>   /* waitpid */
#include <time.h>       /* nanosleep */
#include <sys/un.h>     /* sockaddr_un */
#include <unistd.h>     /* read, write, close, unlink */
#include <errno.h>      /* errno */
#include <stdlib.h>     /* exit */
#include <string.h>     /* strlen, memcmp */
#include <sched.h>      /* sched_yield */

static const struct sockaddr_un server_addr =
        { .sun_family = AF_UNIX, .sun_path = "zt.p3_kvseqd" };

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

static int fork_server( struct kv_tree *root, int count )
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
        int rv = kvsd( sock_fd, root, count );
        close( sock_fd );
        exit( rv );
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

static void check_solution( int n, const char** request,
                            const char **expect, const int *expect_sz )
{
    int sock_fd;

    if ( ( sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 2, "creating client socket" );

    for ( int i = 0; i < 25; ++i )
        if ( connect( sock_fd, ( const struct sockaddr* ) &server_addr,
                      sizeof( server_addr ) ) == 0 )
            break;
        else
        {
            if ( errno != ECONNREFUSED )
                err( 2, "connecting client socket" );
            sched_yield();
        }

    char buffer[ 128 ];
    for ( int i = 0; i < n; i++ )
    {
        assert( write( sock_fd, request[ i ],
                       strlen( request[ i ] ) + 1 ) != -1 );

        int nread, ntotal = 0, nexp = expect_sz[ i ];
        while ( ( nread = read( sock_fd, buffer + ntotal,
                                nexp - ntotal ) ) > 0 )
            ntotal += nread;

        if ( nread == -1 )
            err( 2, "client read" );

        assert( ntotal == nexp );
        assert( memcmp( buffer, expect[ i ], nexp ) == 0 );
    }

    close_or_warn( sock_fd, "client socket" );
}

int main()
{
    struct kv_tree
        key_1 = { .key = "blind", .data = "yourself", .data_len = 8,
                  .left = NULL, .right = NULL },
        key_2 = { .key = "just", .data = "close", .data_len = 5,
                  .left = &key_1, .right = NULL },
        key_3 = { .key = "your", .data = "eyes", .data_len = 4,
                  .left = NULL, .right = NULL },
        key_r = { .key = "to", .data = "truth", .data_len = 5,
                  .left = &key_2, .right = &key_3 };

    const char
        *keys_1[] = { "just" },
        *keys_2[] = { "your", "blind" },
        *keys_3[] = { "to", "lorem", "ipsum" };

    const char
        *values_1[] = { ( "\x00\x00\x00\x05" "close" ) },
        *values_2[] = { ( "\x00\x00\x00\x04" "eyes" ),
                        ( "\x00\x00\x00\x08" "yourself" ) },
        *values_3[] = { ( "\x00\x00\x00\x05" "truth" ),
                        ( "\xff\xff\xff\xff" ),
                        ( "\xff\xff\xff\xff" ) };

    int valsizes_1[] = { 9 },
        valsizes_2[] = { 8, 12 },
        valsizes_3[] = { 9, 4, 4 };

    int pid = fork_server( &key_r, 4 );

    check_solution( 1, keys_1, values_1, valsizes_1 );
    check_solution( 2, keys_2, values_2, valsizes_2 );
    check_solution( 3, keys_3, values_3, valsizes_3 );
    check_solution( 0, NULL, NULL, NULL );

    reap_server( pid );

    return 0;
}
