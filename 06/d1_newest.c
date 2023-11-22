#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>      /* open */
#include <sys/stat.h>   /* fstatat, struct stat */
#include <dirent.h>     /* fdopendir, rewinddir, readdir, … */
#include <unistd.h>     /* dup */
#include <string.h>     /* strdup */
#include <stdlib.h>     /* free, NULL */
#include <stdio.h>      /* dprintf */
#include <stdbool.h>    /* bool */
#include <assert.h>
#include <err.h>

/* V této ukázce budeme pracovat se složkou novým způsobem – naučíme
 * se získat výčet zde přítomných odkazů a s těmito dále pracovat,
 * zejména o nich zjistit další informace pomocí systémového volání
 * ‹fstatat›. Podprogram, na kterém si to předvedeme, se bude
 * jmenovat ‹find_newest› a vrátí jméno odkazu, který patří
 * nejnovějšímu obyčejnému souboru (takovému, který byl naposled
 * modifikován nejblíže současnosti). */

/* Nejprve si nachystáme pomocnou čistou funkci ‹is_newer›, která
 * pro dvojici struktur ‹timespec› rozhodne, zda první z nich
 * reprezentuje pozdější časový okamžik. Ve struktuře ‹stat› máme
 * zaručeno, že ‹tv_nsec› je v rozsahu 0 až 10⁶, tzn. struktura je
 * vždy normalizovaná a hodnoty můžeme bez problémů srovnat po
 * složkách. */

bool is_newer( struct timespec a, struct timespec b )
{
    return a.tv_sec >  b.tv_sec ||
         ( a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec );
}

/* Podprogramu ‹find_newest› předáme složku, se kterou bude
 * pracovat, prostřednictvím popisovače. Výsledkem bude ukazatel na
 * dynamicky alokované jméno odkazu, nebo nulový ukazatel v případě
 * chyby. */

char *find_newest( int dir_fd )
{
    /* Pro práci se seznamem odkazů ve složce slouží především
     * knihovní podprogram ‹readdir›. Tento však nepracuje přímo
     * s popisovačem, ale se strukturou ‹DIR›, kterou musíme nejprve
     * získat – k tomu nám poslouží knihovní funkce ‹fdopendir›,
     * která ale převezme vlastnictví popisovače a tím ho efektivně
     * zničí.
     *
     * Popisovač ‹dir_fd› ale nepatří podprogramu ‹find_newest› (je
     * mu pouze propůjčen volajícím) a tedy musí být zachován. Proto
     * si nejprve vytvoříme kopii voláním ‹dup›. */

    DIR *dir = NULL;

    /* Nachystáme si také několik proměnných, ve kterých budeme
     * uchovávat průběžné výsledky. */

    char *rv = NULL, *newest_name = NULL;
    struct timespec newest_time = { 0 };

    int dup_fd = dup( dir_fd );

    if ( dup_fd == -1 )
        goto out;

    /* Nyní máme k dispozici popisovač, který můžeme předat
     * knihovnímu podprogramu ‹fdopendir›, aniž bychom byli nuceni
     * uvolnit zdroj, který nám nepatří. Popisovač ‹dup_fd› od této
     * chvíle nesmíme přímo používat – stal se de-facto součástí
     * vzniklé struktury ‹DIR›. */

    dir = fdopendir( dup_fd );

    /* Volání ‹fdopendir› může selhat, např. v situaci, kdy nám byl
     * předán neplatný nebo jinak nepoužitelný popisovač (např.
     * neodkazuje na složku). */

    if ( !dir )
        goto out;

    /* Pozice čtení je podobně jako u běžného souboru vlastností
     * samotného objektu „otevřeného adresáře“ který je popisovačem
     * odkazován (není tedy ani vlastností popisovače, ani struktury
     * ‹DIR›). Protože nemáme jistotu, že pozice čtení je při volání
     * ‹find_newest› na začátku složky, použijeme knihovní
     * podprogram ‹rewinddir›, který pozici čtení přesune na začátek
     * složky a zároveň synchronizuje strukturu ‹DIR› s touto novou
     * pozicí. Podprogram ‹rewinddir› trochu neobvykle selhat
     * nemůže. */

    rewinddir( dir );

    /* Nyní jsme připraveni na čtení odkazů. Použijeme k tomu
     * podprogram ‹readdir›, který bude postupně vracet jednotlivé
     * položky adresáře, na který odkazuje předaná struktura ‹DIR› –
     * každé volání přečte jeden odkaz. Výsledek obdržíme jako
     * ukazatel na strukturu typu ‹dirent›. */

    struct dirent *ptr;
    struct stat st;

    while ( ( ptr = readdir( dir ) ) )
    {
        /* Obdrželi jsme nový odkaz, který nyní zpracujeme. Má pouze
         * dvě položky, na které se můžeme spolehnout – ‹d_ino› –
         * číslo odkazovaného i-uzlu a ‹d_name› – jméno odkazu.
         *
         * Další informace získáme voláním ‹fstatat› na toto jméno.
         * V této posloupnosti operací je zabudovaný hazard souběhu
         * – v době mezi čtením seznamu odkazů a voláním ‹fstatat›
         * mohl odkaz přestat existovat. S tím nemůžeme dělat nic a
         * musíme s takovou možností počítat. Pro účely tohoto
         * předmětu můžeme takové zmizení odkazu považovat za
         * fatální chybu a trochu si tak zjednodušit život.
         *
         * Abychom dostali skutečně informace o souboru, který je
         * odkazován adresářem, jako čtvrtý parametr předáme příznak
         * ‹AT_SYMLINK_NOFOLLOW›, který zabezpečí, že volání
         * ‹fstatat› nebude následovat měkké odkazy. */

        if ( fstatat( dir_fd, ptr->d_name, &st,
                      AT_SYMLINK_NOFOLLOW ) == -1 )
            goto out;

        /* Ve struktuře ‹st› jsou nyní vyplněny další informace
         * o odkazovaném souboru (pozor, nikoliv o samotném
         * odkazu!). Mimo jiné se zde nachází položka ‹st_mode›,
         * která kóduje přístupová práva a typ souboru – pro jeho
         * dekódování můžeme použít makra ‹S_IS*›.¹ Obyčejný soubor
         * například poznáme za pomoci makra-predikátu ‹S_ISREG›.
         *
         * Dále struktura ‹stat› obsahuje položku ‹st_mtim›, podle
         * které určíme stáří souboru. Samotné srovnání časových
         * razítek delegujeme na výše uvedenou pomocnou funkci. */

        if ( S_ISREG( st.st_mode ) &&
             is_newer( st.st_mtim, newest_time ) )
        {
            free( newest_name );
            newest_time = st.st_mtim;
            newest_name = strdup( ptr->d_name );

            if ( !newest_name )
                goto out;
        }
    }

    /* Tím je procházení adresáře úspěšně ukončeno a ukazatel
     * ‹newest_name› obsahuje požadovaný výsledek. Uvolníme lokální
     * zdroje a výsledek vrátíme volajícímu. */

    rv = newest_name;
    newest_name = NULL;
out:
    free( newest_name );

    if ( dir )
        closedir( dir );
    else if ( dup_fd != -1 )
        close( dup_fd );

    return rv;
}

int main( void ) /* demo */
{
    int cwd_fd = open( ".", O_DIRECTORY | O_RDONLY );

    if ( cwd_fd == -1 )
        err( 2, "opening working directory" );

    char *name = find_newest( cwd_fd );
    dprintf( STDOUT_FILENO, "the newest file is %s\n", name );
    free( name );
    close( cwd_fd );

    return 0;
}

/* ¹ Kde naleznete dokumentaci těchto maker závisí od systému, na
 *   systémech Linux to je manuálová stránka ‹inode›. Máte-li
 *   k dispozici normu POSIX ve formě manuálových stránek (dostupné
 *   např. na stroji ‹aisa›), naleznete je také pod klíčem
 *   ‹sys_stat.h›. */
