#define _POSIX_C_SOURCE 200809L
#include <unistd.h>     /* write */
#include <fcntl.h>      /* openat */
#include <string.h>     /* strlen */
#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <errno.h>      /* errno, ENOENT */

/* Implementujte podprogram ‹cgrep›, která vypíše všechny řádky ze
 * vstupu ‹fd_in›, které obsahují znak ‹c›. Tyto řádky vypište na
 * popisovač ‹fd_out›. Pro tuto úlohy není stanoven žádný limit na
 * maximální délku řádku. Smíte ovšem předpokládat, že ve vstupním
 * souboru se lze posouvat voláním ‹lseek›.
 *
 * Návratová hodnota: 0 – úspěch; 1 – systémová chyba. */

int cgrep( int fd_in, char c, int fd_out );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char *name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int create_file( int dir_fd, const char *name )
{
    unlink_if_exists( dir_fd, name );
    int fd;

    if ( ( fd = openat( dir_fd, name,
                        O_CREAT | O_TRUNC | O_RDWR,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static void write_file( int dir, const char *name, const char *str )
{
    int fd = create_file( dir, name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

static int check_cgrep( int dir_fd, const char *str, char c,
                        const char *expected )
{
    int in_fd, out_fd;
    char buffer[ 4096 + 1 ] = { 0 };
    const char *in_name  = "zt.r2_test_in";
    const char *out_name = "zt.r2_test_out";

    write_file( dir_fd, in_name, str );
    out_fd = create_file( dir_fd, out_name );
    in_fd = openat( dir_fd, in_name, O_RDONLY );

    if ( in_fd == -1 )
        err( 2, "opening %s", in_name );

    assert( cgrep( in_fd, c, out_fd ) == 0 );
    close_or_warn( in_fd, in_name );

    if ( lseek( out_fd, 0, SEEK_SET ) == -1 )
        err( 1, "seeking in %s", out_name );

    int x;
    if ( ( x = read( out_fd, buffer, 4096 ) ) == -1 )
        err( 2, "reading %s", out_name );

    write( STDOUT_FILENO, buffer, x );
    write( STDOUT_FILENO, expected, strlen( expected ) );

    close_or_warn( out_fd, out_name );
    return strcmp( expected, buffer );
}

int main( void )
{
    int dir_fd = openat( AT_FDCWD, ".", O_DIRECTORY );

    if ( dir_fd == -1 )
        err( 1, "opening working directory" );

    assert( check_cgrep( dir_fd, "x\ny\n", 'x', "x\n" ) == 0 );
    assert( check_cgrep( dir_fd, "x y\ny\n", 'x', "x y\n" ) == 0 );
    assert( check_cgrep( dir_fd, "xx\nxy\n", 'x', "xx\nxy\n" ) == 0 );

    close_or_warn( dir_fd, "working directory" );
    return 0;
}
