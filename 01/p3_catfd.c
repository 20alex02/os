#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlink, lseek */
#include <fcntl.h>      /* open */
#include <assert.h>     /* assert */
#include <string.h>     /* strcmp */
#include <errno.h>      /* errno */
#include <err.h>        /* err */

/* V tomto příkladu bude úkolem implementovat proceduru, jejíž
 * chování se podobá standardnímu programu ‹cat›.
 *
 * Procedura ‹catfd› přijímá 3 parametry:
 *
 *  • ‹fds› – ukazatel na pole popisovačů,
 *  • ‹count› – počet popisovačů zde uložený,
 *  • ‹out_fd› – výstupní popisovač.
 *
 * Účelem této procedury bude přečíst veškerá data z každého
 * popisovače (v zadaném pořadí) a ta zapsat do výstupního
 * popisovače. Pokud vše proběhne bez chyby, vrátí 0, jinak skončí
 * při první chybě a vrátí -1. */

int catfd( int *fds, int count, int out_fd ) {
	char buff;
	int bytes;
	for (int fd = 0; fd < count; ++fd) {
		while ((bytes = read(fds[fd], &buff, 1)) > 0) {
			if (write(out_fd, &buff, 1) != 1) {
				return -1;
			}
		}
		if (bytes == -1) {
			return -1;
		}
	}
	return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink" );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int open_or_die( const char *name )
{
    int fd = openat( AT_FDCWD, name, O_RDONLY );

    if ( fd == -1 )
        err( 2, "opening %s", name );

    return fd;
}

static int create_file( const char *name )
{
    unlink_if_exists( name );
    int fd;

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_WRONLY,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static void write_file( const char *name, const char *str )
{
    int fd = create_file( name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

static int check_catfd( int *fds, int count, const char *expected )
{
    const char *name = "zt.p3_test_out";
    int write_fd, read_fd;
    char buffer[ 4096 + 1 ] = { 0 };

    write_fd = create_file( name );
    catfd( fds, count, write_fd );
    close_or_warn( write_fd, name );

    read_fd = open_or_die( name );

    if ( read( read_fd, buffer, 4096 ) == -1 )
        err( 2, "read" );

    close_or_warn( read_fd, name );

    return strcmp( expected, buffer );
}

int main( void )
{
    unlink_if_exists( "zt.p3_test_out" );

    write_file( "zt.p3_a", "contents of zt.p3_a\n" );
    write_file( "zt.p3_b", "contents of zt.p3_b\n" );
    write_file( "zt.p3_c", "contents of zt.p3_c\n" );

    int fd_a = open_or_die( "zt.p3_a" );
    int fd_b = open_or_die( "zt.p3_b" );
    int fd_c = open_or_die( "zt.p3_c" );

    int lst1[] = { fd_a };
    int lst2[] = { fd_a, fd_b };
    int lst3[] = { fd_a, fd_b, fd_c };

    assert( check_catfd( lst1, 1, "contents of zt.p3_a\n" ) == 0 );

    if ( lseek( fd_a, 0, SEEK_SET ) == -1 ) err( 2, "lseek" );

    assert( check_catfd( lst2, 2, "contents of zt.p3_a\n"
                                  "contents of zt.p3_b\n" ) == 0 );

    if ( lseek( fd_a, 0, SEEK_SET ) == -1 ) err( 2, "lseek" );
    if ( lseek( fd_b, 0, SEEK_SET ) == -1 ) err( 2, "lseek" );

    assert( check_catfd( lst3, 3, "contents of zt.p3_a\n"
                                  "contents of zt.p3_b\n"
                                  "contents of zt.p3_c\n" ) == 0 );

    if ( close( fd_a ) || close( fd_b ) || close( fd_c ) )
        err( 2, "close ");

    unlink_if_exists( "zt.p3_test_out" );
    unlink_if_exists( "zt.p3_a" );
    unlink_if_exists( "zt.p3_b" );
    unlink_if_exists( "zt.p3_c" );

    return 0;
}
