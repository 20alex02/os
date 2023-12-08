#define _POSIX_C_SOURCE 200809L
#include <unistd.h>     /* pipe, read, write, … */
#include <err.h>
#include <assert.h>

/* † Při obousměrné komunikaci pomocí omezených komunikačních front
 * může lehce dojít k uváznutí – obě strany čekají, až se uvolní
 * místo ve frontě, a protože ani jeden z nich data z fronty
 * nevybírá, potřebné místo se neuvolní nikdy.
 *
 * Roury i sockety se v systémech POSIX chovají jako omezené fronty
 * – existuje nějaké maximální množství dat, které je operační
 * systém ochoten uložit do vyrovnávací paměti, než odesílající
 * program (ten, který do roury zapisuje voláním ‹write›) zablokuje.
 *
 * V tomto příkladu bude Vaším úkolem naprogramovat mechanismus,
 * který tento problém řeší na straně programu. Uvažme situaci, kdy
 * chceme externímu programu předat data ke zpracování, a zpracovaná
 * data opět vyzvednout.
 *
 * Představte si například, že chcete ve svém programu použít
 * nástroj ‹grep› – vstupní data mu pošlete rourou, a vyfiltrovaný
 * výstup si vyzvednete z roury opačné. Nejjednodušší řešení, které
 * Vás napadne, je pravděpodobně otevřít obě roury, program spustit,
 * všechna vstupní data zapsat do té první a pak výsledek přečíst
 * z té druhé. Toto řešení bude fungovat je-li dat na výstupu málo.
 * Jakmile jich bude hodně, Váš program uvázne (rozmyslete si proč).
 *
 * Abychom podobné situaci předešli, vytvoříme si následující sadu
 * podprogramů (de facto drobnou knihovnu):
 *
 *  • ‹comm_init› – podprogramu předáme dvojici otevřených
 *    popisovačů a obdržíme ukazatel ‹handle›, který budeme dále
 *    používat pro komunikaci,
 *  • ‹comm_read›, ‹comm_write› mají stejné parametry jako systémová
 *    volání ‹read› a ‹write›, ale místo popisovače souboru obdrží
 *    ukazatel ‹handle› vytvořený podprogramem ‹comm_init›,
 *  • ‹comm_fini› zajistí, že se dokončí všechny zápisy a uvolní
 *    veškeré zdroje spojené s předaným ukazatelem ‹handle› (včetně
 *    asociovaných popisovačů).
 *
 * Podprogram ‹comm_write› nesmí za žádných okolností blokovat –
 * není-li možné data předat systému, musí je interně uložit skrze
 * ukazatel ‹handle› a řízení vrátit volajícímu, jako by zápis
 * uspěl. Podprogram ‹comm_read› blokovat smí (resp. musí –
 * návratová hodnota 0 znamená ukončeni komunikace), ale musí
 * zároveň zabezpečit, že data z předešlých volání ‹comm_write›
 * budou i během takového čekání odeslána.
 *
 * Podprogram ‹comm_init› vrátí v případě selhání nulový ukazatel,
 * ‹comm_read› a ‹comm_write› vrátí počet přečtených resp.
 * zpracovaných bajtů a ‹comm_fini› vrátí v případě úspěchu nulu.
 * V případě chyby vrátí tři posledně jmenované podprogramy hodnotu
 * -1. Zdroje v případě chyby uvolní volající použitím podprogramu
 * ‹comm_fini›. */

void *comm_init( int read_fd, int write_fd );
int comm_read( void *handle, char *buffer, int nbytes );
int comm_write( void *handle, const char *data, int nbytes );
int comm_fini( void *handle );

int main( void )
{
    int bytes, total = 0;
    int fds[ 2 ];
    char wbuffer[ 15 * 128 ] = { 0 };
    char rbuffer[ 33 ];

    if ( pipe( fds ) == -1 )
        err( 1, "creating a pipe" );

    void *handle = comm_init( fds[ 0 ], fds[ 1 ] );

    for ( int i = 0; i < 4; ++i )
    {
        wbuffer[ 7 ] = i;

        assert( comm_write( handle, wbuffer, sizeof wbuffer ) ==
                sizeof wbuffer );
        assert( comm_read( handle, rbuffer, sizeof rbuffer ) ==
                sizeof rbuffer );

        total += sizeof rbuffer;
    }

    while ( ( total < 15 * 128 * 4 ) &&
            ( bytes = comm_read( handle, wbuffer,
                                 sizeof wbuffer ) ) > 0 )
        total += bytes;

    assert( total == 15 * 128 * 4 );
    assert( comm_fini( handle ) == 0 );

    return 0;
}
