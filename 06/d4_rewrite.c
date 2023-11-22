#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <assert.h>

/* V této ukázce se podíváme na to, jak upravit obsah souboru, aniž
 * by nám hrozilo, že uprostřed práce nám začnou selhávat operace a
 * soubor budeme nuceni ponechat v nekonzistentním stavu. Klíčem
 * k tomu je systémové volání ‹renameat›, které zaručuje, že odkaz
 * v adresáři bude «atomicky» přesměrován na nový i-uzel.
 *
 * Můžeme tak nový obsah bez rizika vybudovat ve zcela novém souboru
 * (i-uzlu), a až když s jistotou víme, že celá operace uspěla,
 * původní soubor v jednom atomickém kroku nahradíme novým. Tento
 * krok samozřejmě může selhat, ale selže jako nedělitelný celek.
 *
 * Je tak zejména zaručeno, že odkaz v každou chvíli existuje –
 * vyhneme se tedy i hazardu souběhu s nějakým jiným programem,
 * který by se například pokusil soubor skrze odkaz souběžně
 * otevřít. V různých časových sledech se mu může podařit otevřít
 * původní soubor, nebo nový soubor, ale nemůže se stát, že by
 * pozoroval chybu ‹ENOENT› – neexistenci odkazu. */

/* Tento typ použití systémové služby ‹renameat› si předvedeme na
 * jednoduchém podprogramu, který nahradí všechny výskyty nějakého
 * řetězce ‹str› v zadaném souboru jiným řetězcem, ‹repl›. Samotnou
 * logiku náhrady oddělíme do pomocné procedury ‹rewrite_content›,
 * která bude pracovat s dvojicí popisovačů – vstupním a výstupním.
 * Bude také pracovat s pohyblivým „oknem“, u kterého bude zaručeno,
 * že každá instance ‹str›, která se na vstupu objeví, bude v nějaké
 * chvíli uvnitř okna celá najednou. Posuv tohoto okna delegujeme na
 * další pomocnou proceduru, kterou ale definujeme až níže. */

int slide_window( int out_fd, const char *str, int str_len,
                  const char *repl, int repl_len,
                  char *buffer, int *window );

int rewrite_content( int in_fd, int out_fd, const char *str,
                     const char *repl )
{
    /* Pro jednoduchost budeme požadovat, aby měl vstupní řetězec
     * ‹str› omezenou délku – vyhneme se tak nutnosti použít
     * dynamickou alokaci paměti. Budeme pracovat se segmenty, které
     * mají alespoň dvojnásobnou velikost než ‹str›, abychom
     * zaručili výše uvedenou vlastnost. Protože víme, že ‹in_fd› je
     * popisovačem obyčejného souboru, nemusíme navíc řešit ani
     * krátké čtení (jinde než na konci souboru). */

    int str_len = strlen( str ),
        repl_len = strlen( repl );

    char buffer[ ( str_len > 64 ? 2 * str_len : 128 ) + 1 ];
    int nread;

    /* V proměnné ‹window› budeme uchovávat aktuální velikost okna –
     * počet platných bajtů uložených v poli ‹buffer›. */

    int window = 0;

    /* Dokud to je možné, ‹buffer› doplňujeme tak, aby byl před
     * každým voláním ‹slide_window› plný, resp. tak plný, jak to
     * obsah souboru umožňuje. To je důležité, aby nám neutekl žádný
     * výskyt řetězce ‹str›. */

    while ( ( nread = read( in_fd, buffer + window,
                            sizeof buffer - window - 1 ) ) > 0 )
    {
        /* Načtená data přidáme do pomyslného okna a okno posuneme –
         * zapíšeme do výstupního souboru tolik dat, kolik můžeme,
         * aniž bychom riskovali ztrátu nějakého dosud nedočteného
         * prefixu řetězce ‹str›. Posuv okna může skončit s chybou,
         * protože v něm provádíme zápis do souboru. V takovém
         * případě celou operaci vzdáme a se situací se vypořádáme
         * v podprogramu ‹rewrite› níže. */

        window += nread;

        if ( slide_window( out_fd, str, str_len,
                           repl, repl_len, buffer, &window ) )
            return -1;
    }

    if ( nread == -1 )
        return -1;

    /* Po dočtení souboru do konce můžou v okně zůstat nějaká data.
     * Dokud tomu tak je, budeme okno posouvat. Počet posuvů je
     * omezen podílem ‹sizeof buffer / str_len›. */

    while ( window )
        if ( slide_window( out_fd, str, str_len,
                           repl, repl_len, buffer, &window ) )
            return -1;

    return 0;
}

/* Nyní naprogramujeme pomocnou proceduru ‹slide_window›, která
 * provede jeden posuv okna. */

int slide_window( int out_fd, const char *str, int str_len,
                  const char *repl, int repl_len,
                  char *buffer, int *window )
{
    /* Mohou nastat dvě možnosti: ‹str› se v okně objevuje, nebo
     * nikoliv. Který případ nastal zjistíme za pomoci knihovního
     * podprogramu ‹strstr› – tento nalezne «první» výskyt ‹str›
     * v okně, nebo vrátí nulový ukazatel, když se ‹str› v okně
     * neobjevuje.¹ */

    buffer[ *window ] = 0;
    char *found = strstr( buffer, str );

    if ( found )
    {
        /* Objevíme-li v okně výskyt ‹str›, můžeme okno bezpečně
         * posunout těsně za jeho konec. Data, která ‹str›
         * předchází, nejprve zapíšeme do výstupního souboru, místo
         * kopie ‹str› pak zapíšeme řetězec ‹repl› a okno posuneme. */

        int offset = found - buffer;

        if ( write( out_fd, buffer, offset ) != offset )
            return -1;

        if ( write( out_fd, repl, repl_len ) != repl_len )
            return -1;

        int remaining = *window - offset - str_len;
        memmove( buffer, found + str_len, remaining );
        *window = remaining;
    }
    else
    {
        /* Neobjevuje-li se v okně ‹str›, můžeme část okna zapsat –
         * musíme pouze zaručit, že z okna neodstraníme posledních
         * ‹str_len - 1› bajtů, protože tyto mohou představovat
         * zatím nedočtený výskyt řetězce ‹str› (a není efektivní
         * tuto možnost zkoumat, než bude výskyt dočten do konce). */

        int shift = *window <= str_len ? *window
                                       : *window - str_len;

        if ( write( out_fd, buffer, shift ) != shift )
            return -1;

        memmove( buffer, buffer + shift, *window - shift );
        *window -= shift;
    }

    return 0;
}

/* ¹ Podprogram ‹strstr› funguje žel pouze pro nulou ukončené
 *   řetězce a obecnější (a užitečnější) funkce ‹memmem› není
 *   standardizovaná. S pomocí ‹memmem› by nebylo těžké celý program
 *   upravit tak, aby fungoval i pro binární soubory. */

/* Vyzbrojeni procedurou ‹rewrite_content› můžeme přistoupit k práci
 * s odkazy a voláním ‹renameat›: */

int rewrite( int dir_fd, const char *name,
             const char *str, const char *replace )
{
    int in_fd = -1, out_fd = -1, orig_cwd = -1;
    int rv = -1;
    char tmpname[] = ".rewrite.XXXXXX";

    /* Pro zápis upraveného obsahu budeme potřebovat vytvořit nový
     * i-uzel – to žel není možné, aniž bychom na tento i-uzel
     * vytvořili odkaz v nějakém adresáři. Zároveň ale musíme
     * zabezpečit, že jméno tohoto odkazu nebude kolidovat s žádným
     * již existujícím. K tomu můžeme použít knihovní podprogram
     * ‹mkstemp›, který vytvoří nový obyčejný soubor s unikátním
     * názvem. Zároveň ale potřebujeme zaručit, že i-uzel vytvoříme
     * ve stejném souborovém systému, do kterého náleží zdrojový
     * soubor (který hodláme nahradit). Proto tento dočasný odkaz
     * vytvoříme v téže složce. */

    /* Protože (zatím) ve většině knihoven neexistuje podprogram
     * ‹mkstempat›, pomůžeme si systémovou službou ‹fchdir›, která
     * nastaví pracovní složku podle zadaného popisovače. To s sebou
     * ale nese další komplikaci: nastavení pracovní složky musíme
     * před návratem z podprogramu ‹rewrite› vrátit. Proto si
     * aktuální pracovní adresář uschováme formou popisovače. */

    if ( ( orig_cwd = open( ".", O_DIRECTORY ) ) == -1 ||
         fchdir( dir_fd ) == -1 )
        goto out;

    /* Nyní již můžeme použít ‹mkstemp› k vytvoření dočasného
     * odkazu. Předáme mu ukazatel na pole bajtů, ve kterém je
     * uložena šablona – tato musí končit alespoň šesti znaky ‹X›,
     * které podprogram ‹mkstemp› přepíše tak, aby bylo jméno
     * unikátní. Zároveň vrátí popisovač pro nově vytvořený i-uzel.
     * Při této příležitosti otevřeme i vstupní soubor. */

    if ( ( in_fd = openat( dir_fd, name, O_RDONLY ) ) == -1 ||
         ( out_fd = mkstemp( tmpname ) ) == -1 )
        goto out;

    /* Konečně je vše připraveno k samotnému přepsání obsahu. Na to
     * již máme připravenu proceduru ‹rewrite_content› kterou pouze
     * použijeme. */

    if ( rewrite_content( in_fd, out_fd, str, replace ) == -1 )
        goto out;

    /* Nyní je v nově vytvořeném i-uzlu uložen nový obsah souboru.
     * Pomocí ‹renameat› (atomicky) přesměrujeme původní odkaz na
     * tento nový i-uzel. Rozmyslete si ovšem, co to znamená pro
     * jiné odkazy na původní soubor. */

    if ( renameat( dir_fd, tmpname, dir_fd, name ) == -1 )
        goto out;

    /* Vše je úspěšně dokončeno, nastavíme návratovou hodnotu na 0 a
     * uvolníme lokální zdroje. */

    rv = 0;
out:
    if ( orig_cwd != -1 )
    {
        fchdir( orig_cwd );
        close( orig_cwd );
    }

    if ( in_fd != -1 )
        close( in_fd );

    if ( out_fd != -1 )
        close( out_fd );

    if ( rv != 0 )
        unlinkat( dir_fd, tmpname, 0 );

    return rv;
}

static void create_file( int dir_fd, const char *name,
                         const char *data )
{
    if ( unlinkat( dir_fd, name, 0 ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", name );

    int fd = openat( AT_FDCWD, name, O_CREAT | O_WRONLY, 0755 );

    if ( fd == -1 )
        err( 2, "creating %s", name );
    if ( write( fd, data, strlen( data ) ) == -1 )
        err( 2, "writing into %s", name );

    close( fd );
}

static int check_file( int dir, const char *name,
                       const char *expected )
{
    int nbytes = strlen( expected );
    int rbytes;
    char buffer[ nbytes + 1 ];

    int read_fd = openat( dir, name, O_RDONLY );

    if ( read_fd == -1 )
        err( 2, "opening %s", name );
    if ( ( rbytes = read( read_fd, buffer, nbytes + 1 ) ) == -1 )
        err( 2, "reading %s", name );

    close( read_fd );

    return rbytes == nbytes &&
           memcmp( expected, buffer, nbytes ) == 0;
}

int main( void ) /* demo */
{
    int dir_fd = open( ".", O_DIRECTORY );
    const char *name = "zt.d4_in.txt";

    if ( !dir_fd )
        err( 1, "opening working directory" );

    create_file( dir_fd, name, "foo bar oops" );
    assert( rewrite( dir_fd, name, "oo", "xx" ) == 0 );
    assert( check_file( dir_fd, name, "fxx bar xxps" ) );

    assert( rewrite( dir_fd, name, "xps", "yxx" ) == 0 );
    assert( check_file( dir_fd, name, "fxx bar xyxx" ) );

    assert( rewrite( dir_fd, name, "xx", "wibble" ) == 0 );
    assert( check_file( dir_fd, name, "fwibble bar xywibble" ) );

    assert( rewrite( dir_fd, name, "wibble", "oo" ) == 0 );
    assert( check_file( dir_fd, name, "foo bar xyoo" ) );

    assert( rewrite( dir_fd, name, "xyoo", "oopsie" ) == 0 );
    assert( check_file( dir_fd, name, "foo bar oopsie" ) );

    close( dir_fd );
    return 0;
}
