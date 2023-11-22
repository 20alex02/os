#define _POSIX_C_SOURCE 200809L

#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <unistd.h>     /* read, write, fork, close */
#include <sys/wait.h>   /* waitpid, W* */
#include <assert.h>
#include <err.h>

/* V této ukázce navrhneme jednoduchý podprogram, který přečte
 * veškerá data z bajtově orientovaného socketu (případně roury) a
 * vrátí celkový počet přečtených bajtů. */

int count( int fd )
{
    const int nbytes = 256;
    int bytes_read, total = 0;
    char buffer[ nbytes ];

    /* Jak bylo zmíněno již v úvodu, «krátké čtení» je situace,
     * kterou musíme při bajtově orientované (proudové) komunikaci
     * řešit prakticky vždy. Zkuste si cyklus, ve kterém čtení
     * probíhá, nahradit jediným voláním ‹read›. Srovnejte chování
     * původního a upraveného programu. Srovnejte také výstup
     * z programu ‹strace -f›. */

    while ( 1 )
    {
        if ( ( bytes_read = read( fd, buffer, nbytes ) ) == -1 )
            return -1;

        /* Nulová návratová hodnota značí, že protistrana spojení
         * uzavřela, a žádná další data už doručena nebudou. */

        if ( bytes_read == 0 )
            return total;

        total += bytes_read;
    }
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

int main( void ) /* demo */
{
    int fds[ 2 ];
    int bytes, status;

    /* Všimněte si, že používáme spojovaný, proudový socket –
     * ‹SOCK_STREAM›. */

    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) == -1 )
        err( 1, "socketpair" );

    /* Voláním ‹fork› vytvoříme nový proces, který bude na socket
     * posílat data. V hlavním procesu budeme počítat bajty použitím
     * podprogramu ‹count›. */

    pid_t pid = fork();
    alarm( 5 ); /* die after 5 seconds if we get stuck */

    if ( pid == -1 )
        err( 1, "fork" );

    /* Potomek bude postupně zapisovat data do socketu. */

    if ( pid == 0 ) /* child */
    {
        close_or_warn( fds[ 1 ], "receiver side of a socketpair" );

        if ( write( fds[ 0 ], "hel", 3 ) == -1 ||
             write( fds[ 0 ], "lo ", 3 ) == -1 ||
             write( fds[ 0 ], "world", 5 ) == -1 )
        {
            err( 1, "writing to the socket" );
        }

        close_or_warn( fds[ 0 ], "sender side of a socketpair" );
        return 0;
    }

    close_or_warn( fds[ 0 ], "sender side of a socketpair" );

    /* Výše uvedený podprogram bude běžet až do chvíle, než
     * protistrana neuzavře spojení. Všimněte si, že přesto, že se
     * celá zpráva pohodlně vejde  */

    if ( ( bytes = count( fds[ 1 ] ) ) == -1 )
        err( 1, "counting bytes" );

    /* Ověříme, že počet přečtených bajtů odpovídá počtu odeslaných
     * bajtů. Při analýze programem ‹strace› si můžete všimnout, že
     * v tomto scénáři se ‹read› v podprogramu ‹count› zavolá celkem
     * 4× – jednou pro každý ‹write› výše a jednou po ukončení
     * komunikace, s nulovou návratovou hodnotou. «Pozor!» Toto
     * chování je sice obvyklé, ale není ničím a nijak zaručeno.
     * Navíc nemůžete předpokládat, že protistrana provedla nějaký
     * konkrétní zápis v jedné operaci – zprávy mohou být z různých
     * důvodů odesílatelem děleny na celky, které nijak neodpovídají
     * logické struktuře protokolu. */

    assert( bytes == 3 + 3 + 5 );

    /* Vyčkáme na ukončení procesu–zapisovatele, ověříme, že skončil
     * bez chyb a následně celý program ukončíme. */

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 1, "waiting for child %d", pid );

    assert( WIFEXITED( status ) );
    assert( WEXITSTATUS( status ) == 0 );

    close_or_warn( fds[ 1 ], "receiver side of a socketpair" );

    return 0;
}
