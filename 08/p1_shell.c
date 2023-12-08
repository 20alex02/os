#define _POSIX_C_SOURCE 200809L

#include <string.h>         /* strlen, strcmp */
#include <assert.h>         /* assert */
#include <unistd.h>         /* pipe, read, dup2, fork */
#include <stdlib.h>         /* exit */
#include <err.h>

/* Napište podprogram ‹exec_shell›, který obdrží jednoduchý shellový
 * program v podobě struktury ‹shell_cmd› definované níže a tento
 * příkaz spustí pomocí systémového shellu.
 *
 * Výsledkem budiž:
 *
 *  • v případě úspěchu se podprogram nevrátí (předá řízení shellu),
 *  • hodnota -1 v případě chyby, která znemožní spuštění příkazu,
 *  • hodnota -2 je-li vstupní příkaz špatně utvořený (viz níže). */

struct shell_arg
{
    const char *value;
    struct shell_arg *next;
};

struct shell_cmd
{
    /* Nulou ukončený řetězec ‹command› je název příkazu, který se
     * má spustit. Může se jednat o zabudovaný příkaz shellu.
     * Obsahuje-li příkaz nějaký metaznak,¹ tento musí být shellu
     * předán tak, aby pozbyl svého speciálního významu. Pro
     * jednoduchost, obsahuje-li příkaz apostrof (jednoduchou
     * uvozovku), jedná se o chybu. */

    const char *command;

    /* Zřetězený seznam parametrů, které se mají příkazu ‹command›
     * předat. Podobně jako u příkazů, metaznaky nesmí mít speciální
     * význam a výskyt jednoduché uvozovky považujeme za chybu. */

    struct shell_arg *args;

    /* Stávající příkaz ukončete středníkem a v shellovém programu
     * pokračujte dalším příkazem. */

    struct shell_cmd *next;
};

int exec_shell( struct shell_cmd *cmd );

/* ¹ Metaznakem se zde myslí libovolný znak se speciálním významem.
 *   Jejich seznam a možné způsoby uvození naleznete např.
 *   v sekci 2.2 svazku „Shell and Utilities“ standardu POSIX. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>   /* waitpid */

static int fork_shell( struct shell_cmd *cmd,
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
        exec_shell( cmd );
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
    const char *expect;
    int explen;
    char buffer[ 100 ];

    struct shell_arg hello = { "hello  world", NULL };
    struct shell_cmd echo  = { "echo", &hello, NULL };
    expect = "hello  world\n";
    explen = strlen( expect );

    assert( fork_shell( &echo, buffer, sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    struct shell_cmd echo2 = { "echo", &hello, &echo };
    expect = "hello  world\nhello  world\n";
    explen = strlen( expect );

    assert( fork_shell( &echo2, buffer, sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    struct shell_cmd bad = { "ech'", NULL, NULL };
    assert( exec_shell( &bad ) == -2 );

    struct shell_arg aster = { "*", &hello };
    struct shell_cmd echo3 = { "echo", &aster, &echo };
    expect = "* hello  world\nhello  world\n";
    explen = strlen( expect );

    assert( fork_shell( &echo3, buffer, sizeof buffer ) == explen );
    assert( !memcmp( expect, buffer, explen ) );

    return 0;
}
