#define _POSIX_C_SOURCE 200809L

/* V této ukázce si trochu více přiblížíme systémové volání ‹openat›
 * a podíváme se na základy práce se složkami (adresáři). Jak víte
 * z třetí přednášky PB152, složka asociuje jména¹ se soubory
 * (v obecném smyslu, tzn. nejen obyčejnými).
 *
 * Složku lze otevřít stejně jako jiné typy souborů – takto získaný
 * popisovač tuto složku jednoznačně identifikuje, a to i v situaci,
 * kdy se změní odkazy na tuto složku, a tedy její jméno v nadřazené
 * složce nemusí platit, nebo se může přesunout v adresářové
 * struktuře na úplně jiné místo.² */

#include <stdio.h>      /* dprintf */
#include <unistd.h>     /* read, write */
#include <fcntl.h>
#include <err.h>

/* Nejprve si nachystáme několik jednoduchých pomocných podprogramů,
 * které nám zjednoduší zápis zbytku programu. Protože se jedná
 * o malý uzavřený program, můžeme si dovolit považovat chyby při
 * otevírání souboru za fatální. */

int open_or_die( int dir_fd, const char *path, int flags )
{
    int fd = openat( dir_fd, path, flags );

    if ( fd == -1 )
        err( 1, "error opening %s", path );

    return fd;
}

void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "error closing descriptor %d for file %s", fd, name );
}

int main( int argc, const char **argv ) /* demo */
{
    /* Tento program bude postupně otevírat soubory, kterých jména
     * jsou uložené v následujícím poli ukazatelů, a z každého
     * vypíše prvních pár bajtů na standardní výstup. Nulový
     * ukazatel označuje konec pole. */

    const char * const filenames[] =
    {
        "a0_intro.txt",
        "a1_overview.txt",
        "a2_grading.txt",
        NULL
    };

    /* Aby se nám s programem lépe experimentovalo, spustíme-li jej
     * s parametrem, tento parametr se použije jako cesta ke složce,
     * ve které budeme hledat složku ‹00› (a v ní pak výše uvedené
     * soubory). Doporučujeme zkusit si tento program spustit
     * s různě přichystanými složkami, např. takovou, která
     * podsložku ‹00› vůbec neobsahuje, s takovou, která neobsahuje
     * všechny očekávané soubory, atp. */

    const char *top_path = argc > 1 ? argv[ 1 ] : "..";
    int top_fd = open_or_die( AT_FDCWD, top_path, O_DIRECTORY );
    int dir_fd = open_or_die( top_fd, "00", O_DIRECTORY );

    /* Následující cyklus každý soubor otevře – všimněte si, že
     * používáme pouze «jména» souborů, protože rodičovská složka je
     * do podprogramu ‹open_or_die› předána pomocí popisovače. Tento
     * přístup má dvě zásadní výhody:
     *
     *  1. nemusíme složitě konstruovat «cesty», které by k souborům
     *     vedly – něco jako ‹top_path + "/00/" + name› – zápis,
     *     který v jazyce C samozřejmě nemůžeme použít; navíc
     *  2. takto sestavené cesty mohou v různých iteracích ukazovat
     *     na soubory v různých složkách – souborový systém je
     *     «sdílený» a každá operace je potenciálním «hazardem
     *     souběhu».³ */

    for ( int i = 0; filenames[ i ]; ++i )
    {
        const char *name = filenames[ i ];
        int file_fd = open_or_die( dir_fd, name, O_RDONLY );

        const int nbytes = 10;
        char buffer[ nbytes ];

        int bytes_read = read( file_fd, buffer, nbytes );

        if ( bytes_read == -1 )
            err( 1, "error reading %d bytes from %s",
                 nbytes, name );

        /* Přečtené bajty přepíšeme na standardní výstup a posuneme
         * se na nový řádek. */

        if ( write( STDOUT_FILENO, buffer, bytes_read ) == -1 ||
             dprintf( STDOUT_FILENO, "\n" ) == -1 )
            err( 1, "error writing to stdout" );

        close_or_warn( file_fd, name );
    }

    /* Nezapomeneme uzavřít popisovače složek, které jsme otevřeli
     * na začátku podprogramu. */

    close_or_warn( dir_fd, "00" );
    close_or_warn( top_fd, top_path );
    return 0;
}

/* ¹ Jméno souboru je libovolný řetězec, který ovšem nesmí obsahovat
 *   znak ‹/› ani nulový znak. Délka jména může být operačním
 *   systémem nebo souborovým systémem omezená.
 * ² Protože na složky není dovoleno vytvořit víc než jeden
 *   standardní odkaz, a zároveň není možné neprázdnou složku
 *   odstranit, mohlo by se zdát, že tato situace nemá jak nastat.
 *   Odkazy na složky lze ale atomicky «přesouvat» použitím
 *   systémového volání ‹renameat›. Více si o něm povíme v další
 *   kapitole.
 * ³ Uvažme situaci, kdy všechny vstupní soubory v zadané složce
 *   existují. Sestavujeme-li cesty jak bylo naznačeno, může se
 *   stát, že jiný program složku přejmenuje. Náš program některé
 *   soubory úspěšně vypíše a u jiných ohlásí chybu, a to přesto, že
 *   se soubory ani s odkazy na ně v rodičovské složce se vůbec nic
 *   nestalo. Považujeme-li takovéto chování za chybné (a bylo by to
 *   naprosto logické), prakticky celý náš program tvoří kritickou
 *   sekci vůči přesunu (přejmenování) rodičovské složky, kterou ale
 *   nemáme jak ochránit. Řešení s předáváním složky pomocí
 *   popisovače tuto kritickou sekci (a tedy ani popsaný hazard
 *   souběhu) neobsahuje. */

