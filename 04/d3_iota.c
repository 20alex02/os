#define _POSIX_C_SOURCE 200809L

#include <sys/socket.h> /* socketpair */
#include <fcntl.h>      /* fcntl, F_* */
#include <unistd.h>     /* read, write, fork, close */
#include <sys/wait.h>   /* waitpid, W* */
#include <stdint.h>     /* uint32_t */
#include <arpa/inet.h>  /* ntohl, htonl */
#include <stdlib.h>     /* exit */
#include <poll.h>
#include <assert.h>
#include <err.h>

/* V této ukázce navrhneme jednoduchý podprogram, který bude do
 * dvojice rour zapisovat sekvenci postupně se zvyšujících čísel, až
 * do nějakého předem zadaného limitu. Budeme ale požadovat, aby
 * podprogram dodával data do každého popisovače tak rychle, jak jej
 * protistrana čte. Zejména nás budou zajímat situace, kdy jedna
 * strana čte mnohem pomaleji, než ta druhá (tento případ budeme
 * níže i testovat). Jak je již dobrým zvykem, čísla budeme
 * zapisovat nejvýznamnějším bajtem napřed. */

/* Aby byl zápis rozumně efektivní, nebudeme zapisovat každé
 * čtyřbajtové číslo samostatně – místo toho předvyplníme vhodně
 * velký buffer stoupající posloupností čísel, který pak odešleme
 * najednou. Podprogram ‹iota_update› „posune“ sekvenci v takto
 * nachystaném bufferu. Všimněte si, že vynulovaný buffer vyplní
 * začátkem sekvence (od jedničky). Návratová hodnota určuje počet
 * bajtů, které je potřeba zapsat. */

int iota_update( uint32_t *buffer, int count, uint32_t max )
{
    uint32_t last = ntohl( buffer[ count - 1 ] ) + 1;
    int i = 0;

    for ( ; i < count && last + i <= max; ++i )
        buffer[ i ] = htonl( last + i );

    return i * 4;
}

/* Stav generátoru zapouzdříme do jednoduché struktury. Budeme
 * potřebovat buffer pro odesílání dat a informaci o počtu bajtů,
 * které je ještě potřeba ve stávajícím bufferu odeslat. Poznačíme
 * si také samotný popisovač – podprogram ‹iota_pipe› tak bude
 * jednodušší zapsat. Samotný zápis bude provádět pomocný podprogram
 * ‹iota_write›, kterého implementaci naleznete níže. */

struct iota_state
{
    int fd;
    int nbytes;
    int offset;
    uint32_t buffer[ 64 ];
};

int iota_write( struct iota_state *state,
                int buf_size, uint32_t max );

/* Vstupem pro ‹iota_pipe› budou jednak potřebné popisovače, jednak
 * maximum do kterého má podprogram čísla generovat. Popisovače
 * budou mít volajícím nastaveny příznak ‹O_NONBLOCK› (viz ‹main›) –
 * znamená to, že výsledný zápis může být krátký (zapíše se méně
 * bajtů, než bylo vyžádáno), a zároveň, že takové volání ‹write›
 * nemůže program zablokovat. */

int iota_pipe( int fd_1, int fd_2, uint32_t max )
{
    /* Protože zápis může probíhat různě rychle, budeme pro každý
     * popisovač udržovat stav odděleně. Popisovač, pro který byl
     * již zápis ukončen, uzavřeme, a do příslušné proměnné uložíme
     * hodnotu -1. */

    struct iota_state state[ 2 ] =
    {
        { .fd = fd_1 },
        { .fd = fd_2 }
    };

    /* Dalším nutným prvkem efektivního řešení je systémové volání
     * ‹poll›, které nám umožní čekat než bude některý popisovač
     * připraven k zápisu. Jsou-li oba popisovače zablokované,
     * opakované pokusy o zápis nikam nevedou, a pouze zatěžují
     * systém zbytečnou prací. Připravenost k zápisu indikuje volání
     * ‹poll› příznakem ‹POLLOUT›. Čísla popisovačů v poli ‹pfds›
     * vyplníme až uvnitř hlavního cyklu, protože se mohou uvnitř
     * podprogramu ‹iota_write› změnit. */

    struct pollfd pfds[ 2 ];

    for ( int i = 0; i < 2; ++i )
    {
        state[ i ].offset = 0;
        state[ i ].nbytes = iota_update( state[ i ].buffer, 64, max );
        pfds[ i ].events = POLLOUT;
    }

    /* Nyní je vše připraveno pro hlavní cyklus. */

    while ( state[ 0 ].fd >= 0 || state[ 1 ].fd >= 0 )
    {
        for ( int i = 0; i < 2; ++i )
            pfds[ i ].fd = state[ i ].fd;

        if ( poll( pfds, 2, -1 ) == -1 )
            return -1;

        for ( int i = 0; i < 2; ++i )
            if ( pfds[ i ].revents & POLLOUT )
                if ( iota_write( state + i, 64, max ) == -1 )
                    return -1;
    }

    return 0;
}

int iota_write( struct iota_state *state,
                int buf_size, uint32_t max )
{
    /* Protože není zaručeno, že počet skutečně odeslaných bajtů
     * bude dělitelný 4, všechny zarážky udržujeme v bajtech
     * (nikoliv v položkách). Abychom ukazatel na místo v poli
     * ‹buffer›, odkud chceme zapisovat, spočítali správně, musíme
     * použít „bajtový“ ukazatel (vzpomeňte si, jak funguje
     * ukazatelová aritmetika). */

    uint8_t *data = ( uint8_t * ) state->buffer;

    int written = write( state->fd, data + state->offset,
                         state->nbytes );

    /* Při vstupu do podprogramu ‹iota_write› víme, že popisovač
     * ‹state->fd› byl připraven k zápisu. Máme tedy jistotu, že i
     * neblokující zápis nějaká data odešle – nevíme ale kolik jich
     * bude. Proto musíme krom selhání řešit také krátký zápis. */

    if ( written == -1 ) 
        return -1;

    state->offset += written;
    state->nbytes -= written;

    /* Ověříme, zda v poli ‹buffer› zbývají nějaká data k zápisu.
     * Pokud ne, vyplníme jej novými hodnotami a odpovídajícím
     * způsobem přenastavíme zarážky ‹offset› a ‹nbytes›. */

    if ( state->nbytes == 0 )
    {
        state->nbytes = iota_update( state->buffer, buf_size, max );
        state->offset = 0;
    }

    /* Je-li stále počet bajtů k zápisu nulový, znamená to, že jsme
     * vygenerovali a odeslali všechna požadovaná čísla. Popisovač
     * uzavřeme a nastavíme mu hodnotu -1. Volání ‹poll› tím
     * oznamujeme, že příslušná položka je nevyužitá (popisovače pro
     * ‹poll› se přenastavují v podprogramu ‹iota_pipe› výše). */

    if ( state->nbytes == 0 )
    {
        close( state->fd );
        state->fd = -1;
    }

    return 0;
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

int main( void ) /* demo */
{
    int fds_1[ 2 ], fds_2[ 2 ];

    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds_1 ) == -1 ||
         socketpair( AF_UNIX, SOCK_STREAM, 0, fds_2 ) == -1 )
    {
        err( 1, "socketpair" );
    }

    /* Voláním ‹fork› vytvoříme nový proces, který bude sloužit jako
     * testovací generátor – spustíme v něm proceduru ‹iota_pipe›.
     * Hlavní proces pak bude generátor testovat střídavým čtením
     * z popisovačů. */

    pid_t pid = fork();
    alarm( 120 ); /* die if we get stuck */

    if ( pid == -1 )
        err( 1, "fork" );

    if ( pid == 0 ) /* child */
    {
        close_or_warn( fds_1[ 1 ], "consumer side of a socketpair" );
        close_or_warn( fds_2[ 1 ], "consumer side of a socketpair" );

        /* Popisovače nastavíme do neblokujícího režimu systémovým
         * voláním ‹fcntl›. Pro nastavení příznaků slouží režim
         * ‹F_SETFL›. */

        if ( fcntl( fds_1[ 0 ], F_SETFL, O_NONBLOCK ) == -1 ||
             fcntl( fds_2[ 0 ], F_SETFL, O_NONBLOCK ) == -1 )
            err( 1, "setting O_NONBLOCK on generator sockets" );

        if ( iota_pipe( fds_1[ 0 ], fds_2[ 0 ], 1 << 22 ) == -1 )
            err( 1, "iota_pipe unexpectedly failed" );
        else
            exit( 0 ); /* success */
    }

    close_or_warn( fds_1[ 0 ], "producer side of a socketpair" );
    close_or_warn( fds_2[ 0 ], "producer side of a socketpair" );

    int fd_1 = fds_1[ 1 ],
        fd_2 = fds_2[ 1 ];

    uint32_t reply_1, reply_2;

    /* Pro každé číslo, které přečteme z popisovače ‹fd_1› přečteme
     * z popisovače ‹fd_2› čísel 8. Rozmyslete si, že kdyby
     * generátor zapisoval data synchronně, pomalejší spojení by
     * muselo na konci cyklu ve vyrovnávací paměti udržovat 7/8
     * všech vygenerovaných čísel. Kapacita této paměti je ale
     * omezená, a počet čísel je zvolený tak, aby jistě na tolik
     * hodnot nestačila. */

    for ( uint32_t i = 1; i <= 1 << 22; ++i )
    {
        if ( i % 8 == 0 )
        {
            assert( read( fd_1, &reply_1, 4 ) == 4 );
            assert( ntohl( reply_1 ) == i / 8 );
        }

        assert( read( fd_2, &reply_2, 4 ) == 4 );
        assert( ntohl( reply_2 ) == i );
    }

    /* Ověříme, že generátor po zapsání všech čísel zavřel spojení.
     * Zároveň druhé spojení zůstává v provozu – přečteme zbývající
     * čísla a ověříme, že ‹iota_pipe› bez chyb skončilo. */

    assert( read( fd_2, &reply_2, 4 ) == 0 );

    for ( uint32_t i = ( 1 << 19 ) + 1; i <= 1 << 22; ++i )
    {
        assert( read( fd_1, &reply_1, 4 ) == 4 );
        assert( ntohl( reply_1 ) == i );
    }

    assert( read( fd_1, &reply_1, 4 ) == 0 );

    int status;

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 1, "awaiting child process" );

    assert( WIFEXITED( status ) );
    assert( WEXITSTATUS( status ) == 0 );

    close_or_warn( fd_1, "consumer side of a socketpair" );
    close_or_warn( fd_2, "consumer side of a socketpair" );

    return 0;
}
