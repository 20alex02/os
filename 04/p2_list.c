#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <unistd.h>     /* close, write, unlink */
#include <fcntl.h>      /* open, O_RWDR, O_CREAT, O_TRUNC */

/* V tomto cvičení je Vaším úkolem zpracovat soubor, ve kterém jsou
 * uloženy záznamy pevné velikosti. Každý záznam obsahuje odkaz na
 * následující záznam, který je:
 *
 *  • uložen v posledních 4 bajtech,
 *  • formou celého čísla se znaménkem (v dvojkovém doplňkovém kódu,
 *    nejvýznamnější bajt první),
 *  • které určuje o kolik bajtů dále od začátku soubor začíná
 *    odkazovaný záznam oproti tomu aktuálnímu.
 *
 * Podprogram ‹check_list› ověří, že soubor má správnou strukturu:
 * 
 *  • soubor neobsahuje nic než záznamy,
 *  • každý záznam vyjma posledního odkazuje na začátek jiného záznamu,
 *  • poslední záznam odkazuje sám na sebe (tj. odkaz je nulový),
 *  • na první záznam žádný jiný neodkazuje,
 *  • na každý další záznam existuje právě jeden odkaz.
 *
 * Vstupem pro ‹check_list› je popisovač souboru (o kterém můžete
 * předpokládat, že odkazuje objekt, který lze mapovat do paměti) a
 * velikost jednoho záznamu. Návratová hodnota 0 znamená, že soubor
 * je korektní, hodnota 1, že nikoliv a -1 že při zpracování souboru
 * nastala systémová chyba.
 *
 * Podprogram musí pracovat efektivně i v situaci, kdy je v souboru
 * velké množství malých záznamů. Bez ohledu na obsah souboru musí
 * výpočet skončit (podprogram se nesmí zacyklit). */

int check_list( int fd, int rec_size );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

int check( int rec_size, size_t file_size, const char *data )
{
    int fd = open( "zt.p2_data", O_RDWR | O_CREAT | O_TRUNC, 0600 );
    if ( fd == -1 )
        err( 1, "open" );
    if ( file_size > 0 && write( fd, data, file_size ) == -1 )
        err( 1, "write data" );
    if ( unlink( "zt.p2_data" ) == -1 )
        err( 1, "unlink" );

    int rv = check_list( fd, rec_size );

    if ( close( fd ) == -1 )
        warn( "close" );

    return rv;
}

int main( void )
{
    assert( 0 == check( 1024, 0, "" ) );
    assert( 0 == check( 4, 4, "\0\0\0\0" ) );
    assert( 1 == check( 5, 6, "\0\0\0\0\0\0" ) );

    assert( 0 == check( 4, 8, "\0\0\0\4\0\0\0\0" ) );
    assert( 1 == check( 4, 9, "\0\0\0\5\0\0\0\0\0" ) );
    assert( 1 == check( 4, 9, "\0\0\0\xa" "\0\0\0\0" "\0\0\0\0" ) );
    assert( 1 == check( 4, 9, "\0\0\0\xa" "\0\0\0\xa" "\0\0\0\0" "\xff\xff\xff\xf5" ) );

    assert( 0 == check( 5, 10, "a\0\0\0\5b\0\0\0\0" ) );
    assert( 1 == check( 5, 10, "a\0\0\0\5b\xff\xff\xff\xfc" ) );
    assert( 1 == check( 5, 10, "a\0\0\0\5b\0\0\0\4" ) );
    assert( 0 == check( 5, 15, "a\0\0\0\xa" "b\0\0\0\0" "c\xff\xff\xff\xfb" ) );
    assert( 1 == check( 5, 15, "a\0\0\0\xa" "b\0\0\0\0" "c\xff\xff\xff\6" ) );

    assert( 0 == check( 7, 49, "fst\0\0\0\x2a" "six\0\0\0\7" "end\0\0\0\0"
                               "frt\0\0\0\xe" "trd\xff\xff\xff\xf9"
                               "fif\xff\xff\xff\xe4" "snd\xff\xff\xff\xf2" ) );

    // cyklický seznam
    assert( 1 == check( 7, 49, "fst\0\0\0\x2a" "six\0\0\0\7" "sev\xff\xff\xff\xf2"
                               "frt\0\0\0\xe" "trd\xff\xff\xff\xf9"
                               "fif\xff\xff\xff\xe4" "snd\xff\xff\xff\xf2" ) );

    // laso
    assert( 1 == check( 7, 49, "fst\0\0\0\x2a" "six\0\0\0\7" "sev\0\0\0\7"
                               "frt\0\0\0\xe" "trd\xff\xff\xff\xf9"
                               "fif\xff\xff\xff\xe4" "snd\xff\xff\xff\xf2" ) );

    // rozvětvený strom
    assert( 1 == check( 7, 49, "fst\0\0\0\x2a" "bad\0\0\0\7" "bad\0\0\0\xe"
                               "bad\0\0\0\7" "end\0\0\0\0"
                               "bad\xff\xff\xff\xf9" "snd\xff\xff\xff\xf2" ) );

    // dva seznamy
    assert( 1 == check( 7, 49, "fst\0\0\0\x2a" "no1\0\0\0\7" "no2\0\0\0\7"
                               "no3\0\0\0\xe" "end\0\0\0\0"
                               "no4\0\0\0\0" "snd\xff\xff\xff\xf2" ) );

    // cyklus vedle seznamu
    assert( 1 == check( 7, 49, "fst\0\0\0\x2a" "lo1\0\0\0\7" "lo2\0\0\0\7"
                               "lo3\0\0\0\xe" "end\0\0\0\0"
                               "lo4\xff\xff\xff\xe4" "snd\xff\xff\xff\xf2" ) );
}
