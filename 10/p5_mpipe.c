#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <errno.h>      /* errno, ENOENT */
#include <err.h>        /* err, warn */
#include <unistd.h>     /* write, close, unlink, dup2 */
#include <fcntl.h>      /* open, O_* */
#include <sys/wait.h>   /* WIFEXITED, WEXITSTATUS */

/* Jistě znáte konstrukci shellu ‹příkaz₁ | příkaz₂›, která
 * přesměruje standardní výstup jednoho příkazu na standardní vstup
 * jiného. Vaším úkolem bude implementovat podprogram
 * ‹metered_pipe›, který realizuje stejný koncept, ale zároveň
 * spočítá, kolik bajtů bylo takto vytvořenou rourou přeneseno.
 *
 * Parametry:
 *
 *  • ‹cmd_1› a ‹cmd_2› jsou názvy spustitelných souborů, které
 *    podprogram vyhledá podle proměnné prostředí ‹PATH›,
 *  • ‹argv_1› a ‹argv_2› jsou příslušná pole argumentů, které těmto
 *    programům předá (se stejným významem, jaký jim přisuzuje
 *    systémové volání ‹execve›),
 *  • do výstupních parametrů ‹status_1› a ‹status_2› zapíše
 *    výsledky příslušných programů (ve stejném formátu, jaký
 *    používá systémové volání ‹waitpid› – bude tedy možné na tyto
 *    hodnoty použít makra ‹WIFEXITED›, ‹WEXITSTATUS› atp.).
 *
 * Návratová hodnota -1 značí chybu, nezáporné číslo pak počet bajtů,
 * které byly rourou přeneseny. Nenulový výsledek některého programu
 * nebo jeho ukončení signálem za chybu nepovažujeme, ale selhání
 * volání ‹execvp› ano. Můžete nicméně předpokládat, že návratový kód
 * obou programů bude menší než 127. Chybou rovněž není, pokud druhý
 * program přestane číst a z toho (a právě z toho) důvodu selže zápis
 * do roury. */

int metered_pipe( const char *cmd_1, char * const argv_1[],
                  const char *cmd_2, char * const argv_2[],
                  int *status_1, int *status_2 );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <signal.h>     /* signal, SIGPIPE, SIG_IGN */
#include <string.h>     /* strlen */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void silence_stdout()
{
    int fd = open( "/dev/null", O_WRONLY );
    if ( fd == -1 )
        err( 2, "open /dev/null" );

    if ( dup2( fd, STDOUT_FILENO ) == -1 )
        err( 2, "dup" );

    close_or_warn( fd, "/dev/null" );
}

int main( void )
{
    /* Standardní výstup přesměrováváme do černé díry, aby výstup
     * z programů spouštěných jako součást testů nepronikal na
     * terminál. Chcete-li si přidávat ladicí výpisy, směřujte je
     * na standardní «chybový» výstup. */

    silence_stdout();

    /* Při pokusu o zápis do roury, jejíž čtecí konec nemá nikdo
     * otevřen, obdrží zapisující proces signál SIGPIPE. Ten proces
     * ukončí, není-li řečeno jinak. Zde proto říkáme jinak: signál
     * nechť se ignoruje a místo toho příslušný ‹write› vrátí -1
     * a nastaví ‹errno› na hodnotu ‹EPIPE›. */

    if ( signal( SIGPIPE, SIG_IGN ) == SIG_ERR )
        err( 2, "signal" );

    char *argv_input[] = { "sh", "-c",
                           "echo pojdme vsichni na ircik 2>/dev/null", NULL };
    char *argv_cat_stdin[] = { "cat", NULL };
    char *argv_head_stdin[] = { "head", "-n", "5", NULL };
    char *argv_false[] = { "false", NULL };
    char *argv_infinite[] = { "sh", "-c",
                              "while echo lol 2> /dev/null; do :; done", NULL };

    int status_1,
        status_2;

    assert( 24 == metered_pipe( "sh", argv_input,
                                "cat", argv_cat_stdin,
                                &status_1, &status_2 ) );
    assert( WIFEXITED( status_1 ) );
    assert( WIFEXITED( status_2 ) );
    assert( WEXITSTATUS( status_1 ) == 0 );
    assert( WEXITSTATUS( status_2 ) == 0 );

    assert( 20 <= metered_pipe( "sh", argv_infinite,
                                "head", argv_head_stdin,
                                &status_1, &status_2 ) );
    assert( WIFEXITED( status_1 ) );
    assert( WIFEXITED( status_2 ) );
    assert( WEXITSTATUS( status_1 ) == 0 );
    assert( WEXITSTATUS( status_2 ) == 0 );

    assert( 0 == metered_pipe( "false", argv_false,
                               "cat", argv_cat_stdin,
                               &status_1, &status_2 ) );
    assert( WIFEXITED( status_1 ) );
    assert( WIFEXITED( status_2 ) );
    assert( WEXITSTATUS( status_1 ) == 1 );
    assert( WEXITSTATUS( status_2 ) == 0 );

    assert( -1 == metered_pipe( "/no/such/program", argv_false,
                                "cat", argv_cat_stdin,
                                &status_1, &status_2 ) );

    assert( -1 == metered_pipe( "sh", argv_input,
                                "/no/such/program", argv_false,
                                &status_1, &status_2 ) );

    return 0;
}
