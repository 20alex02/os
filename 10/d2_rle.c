#define _POSIX_C_SOURCE 200809L

#include <unistd.h>   /* fork */
#include <sys/wait.h> /* waitpid */
#include <stdlib.h>   /* exit */
#include <string.h>   /* memcmp */
#include <assert.h>
#include <err.h>

/* Předmětem této ukázky bude použití podprocesu pro asynchronní
 * zpracování dat. Konkrétně naprogramujeme velmi jednoduchý
 * kompresní algoritmus známý jako RLE (z angl. Run-Length
 * Encoding). Podproces, který bude RLE realizovat, bude z jednoho
 * popisovače číst data ke kompresi, a do druhého bude zapisovat
 * komprimovaná data.
 *
 * Protože komprimovaná data bude číst tentýž proces, který zapisuje
 * data nekomprimovaná – rodič – je pro funkčnost této ukázky velmi
 * důležité, že máme záruku, že RLE zapíše nějaká data po přečtení
 * nejvýše 256 bajtů vstupu, a že zapíše nejvýše dvojnásobný objem
 * dat, než přečetlo – data se tak nebudou neomezeně hromadit ani na
 * vstupu ani na výstupu.¹ */

/* Aby se nám s podprocesem pohodlněji pracovalo, rozdělíme si jeho
 * správu do dvou podprogramů – ‹rle_start› a ‹rle_cleanup›.
 * Výsledkem prvního bude „neprůhledný“ ukazatel (s tímto ukazatelem
 * volající nemůže udělat nic jiného, než ho předat podprogramu
 * ‹rle_cleanup›). Prostřednictvím tohoto ukazatele tedy můžeme
 * předat potřebné informace z podprogramu ‹rle_start› do
 * podprogramu ‹rle_cleanup›. Za ukazatelem se bude skrývat
 * velmi jednoduchá struktura:² */

struct rle_handle
{
    pid_t pid;
};

/* Podprogram ‹rle_start› vytvoří podproces RLE a do výstupních
 * parametrů uloží dva popisovače, které může volající použít pro
 * asynchronní kompresi dat – do ‹fd_in› bude zapisovat data ke
 * kompresi a z ‹fd_out› si vyzvedne komprimovaná data. Výsledkem
 * podprogramu ‹rle_start› bude v případě chyby nulový ukazatel. */

/* Samotnou kompresi delegujeme na pomocný podprogram ‹rle_compute›,
 * který definujeme níže. Výsledkem bude -1 dojde-li k chybě, jinak
 * nula. Význam popisovačů ‹fd_in› a ‹fd_out› je relativní vůči
 * práci podprogramu ‹rle_compute› a tedy «opačný» než v hlavním
 * procesu – z ‹fd_in› bude ‹rle_compute› číst a do ‹fd_out› bude
 * zapisovat. */

int rle_compute( int fd_in, int fd_out );

void *rle_start( int *fd_in, int *fd_out )
{
    /* Alokujeme paměť pro ‹handle›. Je lepší alokaci řešit na
     * začátku, protože v případě chyby alokace bychom jinak museli
     * složitě ukončovat již nastartovaný podproces. Toto pořadí má
     * ale také drobnou nevýhodu – v podprocesu musíme paměť opět
     * uvolnit. */

    struct rle_handle *handle =
        calloc( 1, sizeof( struct rle_handle ) );

    /* Nejprve nachystáme komunikační roury – jednu pro vstup a
     * jednu pro výstup. Všechny popisovače inicializujeme na -1,
     * abychom v případě chyby věděli, které popisovače jsou platné
     * a je potřeba je zavřít. */

    int fds_in[ 2 ]  = { -1, -1 },
        fds_out[ 2 ] = { -1, -1 };

    if ( !handle ||
         pipe( fds_in ) == -1 ||
         pipe( fds_out ) == -1 )
        goto err;

    *fd_in  = fds_in[ 1 ];
    *fd_out = fds_out[ 0 ];

    handle->pid = fork();

    if ( handle->pid == -1 ) /* fork error */
        goto err;

    if ( handle->pid == 0 ) /* child */
    {
        /* Jako první akci v potomku zavřeme konce rour, které
         * náleží rodiči. */

        close( fds_in[ 1 ] );
        close( fds_out[ 0 ] );

        /* Dále uvolníme dynamickou paměť, kterou ‹rle_start›
         * alokovalo. Není to striktně vzato nutné, ale z pohledu
         * potomka by se jednalo o únik paměti (memory leak).
         * Typicky v této situaci zejména nebudeme řešit dlouhodobě
         * alokovanou paměť patřící jiným částem programu. */

        free( handle );

        /* Nyní je vše připraveno pro hlavní činnost podprocesu.
         * Musíme si zde dát pozor, abychom nezaměnili popisovače –
         * z roury ‹fds_out› bude číst hlavní proces a ‹rle_compute›
         * do ní tedy bude zapisovat a naopak, do roury ‹fds_in›
         * bude zapisovat hlavní proces a ‹rle_compute› z ní bude
         * číst. Selže-li podprogram ‹rle_compute›, podproces
         * ukončíme s nenulovým návratovým kódem.  */

        if ( rle_compute( fds_in[ 0 ], fds_out[ 1 ] ) == -1 )
            errx( 1, "rle_compute failed" );

        /* Vše proběhlo v pořádku, podproces tedy ukončíme s nulovým
         * návratovým kódem. */

        exit( 0 );
    }

    /* V rodičovském procesu musíme uzavřít opačnou dvojici
     * popisovačů, než které jsme uzavřeli v potomku. Pak je již vše
     * potřebné vyřízeno a můžeme řízení předat volajícímu. */

    close( fds_in[ 0 ] );
    close( fds_out[ 1 ] );

    return handle;

err:
    close( fds_in[ 0 ] );
    close( fds_in[ 1 ] );
    close( fds_out[ 0 ] );
    close( fds_out[ 1 ] );

    return NULL;
}

int rle_compute( int fd_in, int fd_out )
{
    /* Velikost výstupního bufferu musí být oproti tomu vstupnímu
     * dvojnásobná, protože v nejhorším případě budou každá dvě po
     * sobě jdoucí písmena různá, a celkový objem dat se tak
     * zdvojnásobí (je zde vidět, že RLE skutečně není příliš
     * sofistikovaný kompresní algoritmus). */

    char buffer[ 256 ];
    char rle[ 512 ];
    int bytes;

    /* Hlavní cyklus bude načítat data po jednotlivých oknech a
     * komprimovat je. Pro jednoduchost běhy, které kříží hranici
     * okna, komprimujeme jako 2 samostatné dvojice – to samozřejmě
     * není ideální, ale implementace posuvného kompresního okna by
     * program značně zkomplikovala. Kódování, které ‹rle_compute›
     * počítá, je ale i tak korektní, má pouze poněkud horší
     * kompresní poměr. */

    while ( ( bytes = read( fd_in, buffer, sizeof buffer ) ) > 0 )
    {
        int count = 0;
        int j = 0;

        /* Načtená data zpracujeme – všimněte si mezí cyklu a
         * podmínky, která určuje, zda znak vypíšeme nebo přičteme
         * do počítadla. Rozmyslete si, že k poli ‹buffer› je
         * přistupováno korektně (nehrozí přístup mimo meze) a
         * zároveň, že i poslední běh je korektně zpracován (zejména
         * je zapsán na výstup). */

        for ( int i = 0; i <= bytes; ++i )
            if ( ( i == 0 || buffer[ i ] == buffer[ i - 1 ] ) &&
                 count < 256 && i < bytes )
            {
                ++ count;
            }
            else
            {
                rle[ j++ ] = count;
                rle[ j++ ] = buffer[ i - 1 ];
                count = 1;
            }

        /* Zpracovaná data zapíšeme na výstup a pokračujeme ve
         * čtení. */

        if ( write( fd_out, rle, j ) == -1 )
            return -1;
    }

    return bytes; /* ≤ 0 */
}

/* Posledním, ale velmi důležitým, podprogramem je ‹rle_cleanup›,
 * kterému hlavní program předá ‹handle›, který vrátil podprogram
 * ‹rle_start›. Jeho hlavním úkolem je zjistit, zda byl podproces
 * korektně ukončen (použijeme k tomu volání ‹waitpid›). Zároveň zde
 * uvolníme paměť alokovanou pro handle.
 *
 * Pozor! Volající je povinen uzavřít popisovač ‹fd_in› předtím, než
 * zavolá podprogram ‹rle_cleanup›, jinak dojde na tomto místě
 * k uváznutí – zvažte proč. */

int rle_cleanup( void *h_ptr )
{
    struct rle_handle *handle = h_ptr;
    pid_t pid = handle->pid;
    int status;

    free( handle );

    if ( waitpid( pid, &status, 0 ) == -1 )
        return -1;

    return WIFEXITED( status ) && WEXITSTATUS( status ) == 0 ? 0 : -1;
}

/* ¹ Pro obecný případ, kdy podobnou záruku nemáme, bychom v tomto
 *   kontextu museli použít analogii podprogramů ‹comm_read› a
 *   ‹comm_write› z přípravy ‹09/p6›.
 * ² Tento mechanismus bychom v této ukázce nutně nepotřebovali,
 *   protože si předáváme jedinou hodnotu. Stejný vzor ale s výhodou
 *   použijeme i později. */

int main( void ) /* demo */
{
    int fd_in, fd_out;
    void *handle;
    char buffer[ 32 ];
    int bytes, total = 0;

    assert( handle = rle_start( &fd_in, &fd_out ) );
    assert( write( fd_in, "xooooxorrrr", 11 ) == 11 );

    while ( ( bytes = read( fd_out, buffer + total,
                            10 - total ) ) > 0 )
        total += bytes;

    assert( bytes == 0 ); /* not an eof */
    assert( total == 10 );
    assert( memcmp( buffer, "\x1x\x4o\x1x\x1o\x4r", 10 ) == 0 );

    total = 0;

    assert( write( fd_in, "wrrrrrrrrr", 10 ) == 10 );
    while ( ( bytes = read( fd_out, buffer + total,
                            4 - total ) ) > 0 )
        total += bytes;

    assert( bytes == 0 ); /* not an eof */
    assert( total == 4 );
    assert( memcmp( buffer, "\x1w\x9r", 4 ) == 0 );

    close( fd_in );
    close( fd_out );
    assert( rle_cleanup( handle ) == 0 );

    return 0;
}
