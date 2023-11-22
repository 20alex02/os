#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlink */
#include <fcntl.h>      /* open */
#include <assert.h>     /* assert */
#include <errno.h>      /* errno */
#include <err.h>        /* NONPOSIX: err */

/* Podprogram ‹count_distinct› spočítá počet různých bajtů
 * v souboru. Tento počet v případě úspěchu vrátí, jinak vrátí
 * hodnotu -1. */

int count_distinct( int dir_fd, const char *file );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void mk_tmp( const char* str, int len )
{
    const char *name = "zt.r4_test_in";
    int fd = open( name, O_CREAT | O_TRUNC | O_WRONLY, 0666 );

    if ( fd == -1 )
        err( 2, "creating %s", name );

    if ( write( fd, str, len ) == -1 )
        err( 2, "writing %s", name );

    close_or_warn( fd, name );
}

static void unlink_if_exists( int dir, const char* name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinking '%s'", name );
}

int main( void )
{
    int dir_fd = openat( AT_FDCWD, ".", O_DIRECTORY );

    if ( dir_fd == -1 )
        err( 1, "opening working directory" );

    const char* tmp_file = "zt.r4_test_in";
    unlink_if_exists( dir_fd, tmp_file );

    mk_tmp( "ahoj", 4 );
    assert( count_distinct( dir_fd, tmp_file ) == 4 );

    mk_tmp( "hello world", 11 );
    assert( count_distinct( dir_fd, tmp_file ) == 8 );

    mk_tmp( "\xff\x1\x2\x3\xfe\x4\x1\xff\xfe", 9 );
    assert( count_distinct( dir_fd, tmp_file ) == 6 );

    char data[] = { 0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xbf, 0xfb, 0x10, 0x01, 0xab, 0xba, 0xcd, 0xdc,
                    0xEE };
    mk_tmp( data, sizeof data );
    assert( count_distinct( dir_fd, tmp_file ) == 9 );

    unlink_if_exists( dir_fd, tmp_file );
    close_or_warn( dir_fd, "working directory" );
    return 0;
}

