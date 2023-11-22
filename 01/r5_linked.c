#define _POSIX_C_SOURCE 200809L

/* Implementujte funkci ‹linked›, která přečte «první řádek»
 * zadaného souboru. Tento text se interpretuje jako název souboru,
 * kterým se má dále pokračovat. Nejprve ho vypíše (na popisovač
 * ‹out›) a potom soubor otevře a provede pro něj totéž.
 *
 * Tento proces se opakuje, dokud nenarazíme na prázdný soubor,
 * který zřetězenou sekvenci ukončí.
 *
 * Abychom nemuseli komplikovaně načítat neomezeně dlouhé názvy,
 * zavedeme zde limit pro délku názvu souboru 256 bajtů. Je-li první
 * řádek v některém vstupním souboru delší, funkce ‹linked› skončí
 * s návratovou hodnotou -2 (znak konce řádku se do tohoto limitu
 * nepočítá). Nastane-li systémová chyba, funkce nechť vrátí hodnotu
 * -1, jinak pak počet souborů, které otevřela. */

const int name_max = 256;

int linked( const char* file, int out );


int main( void )
{
    return 0;
}
