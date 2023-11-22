/* Tento jednoduchý program demonstruje POSIXové rozhraní pro práci
 * s obyčejným souborem. Jako obvykle začneme direktivami ‹#define
 * _POSIX_C_SOURCE› a ‹#include› – budeme potřebovat hlavičku
 * ‹unistd.h›, která deklaruje většinu systémových volání (nás bude
 * v tuto chvíli zajímat pouze ‹read›), a také hlavičku ‹fcntl.h›,
 * která deklaruje ‹openat› (tato jako jedna z mála není deklarována
 * v ‹unistd.h›). */

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>  /* read, write */
#include <fcntl.h>   /* openat */
#include <string.h>  /* memcmp */
#include <err.h>     /* err */

/* Protože se jedná o velmi jednoduchý program, bude obsahovat pouze
 * proceduru ‹main›. Jak si jistě pamatujete, jedná se o vstupní bod
 * programu (spustíme-li přeložený program, začne se vykonávat
 * odsud). */

int main( void ) /* demo */
{
    /* Začneme tím, že otevřeme soubor pro čtení. Z úvodu víme, že
     * k tomu slouží systémové volání ‹openat›, které má v tomto
     * případě 3 parametry. Prozatím se omezíme na soubory
     * v «pracovní složce» spuštěného programu, jako první parametr
     * tedy předáme ‹AT_FDCWD› (aby program správně pracoval, musíte
     * jej spustit přímo ve složce ‹01›).
     *
     * Dalším parametrem je «název souboru» který chceme otevřít,
     * formou řetězce (ukončeného nulou). Krom samotného názvu zde
     * může stát i cesta (relativní nebo absolutní), ale pro tuto
     * chvíli se opět omezíme na práci s jedinou složkou (tou
     * pracovní).
     *
     * Konečně příznak ‹O_RDONLY› specifikuje, že ze souboru hodláme
     * pouze číst (‹O› od ‹open›, ‹RDONLY› od ‹read only›). */

    const char * const filename = "zz.foo.txt";

    int fd = openat( AT_FDCWD, filename, O_RDONLY );

    /* ‹fd› je tradiční název proměnné, která uchovává popisovač
     * otevřeného souboru (z angl. «f»ile «d»escriptor; samozřejmě
     * tento název lze použít pouze v situaci, kdy pracujeme
     * s jediným popisovačem). Za povšimnutí stojí typ této proměnné
     * – POSIX specifikuje, že popisovače souborů jsou typu ‹int›.
     *
     * Než budeme pokračovat, musíme (jako u prakticky každého
     * systémového volání) ověřit, že otevření souboru proběhlo
     * v pořádku. Manuálová stránka pro systémové volání ‹open›
     * (otevřete ji příkazem ‹man 2 open›) v sekci „return value“
     * píše:
     *
     * > If successful, ‹open()› returns a non-negative integer,
     * > termed a file descriptor.  Otherwise, a value of -1 is
     * > returned and ‹errno› is set to indicate the error. */

    if ( fd == -1 ) /* this would indicate an error */

        /* Protože se jedná o kompletní program (nikoliv samostatnou
         * funkci), lze tento typ chyby chápat jako fatální a
         * program s odpovídající chybovou hláškou ukončit. K tomu
         * použijeme proceduru ‹err›, kterou již známe z kapitoly B.
         */

        err( 1, "opening file %s", filename );

    /* Protože procedura ‹err› ukončí program, dostaneme-li se do
     * tohoto místa, víme, že volání ‹open› uspělo a ‹fd› obsahuje
     * platný popisovač, ze kterého lze číst. Čtení provedeme
     * voláním ‹read›, kterému musíme krom popisovače předat
     * ukazatel na paměť, do které data přečtená ze souboru uloží
     * (angl. obvykle označované jako ‹buffer›).
     *
     * Konečně poslední parametr určuje kolik «nejvýše» bajtů má být
     * přečteno. Tomuto parametru musíme věnovat zvláštní pozornost:
     *
     *  1. Bajtů může být přečteno méně, než jsme žádali – kolik
     *     jich bylo skutečně přečteno zjistíme až z návratové
     *     hodnoty.
     *  2. Vedlejším efektem volání ‹read› je, že do paměti určené
     *     adresou ‹buffer› bude zapsáno až ‹nbytes› bajtů (volání
     *     tedy přepíše hodnoty na adresách ‹buffer + 0›, ‹buffer +
     *     1›, …, ‹buffer + nbytes - 1›). Abychom si omylem
     *     nezničili nějaká nesouvisející data, musíme systémovému
     *     volání ‹read› předat adresu, od které máme vyhrazeno
     *     alespoň ‹nbytes› bajtů.
     * 
     * Jednoduchý způsob, jak vyhradit pevný počet bajtů v paměti,
     * je deklarací lokálního pole. Počet bajtů, které hodláme
     * načíst, si uložíme do pojmenované konstanty ‹nbytes›. */

    const int nbytes = 16;
    const int expect = 4;

    char buffer[ nbytes ];
    ssize_t bytes_read = read( fd, buffer, nbytes );

    /* Jako každé systémové volání může ‹read› selhat. Podobně jako
     * u volání ‹open› tuto skutečnost indikuje návratová hodnota
     * -1.¹
     *
     * Pozor, návratová hodnota ‹0› «není chybou» (říká nám pouze,
     * že žádné další bajty přečíst nelze, protože jsme narazili na
     * konec souboru). */

    if ( bytes_read == -1 )
        err( 1, "error reading from %s", filename );

    /* Dále ověříme, že jsme načetli data, která jsme očekávali.
     * Protože se v těchto případech nejedná o systémové chyby,
     * použijeme místo procedury ‹err› proceduru ‹errx›, která
     * nevypisuje chybu uloženou v proměnné ‹errno›. */

    if ( bytes_read < expect )
        errx( 1, "file %s was shorter than expected, only %zd bytes",
              filename, bytes_read );

    if ( memcmp( buffer, "foo\n", expect ) )
        errx( 1, "file %s has unexpected content", filename );

    /* Po dokončení práce se souborem tento uzavřeme. Všimněte si,
     * že v případě chyby čtení jsme popisovač neuzavřeli – to si
     * můžeme dovolit pouze v situaci, kdy zároveň ukončujeme celý
     * program (tím jsou veškeré zdroje automaticky uvolněny).
     *
     * Systémové volání ‹close› může opět selhat, nicméně situace,
     * kdy se můžeme s takovou chybou smysluplně vypořádat jsou
     * relativně vzácné. Měli bychom ale v každém případě o takovém
     * selhání uživatele informovat, protože tento typ chyby může
     * znamenat ztrátu dat, která program do souboru zapisoval. To,
     * co naopak udělat «nesmíme», je pokusit se soubor zavřít
     * podruhé – v závislosti na systému a okolnostech mohl být
     * popisovač uzavřen i přesto, že volání ‹close› selhalo (a ani
     * nemáme jak zjistit, jestli k tomu došlo nebo nikoliv). */

    if ( close( fd ) != 0 )
        warn( "error closing %s", filename );

    /* Návratová hodnota 0 značí, že program bez chyby doběhl. */

    return 0;
}

/* ¹ Všimněte si, že proměnnou ‹bytes› jsme deklarovali s typem
 *   ‹ssize_t› – jedná se o «znaménkový» typ, na rozdíl od podobně
 *   pojmenovaného typu ‹size_t›. */
