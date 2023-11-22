#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <err.h>
#include <assert.h>

/* V této ukázce přidáme k té předchozí «rekurzi» – budeme pracovat
 * s celým adresářovým podstromem, nikoliv jen jednotlivým
 * adresářem. V takových případech bude obvykle výhodné rozdělit
 * podprogram na dvě části – vstupní (‹tree_depth› definované níže)
 * a rekurzivní (‹tree_depth_rec› definováno zde).
 *
 * Protože potřebujeme být schopni signalizovat chybu, pro tento
 * účel si vyhradíme návratovou hodnotu a pro vše ostatní budeme
 * používat parametry. To nám navíc umožní jednoduše sdílet data
 * mezi různými úrovněmi zanoření. Nachystat tato společná data bude
 * úkolem vstupní procedury ‹tree_depth›, v rekurzi si je budeme
 * předávat ukazatelem. V tomto případě bude společným stavem pouze
 * dosažená maximální hloubka ‹max_depth›.
 *
 * Lokální stav je pak tvořen aktuální hloubkou zanoření, předávaný
 * v parametru ‹depth›. Každá aktivace podprogramu ‹tree_depth_rec›
 * bude pracovat s jednou složkou, kterou jí předáme v parametru
 * ‹root_fd› jako popisovač. Vlastnictví tohoto popisovače přechází
 * voláním na aktivaci podprogramu ‹tree_depth_rec›, které byl
 * předán jako parametr a tato je tedy odpovědna i za jeho uvolnění
 * (uzavření). */

int tree_depth_rec( int root_fd, int *max_depth, int depth )
{
    /* Abychom nemuseli kontrolovat platnost popisovače v místech
     * volání (jsou dvě – vstupní a rekurzivní), sjednotíme tuto
     * kontrolu zde. */

    if ( root_fd == -1 )
        return -1;

    /* Vstupujeme-li do hloubky, která je větší než dosud největší
     * navštívená, tuto skutečnost poznačíme do sdílené stavové
     * proměnné ‹max_depth›.  */

    if ( depth > *max_depth )
        *max_depth = depth;

    /* Dále si připravíme strukturu ‹DIR›, abychom mohli prohledat
     * případné podsložky (které nalezneme použitím podprogramu
     * ‹readdir› níže). */

    int rv = -1;
    DIR *dir = fdopendir( root_fd );

    if ( !dir )
        goto out;

    /* Popisovač kořenová složky je vstupnímu podprogramu
     * ‹tree_depth› předán v neznámém stavu, před čtením
     * jednotlivých položek v adresáři tedy přesuneme pozici čtení
     * na začátek. */

    rewinddir( dir );

    struct dirent *ptr;
    struct stat st;

    /* Následuje standardní iterace položkami adresáře. */

    while ( ( ptr = readdir( dir ) ) )
    {

        /* Novým prvkem je, že musíme hlídat dvě speciální položky,
         * ‹.› a ‹..›, které nejsou stromové – ta první odkazuje na
         * adresář samotný, ta druhá na adresář rodičovský.
         * Kdybychom tyto položky zpracovali, v obou případech
         * bychom skončili v nekonečné smyčce – proto je musíme
         * přeskočit. */

        if ( strcmp( ptr->d_name, "." ) == 0 ||
             strcmp( ptr->d_name, ".." ) == 0 )
            continue;

        /* Nyní získáme informace o i-uzlu. Budou nás zajímat pouze
         * podsložky, proto všechny ostatní typy souborů přeskočíme. */

        if ( fstatat( root_fd, ptr->d_name, &st,
                      AT_SYMLINK_NOFOLLOW ) == -1 )
            goto out;

        if ( !S_ISDIR( st.st_mode ) )
            continue;

        /* Nyní víme, že ‹ptr› popisuje skutečnou podsložku, kterou
         * je potřeba rekurzivně zpracovat. Pro tento účel získáme
         * k této složce popisovač. Případné selhání řešíme na
         * začátku tohoto podprogramu – tato kontrola tedy proběhne
         * až uvnitř rekurzivní aktivace. Ukazatel na sdílený stav
         * ‹max_depth› předáváme beze změny, ale lokální počítadlo
         * hloubky zvýšíme o jedničku. */

        int sub_fd = openat( root_fd, ptr->d_name,
                             O_DIRECTORY | O_RDONLY );

        if ( tree_depth_rec( sub_fd, max_depth, depth + 1 ) == -1 )
            goto out;
    }

    rv = 0;
out:
    if ( dir )
        closedir( dir );

    return rv;
}

/* Rekurzi máme tímto vyřešenu, zbývá naprogramovat vstupní
 * podprogram. Ten je velmi jednoduchý – na začátku nastavíme
 * jak ‹max_depth› tak lokální hloubku ‹depth› na nulu a spustíme
 * rekurzi z předané složky ‹root_fd›.
 *
 * Také zde vyřešíme to, že ‹root_fd› je podprogramu ‹tree_depth›
 * pouze propůjčen a tedy ho nelze předat do ‹tree_depth_rec›, které
 * očekává popisovač, který si může (a musí) přivlastnit. K tomu
 * využijeme systémové volání ‹dup›, které vyrobí kopii popisovače,
 * která bude odkazovat na tentýž otevřený adresář (ale který lze
 * nezávisle zavřít). */

int tree_depth( int root_fd )
{
    int max_depth = 0;

    if ( tree_depth_rec( dup( root_fd ), &max_depth, 0 ) == -1 )
        return -1;

    return max_depth;
}

/* Dále již podprogram ‹tree_depth› pouze otestujeme. */

static int mkdir_or_die( int dir_fd, const char *name )
{
    int fd;

    if ( mkdirat( dir_fd, name, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating directory %s", name );
    if ( ( fd = openat( dir_fd, name, O_DIRECTORY ) ) == -1 )
        err( 1, "opening newly created directory %s", name );

    return fd;
}

int main( void ) /* demo */
{
    int fds[ 5 ];

    fds[ 0 ] = mkdir_or_die( AT_FDCWD, "zt.d2_root" );
    fds[ 1 ] = mkdir_or_die( fds[ 0 ], "a" );
    fds[ 2 ] = mkdir_or_die( fds[ 0 ], "b" );
    fds[ 3 ] = mkdir_or_die( fds[ 2 ], "c" );
    fds[ 4 ] = mkdir_or_die( fds[ 3 ], "d" );

    assert( tree_depth( fds[ 0 ] ) == 3 );
    assert( tree_depth( fds[ 1 ] ) == 0 );
    assert( tree_depth( fds[ 2 ] ) == 2 );
    assert( tree_depth( fds[ 3 ] ) == 1 );
    assert( tree_depth( fds[ 4 ] ) == 0 );

    for ( int i = 0; i < 5; ++i )
        close( fds[ i ] );

    return 0;
}
