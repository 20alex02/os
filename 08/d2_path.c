#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* getenv, setenv, malloc, free */
#include <string.h>     /* strchr, strlen */
#include <stdio.h>      /* snprintf */
#include <assert.h>

/* V této ukázce si předvedeme základy práce s proměnnými prostředí.
 * Naším cílem bude vytvořit podprogram ‹path_append›, který do
 * vybrané proměnné prostředí (jako příklad budeme používat ‹PATH›¹)
 * přidá zadanou cestu, není-li již přítomna. Procvičíme si při tom
 * mimo jiné práci s textovými řetězci (a dynamickou pamětí). */

/* Pro hledání v řetězci, který se skládá z položek oddělených
 * dvojtečkou se nám bude hodit pomocná čistá funkce ‹stpchr›, která
 * vrátí ukazatel na první výskyt zadaného znaku, nebo na ukončovací
 * nulu řetězce, není-li hledaný znak v řetězci přítomen (název je
 * kombinací názvů standardních funkcí ‹stpcpy› a ‹strchr›). */

const char *stpchr( const char *str, int ch )
{
    const char *found = strchr( str, ch );

    if ( found )
        return found;
    else
        return strchr( str, 0 );
}

/* Protože operace, které budeme používat, mohou selhat, podprogramu
 * ‹path_append› přisoudíme návratový typ ‹int› se standardním
 * významem – 0 znamená úspěch, -1 chybu. */

int path_append( const char *var_name, const char *path )
{
    /* Abychom s proměnnou prostředí mohli pracovat, musíme získat
     * její aktuální hodnotu – nejjednodušeji to provedeme
     * knihovním podprogramem ‹getenv›, který má jediný parametr –
     * název hledané proměnné, a který vrátí ukazatel na paměť, kde
     * je uložena hodnota této proměnné. Pozor, jak bylo již zmíněno
     * v úvodu, tuto paměť nesmíme měnit! */

    const char *old_path = getenv( var_name );

    /* Výsledkem ‹getenv› může být nulový ukazatel, není to ale
     * chyba – znamená to pouze, že hledaná proměnná prostředí
     * neexistuje. V takovém případě ji vytvoříme a jsme hotovi.
     * Podobně se zde vypořádáme s případem, kdy je stávající
     * hodnota prázdná – zjednodušíme si tím situaci níže. */

    if ( !old_path || !*old_path )
        return setenv( var_name, path, 1 );

    /* Případ, kdy proměnná již existuje je o něco složitější –
     * musíme nejprve zjistit, zda není požadovaná cesta již
     * v seznamu přítomna. Pokud ano, neprovedeme žádnou akci a
     * oznámíme volajícímu úspěch. */

    const int path_len = strlen( path );
    const char *start, *end;

    for ( start = end = old_path; *end; start = end + 1 )
    {
        /* Nejprve nalezneme konec aktuální položky seznamu. Ta může
         * být ukončena dvojtečkou, nebo je-li poslední, je ukončena
         * nulou (protože hodnota proměnné prostředí jako celek je
         * ukončena nulou). */

        end = stpchr( start, ':' );

        /* Nyní srovnáme aktuální položku (která je uložena mezi
         * ukazateli ‹start› a ‹end›) s řetězcem ‹path›.
         * Nalezneme-li shodu, podprogram ukončíme – cesta je již
         * přítomna a není tedy potřeba proměnnou ‹var_name›
         * upravovat. */

        if ( end - start == path_len &&
             memcmp( start, path, path_len ) == 0 )
            return 0;
    }

    /* Již víme, že ‹path› není v proměnné prostředí ‹var_name›
     * přítomno, musíme jej tedy přidat. Opět k tomu použijeme
     * volání ‹setenv›. Protože ‹path› může být celkem libovolně
     * dlouhý řetězec (a proměnná prostředí může být také
     * potenciálně velmi velká), novou hodnotu sestavíme v dynamické
     * paměti. Krom řetězců, které chceme spojit, potřebujeme
     * v paměti místo na oddělovač ‹:› a koncovou nulu.² */

    int new_size = strlen( old_path ) + path_len + 2;
    char *new_path = malloc( new_size );

    if ( !new_path )
        return -1;

    /* Nyní již jednoduše sestavíme novou cestu a nastavíme ji jako
     * hodnotu proměnné prostředí. */

    snprintf( new_path, new_size, "%s:%s", old_path, path );
    int result = setenv( var_name, new_path, 1 );

    /* Protože podprogram ‹setenv› data z předaného řetězce
     * zkopíruje, alokovanou paměť musíme opět uvolnit. Jiná by byla
     * situace při použití ‹putenv›.³ */

    free( new_path );
    return result;
}

int main() /* demo */
{
    setenv( "PATH", "/usr/bin:/bin", 1 );

    assert( path_append( "PATH", "/bin" ) == 0 );
    assert( !strcmp( getenv( "PATH" ), "/usr/bin:/bin" ) );
    assert( path_append( "PATH", "/usr/bin" ) == 0 );
    assert( !strcmp( getenv( "PATH" ), "/usr/bin:/bin" ) );

    assert( path_append( "PATH", "/sbin" ) == 0 );
    assert( !strcmp( getenv( "PATH" ), "/usr/bin:/bin:/sbin" ) );

    setenv( "PATH", "", 1 );
    assert( path_append( "PATH", "/bin" ) == 0 );
    assert( !strcmp( getenv( "PATH" ), "/bin" ) );
    assert( path_append( "PATH", "/sbin" ) == 0 );
    assert( !strcmp( getenv( "PATH" ), "/bin:/sbin" ) );

    return 0;
}

/* ¹ Proměnná ‹PATH› je sice jediná standardizovaná proměnná tohoto
 *   typu, v různých systémech ale existují podobné proměnné, které
 *   obsahují seznam absolutních cest oddělených dvojtečkami.
 *   Relativně běžná je např. ‹LD_LIBRARY_PATH› (popsaná v manuálové
 *   stránce ‹ld.sq›) nebo třeba ‹PYTHONPATH›, kterou používá
 *   interpret jazyka Python pro vyhledání modulů.
 *
 * ² Volba jmen ‹path_len› vs ‹new_size› není náhodná – proměnná
 *   s názvem ‹len› obsahuje délku řetězce, zatímco proměnná
 *   s názvem ‹size› obsahuje velikost alokovaného bloku paměti.
 *   Tyto hodnoty se liší o jedničku, protože koncová nula se do
 *   délky řetězce nepočítá. */
