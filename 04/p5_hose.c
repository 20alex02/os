#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, pipe */
#include <err.h>        /* err */
#include <stdlib.h>     /* malloc */
#include <fcntl.h>      /* fcntl */
#include <assert.h>     /* assert */
#include <string.h>     /* memcmp */
#include <errno.h>      /* errno */

/* Uvažme situaci analogickou k přípravě ‹p4_collect›, ale se
 * zápisem – naprogramujte podprogram ‹hose›, který obdrží:
 *
 *  • ‹count›   – počet popisovačů, se kterými se bude pracovat,
 *  • ‹fds›     – ukazatel na pole popisovačů,¹
 *  • ‹buffers› – ukazatel na pole ukazatelů, kde každý určuje data,
 *    která se mají zapsat do příslušného popisovače,
 *  • ‹sizes›   – ukazatel na pole čísel, která určují, kolik dat se
 *    má do kterého popisovače zapsat.
 *
 * Není-li možné provést žádný zápis, podprogram ‹hose› bude
 * blokovat, než se situace změní. Jinak provede všechny zápisy,
 * které provést lze, aniž by volání blokovalo, a odpovídajícím
 * způsobem upraví ukazatele v ‹buffers› a velikosti v ‹sizes›.
 * Návratová hodnota:
 *
 *  • 0 – veškeré požadované zápisy byly provedeny,
 *  • kladné číslo – počet popisovačů, které nejsou připravené
 *    k zápisu, ale jsou pro ně přichystaná data,
 *  • -1 – došlo k systémové chybě – opětovné volání se stejnými
 *    parametry se pokusí akci zopakovat.
 *
 * ¹ Je zaručeno, že všechny vstupní popisovače budou mít nastavený
 *   příznak ‹O_NONBLOCK›. */

int hose( int count, int* fds, char** buffers, int* sizes );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <limits.h>     /* PIPE_BUF */

void read_or_die( int fd, char *where, int bytes )
{
    if ( read( fd, where, bytes ) != bytes )
        err( 1, "unable to read from pipe" );
}

static void fill_or_die( int fd, int byte )
{
    char buffer[ 1024 ];
    memset( buffer, byte, 1024 );
    int wrote = write( fd, buffer, 1024 );

    if ( wrote == -1 && errno != EAGAIN && errno != EWOULDBLOCK )
        err( 1, "filling up a pipe" );

    if ( wrote == 1024 )
        return fill_or_die( fd, byte );
}

static void drain_or_die( int fd, int byte )
{
    char buffer[ 1023 ];
    int readb = read( fd, buffer, 1023 );

    if ( readb == -1 )
        err( 1, "draining pipe" );

    for ( int i = 0; i < readb; ++i )
        assert( buffer[ i ] == byte );

    if ( readb == 1023 )
        drain_or_die( fd, byte );
}

static void mk_pipe( int *fd_read, int *fd_write )
{
    int p[ 2 ];

    if ( pipe( p ) != 0 )
        err( 1, "unable to open pipes" );

    *fd_read = p[ 0 ];
    *fd_write = p[ 1 ];

    int flags = fcntl( *fd_write, F_GETFL, 0 );

    if ( flags == -1 )
        err( 1, "unable to get pipe flags" );
    if ( fcntl( *fd_write, F_SETFL, flags | O_NONBLOCK ) == -1 )
        err( 1, "unable to set pipe flags" );
}

int main()
{
    char buffer[ PIPE_BUF * 3 ] = { 0 };
    char input_0[] =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit";
    char input_1[] =
        "Sed tempus at neque id aliquet";
    char input_2[] =
        "Aenean varius felis ac sem blandit, id ornare est auctor";

    char *buffer_ptrs_a[] = { input_0, input_1, input_2 };

    int sizes_a[] = { sizeof input_0,
                      sizeof input_1,
                      sizeof input_2 };

    int fds_read[ 3 ];
    int fds_write[ 3 ];

    for ( int i = 0; i < 3; ++i )
    {
        mk_pipe( fds_read + i, fds_write + i );
        fill_or_die( fds_write[ i ], '0' );
    }

    drain_or_die( fds_read[ 0 ], '0' );
    assert( hose( 3, fds_write, buffer_ptrs_a, sizes_a ) == 2 );
    assert( read( fds_read[ 0 ], buffer, 100 ) == 56 );
    assert( memcmp( buffer, input_0, 56 ) == 0 );
    assert( sizes_a[ 0 ] == 0 );

    drain_or_die( fds_read[ 1 ], '0' );
    drain_or_die( fds_read[ 2 ], '0' );

    assert( hose( 3, fds_write, buffer_ptrs_a, sizes_a ) == 0 );
    assert( read( fds_read[ 1 ], buffer, 100 ) == 31 );
    assert( memcmp( buffer, input_1, 31 ) == 0 );
    assert( read( fds_read[ 2 ], buffer, 100 ) == 57 );
    assert( memcmp( buffer, input_2, 57 ) == 0 );
    assert( sizes_a[ 1 ] == 0 );
    assert( sizes_a[ 2 ] == 0 );

    assert( hose( 3, fds_write, buffer_ptrs_a, sizes_a ) == 0 );

    for ( int i = 0; i < 3; i++)
        fill_or_die( fds_write[ i ], '0' );
    assert( hose( 3, fds_write, buffer_ptrs_a, sizes_a ) == 0 );

    char *buffer_ptrs_b[] = { buffer, input_1, input_2 };
    int sizes_b[ 3 ] = { PIPE_BUF * 3,
                         sizeof input_1,
                         sizeof input_2 };

    read_or_die( fds_read[ 0 ], buffer, PIPE_BUF );
    drain_or_die( fds_read[ 1 ], '0' );

    assert( hose( 3, fds_write, buffer_ptrs_b, sizes_b ) == 1 );
    assert( sizes_b[ 0 ] == 2 * PIPE_BUF );
    assert( sizes_b[ 1 ] == 0 );

    read_or_die( fds_read[ 0 ], buffer, PIPE_BUF );
    drain_or_die( fds_read[ 2 ], '0' );
    assert( hose( 3, fds_write, buffer_ptrs_b, sizes_b ) == 0 );
    assert( sizes_b[ 0 ] == 1 * PIPE_BUF );
    assert( sizes_b[ 2 ] == 0 );

    read_or_die( fds_read[ 0 ], buffer, PIPE_BUF );
    assert( hose( 3, fds_write, buffer_ptrs_b, sizes_b ) == 0 );
    assert( sizes_b[ 0 ] == 0 );

    return 0;
}
