/* Tento program nic nedělá, slouží pouze jako ukázka základní
 * struktury. Ve všech programech v tomto předmětu budeme
 * deklarovat, že jsou napsané s ohledem na normu POSIX.1–2017.
 * K tomu slouží následovná direktiva preprocesoru: */

#define _POSIX_C_SOURCE 200809L

/* Relevantní informace z kapitoly 2 standardu POSIX:
 *
 * > A POSIX-conforming application shall ensure that the feature
 * > test macro _POSIX_C_SOURCE is defined before inclusion of any
 * > header.
 * >
 * > When an application includes a header described by
 * > POSIX.1-2017, and when this feature test macro is defined to
 * > have the value ‹200809L›: All symbols required by POSIX.1-2017
 * > to appear when the header is included shall be made visible.
 *
 * Výše uvedená direktiva ‹#define› není jediný způsob, jak tomuto
 * požadavku dostát, ale je pro naše účely nejjednodušší. Musí vždy
 * stát před jakoukoliv direktivou ‹#include›. */

/* Obvykle následují direktivy ‹#include›, které odkazují tzv.
 * systémové hlavičkové soubory. V těchto souborech jsou deklarovány
 * podprogramy, které poskytuje implementace jazyka C a operační
 * systém. Které hlavičkové soubory je pro použití daného
 * podprogramu vložit je vždy popsáno v odpovídající manuálové
 * stránce (a také v příslušných normách). V ukázkách a kostrách
 * budeme u každého ‹#include› v komentáři uvádět podprogramy,
 * případně konstanty, které z dané hlavičky hodláme využívat. */

#include <stdlib.h>     /* exit */

/* Na tomto místě bude ve většině příkladů popsaný podprogram,
 * napsání kterého je Vaším úkolem. V této ukázce zde není nic. */

/* Konečně bude součástí kostry procedura ‹main›, která představuje
 * vstupní bod výsledného programu. Jejím obvyklým úkolem bude
 * otestovat podprogram, který jste výše naprogramovali. V této
 * ukázce nedělá nic, pouze explicitně ukončí program. */

int main() /* demo */
{
    /* Procedura ‹exit› ukončí vykonávání programu. Má stejný efekt,
     * jako návrat z procedury ‹main›, ale lze ji použít na
     * libovolném místě. V tomto kurzu ji prakticky nikdy nebudete
     * mít důvod použít (může se ale objevit v testech, proto je
     * dobré ji znát). */

    exit( 0 );
}
