#define _POSIX_C_SOURCE 200809L

#include <string.h>     /* strlen, memcmp */
#include <unistd.h>     /* pipe, read, dup2, fork, mkdir */
#include <stdlib.h>     /* exit */
#include <sys/stat.h>   /* mkdir */
#include <fcntl.h>      /* openat, O_* */
#include <errno.h>
#include <assert.h>
#include <err.h>

/* Napište podprogram ‹with_file›, který přijme:
 *
 *  • příkaz, který má v aktuálním procesu spustit (tento vyhledá
 *    v proměnné prostředí ‹PATH›),
 *  • pole ukazatelů ‹argv› se standardním významem,
 *  • popisovač otevřené složky ‹dir_fd›,
 *  • 3 jména souborů v této složce, která se použijí jako
 *    standardní vstup a výstup a chybový výstup pro spuštěný
 *    program.
 *
 * Neexistují-li soubory pro výstup, podprogram je vytvoří, jinak je
 * přepíše. Chování není určeno, jsou-li libovolná dvě jména stejná.
 * Při chybě volání ‹dup2› není určeno, v jakém stavu budou po
 * chybovém návratu z ‹with_files› popisovače standardního vstupu,
 * výstupu a chybového výstupu.
 *
 * Výsledkem budiž:
 *
 *  • v případě úspěchu se podprogram nevrátí,
 *  • hodnota -1 v případě chyby, která znemožní spuštění příkazu,
 *  • hodnota -2 nepodaří-li se otevřít některý soubor nebo
 *    přesměrovat vstup/výstup. */

int with_files( const char *cmd, char * const argv[], int dir_fd,
                const char *in, const char *out, const char *err );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>   /* waitpid */

static int fork_wf( const char *cmd, char * const args[], int dir_fd,
                    const char *a, const char *b, const char *c )
{
    pid_t pid = fork();

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        int rv = with_files( cmd, args, dir_fd, a, b, c );
        close( dir_fd );
        exit( -rv );
    }

    if ( pid > 0 )
    {
        int status;

        if ( waitpid( pid, &status, 0 ) == -1 )
            err( 2, "wait" );

        if ( WIFEXITED( status ) )
            return -WEXITSTATUS( status );
        else
            return -3;
    }

    abort();
}

static void create_file( int dir_fd, const char *file,
                         const char *content, int mode )
{
    int fd = openat( dir_fd, file,
                     O_CREAT | O_TRUNC | O_RDWR, mode );

    if ( fd == -1 )
        err( 2, "creating %s", file );

    if ( write( fd, content, strlen( content ) ) == -1 )
        err( 2, "writing %s", file );

    close( fd );
}

static int read_file( int dir_fd, const char *file,
                      char *buf, int len )
{
    int fd = openat( dir_fd, file, O_RDONLY );

    if ( fd == -1 )
        err( 2, "opening %s", file );

    int bytes = read( fd, buf, len );

    if ( bytes == -1 )
        err( 2, "reading %s", file );

    close( fd );
    return bytes;
}

int main()
{
    const char *dir_name = "zt.p5_dir";
    int dir_fd;

    int explen;
    const char *expect;
    char buffer[ 100 ];

    if ( mkdir( dir_name, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating %s", dir_name );

    if ( ( dir_fd = open( dir_name, O_DIRECTORY | O_CLOEXEC ) ) == -1 )
        err( 1, "opening %s", dir_name );

    create_file( dir_fd, "input_1", "foo\n", 0666 );
    create_file( dir_fd, "input_2", "foo\nbar\n", 0666 );

    char * const argv_cat[] = { "cat", NULL };
    char * const argv_wc[]  = { "wc", "-l", NULL };
    expect = "foo\n";
    explen = strlen( expect );

    assert( fork_wf( "cat", argv_cat, dir_fd,
                     "input_1", "out_1", "err_1" ) == 0 );
    assert( read_file( dir_fd, "out_1", buffer,
                       sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    expect = "foo\nbar\n";
    explen = strlen( expect );

    assert( fork_wf( "cat", argv_cat, dir_fd,
                     "input_2", "out_2", "err_2" ) == 0 );
    assert( read_file( dir_fd, "out_2", buffer,
                       sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    assert( fork_wf( "wc", argv_wc, dir_fd,
                    "input_2", "out_3", "err_3" ) == 0 );
    int bytes = read_file( dir_fd, "out_3", buffer, sizeof buffer );
    assert( bytes > 0 );
    assert( memchr( buffer, '2', bytes ) );

    assert( fork_wf( "wc", argv_wc, dir_fd,
                    "input_2", ".", "err_3" ) == -2 );
    assert( fork_wf( "does not exist", argv_wc, dir_fd,
                    "input_2", "out_4", "err_4" ) == -1 );

    close( dir_fd );
    return 0;
}
