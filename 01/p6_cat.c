#define _POSIX_C_SOURCE 200809L

#include <stdio.h>      /* dprintf */
#include <assert.h>     /* assert */
#include <string.h>     /* strcmp, memset */
#include <unistd.h>     /* unlinkat, close */
#include <fcntl.h>      /* open, openat */
#include <errno.h>      /* errno */
#include <err.h>        /* NONPOSIX: err */

/* Naprogramujte proceduru ‹cat›, která obdrží tyto 3 parametry:
 *
 *   • ‹dir_fd› – popisovač adresáře, ve kterém bude hledat všechny
 *     níže zmíněné soubory,
 *   • ‹list› – jméno souboru se jmény souborů ke čtení,
 *   • ‹out_fd› – «výstupní» popisovač.
 *
 * Soubor ‹list› bude obsahovat na každém řádku jméno souboru.
 * Procedura ‹cat› zapíše obsahy všech těchto souborů (v zadaném
 * pořadí) do popisovače ‹out›.
 *
 * Stejně jako v předešlých příkladech za řádek považujeme
 * posloupnost znaků «zakončenou» ‹'\n'› (nikoliv tedy ‹"\r\n"› nebo
 * ‹'\r'›).
 *
 * Pro zjednodušení navíc zavedeme limit, kdy délka každého řádku
 * smí být nejvýše ‹name_max› bajtů (nepočítaje znak konce řádku).
 */

static const int name_max = 256;

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

/* Návratová hodnota 0 označuje úspěch. Pokud je některý ze řádků
 * souboru ‹list› delší než ‹name_max›, vraťte -2; nastane-li
 * systémová chyba, hodnotu -1.
 *
 * Nápověda: na popisovači k souboru ‹list› je možné používat volání
 * ‹lseek›. Jeho použitím si můžete usnadnit implementaci. */

int readline(int fd, char *buff) {
    int bytes_read;
    int offset = 0;
    while ((bytes_read = read(fd, buff + offset, 1)) > 0) {
        if (buff[offset] == '\n') {
            buff[offset] = '\0';
            return 0;
        }
        if (offset == name_max) {
            return -2;
        }
        ++offset;
    }
    if (bytes_read == -1) {
        return -1;
    }
    return 1;
}

int copy_file_content(int fd_in, int fd_out) {
    int bytes_transferred;
    int c;
    while ((bytes_transferred = read(fd_in, &c, 1)) > 0) {
        if (write(fd_out, &c, 1) != 1) {
            return -1;
        }
    }
    if (bytes_transferred == -1) {
        return -1;
    }
    return 0;
}

int cat( int dir_fd, const char *list, int out_fd ) {
    int rv = -1;
    int file = -1;
    char filename[name_max + 1];
    int list_fd = openat(dir_fd, list, O_RDONLY);
    if (list_fd == -1) {
        return -1;
    }
    int readline_rv;
    while ((readline_rv = readline(list_fd, filename)) == 0) {
        file = openat(dir_fd, filename, O_RDONLY);
        if (file == -1 || copy_file_content(file, out_fd) == -1) {
            goto error;
        }
        close_or_warn(file, filename);
        file = -1;
    }

    if (readline_rv < 0) {
        rv = readline_rv;
        goto error;
    }

    rv = 0;
  error:
    close_or_warn(list_fd, list);
    if (file != -1) {
        close_or_warn(file, filename);
    }
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char *name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", name );
}

static int open_or_die( int dir_fd, const char *name )
{
    int fd = openat( dir_fd, name, O_RDONLY );

    if ( fd == -1 )
        err( 2, "opening %s", name );

    return fd;
}

static int create_file( int dir_fd, const char *name )
{
    unlink_if_exists( dir_fd, name );
    int fd;

    if ( ( fd = openat( dir_fd, name,
                        O_CREAT | O_TRUNC | O_WRONLY,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static int check_cat( int dir_fd, const char *list_name )
{
    int out_fd, result;
    const char *out_name = "zt.p6_test_out";

    out_fd = create_file( dir_fd, out_name );
    result = cat( dir_fd, list_name, out_fd );
    close_or_warn( out_fd, out_name );

    return result;
}

static int check_output( const char* expected )
{
    const char *name = "zt.p6_test_out";
    char buffer[ 4096 + 1 ] = { 0 };

    int read_fd = open_or_die( AT_FDCWD, name );

    if ( read( read_fd, buffer, 4096 ) == -1 )
        err( 2, "reading %s", name );

    close_or_warn( read_fd, name );
    return strcmp( expected, buffer );
}

static void write_file( int dir, const char *name, const char *str )
{
    int fd = create_file( dir, name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

int main( void )
{
    int dir;

    if ( ( dir = open( ".", O_RDONLY ) ) == -1 )
        err( 2, "opening working directory" );

    write_file( dir, "zt.p6_a", "contents of zt.p6_a\n" );
    write_file( dir, "zt.p6_b", "contents of zt.p6_b\n" );
    write_file( dir, "zt.p6_c", "contents of zt.p6_c\n" );
    write_file( dir, "zt.p6_d", "contents of zt.p6_d\n" );

    unlink_if_exists( dir, "zt.p6_test_out" );
    unlink_if_exists( dir, "zt.p6_lst1" );
    unlink_if_exists( dir, "zt.p6_lst2" );
    unlink_if_exists( dir, "zt.p6_lst3" );
    unlink_if_exists( dir, "zt.p6_lst4" );

    int lst1 = create_file( dir, "zt.p6_lst1" );
    int lst2 = create_file( dir, "zt.p6_lst2" );
    int lst3 = create_file( dir, "zt.p6_lst3" );
    int lst4 = create_file( dir, "zt.p6_lst4" );

    dprintf( lst1, "%s\n", "zt.p6_a" );
    dprintf( lst2, "%s\n%s\n%s\n", "zt.p6_a", "zt.p6_b", "zt.p6_c" );
    dprintf( lst3, "%s\n%s\n%s\n%s\n%s\n%s\n",
                   "zt.p6_a", "zt.p6_b", "zt.p6_c",
                   "zt.p6_d", "zt.p6_a", "zt.p6_b" );

    char long_line[ name_max + 2 ];
    memset( long_line, 'a', name_max + 1 );
    long_line[ name_max + 1 ] = 0;
    dprintf( lst4, "%s\n", long_line );

    if ( close( lst1 ) || close( lst2 ) ||
         close( lst3 ) || close( lst4 ) )

        err( 2, "close" );

    assert( check_cat( dir, "zt.p6_lst1" ) == 0 );
    assert( check_output( "contents of zt.p6_a\n" ) == 0 );

    assert( check_cat( dir, "zt.p6_lst2" ) == 0 );
    assert( check_output( "contents of zt.p6_a\n"
                          "contents of zt.p6_b\n"
                          "contents of zt.p6_c\n" ) == 0 );

    assert( check_cat( dir, "zt.p6_lst3" ) == 0 );
    assert( check_output( "contents of zt.p6_a\n"
                          "contents of zt.p6_b\n"
                          "contents of zt.p6_c\n"
                          "contents of zt.p6_d\n"
                          "contents of zt.p6_a\n"
                          "contents of zt.p6_b\n" ) == 0 );

    unlink_if_exists( dir, "zt.p6_a" );
    assert( check_cat( dir, "zt.p6_lst1" ) == -1 );
    assert( errno == ENOENT );

    assert( check_cat( dir, "zt.p6_lst4" ) == -2 );

    unlink_if_exists( dir, "zt.p6_a" );
    unlink_if_exists( dir, "zt.p6_b" );
    unlink_if_exists( dir, "zt.p6_c" );
    unlink_if_exists( dir, "zt.p6_d" );
    unlink_if_exists( dir, "zt.p6_lst1" );
    unlink_if_exists( dir, "zt.p6_lst2" );
    unlink_if_exists( dir, "zt.p6_lst3" );
    unlink_if_exists( dir, "zt.p6_lst4" );
    unlink_if_exists( dir, "zt.p6_test_out" );
    close_or_warn( dir, "working directory" );

    return 0;
}
