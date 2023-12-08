#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <stdbool.h>        /* bool */
#include <string.h>         /* strlen */
#include <unistd.h>         /* write */
#include <fcntl.h>          /* open, O_* */
#include <err.h>

/* Napište podprogram ‹binfmt›, který přijme popisovač otevřeného
 * obyčejného souboru a na jeho základě rozhodne, zda se jedná
 * o soubor ve spustitelném formátu (můžete předpokládat, že
 * popisovač je nastaven na začátek souboru). Za spustitelné
 * považujeme:
 *
 *  • skripty s korektní „hasbang“ hlavičkou (interpret musí
 *    existovat a být spustitelný),
 *  • soubory s platnou hlavičkou formátu ELF (Executable and
 *    Linkable Format).
 *
 * Hlavička souboru ‹ELF› má 16 bajtů s tímto významem (v hranatých
 * závorkách je uveden offset v bajtech od začátku souboru):
 *
 *  • [0] první 4 bajty jsou ‹"\x7f" "ELF"›,
 *  • [4] jeden bajt určuje třídu – 1 = 32b, 2 = 64b programy,
 *  • [5] kódování dat – 1 = LSB, 2 = MSB,
 *  • [6] bajt určující operační systém (libovolný),
 *  • [7] bajt určující verzi ABI (opět libovolný),
 *  • [8] zarovnání osmi nulovými bajty.
 *
 * Výsledkem budiž:
 *
 *  • hodnota 0 proběhla-li validace v pořádku,
 *  • jinak -1 (nastala systémová chyba, která validaci znemožnila).
 *
 * Výsledek validace podprogram zapíše do výstupního parametru
 * ‹valid›. */

int binfmt( int fd, bool *valid );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static int check_binfmt( const char *data, int len, bool *valid )
{
    const char *name = "zt.p3_test";

    if ( !len )
        len = strlen( data );

    int fd = open( name, O_CREAT | O_TRUNC | O_RDWR, 0666 );

    if ( fd == -1 )
        err( 1, "creating %s", name );
    if ( write( fd, data, len ) == -1 )
        err( 1, "writing to %s", name );
    if ( lseek( fd, 0, SEEK_SET ) == -1 )
        err( 1, "seeking in %s", name );

    int rv = binfmt( fd, valid );
    close( fd );
    return rv;
}

int main()
{
    bool valid;

    assert( check_binfmt( "#!/bin/foo\n", 0, &valid ) == 0 );
    assert( !valid );

    assert( check_binfmt( "#!/bin/sh\n", 0, &valid ) == 0 );
    assert( valid );

    assert( check_binfmt( "\x7f" "ELF\x01\x01\x0c\x01"
                          "\0\0\0\0\0\0\0\0", 16, &valid ) == 0 );
    assert( valid );

    assert( check_binfmt( "\x7f" "ELF\x01\x01\x0c\x01"
                          "x\0\0\0\0\0\0\0", 16, &valid ) == 0 );
    assert( !valid );

    assert( check_binfmt( "\x7f" "ELF\x03\x01\x0c\x01"
                          "\0\0\0\0\0\0\0\0", 16, &valid ) == 0 );
    assert( !valid );

    return 0;
}
