#define _POSIX_C_SOURCE 200809L

/* V tomto příkladu budete opět programovat dvojici podprogramů,
 * ‹meter_start› a ‹meter_cleanup›, které budou tentokrát realizovat
 * asynchronní notifikace o počtu přenesených bajtů na předané
 * dvojici popisovačů. */

/* Podprogram ‹meter_start› bude mít 4 parametry:
 *
 *  • dvojici popisovačů ‹fd_1› a ‹fd_2› připojených proudových
 *    socketů, mezi kterými bude obousměrně kopírovat (přeposílat)
 *    data,
 *  • popisovač ‹fd_meter› otevřený pro zápis, do kterého bude
 *    zapisovat informace o množství přeposlaných dat,
 *  • kladné 32bitové číslo ‹count›, které určí, po kolika bajtech
 *    bude měřič informovat o počtu přenesených bajtů.
 *
 * Podprogram ‹meter_start› vrátí volajícímu ukazatel ‹handle›,
 * který volající později předá podprogramu ‹meter_cleanup›. Samotné
 * přeposílání dat a notifikace na popisovači ‹fd_meter› bude
 * provádět asynchronně.
 *
 * Do popisovače ‹fd_meter› bude bez zbytečné prodlevy vždy po
 * přeposlání ‹count› bajtů zapsán celkový počet bajtů, které byly
 * až do této chvíle přeposlány mezi ‹fd_1› a ‹fd_2› (oba směry se
 * sčítají). Rozdíl mezi dvěma po sobě zapsanými hodnotami může být
 * nejvýše 2⋅‹count›. Hodnoty se odesílají jako čtyřbajtové slovo,
 * nejvýznamnější bajt první.
 *
 * Při uzavření spojení na libovolném z popisovačů ‹fd_1› nebo
 * ‹fd_2› uzavře ‹meter_start› zbývající spojení a do ‹fd_meter›
 * zapíše konečný počet úspěšně přeposlaných bajtů a také na něm
 * ukončí spojení.
 *
 * Můžete předpokládat, že veškeré zápisy (do ‹fd_1›, ‹fd_2› a
 * ‹fd_meter›) budou i v blokujícím režimu provedeny obratem
 * (nehrozí tedy uváznutí při zápisu, ani hladovění). */

void *meter_start( int fd_1, int fd_2, int fd_meter, int count );

/* Podprogram ‹meter_cleanup› obdrží ukazatel ‹handle›, který byl
 * vrácen podprogramem ‹meter_start›, a uvolní veškeré zdroje s ním
 * spojené. Návratová hodnota 0 zaručuje, že všechna data byla
 * úspěšně přeposlána a do popisovače ‹fd_meter› byly zapsány
 * všechny notifikace dle požadavků výše. Jinak je výsledkem -1. */

int meter_cleanup( void *handle );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <err.h>
#include <string.h>
#include <assert.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <arpa/inet.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void close_fds( int n, const int* fds, const char* desc )
{
    for ( int i = 0; i < n; ++i )
        close_or_warn( fds[ i ], desc );
}

static void mk_pipe( int* fd_r, int* fd_w )
{
    int p[ 2 ];
    if ( pipe( p ) == -1 )
        err( 2, "pipe" );
    *fd_r = p[ 0 ];
    *fd_w = p[ 1 ];
}

static void mk_socketpair( int* fd1, int* fd2 )
{
    int sp[ 2 ];
    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, sp ) == -1 )
        err( 2, "socketpair" );
    *fd1 = sp[ 0 ];
    *fd2 = sp[ 1 ];
}

static int nread( char* buf, int nexp, int fd )
{
    int nread, ntot = 0;
    while ( ( nread = read( fd, buf + ntot, nexp - ntot ) ) > 0 )
        ntot += nread;
    return nread != -1 ? ntot : -1;
}

static void fill( int fd, ssize_t bytes )
{
    char buf[ 512 ];
    memset( buf, 'x', 512 );

    ssize_t nwrote;
    while ( ( nwrote = write( fd, buf,
                              bytes > 512 ? 512 : bytes ) ) > 0 )
        bytes -= nwrote;

    assert( nwrote != -1 );
    assert( bytes == 0 );
}

static int fork_solution( const int *to_inherit,
                          const int *to_close,
                          int count, int expected_res )
{
    int sync[ 2 ];

    if ( pipe( sync ) == -1 )
        err( 2, "sync pipe" );

    alarm( 5 ); /* if we get stuck */

    pid_t pid = fork();
    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )   /* child -> executes solution */
    {
        close_or_warn( sync[ 0 ], "sync pipe: read end (fork_solution)" );
        close_fds( 3, to_close, "non-inherited fds (fork_solution)" );

        int fd_1 = to_inherit[ 0 ];
        int fd_2 = to_inherit[ 1 ];
        int fd_meter = to_inherit[ 2 ];

        void* handle = meter_start( fd_1, fd_2, fd_meter, count );
        assert( handle != NULL );

        close_fds( 3, to_inherit, "inherited fds (fork_solution)" );

        if ( write( sync[ 1 ], "a", 1 ) == -1 )
            err( 2, "sync write" );
        close_or_warn( sync[ 1 ], "sync pipe: write end (fork_solution)" );

        int res = meter_cleanup( handle );
        if ( res != expected_res )
            errx( 1, "expected_res = %d, res = %d", expected_res, res );
        exit( 0 );
    }

    /* parent -> sends data to the solution */

    close_or_warn( sync[ 1 ], "sync pipe: write end (tests)" );
    char c;
    assert( read( sync[ 0 ], &c, 1 ) == 1 ); /* blocks until meter_start is called */
    close_or_warn( sync[ 0 ], "sync pipe: read end (tests)" );

    return pid;
}

static int reap_solution( pid_t pid )
{
    int status;
    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "waitpid" );
    return WIFEXITED( status ) && WEXITSTATUS( status ) == 0;
}

static pid_t start_tests( int* fd_1, int* fd_2, int* fd_meter,
                          int count, int expected_res )
{
    int fd1_tst, fd1_sol, fd2_tst, fd2_sol, meter_in, meter_out;
    mk_socketpair( &fd1_tst, &fd1_sol );
    mk_socketpair( &fd2_tst, &fd2_sol );
    mk_pipe( &meter_out, &meter_in );

    /* these file descriptors are given to the solution (meter_start)
     * aka inherited fds */
    int fds_sol[ 3 ] = { fd1_sol, fd2_sol, meter_in };
    /* these are relevant only for the tests, aka non-inherited */
    int fds_tests[ 3 ] = { fd1_tst, fd2_tst, meter_out };

    /* fd1_tst <---> fd1_sol ... meter ... fd2_sol <---> fd2_tst
     *               (fd_1)        .       (fd_2)
     *                          meter_in   (fd_meter)
     *                             ↓
     *                          meter_out
     */

    pid_t pid = fork_solution( fds_sol, fds_tests, count, expected_res );
    close_fds( 3, fds_sol, "inherited fds (tests)" );
    *fd_1 = fd1_tst;
    *fd_2 = fd2_tst;
    *fd_meter = meter_out;
    return pid;
}

static void finish_tests( pid_t pid, int fd_1, int fd_2,
                          int fd_meter, int final_expect )
{
    uint32_t final, sink;

    if ( fd_1 != -1 )
        close_or_warn( fd_1, "non-inherited fds (tests): fd_1" );

    if ( fd_2 != -1 )
        close_or_warn( fd_2, "non-inherited fds (tests): fd_2" );

    if ( final_expect != -1 )
    {
        assert( nread( ( char * ) &final, 4, fd_meter ) == 4 );
        assert( read( fd_meter, ( char * ) &sink, 4 ) == 0 );
        assert( ntohl( final ) == ( uint32_t ) final_expect );
    }

    if ( fd_meter != -1 )
        close_or_warn( fd_meter, "non-inherited fds (tests): fd_meter" );

    assert( reap_solution( pid ) );
}

static void assert_notify( int fd_meter, int expected_msgs, unsigned count )
{
    uint32_t last = -1;

    for ( int i = 0; i < expected_msgs; ++i )
    {
        uint32_t curr;
        assert( nread( ( char* ) &curr, 4, fd_meter ) == 4 );
        curr = ntohl( curr );

        if ( last != ( uint32_t ) -1 )
        {
            assert( last < curr );
            assert( curr - last <= 2 * count );
            assert( curr - last >= count );
        }
        last = curr;
    }
}

int main( void )
{
    char buf[ 2048 ];
    char exp_buf[ 2048 ];
    memset( exp_buf, 'x', 2048 );

    /* count ~> 10, expecting 15 bytes total */
    int fd_1, fd_2, fd_meter;
    pid_t pid = start_tests( &fd_1, &fd_2, &fd_meter, 10, 0 );

    fill( fd_1, 12 );
    assert( nread( buf, 12, fd_2 ) == 12 );     /* forwarded w/o delay */
    assert( memcmp( buf, exp_buf, 12 ) == 0 );

    fill( fd_2, 3 );
    assert( nread( buf, 3, fd_1 ) == 3 );
    assert( memcmp( buf, exp_buf, 3 ) == 0 );

    /* 15 bytes total, expecting 1 + 1 notification message */
    assert_notify( fd_meter, 1, 10 );
    /* if tests SIGALRM here, you didn't send enough notifications */

    /* if tests SIGPIPE here, you've sent too many of them */
    finish_tests( pid, fd_1, fd_2, fd_meter, 15 );

    /* count ~> 10, expecting 105 bytes total with 10 + 1 notifications */
    pid = start_tests( &fd_1, &fd_2, &fd_meter, 10, 0 );
    fill( fd_2, 20 );
    fill( fd_1, 20 );
    fill( fd_2, 65 );
    assert( nread( buf, 85, fd_1 ) == 85 );
    assert( memcmp( buf, exp_buf, 85 ) == 0 );
    assert( nread( buf, 20, fd_2 ) == 20 );
    assert( memcmp( buf, exp_buf, 20 ) == 0 );
    assert_notify( fd_meter, 10, 10 );
    finish_tests( pid, fd_1, fd_2, fd_meter, 105 );

    /* count ~> 50, expecting 1001 bytes & 20 + 1 notifications */
    pid = start_tests( &fd_1, &fd_2, &fd_meter, 50, 0 );

    for ( int i = 0; i < 9; ++i )
    {
        fill( fd_2, 100 );
        assert( nread( buf + i * 100, 100, fd_1 ) == 100 );
    }

    fill( fd_2, 101 );
    assert( nread( buf + 900, 101, fd_1 ) == 101 );
    assert( memcmp( buf, exp_buf, 1001 ) == 0 );
    assert_notify( fd_meter, 20, 50 );
    finish_tests( pid, fd_1, fd_2, fd_meter, 1001 );

    /* write to fd_2 fails */
    pid = start_tests( &fd_1, &fd_2, &fd_meter, 10, -1 );
    close_or_warn( fd_2, "fd_2" );
    fill( fd_1, 1 );
    finish_tests( pid, fd_1, -1, fd_meter, -1 );

    /* write to fd_meter fails */
    pid = start_tests( &fd_1, &fd_2, &fd_meter, 10, -1 );
    close_or_warn( fd_meter, "fd_meter" );
    fill( fd_1, 15 );
    finish_tests( pid, fd_1, fd_2, -1, -1 );

    return 0;
}
