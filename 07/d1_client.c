#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write */
#include <stdio.h>      /* perror */
#include <string.h>     /* strlen */
#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <err.h>        /* err */

/* Tento jednoduchý program demonstruje použití (spojovaného)
 * socketu ze strany klienta – v této ukázce se budeme držet
 * socketů typu ‹AF_UNIX›, které jako adresy používají cesty
 * v souborovém systému (koncový bod takového socketu, má-li
 * přiřazenu adresu, je v souborovém příkazu viditelný jako
 * speciální typ souboru – například i příkazem ‹ls›).
 *
 * Tento program obdrží dva parametry: adresu (cestu) k socketu ke
 * kterému se má připojit, a zprávu, kterou má na tento socket
 * odeslat. V součinnosti s další ukázkou si můžete předání zprávy
 * otestovat. */

int main( int argc, const char **argv ) /* demo */
{
    if ( argc != 3 )
        errx( 0, "need 2 arguments: socket_path message" );

    /* Sockety jsou symetrické v tom smyslu, že bez ohledu na to,
     * jsme-li „server“ nebo „klient“, musíme socket vždy připravit
     * voláním ‹socket›. Zde se rozhodne o typu socketu – jakou bude
     * používat komunikační doménu (zde ‹AF_UNIX›) a v jakém bude
     * pracovat režimu (zde spojovaném, ‹SOCK_STREAM›). Výsledkem
     * volání ‹socket› je popisovač otevřeného souboru (do kterého
     * ale pro tuto chvíli nelze ani zapisovat, ani z něj číst).
     *
     * Jak je obvyklé, volání vrátí v případě neúspěchu hodnotu -1 a
     * nastaví ‹errno›. */

    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );

    if ( sock_fd == -1 )
        err( 1, "creating a unix socket" );

    /* Abychom mohli pomocí socketu komunikovat, musíme ho propojit
     * s jiným socketem, typicky v jiném programu. Klientská strana
     * tohoto «připojení» je jednodušší, proto jí začneme.¹ Abychom
     * se mohli připojit k nějakému socketu, musíme znát jeho
     * «adresu» – jak tato adresa přesně vypadá je dáno právě
     * doménou socketu. Proto má každá doména vlastní datový typ,
     * který takovou adresu reprezentuje. V případě socketů
     * ‹AF_UNIX› je to typ ‹sockaddr_un›, který obsahuje jedinou
     * adresovací položku a to ‹sun_path›, do které uložíme cestu
     * (nulou ukončený řetězec). */

    struct sockaddr_un sa = { .sun_family = AF_UNIX };

    if ( strlen( argv[ 1 ] ) >= sizeof sa.sun_path - 1 )
        errx( 1, "socket address too long, maximum is %zu",
                 sizeof sa.sun_path );

    snprintf( sa.sun_path, sizeof sa.sun_path, "%s", argv[ 1 ] );

    /* Tím je adresa vyřešena, nyní pomocí volání ‹connect› socket
     * připojíme. Protože adresy mohou být různých typů, mohou být
     * aj různých velikostí, proto musíme volání ‹connect› sdělit,
     * jak velká je adresa, kterou mu předáváme. Všimněte si, že
     * výsledkem volání ‹connect› «není» nový popisovač – volání
     * změní stav existujícího popisovače ‹sock_fd›. */

    if ( connect( sock_fd, ( struct sockaddr * ) &sa,
                  sizeof sa ) == -1 )
        err( 1, "connecting to %s", sa.sun_path );

    /* Spojení jsme úspěšně navázali, můžeme posílat data. Podobně
     * jako soubory, pomocí socketů lze přenášet libovolné
     * posloupnosti bajtů (nemusíme se nutně omezovat na textová
     * data, která posíláme v tomto případě). */

    const char *message = argv[ 2 ];
    size_t nbytes = strlen( message );

    if ( write( sock_fd, message, nbytes ) == -1 )
        err( 1, "sending data on %s", sa.sun_path );

    /* Socket samozřejmě nesmíme zapomenout zavřít. Krom uvolnění
     * zdrojů má v případě spojovaného socketu uzavření popisovače
     * ještě jednu velmi důležitou funkci – ukončí navázané spojení
     * (druhá strana dostane při čtení po ukončení spojení výsledek
     * „konec souboru“).² */

    if ( close( sock_fd ) == -1 )
        warn( "closing %s", sa.sun_path );

    return 0;
}

/* ¹ Samozřejmě, explicitně připojit lze pouze spojované sockety
 *   (ty zde používané, nebo např. TCP). Datagramové sockety žádné
 *   pevné spojení nenavazují, nicméně volání ‹connect› je pro ně
 *   stále platné, jak uvidíme v pozdější ukázce.
 * ² Dokud spojení neuzavřeme, druhá strana nemá jak zjistit, že už
 *   žádná data nehodláme posílat, a volání ‹read› bude na případné
 *   další zprávy libovolně dlouho čekat. */
