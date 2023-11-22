#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlinkat */
#include <fcntl.h>      /* openat */
#include <assert.h>     /* assert */
#include <errno.h>      /* errno */
#include <err.h>        /* err */
#include <stdint.h>     /* uint32_t */
#include <string.h>     /* memcpy */
#include <stdio.h>      /* dprintf */

/* Knihovna LMDB (Lightning Memory-mapped Database Library) je
 * určená k efektivnímu ukládání slovníků (dvojic klíč–hodnota).
 * Data ukládá do souborů, přitom samotná data jsou uspořádána do
 * B+stromů. Vaším úkolem bude naprogramovat funkci, která zjistí,
 * je-li zadaný klíč přítomen v databázi. Databáze je funkci předána
 * formou popisovače otevřeného datového souboru. Návratová hodnota
 * 0 znamená úspěch, -1 systémovou chybu, -2 chybu ve formátu
 * souboru. V případě úspěchu je přítomnost klíče poznačena na
 * adresu ‹found›.
 *
 * Předpokládáme, že databázi souběžně nepoužívá žádná jiná
 * aplikace. Také předpokládáme, že klíče jsou řazeny
 * lexikograficky, a že databáze používá stejné pořadí bajtů ve
 * slově („endianitu“) jako počítač, na kterém tento program běží.
 * */

int lmdb_has_key( int fd, const char *key, int key_len,
                  int *found );

/* Databázové soubory systému LMDB jsou strukturovány do stránek,
 * které odpovídají stránkám paměti operačního systému.¹ Každá
 * stránka je rozdělena na 3 části: metadatovou, prázdnou a datovou.
 * Metadatová část začíná šestnáctibajtovou hlavičkou (v hranatých
 * závorkách je uvedena počáteční adresa dané položky,
 * šestnáctkově):
 *
 *  • [00] 64bitové pořadové číslo stránky v souboru,
 *  • [08] nevyužité 16bitové číslo,²
 *  • [0a] 16bitové pole příznaků,
 *  • [0c] 16bitová celková velikost metadatové části,
 *  • [0e] 16bitové číslo prvního bajtu datové části.
 *
 * První dvě stránky datového souboru popisují databázi jako celek
 * a po výše uvedené hlavičce v každé z nich následuje:
 *
 *  • [10] 32bitové identifikační číslo (vždy 0xbeefc0de),
 *  • [14] 32 + 64 + 64 bitů, které přeskočíme,
 *  • [28] 6 × 64 bitů metadat o prázdném místě,
 *  • [58] informace o hlavním B+stromě:
 *    ◦ [58] 32 nevyužitých bitů,
 *    ◦ [5c] 16 bitů příznaků,
 *    ◦ [5e] 16bitová hloubka stromu,
 *    ◦ [60] 3 × 64 bitů statistických informací,
 *    ◦ [78] 64bitový počet klíčů,
 *    ◦ [80] číslo kořenové stránky,
 *  • [88] číslo poslední stránky,
 *  • [90] pořadové číslo transakce, která tuto stránku zapsala.³
 *
 * Samotné B+stromy jsou uloženy po stránkách – jedna stránka
 * obsahuje jeden uzel. V metadatové části jsou uloženy 16bitové
 * odkazy na začátky jednotlivých klíčů – odkaz je index prvního
 * bajtu daného klíče, počítáno od začátku stránky.⁴ Jedná-li se
 * o vnitřní uzel nebo o list se pozná z příznaků v metadatové
 * hlavičce stránky – vnitřní uzly mají nastavený nejnižší bit
 * (‹0x01›), listy mají nastavený druhý nejnižší bit (‹0x02›).
 *
 * Formát klíče se mírně liší podle toho, jestli se jedná o vnitřní
 * uzel nebo o list. Vnitřní uzly používají tuto strukturu:
 *
 *  • 48bitové číslo stránky (uzlu), na kterou tento klíč odkazuje,
 *  • 16bitová velikost klíče,
 *  • samotný klíč.
 *
 * Protože ke každému klíči je připojen jeden odkaz, musí vnitřní
 * uzly obsahovat jeden prázdný klíč navíc – je to ten nejvíce vlevo
 * a má nastavenu délku klíče na nulu.⁵
 *
 * Listy pak používají tuto strukturu dat:
 *
 *  • 32bitová velikost hodnoty náležící tomuto klíči,
 *  • 16 bitů příznaků (uvažujeme nulové),
 *  • 16bitová velikost klíče,
 *  • samotný klíč,
 *  • hodnota náležící klíči. */

/* Poznámky na závěr:
 *
 *  • pro práci s čísly pevné bitové šířky se budou hodit typy
 *    ‹uint16_t›, ‹uint64_t›, ‹int16_t›, atd., definované hlavičkou
 *    ‹stdint.h›,
 *  • při testování se může hodit modul ‹lmdb› pro Python:
 *    ◦ získáte jej příkazem ‹pip install lmdb›,
 *    ◦ ‹env = lmdb.open('test')› vytvoříte prázdnou databázi,
 *    ◦ ‹txn = env.begin(write=True)› otevřete transakci,
 *    ◦ ‹txn.put(b'key', b'value')› vložíte dvojici klíč/hodnota,
 *    ◦ ‹txn.commit()› transakci ukončíte,
 *    ◦ datový soubor se bude jmenovat ‹test/data.mdb›. */

/* ¹ S databázovými soubory lze pracovat i na systémech, které mají
 *   jinou velikost stránky, než ten, který databázi vytvořil. Touto
 *   situací se zde ale zabývat nebudeme – pro účely této úlohy
 *   můžete předpokládat, že soubor používá nativní velikost
 *   stránky, a že tato je 4KiB.
 * ² Používá se pouze pro typy stránek, které zde nebudeme uvažovat.
 * ³ Protože nás zajímá pouze nejnovější transakce, budeme
 *   metadatovou stránku s nižším číslem transakce ignorovat.
 * ⁴ Tyto odkazy jsou seřazené podle klíče, na který odkazují, aby
 *   mezi nimi bylo možné vyhledávat půlením intervalu. Samotné
 *   klíče jsou uloženy v datové části již v libovolném pořadí kvůli
 *   efektivnějšímu vkládání.
 * ⁵ B+stromy zde používané mají ještě jednu odlišnost od těch,
 *   které znáte z IB002 – uzly neobsahují pevný počet klíčů,
 *   protože je pevná velikost stránky ve které je uzel uložen a
 *   zároveň jsou uvnitř uzlu uloženy klíče proměnné délky. Uzel má
 *   tedy tolik potomků, kolik se do něj vejde klíčů (velikost klíče
 *   je omezena tak, aby se do každého uzlu vešly alespoň dva). */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char* name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinkat '%s'", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void write_file( int dir, const char *name,
                        const char *str, int count )
{
    unlink_if_exists( dir, name );
    int fd;

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_RDWR,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    if ( write( fd, str, count ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

void meta( char *start, uint64_t pgno, uint64_t root, uint16_t depth, uint64_t txno )
{
    uint16_t meta = 0x08;
    uint32_t magic = 0xbeefc0de;
    uint32_t version = 1;
    uint32_t pagesize = 0x1000;
    uint16_t flags = 0x08;
    uint64_t inv = -1;
    uint64_t last = root == inv ? 1 : root;
    uint64_t mapsize = 0xa00000;
    uint64_t leaves = root == inv ? 0 : 1;

    memcpy( start + 0x00, &pgno, sizeof pgno );
    memcpy( start + 0x0a, &meta, sizeof meta );
    memcpy( start + 0x10, &magic, sizeof magic );
    memcpy( start + 0x14, &version, sizeof version );
    memcpy( start + 0x20, &mapsize, sizeof mapsize );
    memcpy( start + 0x28, &pagesize, sizeof pagesize );
    memcpy( start + 0x2c, &flags, sizeof flags );

    memcpy( start + 0x50, &inv, sizeof inv );
    memcpy( start + 0x5e, &depth , sizeof depth );
    memcpy( start + 0x68, &leaves, sizeof leaves );
    memcpy( start + 0x80, &root, sizeof root );
    memcpy( start + 0x88, &last, sizeof last );
    memcpy( start + 0x90, &txno, sizeof txno );
}

int main( int argc, const char **argv )
{
    int dir, fd;
    const char *name;
    int found;

    if ( ( dir = open( ".", O_RDONLY ) ) == -1 )
        err( 2, "opening working directory" );

    if ( argc > 1 )
    {
        if ( argc < 3 )
            errx( 1, "2 arguments expected: file key" );

        name = argv[ 1 ];
    }
    else
    {

        name = "zt.c_data.mdb";
        char data[ 3 * 4096 ] = { 0 };

        uint16_t flags = 0x02;
        uint16_t mb_lower = 0x12, mb_upper = 0x0ff6;
        uint64_t pgno_2 = 2;

        meta( data + 0x0000, 0, -1, 1, 0 );
        meta( data + 0x1000, 1, 2, 1, 1 );

        memcpy( data + 0x2000, &pgno_2, sizeof pgno_2 );
        memcpy( data + 0x200a, &flags, sizeof flags );
        memcpy( data + 0x200c, &mb_lower, sizeof mb_lower );
        memcpy( data + 0x200e, &mb_upper, sizeof mb_upper );
        memcpy( data + 0x2010, &mb_upper, sizeof mb_upper );
        memcpy( data + 0x2ff6, "\x01\x00\x00\x00\x00\x00\x01\x00xy", 10 );

        unlink_if_exists( dir, name );
        write_file( dir, name, data, sizeof data );
    }

    if ( ( fd = openat( dir, name, O_RDONLY ) ) == -1 )
        err( 2, "opening %s", name );

    if ( argc > 1 )
    {
        const char *key = argv[ 2 ];

        if ( lmdb_has_key( fd, key, strlen( key ), &found ) )
            err( 1, "looking up the key in %s", name );

        dprintf( STDOUT_FILENO, "key %s %s in file %s\n",
                 argv[ 2 ], found ? "found" : "not found", name );
    }
    else
    {
        assert( lmdb_has_key( fd, "x", 1, &found ) == 0 );
        assert( found );
        assert( lmdb_has_key( fd, "y", 1, &found ) == 0 );
        assert( !found );

        unlink_if_exists( dir, name );
    }

    close_or_warn( fd, name );
    close_or_warn( dir, "working directory" );
    return 0;
}
