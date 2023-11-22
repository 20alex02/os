#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <stdint.h>     /* uint64_t */
#include <unistd.h>     /* close, write, unlink */
#include <fcntl.h>      /* open, O_RWDR, O_CREAT, O_TRUNC */

/* V tomto cvičení je Vaším úkolem zpracovat soubor, ve kterém je
 * uložena tabulka se záznamy pevně daných velikostí (v bitech).
 * Podprogram ‹sum› obdrží:
 *
 *  • ‹fd› – popisovač souboru, se kterým bude pracovat (můžete
 *    předpokládat, že objekt, se kterým je svázaný, lze mapovat do
 *    paměti),
 *  • ‹cols› – počet sloupců zde uložené tabulky,
 *  • ‹sizes› – pole čísel (pro každý sloupec jedno), které určuje
 *    kolik bitů daný sloupec obsahuje (1–64),
 *  • ‹results› – pole ‹cols› 64bitových čísel, kam uloží součty
 *    jednotlivých sloupců.
 *
 * Hodnoty jsou uloženy od nejvýznamnějšího bajtu po nejméně
 * významný, vždy v nejmenším možném počtu bajtů. Není-li počet bitů
 * sloupce dělitelný 8, zbývající nejvyšší bity nejvyššího bajtu jsou
 * nevyužity. Tyto bity ignorujte – ve vstupním souboru mohou mít
 * libovolné hodnoty. Hodnoty ve sloupcích jsou bezznaménkové.
 *
 * Nedošlo-li při sčítání k chybě, podprogram vrátí hodnotu 0.
 * V případě systémové chyby vrátí -1 a pokud soubor obsahuje
 * neúplný řádek, vrátí -2, aniž by změnil pole ‹results›.
 *
 * Podprogram musí pracovat efektivně i v situaci, kdy soubor
 * obsahuje velmi velký počet krátkých řádků. */

int sum(int fd, int cols, int *sizes, uint64_t *results) {
    (value + (roundTo - 1)) & ~(roundTo - 1)
    uint32_t *base = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    return 0;
}


/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

int main(void) {
    unsigned char data[] =
            {
                    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
            };

    int sizes[16];
    uint64_t res[16];
    for (int i = 0; i < 16; ++i) {
        sizes[i] = 8;
        res[i] = 0xdeadbeef;
    }

    int fd = open("zt.p1_table", O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd == -1)
        err(1, "open table");

    // prázdný soubor je korektní vstup
    assert(sum(fd, 1, sizes, res) == 0);
    assert(res[0] == 0);
    assert(res[1] == 0xdeadbeef);

    if (write(fd, data, sizeof data) == -1)
        err(1, "write table");

    // poslední řádek obsahuje jen jeden sloupec
    assert(sum(fd, 3, sizes, res) == -2);

    assert(sum(fd, 1, sizes, res) == 0);
    assert(res[0] == 120);
    assert(res[1] == 0xdeadbeef);

    assert(sum(fd, 4, sizes, res) == 0);
    assert(res[0] == 24);
    assert(res[1] == 28);
    assert(res[2] == 32);
    assert(res[3] == 36);
    assert(res[4] == 0xdeadbeef);

    assert(sum(fd, 16, sizes, res) == 0);
    for (unsigned i = 0; i < 16; ++i)
        assert(res[i] == i);

    sizes[0] = 64;
    assert(sum(fd, 1, sizes, res) == 0);
    assert(res[0] == 0x080a0c0e10121416);

    sizes[0] = sizes[1] = 28;
    assert(sum(fd, 2, sizes, res) == 0);
    assert(res[0] == 0x080a0c0e);
    assert(res[1] == 0x10121416);

    sizes[0] = 1;
    assert(sum(fd, 1, sizes, res) == 0);
    assert(res[0] == 8);

    sizes[1] = 24;
    assert(sum(fd, 2, sizes, res) == 0);
    assert(res[0] == 0);
    assert(res[1] == 0x1c2024);

    // subbajtové sloupce nejsou uloženy natěsno
    sizes[0] = 2;
    sizes[1] = 6;
    assert(sum(fd, 2, sizes, res) == 0);
    assert(res[0] == 8);
    assert(res[1] == 64);

    if (close(fd) == -1)
        warn("close");
    if (unlink("zt.p1_table") == -1)
        err(1, "unlink");
}
