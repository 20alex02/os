#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlink */
#include <fcntl.h>      /* open */
#include <assert.h>     /* assert */
#include <string.h>     /* strlen */
#include <errno.h>      /* errno */
#include <err.h>        /* NONPOSIX: err */

/* POSIX definuje řádek jako posloupnost libovolných znaků
 * zakončenou znakem nového řádku ‹\n› (U+000A, označovaný ‹line
 * feed› nebo též ‹newline›).
 *
 * Implementujte podprogram ‹count_lines›, který spočítá řádky na
 * vstupu daném popisovačem ‹fd› a ověří, zda vstup neobsahuje žádné
 * nekompletní řádky. Počet (kompletních) řádků vrátí skrze ukazatel
 * ‹count›.
 *
 * Vstup zpracovávejte postupně po malých částech (množství paměti
 * potřebné pro spuštění programu by nemělo záviset na velikosti
 * vstupu).
 *
 * Návratová hodnota bude:
 *
 *   • ‹0› proběhlo-li vše v pořádku,
 *   • ‹1› obsahuje-li soubor nekompletní řádek,
 *   • ‹2› v případě selhání čtení nebo jiné systémové chyby
 *     (v tomto případě navíc není určeno, jaká hodnota bude zapsána
 *     do výstupního parametru ‹count›). */

int count_lines( int fd, int *count ) {
	int lines = 0;
	char c = '\n';
	int bytes_read = 0;
	while ((bytes_read = read(fd, &c, 1)) == 1) {
		if (c == '\n') {
			++lines;
		}
	}
	if (bytes_read == -1) {
		return 2;
	}
	*count = lines;
	if (c != '\n') {
		return 1;
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

static int check_file( const char *file, int *count )
{
    int fd, result;

    if ( ( fd = openat( AT_FDCWD, file, O_RDONLY ) ) == -1 )
        err( 2, "opening %s", file );

    result = count_lines( fd, count );
    close_or_warn( fd, file );
    return result;
}

static int check_string( const char *str, int *count )
{
    int fd;
    const char *name = "zt.p1_test_in";

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_WRONLY,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
    return check_file( name, count );
}

int main( void )
{
    unlink_if_exists( "zt.p1_test_in" );

    int count = -1;
    assert( check_string( "\n", &count ) == 0 );
    assert( count == 1 );

    assert( check_string( "a\nb\nc\n", &count ) == 0 );
    assert( count == 3 );

    assert( check_string( "hello world\n\n", &count ) == 0 );
    assert( count == 2 );

    assert( check_string( "hello world\r\n\r\n", &count ) == 0 );
    assert( count == 2 );

    assert( check_string( "Roses are red,\n"
                             "violets are blue,\n"
                             "hello world\n"
                             "possibly from Linux/GNU.\n",
                             &count ) == 0 );
    assert( count == 4 );

    assert( check_string( "", &count ) == 0 );
    assert( count == 0 );

    assert( check_string( " \n ", &count ) == 1 );
    assert( check_string( "\n \ninvalid line", &count ) == 1 );
    assert( check_string( " a b c", &count ) == 1 );

    int fd_wronly = open( "/dev/null", O_WRONLY );
    if ( fd_wronly == -1 )
        err( 2, "opening /dev/null" );

    assert( count_lines( fd_wronly, &count ) == 2 );
    assert( errno == EBADF );

    unlink_if_exists( "zt.p1_test_in" );
    return 0;
}
