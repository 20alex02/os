#define _POSIX_C_SOURCE 200809L
#include <sys/wait.h>

/* Jistě znáte konstrukci shellu ‹příkaz₁ | příkaz₂›, která
 * přesměruje standardní výstup jednoho příkazu na standardní vstup
 * jiného. Vaším úkolem bude implementovat podprogram ‹pipe_files›,
 * který realizuje stejný koncept, konkrétně ve tvaru
 *
 *     příkaz₁ < soubor₁ | příkaz₂ > soubor₂
 *
 * Parametry:
 *
 *  • ‹cmd_1› a ‹cmd_2› jsou názvy spustitelných souborů, které
 *    podprogram vyhledá podle proměnné prostředí ‹PATH›,
 *  • ‹argv_1› a ‹argv_2› jsou příslušná pole argumentů, které těmto
 *    programům předá (se stejným významem, jaký jim přisuzuje
 *    systémové volání ‹execvp›),
 *  • ‹file_1› a ‹file_2› jsou cesty k souborům – ‹file_1› je
 *    vstupní soubor pro ‹cmd_1› a ‹file_2› je výstupní soubor pro
 *    ‹cmd_2›,
 *  • do výstupních parametrů ‹status_1› a ‹status_2› zapíše
 *    výsledky příslušných programů (ve stejném formátu, jaký
 *    používá systémové volání ‹waitpid› – bude tedy možné na tyto
 *    hodnoty použít makra ‹WIFEXITED›, ‹WEXITSTATUS› atp.).
 *
 * Návratová hodnota -1 značí chybu, nula znamená, že vše proběhlo
 * v pořádku. Nenulový výsledek některého programu nebo jeho
 * ukončení signálem za chybu nepovažujeme, ale selhání volání
 * ‹execvp› ano. Můžete nicméně předpokládat, že návratový kód obou
 * programů bude menší než 127. */

int pipe_files( const char *cmd_1, char * const argv_1[],
                const char *cmd_2, char * const argv_2[],
                const char *file_1, const char *file_2,
                int *status_1, int *status_2 );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <err.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <limits.h>

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink" );
}

static void mk_file( const char* file, const char* content )
{
    int fd = open( file, O_WRONLY | O_CREAT, 0777 );
    if ( fd == -1 )
        err( 2, "open" );
    if ( write( fd, content, strlen( content ) ) == -1 )
        err( 2, "write" );
    close( fd );
}

static int filecmp( const char* file, const char* expected )
{
    int fd = open( file, O_RDONLY );
    assert( fd != -1 );

    off_t sz = lseek( fd, 0, SEEK_END );

    if ( sz == -1 || lseek( fd, 0, SEEK_SET ) == -1 )
        err( 2, "lseek" );

    char buf[ sz ];
    memset( buf, 0, sz );

    assert( read( fd, buf, sz ) != -1 );
    close( fd );

    return sz == ( int ) strlen( expected ) &&
           memcmp( buf, expected, strlen( expected ) ) == 0 ? 0 : -1;
}

int main( void )
{
    int status1, status2;
    char * const argv_wc[]  = { "wc", "-l", NULL };
    char * const argv_cat[] = { "cat", NULL };
    char * const argv_sed[] = { "sed", "-e", "s, ,,g", NULL };
    char * const argv_sed_file[] = { "sed", "-e", "s, ,,g",
                                     "zt.p4_pipe_in", NULL };
    char * const argv_empty[] = { NULL };
    char * const argv_false[] = { "false", NULL };

    unlink_if_exists( "zt.p4_pipe_in" );
    unlink_if_exists( "zt.p4_pipe_out" );
    mk_file( "zt.p4_pipe_in", "test\n" );

    assert( pipe_files( "wc", argv_wc, "sed", argv_sed,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == 0 );
    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) == 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) == 0 );
    assert( filecmp( "zt.p4_pipe_out", "1\n" ) == 0 );

    unlink_if_exists( "zt.p4_pipe_in" );
    unlink_if_exists( "zt.p4_pipe_out" );
    mk_file( "zt.p4_pipe_in", "hello\nworld\nthree\n" );

    assert( pipe_files( "wc", argv_wc, "sed", argv_sed,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == 0 );
    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) == 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) == 0 );
    assert( filecmp( "zt.p4_pipe_out", "3\n" ) == 0 );

    /* no unlinking */

    mk_file( "zt.p4_pipe_in", "hello\nworld\nthree\n" );
    assert( pipe_files( "wc", argv_wc, "sed", argv_sed,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == 0 );
    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) == 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) == 0 );
    assert( filecmp( "zt.p4_pipe_out", "3\n" ) == 0 );

    /* first command does not exits, exec* fails */
    assert( pipe_files( "hello", argv_empty, "sed", argv_sed,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == -1 );

    /* same, but the second one */
    assert( pipe_files( "wc", argv_wc, "world", argv_empty,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == -1 );

    /* input file does not exist */
    unlink_if_exists( "zt.p4_pipe_in" );
    assert( pipe_files( "wc", argv_wc, "sed", argv_sed,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == -1 );

    /* first command runs, but does not succeed */
    /* it may print error message to stderr, that's okay */
    mk_file( "zt.p4_pipe_in", "mom?\n" );
    assert( pipe_files( "false", argv_false, "sed", argv_sed_file,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == 0 );

    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) != 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) == 0 );

    /* same, but the second one */
    assert( pipe_files( "wc", argv_wc, "false", argv_false,
                       "zt.p4_pipe_in", "zt.p4_pipe_out",
                       &status1, &status2 ) == 0 );
    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) == 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) != 0 );

    char buffer[ PIPE_BUF * 2 ];
    memset( buffer, 'x', sizeof buffer );
    buffer[ sizeof( buffer ) - 1 ] = 0;

    mk_file( "zt.p4_pipe_in", buffer );

    assert( pipe_files( "cat", argv_cat, "cat", argv_cat,
                        "zt.p4_pipe_in", "zt.p4_pipe_out",
                        &status1, &status2 ) == 0 );
    assert( WIFEXITED( status1 ) && WEXITSTATUS( status1 ) == 0 );
    assert( WIFEXITED( status2 ) && WEXITSTATUS( status2 ) == 0 );
    assert( filecmp( "zt.p4_pipe_out", buffer ) == 0 );

    return 0;
}
