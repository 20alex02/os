#define _POSIX_C_SOURCE 200809L

#include <string.h>         /* strlen, strcmp */
#include <assert.h>         /* assert */
#include <unistd.h>         /* pipe, read, dup2, fork */
#include <stdlib.h>         /* exit */
#include <err.h>

/* Napište podprogram ‹execp›, který bude pracovat stejně jako
 * ‹execvp›¹ s tím rozdílem, že v poli ‹args› jsou mu předány pouze
 * skutečné parametry. Podprogram ‹execp› automaticky těmto
 * parametrům předřadí cestu k souboru, který bude spuštěn.
 *
 * Výsledkem budiž:
 *
 *  • v případě úspěchu se podprogram nevrátí,
 *  • hodnota -1 v případě chyby, která znemožní spuštění příkazu,
 *  • hodnota -2 nebyl-li soubor v cestě nalezen. */

int execp( const char *cmd, char * const args[] );

/* ¹ Použije se první soubor se jménem ‹cmd›, který je nalezen ve
 *   složce vyjmenované v proměnné prostředí ‹PATH›, bez ohledu na
 *   spustitelnost nebo formát tohoto souboru. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>   /* waitpid */

static int fork_execp( const char *cmd, char * const args[],
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
        execp( cmd, args );
        exit( 1 );
    }

    if ( pid > 0 )
    {
        close( fds[ 1 ] );
        int status, bytes = 0, total = 0;

        while ( ( bytes = read( fds[ 0 ], output + total,
                                          out_len - total ) ) > 0 )
            total += bytes;

        if ( bytes == -1 )
            err( 2, "reading from pipe" );

        close( fds[ 0 ] );

        if ( waitpid( pid, &status, 0 ) == -1 )
            err( 2, "wait" );

        if ( WIFEXITED( status ) && WEXITSTATUS( status ) == 0 )
            return total;
        else
            return -1;
    }

    abort();
}

int main()
{
    const char *expect;
    int explen;
    char buffer[ 100 ];

    char * const args[] = { "-c", "echo $0", NULL };
    expect = "/bin/sh\n";
    explen = strlen( expect );

    if ( setenv( "PATH", "/bin:/sbin", 1 ) == -1 )
        err( 2, "setenv PATH" );

    assert( fork_execp( "sh", args, buffer,
                        sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    assert( execp( "does not exist", args ) == -2 );

    return 0;
}
