#define _POSIX_C_SOURCE 200809L

#include <sys/socket.h> /* socketpair, send, recv */
#include <err.h>        /* err */
#include <poll.h>       /* poll, pollfd */
#include <stdint.h>     /* uint8_t */
#include <stdlib.h>     /* exit */
#include <unistd.h>     /* fork */
#include <arpa/inet.h>  /* htonl, ntohl */
#include <assert.h>
#include <signal.h>     /* kill, SIGTERM */
#include <sys/wait.h>   /* wait, W* */

/* V této ukázce se budeme zabývat systémovým voláním ‹poll› –
 * uvažme situaci, kdy máme dva popisovače, které oba slouží ke
 * komunikaci (pro účely ukázky použijeme datagramové sockety, ale
 * ‹poll› lze stejným způsobem používat i s jinými typy popisovačů).
 *
 * Podprogram ‹pong› bude odpovídat na zprávy na libovolném z obou
 * popisovačů, aniž by docházelo k prodlevám při odpovědi v situaci,
 * kdy komunikuje pouze jedna ze dvou připojených protistran. */

/* Samotnou komunikaci s protistranou delegujeme na pomocný
 * podprogram, recv_and_reply, který implementujeme níže. */

void recv_and_reply( int fd );

void pong( int fd_1, int fd_2 )
{
    /* Pro použití systémového volání ‹poll› budeme potřebovat pole
     * struktur typu ‹pollfd›, které popisují, na jaké podmínky
     * hodláme čekat. Volání ‹poll› bude ukončeno, jakmile nastane
     * libovolná z nich. Které podmínky nastaly pak přečteme ze
     * stejného pole – volání ‹poll› před návratem upraví předané
     * položky ‹revents›. */

    struct pollfd pfds[ 2 ];

    /* Každá instance ‹posllfd› se váže k jednomu popisovači:
     * položka ‹fd› určí tento popisovač, zatímco položka ‹events›
     * určí, jaké podmínky na tomto popisovači nás zajímají. To,
     * jestli podmínka platila ještě před voláním ‹poll› nebo začala
     * platit až později nerozhoduje (jak bylo zmíněno v úvodu,
     * ‹poll› poskytuje tzv. level-triggered rozhraní). */

    pfds[ 0 ].fd = fd_1;
    pfds[ 1 ].fd = fd_2;

    /* Podmínky nastavujeme položkou ‹events› – pro nás v tuto
     * chvíli nejdůležitější je ‹POLLIN›, která indikuje, že
     * z deskriptoru je možné «bez blokování» získat data, tzn.
     * «jedno» volání ‹recv› (nebo ‹read›) na tomto popisovači se
     * vrátí ihned a předá programu nějaká data (kolik dat to bude
     * nám volání ‹poll› nesdělí). */

    pfds[ 0 ].events = POLLIN;
    pfds[ 1 ].events = POLLIN;

    /* Položku ‹revents› inicializovat nemusíme – každé volání
     * ‹poll› ji přepíše aktuálním stavem popisovače. Tím je
     * nastavení parametrů pro ‹poll› u konce, můžeme tedy
     * přistoupit k hlavnímu cyklu:  */

    while ( 1 )
    {
        /* Bezpodmínečně zavolat ‹recv› na některém z popisovačů by
         * v tuto chvíli mohlo zablokovat program (případně i vést
         * k uváznutí). Proto musíme začít voláním ‹poll›. Krom
         * ukazatele na pole struktur ‹pollfd› mu předáme počet
         * položek tohoto pole a maximální dobu (počet milisekund),
         * po kterou hodláme čekat. V tomto předmětu to bude
         * prakticky vždy -1 – budeme čekat libovolně dlouho. */

        if ( poll( pfds, 2, -1 ) == -1 )
            err( 1, "poll" );

        /* Návratové hodnoty jiné než -1 nás nemusí v tuto chvíli
         * zajímat – veškeré informace, které potřebujeme, jsou
         * obsaženy v poli ‹pfds› v položkách ‹revents›. Samotné
         * odeslání  */

        for ( int i = 0; i < 2; ++i )
            if ( pfds[ i ].revents & POLLIN )
                recv_and_reply( pfds[ i ].fd );

        /* Tím je procedura ‹pong› hotova – bude čekat a odpovídat
         * na obou popisovačích souběžně, i přesto, že se jedná o na
         * první pohled zcela sekvenční program. Níže si tuto
         * funkčnost otestujeme. */
    }
}

void recv_and_reply( int fd )
{
    /* V tomto místě máme zajištěno, že ‹recv› na popisovači ‹fd›
     * nám vrátí nějakou zprávu. Nebudeme s ní dělat nic jiného, než
     * že protistraně pošleme velikost zprávy, kterou jsme obdrželi,
     * jako čtyřbajtové číslo. Omezíme se na 4096 bajtů – pro větší
     * zprávy odpovíme 4096. */

    uint8_t message[ 4096 ];
    int bytes = recv( fd, message, 4096, 0 );

    if ( bytes == -1 )
        err( 1, "recv" );

    /* Jak je v komunikačních protokolech zvykem, slovo (číslo)
     * odešleme v pořadí nejvýznamnější bajt první (big endian).
     * Využijeme k tomu knihovní funkci ‹htonl› («h»ost «to»
     * «n»etwork, «l»ong). */

    uint32_t reply = htonl( bytes );

    /* Nezbývá, než správu odeslat. Zde budeme trochu podvádět, a
     * spolehneme se na to, že odeslání proběhne okamžitě –
     * předpoklad, který není zcela neopodstatněný, protože
     * odesíláme malé zprávy a lze očekávat, že ve vyrovnávací
     * paměti operačního systému je pro zprávu dostatek místa. Ve
     * skutečném programu bychom ale na tomto místě měli použít
     * neblokující operaci v součinnosti s voláním ‹poll› – tím se
     * budeme ale až zabývat příště. */

    if ( send( fd, &reply, 4, 0 ) == -1 )
        err( 1, "send" );

    /* Protože odesíláme datagram, situace, že bude odesláno méně
     * bajtů než bylo požadováno nastat nemůže (jednalo by se
     * o chybu a výsledek by byl -1). Samozřejmě, můžete si tuto
     * skutečnost pojistit (jestli voláním ‹assert› nebo ‹errx› je
     * otázka osobních preferencí). */
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

int main( void ) /* demo */
{
    int fds_1[ 2 ], fds_2[ 2 ];

    if ( socketpair( AF_UNIX, SOCK_DGRAM, 0, fds_1 ) == -1 ||
         socketpair( AF_UNIX, SOCK_DGRAM, 0, fds_2 ) == -1 )
    {
        err( 1, "socketpair" );
    }

    /* Voláním ‹fork› vytvoříme nový proces, který bude sloužit jako
     * testovací server – spustíme v něm proceduru ‹pong›. Hlavní
     * proces bude tento server testovat posíláním zpráv. */

    pid_t pid = fork();
    alarm( 5 ); /* die after 5 seconds if we get stuck */

    if ( pid == -1 )
        err( 1, "fork" );

    if ( pid == 0 ) /* child */
    {
        close_or_warn( fds_1[ 1 ], "client side of a socketpair" );
        close_or_warn( fds_2[ 1 ], "client side of a socketpair" );
        pong( fds_1[ 0 ], fds_2[ 0 ] );
        exit( 1 ); /* should never return */
    }

    close_or_warn( fds_1[ 0 ], "server side of a socketpair" );
    close_or_warn( fds_2[ 0 ], "server side of a socketpair" );

    int fd_1 = fds_1[ 1 ],
        fd_2 = fds_2[ 1 ];

    uint32_t reply_1, reply_2;

    /* Zde zvolené (nebo analogické) pořadí ‹send›/‹recv› by
     * v případě, že ‹pong› nepracuje správně, vedlo k uváznutí.
     * Zkuste si zakomentovat volání ‹poll› výše a program spustit
     * v této verzi. Ověřte si také, že v pořadí send 1, send 2,
     * recv 1, recv 2 program pracuje i bez volání ‹poll›.
     * Rozmyslete si, proč tomu tak je.  */

    if ( send( fd_1, "hello", 5, 0 ) == -1 )
        err( 1, "sending hello" );
    if ( send( fd_2, "bye", 3, 0 ) == -1 )
        err( 1, "sending hello" );

    if ( recv( fd_2, &reply_2, 4, 0 ) == -1 )
        err( 1, "recv on fd_2" );
    if ( recv( fd_1, &reply_1, 4, 0 ) == -1 )
        err( 1, "recv on fd_1" );

    assert( ntohl( reply_1 ) == 5 );
    assert( ntohl( reply_2 ) == 3 );

    int status;

    if ( kill( pid, SIGTERM ) == -1 ||
         waitpid( pid, &status, 0 ) == -1 )
        err( 1, "terminating child process" );

    assert( WIFSIGNALED( status ) );
    assert( WTERMSIG( status ) == SIGTERM );

    close_or_warn( fd_1, "client side of a socketpair" );
    close_or_warn( fd_2, "client side of a socketpair" );

    return 0;
}
