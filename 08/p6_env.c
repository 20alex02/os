#define _POSIX_C_SOURCE 200809L

#include <string.h>     /* strlen, strcmp */
#include <unistd.h>     /* pipe, read, dup2, fork */
#include <stdlib.h>     /* exit */
#include <assert.h>
#include <err.h>

/* Napište podprogram ‹with_env›, který obdrží:
 *
 *  • příkaz, který spustí (příslušný spustitelný soubor najde
 *    pomocí proměnné prostředí ‹PATH›),
 *  • pole ukazatelů ‹argv› se standardním významem,
 *  • ukazatel na hlavu zřetězeného seznamu, který popisuje změny
 *    v proměnných prostředí, které má provést (definici příslušné
 *    struktury naleznete níže).
 *
 * Je-li ‹value› nulový ukazatel, znamená to, že tato proměnná má
 * být z prostředí zcela odstraněna. Je-li stejná proměnná v seznamu
 * vícekrát, použije se nastavení podle posledního výskytu.
 *
 * V případě úspěchu se podprogram nevrátí (předá řízení novému
 * programu), jinak vrátí hodnotu -1. V případě chyby není určeno,
 * v jakém stavu bude zanecháno prostředí. */

struct env_var
{
    const char *name;
    const char *value;
    struct env_var *next;
};

int with_env( const char *cmd, char *const argv[],
              struct env_var *env );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>   /* waitpid */

static int fork_env( const char *cmd, char * const argv[],
                     struct env_var *env,
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
        with_env( cmd, argv, env );
        exit( 1 );
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
            return -1;
    }

    abort();
}

int main()
{
    int explen;
    const char *expect;
    char buffer[ 100 ];

    char * const
        argv_foo[] = { "sh", "-c", "env | grep FOO_BAR; exit 0", NULL };
    char * const
        argv_bar[] = { "sh", "-c", "echo $FOO", NULL };

    struct env_var
        var_3 = { "FOO_BAR", NULL, NULL },
        var_2 = { "FOO_BAR", "x", &var_3 },
        var_1 = { "FOO", "y", &var_2 };

    assert( fork_env( "sh", argv_foo, &var_1,
            buffer, sizeof buffer ) == 0 );

    expect = "y\n";
    explen = strlen( expect );

    assert( fork_env( "sh", argv_bar, &var_1,
            buffer, sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    assert( with_env( "does not exist", argv_foo, &var_1 ) == -1 );

    return 0;
}
