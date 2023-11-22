#define _POSIX_C_SOURCE 200809L

/* V této ukázce se podíváme na to, jak se lze vypořádat s fatálními
 * chybami, zejména těmi, které jsou zaviněny selháním systémového
 * volání. Pro tento účel budeme používat «nestandardní» rozšíření
 * v podobě hlavičkového souboru ‹err.h›. Tento kompromis volíme ze
 * dvou důvodů:
 *
 *  1. jedná se o velmi široce podporované rozšíření a není
 *     jednoduché nalézt POSIX-kompatibilní operační systém, který
 *     by je neposkytoval,
 *  2. definice funkcí ‹err›, ‹errx›, ‹warn› a ‹warnx›, které budeme
 *     v tomto předmětu používat, se vejdou na cca 50 řádků, takže
 *     v případě potřeby je není problém do programů doplnit
 *     (zajímají-li Vás možné definice, naleznete je v poslední
 *     ukázce této kapitoly). */

#include <err.h>        /* err, errx, warn, warnx */
#include <stdio.h>      /* dprintf */
#include <unistd.h>     /* STDOUT_FILENO */

/* Tento program nedělá nic jiného, než že na standardní výstup
 * zapíše řetězec. Podle toho, jak ho spustíme, může i tato velmi
 * jednoduchá operace selhat. Podrobněji se základy vstupu a výstupu
 * budeme zabývat v první kapitole. */

int main( void ) /* demo */
{
    /* Pro vypořádání se s chybou, která není fatální, ale je
     * žádoucí na ni upozornit uživatele, budeme obvykle používat
     * proceduru ‹warn›, která vypíše chybové hlášení ale pokračuje
     * ve vykonávání programu. Spustíte-li si tento program např.
     * příkazem, dostanete výpis, který krom samotné chyby vypíše:
     *
     *  1. jméno programu (toto je užitečné zejména ve chvíli, kdy
     *     uživatel spustil několik programů najednou, třeba
     *     spojených rourou)
     *  2. popis poslední «systémové» chyby – výčet možných chyb
     *     naleznete např. v manuálové stránce ‹man 7p errno›.
     *
     * Příklad:
     *
     *     $ ./d3_err > /dev/full
     *     d3_err: write to stdout failed: No space left on device
     *     d3_err: write to stdout failed: No space left on device
     *     $ echo $?
     *     1
     *
     * Všimněte si zejména poslední části chybového výpisu, kde je
     * uvedeno jakým způsobem poslední systémové volání selhalo. Je
     * ovšem naším úkolem dodat uživateli dostatečný kontext, aby
     * mohl tuto část chybového výpisu interpretovat (je například
     * dobré v první části chyby zmínit s jakým souborem se
     * pracovalo). */

    if ( dprintf( STDOUT_FILENO, "foo\n" ) == -1 )
        warn( "write to stdout failed" );

    /* Procedura ‹err› pracuje podobně, ale program zároveň ukončí,
     * a to s návratovým kódem, který jí byl předán v prvním
     * parametru. Varianty ‹warnx› a ‹errx› se liší tím, že
     * nevypisují poslední část, tzn. popis systémové chyby. Můžeme
     * je tedy použít v situaci, kdy chyba nepochází ze systémového
     * volání. */

    if ( dprintf( STDOUT_FILENO, "bar\n" ) == -1 )
        err( 1, "write to stdout failed" );

    return 0;
}
