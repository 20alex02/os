#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read */
#include <stdio.h>      /* perror, printf */
#include <string.h>     /* strcpy */
#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <err.h>        /* err, errx, warn */
#include <errno.h>      /* errno, ENOENT */

/* Tento program představuje velmi jednoduchý server, který
 * spolupracuje s klientem z předchozí ukázky. Server se vyznačuje
 * dvěma vlastnostmi:
 *
 *  1. server je ta strana, která má reprezentovatelnou adresu –
 *     známe-li tuto adresu, můžeme se k serveru připojit,¹
 *  2. server komunikuje s větším počtem protistran, typicky
 *     souběžně (i když my se v této kapitole omezíme na sekvenční
 *     odbavování klientů).
 *
 * Server, který zde naprogramujeme, bude velmi jednoduchý – zprávy,
 * které obdrží, jednoduše přepíše na svůj standardní výstup.
 * Klientům nebude odesílat žádné odpovědi (ale připojený socket je
 * plně duplexní – lze z něj nejen číst, ale do něj i zapisovat). */

int main( int argc, const char **argv )
{
    if ( argc != 2 )
        errx( 0, "expected argument: socket_path" );

    /* První část serveru je shodná se serverem – vytvoříme socket
     * ve správné doméně a správného typu. */

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sock_fd == -1 )
        err( 1, "creating a unix socket" );

    /* Opět budeme potřebovat adresu – tentokrát se na ni nebudeme
     * připojovat, ale «poslouchat» a tím umožníme klientům, aby se
     * k této adrese připojili. Postup pro vytvoření adresy je opět
     * shodný s klientem */

    struct sockaddr_un sa = { .sun_family = AF_UNIX };

    if ( strlen( argv[ 1 ] ) >= sizeof sa.sun_path - 1 )
        errx( 1, "socket address too long, maximum is %zu",
                 sizeof sa.sun_path );

    snprintf( sa.sun_path, sizeof sa.sun_path, "%s", argv[ 1 ] );

    /* Tím ale podobnost končí. Předtím, než nachystanému socketu
     * přiřadíme adresu, musíme tuto ještě uvolnit – volání ‹bind›
     * selže, je-li předaná cesta již existuje (bez ohledu na to,
     * jakého je typu soubor, který odkazuje).² To provedeme pomocí
     * volání ‹unlink›, které možná znáte z testů. Nepodaří-li se
     * soubor odstranit, nemá smysl pokračovat – volání ‹bind› by
     * selhalo. */

    if ( unlink( sa.sun_path ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", sa.sun_path );

    /* Konečně můžeme socket «svázat» s adresou, voláním ‹bind›
     * (význam parametrů je stejný, jako u volání ‹connect›). */

    if ( bind( sock_fd, ( struct sockaddr * ) &sa, sizeof sa ) )
        err( 1, "binding a unix socket to %s", sa.sun_path );

    /* Samotné svázání s adresou nicméně nevytvoří server – k tomu
     * musíme socket uvést do režimu, kdy bude možné se k němu
     * na této adrese připojit. To provedeme voláním ‹listen›, které
     * má dva parametry: popisovač socketu a tzv. backlog, totiž
     * maximální počet klientů, které operační systém zařadí do
     * fronty v situaci, kdy náš program není schopen ihned reagovat
     * na požadavek ke spojení. */

    if ( listen( sock_fd, 5 ) )
        err( 1, "listen on %s", sa.sun_path );

    /* Komunikaci s klientem konečně navážeme voláním ‹accept›,
     * které se podobá na ‹read› – program bude v tomto volání čekat
     * (blokovat) dokud se nepřipojí nějaký klient. Po navázání
     * spojení na úrovni operačního systému volání ‹accept› vrátí
     * «nový» popisovač, který reprezentuje toto navázané spojení.
     *
     * Původní popisovač (uložený v ‹sock_fd›) nadále slouží
     * k navazování spojení, a můžeme na něm kdykoliv opět zavolat
     * ‹accept›, čím navážeme nové spojení s novým klientem. Nový
     * popisovač (výsledek ‹accept›) pak používáme ke komunikaci
     * s jedním konkrétním připojeným klientem. */

    /* Druhý a třetí parametr volání ‹accept› umožňují programu
     * získat adresu protistrany (klienta). Pro sockety v doméně
     * ‹AF_UNIX› je tato adresa typicky prázdná a tedy nezajímavá,
     * proto předáme volání ‹accept› dva nulové ukazatele.³ */

    int client_fd, bytes;
    char buffer[ 64 ];

    while ( ( client_fd = accept( sock_fd, NULL, NULL ) ) >= 0 )
    {
        /* Popisovač, který jsme od ‹accept› obdrželi už lze
         * používat podobně jako „obyčejný“ soubor. Je zde ovšem
         * jeden důležitý rozdíl – čtení může být „krátké“ i
         * v situaci, kdy nejsme u konce komunikace. Jak velké budou
         * úseky dat, které voláním ‹read› získáme, rozhoduje mimo
         * jiné to, jak klient data odesílá. Proto musíme být
         * připraveni, že ‹read› přečte třeba polovinu řádku, nebo
         * podobně nekompletní data. Protože tento server data pouze
         * přeposílá na jiný popisovač, vystačíme si zde
         * s jednoduchým cyklem, který zpracuje vždy tolik dat,
         * kolik je k dispozici.⁴ */

        while ( ( bytes = read( client_fd, buffer, 64 ) ) > 0 )
            dprintf( STDOUT_FILENO, "%.*s", bytes, buffer );

        if ( bytes == -1 )
            warn( "reading from client" );

        dprintf( STDOUT_FILENO, "\n" );

        /* Jakmile druhá strana spojení ukončí, popisovač uzavřeme a
         * jsme připraveni přijmout další spojení od dalšího
         * klienta.⁵ */

        if ( close( client_fd ) == -1 )
            warn( "closing connection" );
    }

    err( 1, "accept" );
}

/* ¹ Jak uvidíme v následující ukázce, při datagramové komunikaci
 *   tato asymetrie zmizí – nespojovaná komunikace vyžaduje, aby
 *   měly reprezentovatelnou adresu obě strany.
 * ² Je otázkou, má-li server adresu uvolnit, nebo selhat. Protože
 *   server, který v této situaci selže, je přinejlepším nepohodlný
 *   (a přinejhorším frustrující), budeme se držet přístupu, kdy
 *   server případný existující socket nejprve odstraní. Může být
 *   smysluplné před odstraněním souboru ověřit, že se jedná
 *   o socket, nicméně prozatím k tomu nemáme prostředky.
 * ³ V tomto předmětu nebudeme mít prostor programovat internetový
 *   server, ale to je situace, kdy bychom adresu protistrany mohli
 *   potřebovat, a kdy bude obsahovat smysluplné informace.
 * ⁴ Zkuste si předchozí ukázku upravit tak, aby odeslala nejprve
 *   část dat, pak vyčkala, např. voláním ‹sleep›, a pak odeslala
 *   zbytek, a sledujte, jaký to bude mít dopad na chování serveru.
 * ⁵ Další věc, kterou je dobré si zkusit (zejména máte-li už
 *   upraveného klienta, který volá ‹sleep›) je pokusit se připojit
 *   několika klienty najednou a opět sledovat chování. Přidejte si
 *   ladící výpisy, nebo zkuste použít program ‹strace›. */
