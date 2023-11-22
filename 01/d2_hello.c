#define _POSIX_C_SOURCE 200809L

/* Krom obyčejných souborů lze popisovače využít k práci s řadou
 * dalších podobných zdrojů. V tomto programu si ukážeme, jak
 * pracovat s takzvaným standardním vstupem a výstupem (angl.
 * standard input/output, nebo také stdio). Tento se v systémech
 * POSIX skládá ze tří částí, reprezentovaných třemi popisovači:¹
 *
 *  • standardní vstup, na popisovači číslo 0, symbolicky
 *    ‹STDIN_FILENO›, který je implicitně „připojený ke klávesnici“
 *    (čtením z tohoto popisovače získáme bajty, které uživatel
 *    zadal do terminálu po spuštění programu),
 *  • standardní výstup, na popisovači číslo 1, symbolicky
 *    ‹STDOUT_FILENO›, který je implicitně „připojený k obrazovce“
 *    (zápisem do tohoto popisovače zobrazujeme data na obrazovku
 *    terminálu),
 *  • standardní «chybový» výstup, na popisovači číslo 2, symbolicky
 *    ‹STDERR_FILENO›, který je implicitně spojen s předchozím a
 *    tedy odesílán na obrazovku.
 *
 * Přesto, že se na pohled chovají zaměnitelně, je mezi standardním
 * výstupem a standardním chybovým výstupem klíčový «sémantický»
 * rozdíl – výstup běžného neinteraktivního programu lze pomyslně
 * rozdělit do dvou oddělených proudů:
 *
 *  • užitný výstup, který je hlavním výsledkem výpočtu a tzv.
 *  • diagnostický výstup, který obsahuje informace o průběhu
 *    výpočtu, zejména o chybách.
 *
 * Nikoho zřejmě nepřekvapí, že tyto dva typy výstupů patří každý na
 * odpovídající typ standardního výstupu. Důležitým efektem tohoto
 * rozdělení je, že když standardní výstup «přesměrujeme» (např. do
 * souboru, nebo na vstup dalšího programu), nebudou se do dat
 * určených k dalšímu zpracování míchat diagnostické výstupy –
 * naopak, uživateli budou nadále přístupné na obrazovce.² */

#include <unistd.h> /* write, STDIN_FILENO, … */
#include <stdio.h>  /* dprintf */
#include <err.h>    /* err */

int main( void ) /* demo */
{
    /* Veškerý výstup do popisovače otevřeného souboru je realizován
     * systémovým voláním ‹write›.³ Zejména veškeré sofistikované
     * knihovní funkce pro výstup (např. ‹dprintf›, ale třeba i
     * procedury z rodiny ‹err›) nakonec výstup realizují voláním
     * ‹write›. Systémové volání ‹write› je ve svém principu velmi
     * jednoduché: předáme mu relevantní popisovač, ukazatel na
     * paměť a počet bajtů, které má vypsat. */

    const int nbytes = 5;
    int bytes_written = write( STDOUT_FILENO, "hello", nbytes );

    /* Uspěje-li volání ‹write›, náš program právě na standardní
     * výstup zapsal 5 bajtů, 0x68, 0x65, 0x6c, 0x6c a konečně 0x6f
     * (viz také ‹man ascii›). Úspěch ale ani u takto na první
     * pohled jednoduché operace není zaručen. Výstup mohl být
     * například uživatelem přesměrován do souboru nebo do roury.
     * Za určitých okolností může také zápis uspět částečně, ale
     * touto situací se prozatím nebudeme zabývat.⁴
     *
     * Protože standardní výstup a standardní chybový výstup nemusí
     * být tentýž objekt, má i v případě selhání zápisu na
     * standardní výstup smysl zapsat na chybový výstup hlášení
     * o chybě. Je již na uživateli, aby chybový výstup směroval na
     * podle možnosti spolehlivá zařízení. Krom ukončení programu
     * s chybovým kódem nemáme žádnou možnost, jak na chybu při
     * výpisu chybové hlášky reagovat. */

    if ( bytes_written == -1 )
        err( 1, "write to stdout failed" );

    /* Krom přímého použití systémového volání ‹write› budeme
     * k zápisu používat knihovní proceduru ‹dprintf›, která nám
     * umožní jednodušeji sestavovat textové zprávy. Protože
     * ‹dprintf› vnitřně používá systémová volání, která mohou
     * (opět) selhat, musí se s případnou chybou nějak vypořádat.
     * Protože se jedná o knihovní podprogram, který by měl být
     * použitelný v mnoha různých situacích, prakticky jedinou
     * rozumnou možností je informaci o selhání předat volajícímu.
     * Podobně jako samotná systémová volání k tomu používá
     * návratovou hodnotu. Rychlý pohled do ‹man dprintf› nám sdělí,
     * že výsledkem funkce je počet zapsaných bajtů, resp. -1 pokud
     * nastala nějaká chyba. */

    if ( dprintf( STDOUT_FILENO, " world\n" ) == -1 )
        err( 1, "write to stdout failed" );

    /* Konečně vypíšeme nějaký text i na chybový výstup. Protože
     * selhání výstupu v tomto případě nemáme jak dále
     * diagnostikovat, program pouze ukončíme s chybou. */

    if ( dprintf( STDERR_FILENO, "hello stderr\n" ) == -1 )
        return 2;

    /* Abyste si ověřili, že rozumíte tomu, jak se tento program
     * chová, zkuste si jej spustit následovnými způsoby, a ujistěte
     * se, že jste tento výsledek očekávali (soubor ‹/dev/full›
     * simuluje situaci, kdy dojde místo v souborovém systému – není
     * standardizován, ale na stroji ‹aisa› je k dispozici):
     *
     *     $ ./d2_hello
     *     $ ./d2_hello > /dev/null
     *     $ ./d2_hello > /dev/full
     *     $ ./d2_hello 2> /dev/null
     *     $ ./d2_hello 2> /dev/full
     *
     * Návratový kód programu si můžete bezprostředně po jeho
     * ukončení vypsat příkazem ‹echo $?› (platí interprety příkazů,
     * které se drží normy POSIX). */

    return 0;
}

/* ¹ Součástí jazyka C (nezávisle na standardu POSIX) jsou knihovní
 *   funkce pro práci standardním vstupem a výstupem, které možná
 *   znáte. Patří sem např. procedury ‹printf›, ‹fprintf›, ‹puts›,
 *   atp. – tyto můžete v principu používat jako ladící pomůcky, ale
 *   v hotových programech se jim raději vyhněte. Potřebujete-li
 *   alternativu k ‹fprintf›, použijte ‹dprintf›. «Pozor»! Programy,
 *   které míchají vstupně-výstupní prostředky jazyka C s těmi
 *   POSIX-ovými, jsou náchylné na těžko odhalitelné chyby (zejména
 *   při nesprávném nebo chybějícím použití procedury ‹fflush›).
 * ² Mohli byste mít pocit, že výstup Vašeho programu nebude nikdo
 *   dále automaticky zpracovávat a tedy na toto rozdělení nemusíte
 *   dbát. Uvědomte si ale, že i běžné použití programu ‹grep› pro
 *   vyhledání relevantních řádků ve výstupu na toto rozdělení
 *   spoléhá. Uvažte situaci, kdy ‹./program | grep system› nic
 *   nevypíše – je to proto, že program selhal, ale ‹grep› chybovou
 *   hlášku z výstupu odstranil, nebo proto, že žádný řádek ve
 *   výpisu neobsahoval řetězec ‹system›?
 * ³ Nebo některou jeho variantou – ‹pwrite›, ‹writev›, ‹pwritev›,
 *   které jdou ale nad rámec tohoto předmětu.
 * ⁴ Situace s částečnými zápisy je poněkud komplikovaná – pro
 *   blokující popisovač (případ, kdy by byl standardní vstup nebo
 *   výstup nastaven do neblokujícího režimu, nebudeme řešit) může
 *   být zápis (nebo analogicky i čtení) přerušeno signálem. Protože
 *   naše programy nebudou signály obsluhovat, doručení signálu
 *   zároveň ukončí program, a tedy částečné zápisy nebo čtení
 *   nemusíme řešit, není-li pro to speciální důvod (např. použití
 *   neblokujících popisovačů – ‹O_NONBLOCK›). */
