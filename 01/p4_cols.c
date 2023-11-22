#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlink */
#include <fcntl.h>      /* open */
#include <assert.h>     /* assert */
#include <string.h>     /* strcmp */
#include <stdio.h>      /* dprintf */
#include <errno.h>      /* errno */
#include <err.h>        /* NONPOSIX: err */
#include <stdbool.h>
#include <stdlib.h>


/* Vaším úkolem je implementovat proceduru ‹write_aligned›, jejímž
 * účelem bude na výstupní popisovač vypsat tabulku v níže popsaném
 * formátu, avšak doplněnou o mezery tak, aby byl každý sloupec
 * zarovnaný vpravo. Zároveň by měl být použit minimální nutný počet
 * takových mezer.
 *
 * CSV je velmi jednoduchý formát na ukládání tabulek. Jelikož však
 * neexistuje¹ jediný standard, který by byl všeobecně dodržován, je
 * možné se setkat s množstvím rozdílných variant.
 *
 * Pro toto zadání uvažme následující:
 *
 *   • Hodnoty jsou ukládány «oddělené» znakem čárky ‹,› – U+002C.
 *   • Počet hodnot na každém řádku odpovídá počtu sloupců.
 *     Všechny řádky tak musí obsahovat stejný počet hodnot, jinak
 *     by se nejednalo o validní tabulku.
 *   • Každý řádek je «zakončen» znakem ‹\n› (U+000A, označovaný
 *     ‹line feed› nebo též ‹newline›).
 *
 * Protože budeme beztak hodnoty zarovnávat mezerami, pro lepší
 * čitelnost budeme také za každou oddělovací čárkou zapisovat
 * alespoň jednu mezeru. Procedura akceptuje čtyři parametry:
 *
 *   • ‹fd› – «výstupní» popisovač, na který má být zapsána
 *     vpravo zarovnaná tabulka;
 *   • ‹values› – ukazatel na pole celočíselných hodnot o velikosti
 *     alespoň ‹cols × rows›;
 *   • ‹cols› – počet sloupců;
 *   • ‹rows› – počet řádků.
 *
 * Návratová hodnota nechť je v případě úspěchu 0 a jinak -1
 * (například selže-li zápis na zadaný popisovač).
 *
 * «Příklad:» Pro hodnoty ‹123, 456, 789, 1, 2, 3, 12, 3, 456› a
 * velikost tabulky 3 × 3 očekáváme, že na výstup bude vypsán
 * řetězec:
 *
 *     "123, 456, 789\n"
 *     "  1,   2,   3\n"
 *     " 12,   3, 456\n"
 *
 * Nápověda: jistě Vám přijde vhod procedura ‹dprintf› –
 * doporučujeme podívat se zejména co znamená znak ‹*› ve
 * formátovacím řetězci. Rovněž může být užitečná funkce ‹snprintf›
 * s omezením na nulovou délku.
 *
 * ¹ Existuje definice dle RFC 4180, nicméně tato se nezdá být
 *   širší komunitou považována za závaznou. */

int get_number_length(int number) {
    int length = number > 0 ? 0 : 1;
    while (number != 0) {
        number /= 10;
        ++length;
    }
    return length;
}

void get_paddings(const int *values, int cols, int rows, int paddings[cols]) {
	int max = *values;
	int min = *values;
	int n, len_min, len_max;
    for (int col = 0; col < cols; ++col) {
        for (int row = 0; row < rows; ++row) {
            n = values[row * cols + col];
            if (n > max) {
                max = n;
            }
            if (n < min) {
                min = n;
            }
        }
        len_max = get_number_length(max);
        len_min = get_number_length(min);
        paddings[col] = len_max > len_min ? len_max : len_min;
    }
}

int write_aligned( int fd, const int *values, int cols, int rows ) {
	int paddings[cols];
    get_paddings(values, cols, rows, paddings);
    for (int row = 0; row < rows; ++row) {
		for (int col = 0; col < cols; ++col) {
			if (dprintf(fd, "%*d", paddings[col], values[row * cols + col]) < 0) {
				return -1;
			}
			if (col + 1 < cols && dprintf(fd, ", ") < 0) {
				return -1;
			}
		}
		if (dprintf(fd, "\n") < 0) {
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

static int check_aligned( const int *values, int cols, int rows,
                          const char *expected )
{
    const char *name = "zt.p4_test_out";
    char buffer[ 4096 + 1 ] = { 0 };
    int read_fd, write_fd;

    write_fd = openat( AT_FDCWD, name,
                       O_CREAT | O_TRUNC | O_WRONLY, 0666 );
    read_fd  = openat( AT_FDCWD, name, O_RDONLY );

    if ( write_fd == -1 )
        err( 2, "creating %s", name );
    if ( read_fd == -1 )
        err( 2, "opening %s", name );

    assert( write_aligned( write_fd, values, cols, rows ) == 0 );

    if ( read( read_fd, buffer, 4096 ) == -1 )
        err( 2, "read" );

    if ( close( write_fd ) == -1 || close( read_fd ) == -1 )
        warn( "closing %s", name );

    return strcmp( expected, buffer );
}

int main( void )
{
    unlink_if_exists( "zt.p4_test_out" );
    const char *a_fmt, *b_fmt, *c_fmt, *d_fmt;

    int a[] = { 123456,
                     1, };
    a_fmt =    "123456\n"
               "     1\n";

    int b[] = { 123, 456,
                  1,   2, };
    b_fmt =    "123, 456\n"
               "  1,   2\n";

    int c[] = { 123, 456, 789,
                  1,   2,   3,
                 12,   0, 123, };

    c_fmt    = "123, 456, 789\n"
               "  1,   2,   3\n"
               " 12,   0, 123\n";

    int d[] = {  123,     45,
                   1,      2,
                9999,      0,
                   1, 100001, };
    d_fmt   =  " 123,     45\n"
               "   1,      2\n"
               "9999,      0\n"
               "   1, 100001\n";

    assert( check_aligned( a, 1, 2, a_fmt ) == 0 );
    assert( check_aligned( b, 2, 2, b_fmt ) == 0 );
    assert( check_aligned( c, 3, 3, c_fmt ) == 0 );
    assert( check_aligned( d, 2, 4, d_fmt ) == 0 );

    unlink_if_exists( "zt.p4_test_out" );
    return 0;
}
