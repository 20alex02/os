#define _POSIX_C_SOURCE 200809L
#include <unistd.h>     /* write */
#include <fcntl.h>      /* openat */
#include <string.h>     /* strlen */
#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <errno.h>      /* errno, ENOENT */

/* Za slovo budeme považovat posloupnost „nebílých“ znaků, po které
 * následují jeden či více „bílých“ znaků, nebo konec vstupu. Bílé
 * znaky uvažujeme ve smyslu standardní funkce ‹isspace› deklarované
 * v hlavičce <ctype.h>.
 *
 * Podprogram ‹count_words› zpracuje soubor o zadané cestě ‹file› a
 * výsledek vrátí skrze ukazatel ‹count›.
 *
 * Nastane-li systémová chyba, podprogram vrátí -1 (přitom hodnota
 * na adrese ‹count› není určena). V opačném případě vrátí 0. */

int count_words( int dir_fd, const char *file, int *count );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char* name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinkat '%s'", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int check_words( int dir_fd, const char *str )
{
    const char *name = "zt.r1_test_in";
    int fd;
    int count;

    unlink_if_exists( dir_fd, name );

    if ( ( fd = openat( dir_fd, name,
                        O_CREAT | O_TRUNC | O_WRONLY,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
    assert( count_words( dir_fd, name, &count ) == 0 );
    return count;
}

int main( void )
{
    int dir_fd = openat( AT_FDCWD, ".", O_DIRECTORY );

    if ( dir_fd == -1 )
        err( 1, "opening working directory" );

    assert( check_words( dir_fd, "x\n" ) == 1 );
    assert( check_words( dir_fd, "x\ny\n" ) == 2 );
    assert( check_words( dir_fd, "foo bar\n" ) == 2 );
    assert( check_words( dir_fd, "foo bar\nbaz\n" ) == 3 );
    assert( check_words( dir_fd, "foo" ) == 1 );

    close_or_warn( dir_fd, "working directory" );
    return 0;
}
