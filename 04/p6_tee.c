#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, pipe */
#include <assert.h>     /* assert */
#include <string.h>     /* memcmp */
#include <stdlib.h>     /* malloc */
#include <err.h>        /* err */
#include <errno.h>

/* ‡ Uvažme situaci, kdy chceme stejná data zapisovat do několika
 * komunikačních kanálů, které ale nejsou tato data schopna
 * zpracovávat stejně rychle. Zároveň nechceme program blokovat
 * v situaci, kdy některý komunikační kanál není schopen data ihned
 * přijmout. Navrhněte sadu podprogramů, která bude tento problém
 * řešit:
 *
 *  • ‹tee_init› obdrží pole popisovačů, do kterých si přejeme data
 *    kopírovat, a vrátí ukazatel ‹handle› (nebo nulový ukazatel
 *    v případě selhání),
 *  • ‹tee_write› obdrží ukazatel ‹handle›, ukazatel na data a
 *    velikost dat, které si přejeme rozeslat,
 *  • ‹tee_fini› zapíše všechna zbývající data a uvolní veškeré
 *    zdroje přidružené k předanému ukazateli ‹handle›.
 *
 * Podprogram ‹tee_write› nebude za žádných okolností blokovat.
 * Zároveň zabezpečí, že veškerá data která odeslat lze budou
 * odeslána ihned, a že data, která už byla odeslána do všech kanálů
 * nebudou zabírat paměť. Podprogram ‹tee_write› lze volat s nulovou
 * velikostí, v takovém případě pouze provede případné odložené
 * zápisy. Návratová hodnota ‹tee_write› je:
 *
 *  • 0 – veškerá data byla odeslána všem příjemcům,
 *  • kladné číslo – počet popisovačů, které jsou „pozadu“,
 *  • -1 – nastala chyba, která znemožnila zpracování předaných dat
 *    (data lze bezpečně do ‹tee_write› předat znovu, aniž by
 *    hrozilo dvojité odeslání).
 *
 * Nastane-li chyba při zápisu na některý popisovač, ‹tee_write›
 * tento popisovač přeskočí a data se pokusí při dalším volání opět
 * odeslat.
 *
 * Konečně ‹tee_fini› dokončí veškeré zápisy. Dojde-li při některém
 * z nich k chybě, vrátí ‹-1›, ale až potom, co dokončí všechny
 * zápisy, které provést lze, a uvolní zdroje spojené s ukazatelem
 * ‹handle› (včetně uzavření všech přidružených popisovačů).
 *
 * Očekává se, že tee_fini zavře všechny popisovače předané tee_ini */

void *tee_init( int fd_count, int *fds );
int tee_write( void *handle, const char *data, int nbytes );
int tee_fini( void *handle );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void fill( int fd, int byte )
{
    char buffer[ 1024 ];
    memset( buffer, byte, 1024 );
    int wrote = write( fd, buffer, 1024 );

    if ( wrote == -1 && errno != EAGAIN && errno != EWOULDBLOCK )
        err( 1, "filling up a pipe" );

    if ( wrote == 1024 )
        return fill( fd, byte );
}

static void drain( int fd, int byte )
{
    char buffer[ 1023 ];
    int readb = read( fd, buffer, 1023 );

    if ( readb == -1 )
        err( 1, "draining pipe" );

    for ( int i = 0; i < readb; ++i )
        assert( buffer[ i ] == byte );

    if ( readb == 1023 )
        drain( fd, byte );
}

int main()
{
    char buffer[ 256 ];
    int fds_read[ 3 ], fds_write[ 3 ];

    for ( int i = 0; i < 3; i++ )
    {
        int p[ 2 ];

        if ( pipe( p ) != 0 )
            err( 1, "unable to open pipes" );

        fds_read[ i ] = p[ 0 ];
        fds_write[ i ] = p[ 1 ];
    }

    void *h = tee_init( 3, fds_write );

    assert( tee_write( h, "hello", 5 ) == 0 );

    for ( int i = 0; i < 3; i++ )
    {
        assert( read( fds_read[ i ], buffer, 100 ) == 5 );
        assert( memcmp( buffer, "hello", 5 ) == 0 );
    }

    fill( fds_write[ 2 ], 0 );
    assert( tee_write( h, "world", 5 ) == 1 );
    drain( fds_read[ 2 ], 0 );

    assert( tee_write( h, "lorem", 5 ) == 0 );

    for ( int i = 0; i < 3; i++ )
    {
        assert( read( fds_read[ i ], buffer, 100 ) == 10 );
        assert( memcmp( buffer, "worldlorem", 10 ) == 0 );
    }

    fill( fds_write[ 1 ], 0 );
    fill( fds_write[ 2 ], 0 );

    assert( tee_write( h, "ipsum", 5 ) == 2 );
    drain( fds_read[ 2 ], 0 );
    assert( tee_write( h, "dolor", 5 ) == 1 );

    drain( fds_read[ 1 ], 0 );
    assert( tee_write( h, "sit", 3 ) == 0 );

    for ( int i = 0; i < 3; i++ )
    {
        assert( read( fds_read[ i ], buffer, 100 ) == 13 );
        assert( memcmp( buffer, "ipsumdolorsit", 13 ) == 0 );
    }

    for ( int i = 0; i < 3; i++ )
        fill( fds_write[ i ], 0 );

    assert( tee_write( h, "amet", 4 ) == 3 );
    drain( fds_read[ 0 ], 0 );
    assert( tee_write( h, "consectetur", 11 ) == 2 );

    assert( read( fds_read[ 0 ], buffer, 100 ) == 15 );
    assert( memcmp( buffer, "ametconsectetur", 15 ) == 0 );

    drain( fds_read[ 1 ], 0 );
    assert( tee_write( h, "adipiscing", 10 ) == 1 );

    assert( read( fds_read[ 0 ], buffer, 100 ) == 10 );
    assert( memcmp( buffer, "adipiscing", 10 ) == 0 );
    assert( read( fds_read[ 1 ], buffer, 100 ) == 25 );
    assert( memcmp( buffer, "ametconsecteturadipiscing", 25 ) == 0 );

    drain( fds_read[ 2 ], 0 );
    assert( tee_write( h, "elit", 4 ) == 0 );

    assert( read( fds_read[ 0 ], buffer, 100 ) == 4 );
    assert( memcmp( buffer, "elit", 4 ) == 0 );
    assert( read( fds_read[ 1 ], buffer, 100 ) == 4 );
    assert( memcmp( buffer, "elit", 4 ) == 0 );
    assert( read( fds_read[ 2 ], buffer, 100 ) == 29 );
    assert( memcmp( buffer, "ametconsecteturadipiscingelit", 29 ) == 0 );

    for ( int i = 0; i < 3; i++ )
        fill( fds_write[ i ], 0 );

    assert( tee_write( h, "sed", 3 ) == 3 );

    for ( int i = 0; i < 3; i++ )
        drain( fds_read[ i ], 0 );

    assert( tee_fini( h ) == 0 );

    for ( int i = 0; i < 3; i++ )
    {
        assert( read( fds_read[ i ], buffer, 100 ) == 3 );
        assert( memcmp( buffer, "sed", 3 ) == 0 );
    }
}
