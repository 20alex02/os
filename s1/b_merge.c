#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, pread, close, unlinkat */
#include <fcntl.h>      /* openat */
#include <assert.h>     /* assert */
#include <string.h>     /* memcmp */
#include <errno.h>      /* errno */
#include <err.h>        /* err */

/* Uvažme formát souborů, které obsahují záznamy pevné délky uložené
 * těsně za sebou. Tyto záznamy jsou vždy vzestupně lexikograficky
 * uspořádané podle nějakého klíče, který je zadaný jako rozsah
 * bajtů od-do (zleva uzavřený, zprava otevřený interval). Vaším
 * úkolem je načíst dva soubory tohoto typu a sloučit je do jednoho:
 *
 *  • vstupní soubory mají stejnou velikost záznamu,
 *  • soubory mohou být mnohem větší, než je dostupná paměť,
 *  • vstupy mohou být roury nebo jiné objekty, které neumožňují
 *    opakované čtení (podobně výstup),
 *  • výstup nebude obsahovat dva záznamy se stejným klíčem, a to
 *    ani v situaci, kdy je obsahoval některý soubor na vstupu
 *    (použije se první záznam z prvního souboru, který ho
 *    obsahuje).
 *
 * Návratová hodnota 0 značí úspěch, -1 systémovou chybu, -2
 * nekompletní záznam v některém vstupním souboru. */

int merge( int in1_fd, int in2_fd, int record_size,
           int key_begin, int key_end,
           int out_fd );

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

static int create_file( int dir, const char *name )
{
    unlink_if_exists( dir, name );
    int fd;

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_RDWR,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static int check_output( int fd, const char *name,
                         int offset, int nbytes,
                         const char *expected )
{
    char buffer[ nbytes ];

    if ( pread( fd, buffer, nbytes, offset ) == -1 )
        err( 2, "reading %s (fd %d)", name, fd );

    return memcmp( expected, buffer, nbytes );
}

static void write_file( int dir, const char *name,
                        const char *str, int count )
{
    int fd = create_file( dir, name );

    if ( write( fd, str, count ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

int main( void )
{
    int dir, fd_in_1, fd_in_2, fd_out;
    const char *name_in_1 = "zt.b_input_1",
               *name_in_2 = "zt.b_input_2",
               *name_out  = "zt.b_output";

    if ( ( dir = open( ".", O_RDONLY ) ) == -1 )
        err( 2, "opening working directory" );

    unlink_if_exists( dir, name_in_1 );
    unlink_if_exists( dir, name_in_2 );
    unlink_if_exists( dir, name_out );

    write_file( dir, name_in_1, "\x00\x01", 2 );
    write_file( dir, name_in_2, "\x01\x00", 2 );

    fd_out = create_file( dir, name_out );

    if ( ( fd_in_1 = openat( dir, name_in_1, O_RDONLY ) ) == -1 )
        err( 2, "opening %s", name_in_1 );
    if ( ( fd_in_2 = openat( dir, name_in_2, O_RDONLY ) ) == -1 )
        err( 2, "opening %s", name_in_2 );

    assert( merge( fd_in_1, fd_in_2, 2, 0, 2, fd_out ) == 0 );
    assert( check_output( fd_out, name_out, 0, 4,
                          "\x00\x01\x01\x00" ) == 0 );

    unlink_if_exists( dir, name_in_1 );
    unlink_if_exists( dir, name_in_2 );
    unlink_if_exists( dir, name_out );
    close_or_warn( fd_in_1, name_in_1 );
    close_or_warn( fd_in_2, name_in_2 );
    close_or_warn( fd_out, name_out );
    close_or_warn( dir, "working directory" );

    return 0;
}
