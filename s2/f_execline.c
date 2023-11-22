#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <string.h>     /* strcmp */
#include <stdlib.h>     /* exit */
#include <unistd.h>     /* close, unlink, fork */
#include <fcntl.h>      /* open */
#include <errno.h>      /* errno */

/* Z předchozího studia znáte shell. Základní operací shellu je
 * vytvoření nového procesu (voláním ‹fork›), typicky následované
 * spuštěním nového programu v tomto procesu (voláním ‹exec›),
 * přičemž rodičovský proces pouze čeká na ukončení potomka.
 *
 * Toto uspořádání má samozřejmě řadu výhod, není ale pro jednoduché
 * skripty příliš efektivní – neustálé vytváření nových procesů
 * vyžaduje od systému značné množství práce. Přitom v situaci, kdy
 * nepotřebujeme žádnou souběžnost, bychom si teoreticky měli
 * vystačit s jediným procesem. To má svá vlastní úskalí, ale pro
 * jednoduché posloupnosti operací si bez větších problémů vystačíme
 * s opakovaným voláním ‹exec› v jediném procesu (tzn. bez použití
 * volání ‹fork›).
 *
 * Vaším úkolem bude naprogramovat malou sadu utilit (‹cd›, ‹cat› a
 * ‹test›), které umožní sestavovat jednoduché skripty v tomto
 * stylu. Každá utilita bude realizována jednou procedurou, níže
 * připojený ‹main› se pak postará, aby se spustila ta správná
 * procedura podle toho, jak byl přeložený program spuštěn. Pro
 * účely testování tedy stačí pro každou proceduru vytvořit měkký
 * odkaz daného jména:
 *
 *     $ ln -s f_execline cd
 *     $ ln -s f_execline cat
 *     $ ln -s f_execline test
 *
 * Utility pak můžete testovat např takto:
 *
 *     env PATH=$(pwd):$PATH test -L cd echo yes ; echo no
 *
 * Následují jednotlivé programy. Elipsa (‹…›) v popisu utility
 * značí „zbytek příkazu“, tedy parametry, které samotná utilita
 * nezpracovala a tvoří tak příkaz, který spustí voláním ‹execvp›.
 */

/* Program ‹cd {cesta} …› změní pracovní složku podle prvního
 * parametru a zbytek příkazového řádku spustí ve stávajícím
 * procesu. Uvažme např. příkaz ‹cd / ls -hl› – ten uvedené šabloně
 * odpovídá takto:
 *
 *  • ‹cd ≡ cd›,
 *  • ‹{cesta} ≡ /›,
 *  • ‹… ≡ ls -hl›.
 *
 * Po nastavení pracovní složky na ‹dir› tedy spustíme ‹ls -hl›. */

void cd( int argc, char **argv );

/* Program ‹cat {popisovač} {soubor} …› přesměruje zadaný popisovač
 * na zadaný soubor (pro jednoduchost vždy otevíráme soubor pro
 * čtení a připojující zápis), poté spustí zbytek příkazového řádku.
 * Popisovač může být zadán číselně, nebo jako slovo ‹stdin›, ‹stdout›
 * a ‹stderr›. Např.
 *
 *     cat stdout out.txt grep pomeranč in.txt
 *     cat stdout out.txt cat stdin in.txt grep pomeranč
 *
 * jsou ekvivalenty klasických shellových příkazů
 *
 *     grep pomeranč in.txt >> out.txt
 *     grep pomeranč < in.txt >> out.txt
 */

void cat( int argc, char **argv );

/* Konečně program ‹test {přepínač} {cesta} … ; …› je zvláštní
 * v tom, že může pokračovat dvěma různými příkazy, podle výsledku
 * testu. Zde ‹přepínač› je jedno z ‹-f›, ‹-d›, ‹-s› nebo ‹-L› ve
 * stejném významu jako u POSIXové utility ‹test› (obyčejný soubor,
 * složka, neprázdný soubor a měkký odkaz).¹ Je-li výsledek
 * pozitivní, provede se zbytek příkazu ‹…› od začátku až k prvnímu
 * středníku. Je-li výsledek negativní, provedený příkaz začíná po
 * prvním středníku, např:
 *
 *     test -f soubor rm soubor ; test -d soubor rmdir soubor
 *
 * odpovídá klasickému shellu:
 *
 *     if test -f soubor; then
 *         rm soubor
 *     elif test -d soubor; then
 *         rmdir soubor
 *     fi
 */

void test( int argc, char **argv );

/* ¹ Věnujte pozornost tomu, jak se POSIXová utilita chová, je-li
 *   soubor symbolickým odkazem, při jiných testech než ‹-L›.
 */

/* Pro všechny utility platí, že je-li zbývající příkaz prázdný,
 * neprovede se nic a proces je ukončen. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <libgen.h>     /* basename */
#include <sys/wait.h>   /* waitpid */
#include <sys/stat.h>   /* mkdir */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int reap( pid_t pid )
{
    int status;

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "waitpid %d", pid );

    if ( WIFEXITED( status ) )
        return WEXITSTATUS( status );
    else
        return -1;
}

static int file_contains( const char *path, const char *content )
{
    int fd = open( path, O_RDONLY );
    if ( fd == -1 )
        err( 2, "opening %s for reading", path );

    char buf[ 256 ];
    ssize_t bytes_read = read( fd, buf, 255 );
    if ( bytes_read == -1 )
        err( 2, "reading from %s", path );

    close_or_warn( fd, path );

    buf[ bytes_read ] = '\0';
    return strcmp( buf, content ) == 0;
}

static int exec_fun( void ( *fun )( int, char ** ), int argc, char **argv )
{
    pid_t pid = fork();
    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        fun( argc, argv );
        exit( 0 );
    }

    return reap( pid );
}

static int exec_test( char *flag, char *file )
{
    char *argv[ 13 ] = { "test", flag, file,
                         "sh", "-c", "echo \"y$@\" >> zt.f_out", "--", ";",
                         "sh", "-c", "echo \"n$@\" >> zt.f_out", "--", NULL };
    return exec_fun( test, 12, argv );
}


static void unlink_if_exists( const char* file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", file );
}

static void clear_file( const char *file )
{
    if ( truncate( file, 0 ) == -1 )
        err( 2, "truncate %s", file );
}

int main( int argc, char **argv )
{
    /* Je-li program spuštěn pod jedním ze oněch tří jmen, předá
     * řízení příslušné podproceduře. */
    if ( argc > 0 )
    {
        const char *name = basename( argv[ 0 ] );

        if ( !strcmp( name, "cd" ) )
           return cd( argc, argv ), 0;
        if ( !strcmp( name, "cat" ) )
           return cat( argc, argv ), 0;
        if ( !strcmp( name, "test" ) )
           return test( argc, argv ), 0;
    }

    /* Jinak následují obvyklé testy. */

    if ( mkdir( "zt.f_dir", 0700 ) == -1 && errno != EEXIST )
        err( 2, "creating test directory" );

    /* Testy ‹cd› */

    unlink_if_exists( "zt.f_dir/mark" );

    char *cd_argv[ 5 ] = { "cd", "zt.f_dir", "touch", "mark", NULL };
    assert(( exec_fun( cd, 4, cd_argv ) == 0 ));
    cd_argv[ 2 ] = NULL;
    assert(( exec_fun( cd, 2, cd_argv ) == 0 ));

    assert(( unlink( "zt.f_dir/mark" ) == 0 ));

    /* Testy ‹cat› */

    unlink_if_exists( "zt.f_cat" );

    char *cat_out_argv[ 6 ] = { "cat", "stdout", "zt.f_cat",
                                "echo", "line", NULL };
    assert(( exec_fun( cat, 5, cat_out_argv ) == 0 ));
    cat_out_argv[ 1 ] = "1";
    assert(( exec_fun( cat, 5, cat_out_argv ) == 0 ));
    assert(( file_contains( "zt.f_cat", "line\nline\n" ) ));

    char *cat_in_argv[ 7 ] = { "cat", "stdin", "zt.f_cat",
                               "sh", "-c", "cat > zt.f_out", NULL };
    assert(( exec_fun( cat, 6, cat_in_argv ) == 0 ));
    assert(( file_contains( "zt.f_out", "line\nline\n" ) ));

    unlink_if_exists( "zt.f_cat" );

    /* Testy ‹test› */

    unlink_if_exists( "zt.f_out" );
    unlink_if_exists( "zt.f_link" );

    if ( symlink( "zt.f_out", "zt.f_link" ) == -1 )
        err( 2, "symlink zt.f_link" );

    assert(( exec_test( "-f", "zt.f_out" ) == 0 ));
    assert(( exec_test( "-f", "zt.f_out" ) == 0 ));
    assert(( exec_test( "-f", "zt.f_link" ) == 0 ));
    assert(( file_contains( "zt.f_out", "n\ny\ny\n" ) ));

    clear_file( "zt.f_out" );
    assert(( exec_test( "-d", "zt.f_out" ) == 0 ));
    assert(( exec_test( "-d", "zt.f_dir" ) == 0 ));
    assert(( file_contains( "zt.f_out", "n\ny\n" ) ));

    clear_file( "zt.f_out" );
    assert(( exec_test( "-s", "zt.f_out" ) == 0 ));
    assert(( exec_test( "-s", "zt.f_out" ) == 0 ));
    assert(( file_contains( "zt.f_out", "n\ny\n" ) ));

    clear_file( "zt.f_out" );
    assert(( exec_test( "-L", "zt.f_out" ) == 0 ));
    assert(( exec_test( "-L", "zt.f_link" ) == 0 ));

    assert(( file_contains( "zt.f_out", "n\ny\n" ) ));

    /* chybějící druhý příkaz */
    char *test_argv[ 9 ] = { "test", "-f", "zt.f_out", "sh", "-c",
                             "echo \"x$@\" >> zt.f_out", "--", ";", NULL };
    clear_file( "zt.f_out" );
    assert(( exec_fun( test, 8, test_argv ) == 0 ));
    /* chybějící středník */
    test_argv[ 7 ] = NULL;
    assert(( exec_fun( test, 7, test_argv ) == 0 ));
    assert(( file_contains( "zt.f_out", "x\nx\n" ) ));

    unlink_if_exists( "zt.f_out" );
    unlink_if_exists( "zt.f_link" );

    assert(( rmdir( "zt.f_dir" ) == 0 ));
}
