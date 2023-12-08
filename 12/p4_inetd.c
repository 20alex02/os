#define _POSIX_C_SOURCE 200809L
#include <sys/wait.h>
#include <sys/socket.h> /* socketpair, recv */
#include <stdio.h>      /* dprintf */
#include <unistd.h>     /* close */
#include <string.h>     /* memcmp */
#include <fcntl.h>      /* fcntl, F_*, FD_* */
#include <err.h>
#include <errno.h>

/* Klasickým způsobem jak implementovat internetový server je pomocí
 * tzv. ‹inetd› – serveru, který poslouchá na socketu a pro každé
 * příchozí spojení spustí zadaný program v novém procesu. Tento
 * program, např. ‹smtpd›, ke komunikaci používá standardní vstup a
 * výstup – program ‹inetd› pak zařídí, že je tento vstup/výstup
 * přesměrován do socketu, který reprezentuje spojení s klientem.
 *
 * Vaším úkolem je naprogramovat tuto druhou část programu ‹inetd›,
 * tedy spuštění programu a přesměrování jeho vstupu a výstupu do
 * předaného socketu. Práci rozdělíme do dvou podprogramů,
 * ‹inetd_fork› (spustí proces) a ‹inetd_wait› (vyčká na jeho
 * ukončení).
 *
 * Parametry ‹inetd_fork›:
 *
 *  • ‹cmd› je název spustitelného souboru, který podprogram vyhledá
 *    podle proměnné prostředí ‹PATH›,
 *  • ‹argv› je pole argumentů, které tomuto programu předá (se
 *    stejným významem, jaký mu přisuzuje systémové volání
 *    ‹execvp›),
 *  • ‹fd› je popisovač připojeného proudového socketu.
 *
 * Vlastnictví popisovače ‹sock_fd› je v případě úspěchu podprogramu
 * ‹inetd_fork› přeneseno na nově spuštěný proces. Popisovač bude
 * tedy nejpozději před ukončením podprogramu ‹inetd_wait› uzavřen.
 *
 * Není-li možné vytvořit nový proces, výsledkem ‹inetd_fork› bude
 * nulový ukazatel. Jinak je výsledkem ‹handle›, který později
 * volající předá podprogramu ‹inetd_wait›. Výsledkem ‹inetd_wait›
 * je v případě úspěchu 0, jinak -1. */

void *inetd_fork( const char *cmd, char * const argv[], int fd );
int   inetd_wait( void * );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>

int main( void )
{
    int status;
    int fds[ 2 ];
    char buffer[ 24 ];

    char * const argv[] = { "sed", "-u", "-e", "s,x,y,g", NULL };

    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) == -1 )
        err( 1, "creating a socketpair" );

    if ( fcntl( fds[ 1 ], F_SETFD, FD_CLOEXEC ) == -1 )
        err( 1, "setting close on exec on a socket" );

    void *sed = inetd_fork( "sed", argv, fds[ 0 ] );

    assert( dprintf( fds[ 1 ], "hello\n" ) > 0 );
    assert( recv( fds[ 1 ], buffer, 6, MSG_WAITALL ) == 6 );
    assert( memcmp( buffer, "hello\n", 6 ) == 0 );

    assert( dprintf( fds[ 1 ], "xyzx\n" ) > 0 );
    assert( recv( fds[ 1 ], buffer, 5, MSG_WAITALL ) == 5 );
    assert( memcmp( buffer, "yyzy\n", 5 ) == 0 );

    if ( close( fds[ 1 ] ) == -1 )
        err( 1, "closing test end of the socket" );

    assert( inetd_wait( sed ) == 0 );
    return 0;
}
