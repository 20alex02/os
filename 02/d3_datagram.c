#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read */
#include <stdio.h>      /* perror, printf */
#include <string.h>     /* strcpy */
#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <err.h>        /* err, errx, warn */
#include <errno.h>      /* errno, ENOENT */

/* V této ukázce budeme pracovat s «datagramovými» sockety – ty se
 * od spojovaných liší ve dvou ohledech:
 *
 *  1. není potřeba (ani není možné) navazovat spojení – s tím je
 *     spojena i větší symetrie mezi komunikujícími stranami, např.
 *     v tom, že obě strany musí mít reprezentovatelnou adresu,
 *  2. datagram je doručen vždy jako celek,¹ a to i na úrovni volání
 *     ‹sendto›/‹recvfrom› – není tedy potřeba řešit situace, kdy
 *     ‹read› skončí „uprostřed věci“ a my musíme data skládat na
 *     straně programu.
 *
 * Program bude mít dva režimy, v jednom bude čekat na zprávu, na
 * kterou obratem odpoví, v tom druhém odešle zprávu a vyčká na
 * odpověď. Spustíme-li program dvakrát v různých režimech, uvidíme
 * průběh komunikace. */

/* Protože budeme ‹bind› potřebovat na několika místech, vytvoříme
 * si pomocnou proceduru, která daný socket sváže se zadanou adresou
 * (cestou). Samotné volání ‹bind› se od případu spojovaných socketů
 * nijak neliší. */

void setup_unix( struct sockaddr_un *sa, const char *path )
{
    if ( strlen( path ) >= sizeof sa->sun_path - 1 )
        errx( 1, "socket address %s too long, maximum is %zu",
                 path, sizeof sa->sun_path );

    snprintf( sa->sun_path, sizeof sa->sun_path, "%s", path );
}

void bind_unix( int sock_fd, const char *addr )
{
    struct sockaddr_un sa = { .sun_family = AF_UNIX };

    setup_unix( &sa, addr );

    if ( unlink( sa.sun_path ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", sa.sun_path );

    if ( bind( sock_fd, ( struct sockaddr * ) &sa, sizeof sa ) )
        err( 1, "binding a unix socket to %s", sa.sun_path );
}

/* Poslouchající program potřebuje pouze adresu, na které bude
 * poslouchat – odpověď odešle zpátky na adresu, ze které původní
 * zpráva přišla. */

void listener( int sock_fd, const char *addr )
{
    bind_unix( sock_fd, addr );

    /* Pro datagramový socket je tímto nastavení hotovo – pro
     * komunikaci budeme, podobně jako na klientské straně spojované
     * komunikace, používat přímo ‹sock_fd›. Pro získání jednoho
     * datagramu od libovolného klienta použijeme volání ‹recvfrom›,
     * které jednak získá odeslaná data, ale také «adresu»
     * odesílatele, kterou budeme potřebovat, abychom mohli odeslat
     * odpověď. */

    struct sockaddr_un sun;
    struct sockaddr *sa = ( struct sockaddr * ) &sun;
    socklen_t sa_size = sizeof sun;

    char buffer[ 512 ];
    int bytes_recvd;

    if ( ( bytes_recvd = recvfrom( sock_fd, buffer, 512, 0,
                                   sa, &sa_size ) ) == -1 )
        err( 1, "recvfrom on %s", addr );

    /* Podobně jako ‹read›, ‹recvfrom› je implicitně blokující –
     * návrat z volání znamená, že jsme obdrželi zprávu. Zprávu
     * ohlásíme na standardní výstup a zároveň odešleme odpověď. */

    dprintf( STDOUT_FILENO, "listener received: \"%.*s\" from %s\n",
            bytes_recvd, buffer, sun.sun_path );

    if ( sendto( sock_fd, "pong", 4, 0, sa, sa_size ) == -1 )
        err( 1, "sending pong on %s", sun.sun_path );

}

/* Odesílatel bude potřebovat adresy dvě – adresu, ze které bude
 * odesílat (a na které obdrží případné odpovědi) a adresu na kterou
 * má počáteční zprávu odeslat. */

void sender( int sock_fd,
              const char *addr_us, const char *addr_dest )
{
    bind_unix( sock_fd, addr_us );

    struct sockaddr_un sun_dest = { .sun_family = AF_UNIX };
    struct sockaddr *sa_dest = ( struct sockaddr * ) &sun_dest;
    socklen_t sa_size = sizeof sun_dest;

    setup_unix( &sun_dest, addr_dest );

    /* Počáteční zprávu odešleme voláním ‹sendto›, kterému předáme
     * jak zprávu, tak adresu, na kterou si ji přejeme doručit.
     * V tomto případě jsme cílovou adresu získali z příkazové
     * řádky. */

    if ( sendto( sock_fd, "ping", 4, 0, sa_dest, sa_size ) == -1 )
        err( 1, "sending ping to %s", addr_dest );

    /* A nyní počkáme na odpověď. Pozor! Volání ‹recv› přijme zprávu
     * od kteréhokoliv odesílatele, a nemáme jak určit, od kterého.
     * Pro tento jednoduchý program bude ‹recv› stačit, aniž bychom
     * učinili nějaká další opatření. Abychom se ujistili, že zpráva
     * je skutečně odpovědí na náš „ping“, mohli bychom použít
     * volání ‹connect› – tím sice nenavážeme u datagramového
     * socketu žádné spojení, ale nastavíme dvě adresy, které si
     * operační systém k socketu uloží:
     *
     *  • implicitní cílovou adresu, kterou použije pro volání
     *    ‹send› (u kterého, na rozdíl od ‹sendto›, adresu uvést
     *    nemůžeme) – tuto adresu můžeme vždy „přebít“ použitím
     *    volání ‹sendto›,
     *  • adresu, ze které hodláme zprávy přijímat – operační systém
     *    bude zprávy od jiných odesílatelů odmítat.
     *
     * Protože ‹connect› na datagramovém socketu pouze nastavuje
     * asociované adresy, ale nevytváří žádné spojení, lze ‹connect›
     * na jednom takovém socketu volat opakovaně a tím asociaci
     * měnit. */

    char buffer[ 512 ];
    int bytes_recvd;

    if ( ( bytes_recvd = recv( sock_fd, buffer, 512, 0 ) ) == -1 )
        err( 1, "recv on %s", addr_us );

    /* Zprávu, kterou jsme obdrželi, nakonec vypíšeme uživateli. */

    dprintf( STDOUT_FILENO, "sender received: \"%.*s\"\n",
             bytes_recvd, buffer );
}

int main( int argc, const char **argv )
{
    if ( argc != 3 && argc != 4 )
        errx( 0, "expected arguments: <send|recv> recv_socket [send_socket]" );

    /* V obou případech budeme ke komunikaci potřebovat socket: */

    int sock_fd = socket( AF_UNIX, SOCK_DGRAM, 0 );

    if ( sock_fd == -1 )
        err( 1, "creating a unix socket" );

    /* Dále pouze určíme režim, a spustíme příslušnou proceduru. Pro
     * vyzkoušení použijte tyto příkazy (v tomto pořadí, ale každý
     * v jiném terminálu, samozřejmě na stejném počítači a ve stejné
     * pracovní složce):
     *
     *    $ ./d3_datagram recv zt.foo
     *    $ ./d3_datagram send zt.bar zt.foo */

    if ( strcmp( argv[ 1 ], "recv" ) == 0 )
    {
        listener( sock_fd, argv[ 2 ] );
    }
    else if ( strcmp( argv[ 1 ], "send" ) == 0 )
    {
        if ( argc != 4 )
            errx( 1, "expected arguments: send recv_socket send_socket" );

        sender( sock_fd, argv[ 2 ], argv[ 3 ] );
    }
    else
        errx( 1, "expected 'send' or 'recv' as first argument" );

    return 0;
}

/* ¹ Je-li doručen. Typickým datagramovým protokolem je UDP, které
 *   doručení datagramu nezaručuje. Podobně není zaručeno pořadí
 *   doručení jednotlivých datagramů mezi sebou. Ani jeden z těchto
 *   problémů nás v této chvíli nemusí trápit, protože sockety
 *   v doméně ‹AF_UNIX› datagramy doručují spolehlivě a v pořadí
 *   odeslání. */
