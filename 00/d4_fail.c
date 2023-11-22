/* Zde si ukážeme několik technik, které nám pomohou vypořádat se
 * s opravitelnými chybami. Připomínáme, že za opravitelné
 * považujeme chyby, po kterých může program jako celek smysluplně
 * pokračovat ve své činnosti. V tomto předmětu to bude zejména
 * v případech, kdy budeme psát znovupoužitelné podprogramy.
 *
 * Nastane-li opravitelná chyba, snažíme se dotčený podprogram
 * ukončit tak, aby:
 *
 *  1. nedošlo k úniku zdrojů, tzn. každý zdroj, který podprogram
 *     vyžádal, musí být vrácen, nebo se musí stát součástí stavu
 *     programu (a tedy bude vrácen později, v rámci standardního –
 *     nechybového – úklidu),
 *  2. byly zachovány veškeré požadované invarianty, na které mohl
 *     mít podprogram vliv – v ideálním případě je chování
 *     podprogramu „všechno anebo nic“, tzn. pokud podprogram
 *     neuspěl, nebude mít žádný pozorovatelný efekt,¹
 *  3. volající byl o tomto selhání co možná nejpodrobněji
 *     informován. */

/* ¹ Toto je samozřejmě v praxi těžké stoprocentně zajistit, nicméně
 *   čím blíže se tomuto ideálu dokážeme přiblížit, tím snazší bude
 *   daný podprogram použít v nějakém větším celku. */

#define _POSIX_C_SOURCE 200809L
#include <unistd.h>     /* read, write */
#include <fcntl.h>      /* openat */
#include <err.h>        /* warn */
#include <errno.h>      /* errno */

/* V této ukázce trochu předběhneme učivo a naprogramujeme si
 * jednoduchou proceduru, která vytvoří kopii souboru v souborovém
 * systému. Podrobněji se budeme systémovými voláními ‹openat›,
 * ‹read› a ‹write› zabývat v první kapitole. */

int copy_1( int from_dir, const char *from_name,
            int to_dir, const char *to_name )
{
    int fd_in, fd_out;

    /* Voláním ‹openat› otevřeme soubor s názvem ‹from_name› ve
     * složce ‹from_dir›. Nyní jsou důležité dvě věci:
     *
     *  1. systémové volání ‹openat› může selhat, například proto,
     *     že požadovaný soubor neexistuje, a s touto situací se
     *     budeme muset vypořádat,
     *  2. je-li soubor úspěšně otevřen, popisovač souboru, který
     *     získáme jako návratovou hodnotu, reprezentuje «zdroj»
     *     který je nutné později opět uvolnit (systémovým voláním
     *     ‹close›). */

    fd_in = openat( from_dir, from_name, O_RDONLY );

    /* Neúspěch volání ‹openat› poznáme tak, že ‹fd_in› obsahuje
     * hodnotu -1. Protože v tomto kontextu se jedná o opravitelnou
     * chybu, podprogram ukončíme. Navíc jsme dosud neprovedli žádné
     * „viditelné“ akce, tak nemusíme řešit jejich vrácení. */

    if ( fd_in == -1 )
        return 1;

    /* Abychom mohli kopírovat data, musíme otevřít (a případně
     * vytvořit) i cílový soubor. Opět k tomu použijeme volání
     * ‹openat›. */

    fd_out = openat( to_dir, to_name, O_WRONLY | O_CREAT, 0666 );

    /* Opakuje se zde situace, kdy volání ‹openat› může selhat, ale
     * nyní máme nový problém – první volání ‹openat› uspělo a tedy
     * ‹fd_in› odkazuje na «alokovaný zdroj», totiž popisovač
     * souboru. Abychom dodrželi zásadu, že podprogram, který
     * selhal, by neměl mít žádný efekt, musíme tento popisovač před
     * návratem uzavřít. */

    if ( fd_out == -1 )
    {
        /* Dostáváme se nyní do trochu nešťastné situace, kdy
         * vrácení zdroje může také selhat. V takové situaci nám
         * nezbývá, než uživatele varovat a pokračovat ve výpočtu
         * (alternativně bychom mohli tuto chybu považovat za
         * fatální a program ukončit).
         *
         * Volání ‹close› na tomto místě přináší ještě jeden problém
         * – hodnota ‹errno›, která popisuje z jakého důvodu selhalo
         * volání ‹openat›, bude při volání ‹close› přepsána, a
         * volající tedy nebude moct hodnotu ‹errno› použít. Měli
         * bychom tedy tuto hodnotu uložit a před návratem
         * z podprogramu ‹copy_1› obsah ‹errno› obnovit. Toto platí
         * o jakémkoliv systémovém volání, nebo podprogramu, který
         * systémová volání vnitřně používá. Použijeme-li např.
         * proceduru ‹warn›, tato může ‹errno› také přepsat. */

        int saved_errno = errno;

        if ( close( fd_in ) == -1 )
            warn( "failed to close file %s", from_name );

        errno = saved_errno;
        return 1;
    }

    /* Máme nyní otevřeny oba dotčené soubory. Nyní budeme
     * potřebovat místo v paměti, do kterého nejprve načteme blok
     * dat ze vstupního souboru a tento pak obratem zapíšeme do
     * souboru výstupního. Opět platí, že každá z těchto operací
     * může selhat a tuto situaci musíme řešit. */

    const int nbytes = 1024;
    char buffer[ nbytes ];
    int bytes_read, bytes_written;

    do {
        bytes_read = read( fd_in, buffer, nbytes );

        if ( bytes_read == -1 )
        {
            /* Výsledek -1 znamená, že čtení selhalo. Operaci musíme
             * ukončit a opět musíme zároveň vrátit vše, co můžeme,
             * do původního stavu. To už v tuto chvíli nebude možné
             * zcela splnit, protože volání ‹openat› pro výstupní
             * soubor mohlo tento soubor vytvořit, ale nemůžeme zde
             * tento soubor odstranit, protože není zaručeno, že pod
             * jménem ‹to_name› ve složce ‹from_dir› se stále
             * nachází soubor, který jsme o několik řádků dříve
             * vytvořili.  Omezíme se tedy na uvolnění zdrojů. */

            int saved_errno = errno;

            if ( close( fd_in ) == -1 )
                warn( "failed to close file %s", from_name );
            if ( close( fd_out ) == -1 )
                warn( "failed to close file %s", to_name );

            errno = saved_errno;
            return 2;
        }

        bytes_written = write( fd_out, buffer, bytes_read );

        if ( bytes_written == -1 )
        {
            /* A ještě jednou totéž. */

            int saved_errno = errno;

            if ( close( fd_in ) == -1 )
                warn( "failed to close file %s", from_name );
            if ( close( fd_out ) == -1 )
                warn( "failed to close file %s", to_name );

            errno = saved_errno;
            return 2;
        }
    } while ( bytes_read != 0 );

    /* Data jsou úspěšně zkopírována, můžeme tedy oba soubory zavřít
     * a oznámit volajícímu úspěch. */

    if ( close( fd_in ) == -1 )
        warn( "failed to close file %s", from_name );
    if ( close( fd_out ) == -1 )
        warn( "failed to close file %s", to_name );

    return 0;
}

/* Jistě jste si všimli, že procedura ‹copy_1› obsahuje velké
 * množství redundantního kódu (a jistě také víte, že to není
 * dobře). Máme dva základní prostředky, které nám pomohou se
 * s tímto vypořádat. Jedním jsou pomocné podprogramy. Jako „nízko
 * visící ovoce“ se nabízí procedura ‹close_or_warn›, která nám
 * zjednoduší zavírání souborů, aniž bychom museli přijmout tzv.
 * tichá selhání.
 *
 * Jako dodatečné vylepšení navíc budeme vstupní popisovač
 * s hodnotou -1 ignorovat – žádný platný popisovač nemůže nikdy mít
 * tuto hodnotu. Jedná se o podobný „trik“ jako používá knihovní
 * procedura ‹free›, kterou lze bez rizika zavolat na nulový
 * ukazatel. */

void close_or_warn( int fd, const char *name )
{
    int saved_errno = errno;

    if ( fd != -1 && close( fd ) == -1 )
        warn( "failed to close file %s", name );

    errno = saved_errno;
}

/* Druhým prostředkem je poněkud kontroverzní příkaz ‹goto›. To, že
 * budeme někdy ‹goto› používat pro ošetření chyb neznamená, že
 * můžete ‹goto› „beztrestně“ použít na cokoliv. Problém, který zde
 * ‹goto› řeší je, že odkazy na zdroje alokované podprogramem
 * obvykle ukládáme do lokálních proměnných, které bychom případnému
 * pomocnému „úklidovému“ podprogramu museli vždy všechny předat –
 * to je zápisově značně nepraktické.² */

int copy_2( int from_dir, const char *from_name,
            int to_dir, const char *to_name )
{
    int fd_in = -1, fd_out = -1;
    int rv = 1;

    const int nbytes = 1024;
    char buffer[ nbytes ];
    int bytes_read, bytes_written;

    if ( ( fd_in = openat( from_dir, from_name, O_RDONLY ) ) == -1 )
        goto error;

    if ( ( fd_out = openat( to_dir, to_name, O_WRONLY | O_CREAT,
                            0666 ) ) == -1 )
        goto error;

    rv = 2;

    do {

        if ( ( bytes_read = read( fd_in, buffer, nbytes ) ) == -1 )
            goto error;

        if ( ( bytes_written = write( fd_out, buffer,
                                      bytes_read ) ) == -1 )
            goto error;

        /* Za povšimnutí stojí, že neřešíme situaci, kdy je
         * ‹bytes_written› nezáporné, ale zároveň menší než
         * ‹bytes_read›. Rozpravu na toto téma si necháme na
         * později. */

    } while ( bytes_read != 0 );

    rv = 0;

error:
    close_or_warn( fd_in, from_name );
    close_or_warn( fd_out, to_name );
    return rv;
}

int main( void ) /* demo */
{
    int wd = openat( AT_FDCWD, ".", O_DIRECTORY );

    if ( wd == -1 )
        err( 1, "could not open working directory" );

    const char *src = "a0_intro.txt";
    const char *dest = "zt.intro.txt";

    if ( copy_1( wd, src, wd, dest ) != 0 )
        warn( "copying %s to %s failed", src, dest );

    if ( copy_2( wd, src, wd, dest ) != 0 )
        warn( "copying %s to %s failed", src, dest );

    if ( unlinkat( wd, dest, 0 ) != 0 )
        warn( "removing %s failed", dest );

    return 0;
}

/* ² Oddělení úklidu do pomocného podprogramu má i jiné problémy.
 *   Můžete si zkusit takový úklidový podprogram napsat, abyste
 *   zjistili, jaké kompromisy to obnáší. */
