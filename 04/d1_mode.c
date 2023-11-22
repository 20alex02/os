#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>      /* open */
#include <sys/mman.h>   /* mmap, munmap */
#include <stdlib.h>     /* malloc, free, NULL */
#include <unistd.h>     /* write, lseek */
#include <stdint.h>     /* uint32_t, UINT32_MAX */
#include <arpa/inet.h>  /* ntohl, htonl */
#include <assert.h>
#include <err.h>

/* V této ukázce budeme pracovat se systémovým voláním ‹mmap›
 * v režimu pouze pro čtení (zápis si předvedeme v dalším programu).
 * Procedura ‹mode›, kterou budeme programovat níže, nalezne mód –
 * nejčastější hodnotu – v datovém souboru, který obsahuje záznamy
 * pevné délky. */

/* Abychom mohli počítat výskyty jednotlivých hodnot, budeme
 * potřebovat slovník – nejjednodušší implementace slovníku je
 * v tomto případě hašovací tabulka. Pro jednoduchost použijeme
 * pevně velkou tabulku a konfliktní záznamy budeme udržovat
 * v zřetězeném seznamu. Každý uzel bude obsahovat počítanou hodnotu
 * ‹value›, která představuje klíč tabulky, a počítadlo výskytů
 * ‹counter›. */

struct hash_node {
    uint32_t value;
    int counter;
    struct hash_node *next;
};

/* Samotná tabulka je pak pouze pole ukazatelů na hlavy takovýchto
 * zřetězených seznamů. */

struct hash_table {
    struct hash_node *buckets[65536];
};

/* Podprogram ‹hash_get› nalezne v tabulce uzel, který odpovídá
 * klíči ‹value›. Není-li takový uzel v tabulce přítomný, do tabulky
 * jej přidá. Selže-li alokace paměti, vrátí nulový ukazatel. */

struct hash_node *hash_get(struct hash_table *ht, uint32_t value) {
    uint32_t mux = value * 1077630871;
    uint16_t hash = (mux & 0xffff) ^ (mux >> 16);
    struct hash_node *node;

    for (node = ht->buckets[hash]; node != NULL; node = node->next)
        if (node->value == value)
            break;

    if (!node) {
        if (!(node = malloc(sizeof(struct hash_node))))
            return NULL;
        node->next = ht->buckets[hash];
        node->value = value;
        node->counter = 0;
        ht->buckets[hash] = node;
    }

    return node;
}

/* Podprogram ‹hash_free› uvolní paměť alokovanou pro hašovací
 * tabulku. */

void hash_free(struct hash_table *ht) {
    struct hash_node *n, *next;

    for (int i = 0; i < 65536; ++i)
        for (n = ht->buckets[i]; n != NULL; n = next) {
            next = n->next;
            free(n);
        }
}

/* Podprogram ‹mode› bude mít 4 parametry:
 *
 *  • popisovač vstupního souboru,
 *  • počet čtyřbajtových slov v jednom záznamu ‹record_size›,
 *  • pořadové číslo čtyřbajtového slova v záznamu, které budeme
 *    zpracovávat ‹value_index›,
 *  • výstupní parametr ‹result›.
 *
 * Hodnoty, které budeme zpracovávat, budou čtyřbajtové, uložené od
 * nejvýznamnějšího bajtu. */

int mode(int fd, int record_size, int value_index,
         uint32_t *result) {
    /* Nejprve zjistíme velikost souboru. Není-li tato dělitelná
     * velikostí záznamu, jedná se o chybu a podprogram ukončíme. */

    ssize_t size = lseek(fd, 0, SEEK_END);

    if (size == -1 || size % record_size != 0)
        return -1;

    /* Dále vytvoříme mapování souboru do paměti. První parametr
     * systémové služby ‹mmap› bude vždy nulový ukazatel¹, dále jí
     * předáme:
     *
     *  • velikost mapování (v tomto případě velikost souboru),
     *  • přístupový mód (v tomto případě „pouze pro čtení“),
     *  • režim sdílení (‹MAP_PRIVATE› protože do souboru nehodláme
     *    zapisovat),
     *  • popisovač souboru, kterého obsah chceme do paměti mapovat,
     *  • místo, od kterého chceme obsah souboru mapovat (v bajtech,
     *    v tomto případě mapujeme celý soubor od prvního bajtu). */

    uint32_t *base = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    int retcode = -1;

    /* Výsledkem volání ‹mmap› je ukazatel na místo v paměti, kde
     * mapování začíná. Jako každé systémové volání ale může ‹mmap›
     * skončit chybou – trochu netradičně pro volání, které vrací
     * ukazatel, vrátí ‹mmap› v případě chyby hodnotu -1
     * (přetypovanou na ukazatel). */

    if (base == (void *) -1)
        return -1;

    /* Vytvoříme prázdnou hašovací tabulku a iterujeme všemi záznamy
     * v souboru, přitom u každého si poznačíme výskyt příslušné
     * hodnoty do tabulky. */

    struct hash_table ht = {NULL};
    struct hash_node *node;

    for (ssize_t offset = 0; offset < size / 4; offset += record_size) {
        uint32_t value = ntohl(base[offset + value_index]);
        if (!(node = hash_get(&ht, value)))
            goto out;
        node->counter++;
    }

    /* Nyní nezbývá, než nalézt hodnotu s nejvyšším počtem výskytů.
     * Je-li takových víc, vrátíme nejmenší z nich. */

    *result = UINT32_MAX;
    int max = 0;

    for (int i = 0; i < 65536; ++i)
        for (node = ht.buckets[i]; node != NULL; node = node->next)
            if (node->counter > max ||
                (node->counter == max && node->value < *result)) {
                *result = node->value;
                max = node->counter;
            }

    retcode = 0;
    out:

    /* Nezapomeneme uvolnit zdroje – jak paměť alokovanou pro
     * hašovací tabulku, tak mapování souboru do paměti. */

    hash_free(&ht);
    if (munmap(base, size) == -1)
        warn("unmapping region %p", base);
    return retcode;
}

int main() /* demo */
{
    const char *name = "zt.d1_data.bin";
    int fd = open(name, O_CREAT | O_TRUNC | O_RDWR, 0666);

    if (fd == -1)
        err(1, "creating file %s", name);

    uint32_t buffer[65536];

    for (int i = 0; i < 65536; ++i)
        switch (i % 32) {
            case 0:
                buffer[i] = htonl(7);
                break;
            case 1:
                buffer[i] = htonl(1 + ((i - 1) % 64 == 0));
                break;
            default:
                buffer[i] = i;
        }

    for (int i = 0; i < 256; ++i)
        if (write(fd, buffer, sizeof buffer) == -1)
            err(1, "writing data into %s", name);

    uint32_t result;

    assert(mode(fd, 8, 0, &result) == 0);
    assert(result == 7);
    assert(mode(fd, 8, 1, &result) == 0);
    assert(result == 1);

    if (close(fd) == -1)
        warn("closing %s", name);

    return 0;
}
