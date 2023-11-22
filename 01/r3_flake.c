#define _POSIX_C_SOURCE 200809L


/* V této úloze budeme programovat funkci ‹path›, jejímž účelem bude
 * ověřit, zda zadaný soubor splňuje určitou vlastnost.
 *
 * Soubor musí obsahovat řádky nejvýše dané délky (ta je funkci
 * předána jako druhý parametr ‹len›). Každý řádek je zakončen
 * znakem '\n' a obsahuje pouze symboly ' ' a '*'. Na každém řádku
 * se musí vyskytovat právě jednou znak '*', který označuje pozici
 * pomyslného robota.
 *
 * Nevadí, pokud řádek obsahuje mezery mezi tímto znakem a koncem
 * řádku.
 *
 * Vaším úkolem je rozhodnout, zda mezi dvěma řádky vždy platí, že
 * robot změnil pozici nejvýše o jedna.
 *
 * Příklad: Uvažme následující řádky.
 *  1: "          *    \n"
 *  2: "           *\n"
 *  3: "         *\n"
 * Mezi řádky 1 a 2 se robot posunul o jedno pozici doprava, to je
 * v pořádku. Mezi řádky 2 a 3 se však robot posunul o dvě doleva,
 * tedy soubor celkově není validní.
 *
 * Návratová hodnota:
 *  • 0 – čtení úspěšné a soubor je validní;
 *  • 1 – čtení úspěšné, ale robot se posunul o více pozic;
 *  • 2 – čtení úspěšné, ale soubor je špatného formátu;
 *  • 3 – čtení neúspěšné. */

int path( const char* file, int len );


void main( void )
{
    return 0;
}
