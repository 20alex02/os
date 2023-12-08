#define _POSIX_C_SOURCE 200809L

#include <string.h>         /* strlen, memcmp */
#include <assert.h>         /* assert */
#include <unistd.h>         /* pipe, read, dup2, fork */
#include <stdlib.h>         /* exit */
#include <sys/stat.h>       /* S_* */
#include <fcntl.h>          /* openat, O_* */
#include <errno.h>
#include <err.h>

/* Napište podprogram ‹uexec›, který přijme:
 *
 *  • popisovač otevřeného adresáře,
 *  • název souboru, který má spustit,
 *  • pole ukazatelů ‹argv› se standardním významem.
 *
 * Podprogram nejprve ověří, že soubor nemá nastavený příznak
 * ‹setgid› ani ‹setuid› (podobně jako jiná práva, lze tyto příznaky
 * zjistit z položky ‹st_mode› struktury ‹stat›, konkrétně pomocí
 * maker ‹S_ISUID› a ‹S_ISGID›).
 *
 * Nastavení pracovní složky spuštěného programu nehraje roli.
 * Prostředí nechť zůstane beze změn.
 *
 * Výsledkem budiž:
 *
 *  • v případě úspěchu se podprogram nevrátí,
 *  • hodnota -1 v případě chyby, která znemožní spuštění příkazu
 *    nebo zjištění práv,
 *  • hodnota -2 byl-li soubor nalezen, ale má nastavený příznak
 *    ‹setuid› nebo ‹setgid› a proto nebyl spuštěn. */

int uexec( int dir_fd, const char *name, char * const argv[] );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>   /* waitpid */

static int fork_uexec( int dir_fd, const char *name,
                       char * const argv[],
                       char *output, int out_len )
{
    int fds[ 2 ];

    if ( pipe( fds ) == -1 )
        err( 1, "pipe" );

    pid_t pid = fork();

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        dup2( fds[ 1 ], 1 );
        close( fds[ 0 ] );
        close( fds[ 1 ] );
        int rv = uexec( dir_fd, name, argv );
        close( dir_fd );
        exit( -rv );
    }

    if ( pid > 0 )
    {
        close( fds[ 1 ] );
        int status, bytes = 0, total = 0;

        while ( ( bytes = read( fds[ 0 ], output + total,
                                          out_len - total ) ) > 0 )
            total += bytes;

        close( fds[ 0 ] );

        if ( bytes == -1 )
            err( 2, "reading from pipe" );

        if ( waitpid( pid, &status, 0 ) == -1 )
            err( 2, "wait" );

        if ( WIFEXITED( status ) && WEXITSTATUS( status ) == 0 )
            return total;
        else
            return -WEXITSTATUS( status );
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

int main()
{
    const char *dir_name = "zt.p4_dir";
    int dir_fd;

    int explen;
    const char *expect;
    char buffer[ 100 ];

    if ( mkdir( dir_name, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating %s", dir_name );

    if ( ( dir_fd = open( dir_name, O_DIRECTORY | O_CLOEXEC ) ) == -1 )
        err( 1, "opening %s", dir_name );

    create_file( dir_fd, "prog_1", "#!/bin/sh\necho hello", 0777 );
    create_file( dir_fd, "prog_2", "#!/bin/sh\necho nope", 0777 );

    if ( fchmodat( dir_fd, "prog_2", 0777 | S_ISUID, 0 ) == -1 )
        err( 2, "chmod failed" );

    char * const argv[] = { "foo", NULL };
    expect = "hello\n";
    explen = strlen( expect );

    assert( fork_uexec( dir_fd, "prog_1", argv,
                        buffer, sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );
    assert( fork_uexec( dir_fd, "prog_2", argv,
                        buffer, sizeof buffer ) == -2 );
    assert( fork_uexec( dir_fd, "not there", argv,
                        buffer, sizeof buffer ) == -1 );

    close( dir_fd );
    return 0;
}
