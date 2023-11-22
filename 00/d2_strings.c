/* Operační systém a také většina programů při své činnosti
 * komunikuje s uživatelem. Tato komunikace je obvykle postavena na
 * textu, a proto musí mít program a operační systém společný
 * způsob, kterým text reprezentuje v paměti. I kdybychom si
 * odmysleli klasické výpisy na obrazovku, tak základní věc jako
 * soubor má «jméno», které je samozřejmě také kusem textu.
 *
 * Normy ISO C a POSIX specifikují některé základní charakteristiky
 * kódování textu:
 *
 *  1. každé písmeno¹ je kódováno nějakou posloupností bajtů, a
 *     fragment textu (tzv. «řetězec») je v paměti uložen tak, že
 *     kódování jednotlivých znaků (písmen) jsou uložena za sebou
 *     (na postupně se zvyšujících adresách),
 *  2. nulový bajt je vyhrazen pro speciální účely (pro řadu
 *     knihovních a systémových funkcí označuje konec řetězce, tzn.
 *     bajt uložený bezprostředně před nulovým bajtem je poslední
 *     bajt kódující daný řetězec),
 *  3. vybranou množinu znaků, nazývanou Portable Character Set,
 *     musí být systém schopen kódovat, a to navíc tak, že každému
 *     znaku odpovídá jeden bajt (jedná se o většinu znaků
 *     obsažených v ASCII²),
 *  4. konkrétní číselné hodnoty, kterými jsou tyto znaky kódované,
 *     nejsou pevně určeny (s výjimkou nulového znaku, který musí
 *     být kódován nulovým bajtem), ale v praxi každý systém, který
 *     potkáte, bude používat kódování ASCII, a naprostá většina
 *     UTF-8 (které je nadmnožinou ASCII). */

#define _POSIX_C_SOURCE 200809L
#include <string.h>     /* strlen, strcmp */
#include <stdio.h>      /* dprintf, snprintf */
#include <unistd.h>     /* STDOUT_FILENO */
#include <assert.h>

int main( void ) /* demo */
{
    /* Nejjednodušší způsob, jak v programu získat kódování nějakého
     * textu je pomocí tzv. «řetězcového literálu». Zdrojový kód je
     * samozřejmě také text, je tedy zcela logické, že programovací
     * jazyky nám umožňují část textu označit za data. Zapíšeme-li
     * v jazyce C do programu řetězcový literál, překladač v paměti
     * programu vyhradí potřebné místo a uloží do tohoto místa
     * kódování uvozeného textu, ukončeno nulovým bajtem. Protože
     * řetězcový literál je výraz, má hodnotu – tato hodnota je
     * (opět platí pro jazyk C) «ukazatel» na takto vyhrazenou
     * paměť. Je obvyklé, že tato paměť je označena «pouze pro
     * čtení», do paměti řetězcového literálu tedy «není dovoleno»
     * zapsat jiná data. */

    const char * const string = "hello";

    /* Pojmenovaná konstanta ‹string› nyní obsahuje ukazatel na
     * paměť, kde je uloženo kódování řetězce ‹hello› ukončené
     * nulovým bajtem. Pro práci s takto uloženými řetězci poskytuje
     * jazyk C sadu základních funkcí. První z nich je ‹strlen›,
     * která zjistí počet «bajtů» (nikoliv znaků), kterými je
     * řetězec zakódovaný. Např.: */

    assert( strlen( string ) == 5 );
    assert( strlen( "věc" ) == 4 );

    /* Pozor, ‹strlen› prochází paměť od předaného ukazatele po
     * bajtech, až dokud nenajde nulový bajt. Není-li na předané
     * adrese uložen nulou ukončený řetězec, může se stát celkem
     * cokoliv (včetně chyby ochrany paměti). */

    /* Další užitečnou funkcí je ‹strcmp›, která po bajtech srovnává
     * dvě oblasti paměti, až dokud nenarazí na rozdílný nebo na
     * nulový bajt. Návratová hodnota je 0, jsou-li na zadaných
     * adresách uloženy stejné bajty³, záporná je-li levá strana
     * lexikograficky (po bajtech) menší a kladná jinak. */

    assert( strcmp( string, "hello" ) == 0 );

    /* Protože řetězce jsou v paměti zakódované jako posloupnost
     * bajtů, můžeme samozřejmě tuto posloupnost bajtů do paměti
     * uložit přímo.⁴ */

    const char thing_1[] = { 0x76, 0xc4, 0x9b, 0x63, 0 };
    const char thing_2[] = { 0x76, 0x65, 0xcc, 0x8c, 0x63, 0 };

    assert( strcmp( "věc",  thing_1 ) == 0 );
    assert( strcmp( "věc",  thing_2 ) != 0 );

    /* Pro tzv. formátovaný výpis můžeme využít knihovní funkce
     * ‹dprintf›. Podrobný popis formátovacího řetězce (druhý
     * parametr) naleznete v její manuálové stránce (‹man dprintf›).
     * Nám v tuto chvíli postačí, že za každou ‹%›-sekvenci se
     * při výpisu dosadí postupně další parametry, a že daná
     * ‹%›-sekvence popisuje tzv. «konverzi», která určuje, jak se
     * má daný parametr vypsat. Konverze ‹%s› interpretuje příslušný
     * parametr jako ukazatel na paměť, která kóduje nulou ukončený
     * řetězec. */

    dprintf( STDOUT_FILENO, "%s - %s\n", thing_1, thing_2 );

    /* Pro výpis číselných hodnot typicky použijeme konverze ‹%d›
     * (desítková) nebo ‹%x› (šestnáctková), případně jsou-li
     * předané hodnoty typu ‹long›, použijeme ‹%ld› nebo ‹%lx›. Pro
     * desítkový výpis bez znaménka použijeme ‹%u› nebo ‹%lu›. */

    dprintf( STDOUT_FILENO, "%d %u %x\n", thing_1[ 0 ],
             (unsigned char) thing_1[ 1 ],
             (unsigned char) thing_1[ 2 ] );

    /* Krom zápisu do souboru (resp. na standardní výstup) můžeme
     * někdy potřebovat pomocí formátování nachystat řetězec
     * v paměti. K tomu lze použít funkci ‹snprintf›, které předáme
     * ukazatel na přichystané místo v paměti, kam bude uloženo
     * výsledné kódování, počet vyhrazených bajtů, formátovací
     * řetězec a případné další parametry.
     *
     * Tato funkce zapíše tolik bajtů výsledného řetězce, kolik
     * umožní vyhrazené místo. Návratová hodnota pak indikuje, kolik
     * bajtů bylo potřeba (bez ohledu na to, jestli se do vyhrazené
     * paměti vešly nebo nikoliv). Poslední zapsaný bajt je vždy
     * nulový, a to i v situaci, kdy zapsaný řetězec není kompletní.
     */

    char buffer[ 5 ];
    assert( snprintf( buffer, 5, "123456789" ) == 9 );
    assert( strcmp( buffer, "1234" ) == 0 );

    return 0;

    // cflags: -Wno-format-truncation
}

/* ¹ Co přesně je písmeno je mnohem komplikovanější otázka, než by
 *   se mohlo zdát. Zájemce o obecně uznávanou definici odkazujeme
 *   na normu Unicode. To, co tady zjednodušeně nazýváme písmenem,
 *   je správně tzv. kódový bod.
 * ² ASCII je norma, která popisuje kódování základní 127-znakové
 *   abecedy, používaná původně ve Spojených státech (proto název
 *   „American Standard Code for Information Interchange“), nyní ale
 *   celosvětově, zejména jako podmnožina kódování UTF-8 standardu
 *   Unicode.
 * ³ Rádi bychom zde řekli, že funkce srovnává řetězce, to by ale
 *   bylo mírně zavádějící. Některé řetězce lze kódovat více než
 *   jedním ekvivalentním způsobem, např. ‹věc› lze v UTF-8
 *   zakódovat jako ‹76 c4 9b 63› nebo jako ‹76 65 cc 8c 63›. Tyto
 *   sekvence reprezentují tentýž «text», ale protože jsou v paměti
 *   uloženy jako různé posloupnosti bajtů, funkce ‹strcmp› je bude
 *   považovat za odlišné.
 * ⁴ Toto bude samozřejmě fungovat pouze tehdy, kdy známe konkrétní
 *   kódování používané daným systémem. Spustíte-li tento program na
 *   systému, který nepoužívá UTF-8, výsledky se mohou lišit.
 *   Zejména výstup na obrazovce nebude odpovídat tomu, co bychom
 *   očekávali. */
