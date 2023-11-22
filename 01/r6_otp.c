#define _POSIX_C_SOURCE 200809L


/* V této úloze si naprogramujeme jednoduchou techniku
 * z kryptografie označovanou jako „one-time pad.“
 *
 * Funkci ‹otp› jsou zadány dva názvy souborů a výstupní popisovač.
 * Úkolem této funkce je na tento popisovač vypsat
 *     ‹obsah prvního XOR obsah druhého›.
 *
 * «Druhý» soubor zde považujeme za klíč a pro bezpečnost této
 * kryptografické techniky je nezbytné, aby jeho délka byla
 * «alespoň» taková jako je délka prvního.
 *
 * V případě, že tento požadavek není splněn, funkce nechť vrátí -2.
 * Jestliže nastane problém ve čtení některého souboru či problém
 * v zápisu na výstupní popisovač, vraťte -1. Pokud je vše úspěšné,
 * vraťte 0. */

int otp( const char* file, const char* key_file, int out );


int main( void )
{
    return 0;
}
