#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, close */
#include <sys/socket.h> /* socket, connect */
#include <sys/un.h>     /* sockaddr_un */
#include <poll.h>       /* poll, pollfd, POLLIN */
#include <errno.h>
#include <stdlib.h>     /* calloc, free */
#include <pthread.h>
#include <stdatomic.h>  /* atomic_int */
#include <assert.h>

/* V této ukázce naprogramujeme jednoduchý vícevláknový server.
 * Výhodou tohoto přístupu je zjednodušení komunikace – protože
 * každému klientu náleží samostatné vlákno, můžeme bez problémů
 * používat blokující systémová volání jako ‹read› a ‹write›.
 *
 * Nevýhodou oproti serveru postavenému s použitím mechanismem
 * ‹poll› je komplikovanější synchronizace (přístup ke sdíleným
 * datům musí být velmi důsledně chráněn) a větší spotřeba
 * výpočetních zdrojů (jak procesorového času tak paměti).
 *
 * Ve srovnání se serverem, který pro každého klienta vytvoří celý
 * nový proces  je naopak použití vláken úspornější, ale méně
 * robustní. Synchronizace mezi vlákny je také typicky náročnější
 * než synchronizace procesů, i když je to spíše vlastnost návrhu
 * než vláken samotných – vlákna umožňují snáze sdílet stav, proto
 * vícevláknové programy často sdílí víc a komplikovanějších dat. */

/* Server který budeme programovat bude pracovat jako jednoduchá
 * reverzní proxy – bude přijímat spojení od klientů a data bude
 * přeposílat pevně zvolenému „aplikačnímu“ serveru. Lze si asi
 * lehce představit, že bychom tento server rozšířili o možnost
 * distribuovat zátěž mezi více aplikačních serverů, případně
 * o kontrolu přístupových práv atp. */

/* Jak již bylo naznačeno, každý klient bude obsluhován samostatným
 * vláknem. Informace, které bude toto vlákno potřebovat ke své
 * činnosti mu předáme ve struktuře ‹client_state›. Tato bude
 * zároveň sloužit k předání výsledku zpět hlavnímu vláknu.
 * Informace o aktivních vláknech budeme udržovat ve zřetězeném
 * seznamu. */

struct client_state
{
    /* Popisovače, se kterými bude vlákno pracovat. Během existence
     * pracovního vlákna jsou tyto položky „vlastnictvím“ tohoto
     * vlákna – žádné jiné vlákno je nebude ani číst ani měnit. Před
     * vznikem pracovního vlákna je hlavní vlákno nastaví na
     * počáteční hodnoty a po ukončení pracovního vlákna je může
     * přečíst. */

    int client_fd;
    int upstream_fd;

    /* K aplikačnímu serveru se budeme připojovat pomocí UNIXového
     * socketu, nicméně změna na internetový socket (např. proto, že
     * aplikační server běží na jiném počítači) by byla velmi
     * jednoduchá. */

    struct sockaddr_un upstream_addr;

    /* Výsledek. Zde musíme použít atomickou proměnnou, protože
     * skrze ní budeme komunikovat s hlavním vláknem: hodnota -2
     * znamená, že vlákno pracuje, -1 že skončilo chybou a 0 že
     * skončilo úspěšně. Abychom hlavní vlákno nezablokovali,
     * ‹pthread_join› budeme během hlavního cyklu volat pouze na
     * ukončená vlákna. */

    atomic_int result;

    /* Informace používané a udržované hlavním vláknem. */

    int started;
    pthread_t tid;
    struct client_state *next;
};

/* Pomocný podprogram ‹copy› zkopíruje jeden blok dat z popisovače
 * ‹from_fd› do popisovače ‹to_fd›. Výsledkem bude počet přenesených
 * bajtů (0 znamená konec přenosu) nebo -1 v případě chyby. */

static int copy( int from_fd, int to_fd )
{
    char buffer[ 256 ];
    int nread;

    if ( ( nread = read( from_fd, buffer, sizeof buffer ) ) <= 0 )
        return nread;

    if ( write( to_fd, buffer, nread ) == -1 )
    {
        if ( errno == EPIPE )
            return 0;
        else
            return -1;
    }

    return nread;
}

/* Podprogram ‹client_thread› představuje hlavní část serveru –
 * každé pracovní vlákno vstoupí při svém vzniku do tohoto
 * podprogramu a jeho návratem je ukončeno. Úkolem podprogramu
 * ‹client_thread› je komunikovat s klientem a přeposílat data. */

void *client_thread( void *state )
{
    struct client_state *st = state;
    int rv = -1;

    /* Při vstupu do ‹client_thread› máme navázané spojení
     * s klientem, nicméně abychom mohli pokračovat, musíme navázat
     * také spojení s aplikačním serverem. Toto provedeme
     * v klientském vlákně, protože i samotné navázání spojení je
     * blokující a potenciálně pomalá operace (příslušný aplikační
     * server může být zaneprázdněn a spojení tak přijmout
     * s prodlevou). */

    if ( ( st->upstream_fd =
                socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        goto err;

    if ( connect( st->upstream_fd,
                  ( struct sockaddr * ) &st->upstream_addr,
                  sizeof( st->upstream_addr ) ) == -1 )
        goto err;

    /* Pro samotné přeposílání dat využijeme mechanismu ‹poll›,
     * protože jinak bychom pro každý směr potřebovali samostatné
     * vlákno, čím by se server značně zkomplikoval. Pro
     * jednoduchost si zde ovšem dovolíme předpokládat, že je-li
     * komunikace zablokovaná odesíláním dat v jednom směru, můžeme
     * po tuto dobu zdržet i směr opačný. */

    struct pollfd pfd[ 2 ] =
    {
        { .fd = st->upstream_fd, .events = POLLIN },
        { .fd = st->client_fd,   .events = POLLIN }
    };

    /* Samotný cyklus pro přeposílání dat je standardní. */

    while ( 1 )
    {
        if ( poll( pfd, 2, -1 ) == -1 )
            goto err;

        for ( int i = 0; i < 2; ++i )
            if ( pfd[ i ].revents & ( POLLIN | POLLHUP ) )
                switch ( copy( pfd[ i ].fd, pfd[ 1 - i ].fd ) )
                {
                    case 0: goto out;
                    case -1: goto err;
                    default: ;
                }
    }

    /* Po uzavření spojení (ať už ze strany serveru nebo ze strany
     * klienta) uklidíme a zápisem do položky ‹result› oznámíme
     * hlavnímu vláknu, že jsme hotovi. */

out:
    rv = 0;
err:
    if ( close( st->client_fd ) == -1 ||
         ( st->upstream_fd != -1 &&
           close( st->upstream_fd ) == -1 ) )
    {
        rv = -1;
    }

    st->client_fd = -1;
    st->upstream_fd = -1;
    st->result = rv;
    return NULL;
}

/* Konečně podprogram ‹proxy_server› bude realizovat hlavní cyklus
 * serveru – bude přijímat spojení, vytvářet pracovní vlákna a
 * ukončená vlákna opět uklízet. Budeme předpokládat, že socket
 * nastavil volající. Úklid vláken delegujeme na pomocný podprogram,
 * který definujeme níže. */

static int reap_threads( struct client_state **head, int wait );

int proxy_server( int sock_fd, int count,
                  struct sockaddr_un *upstream )
{
    int rv = -1;
    struct client_state *head = NULL;

    /* Abychom si zjednodušili testování, jedno volání
     * ‹proxy_server› obslouží nejvýše ‹count› klientů. Typičtější
     * by bylo mít na tomto místě nekonečnou smyčku a pro její
     * ukončení použít nějaký asynchronní mechanismus. */

    for ( int done = 0; done < count; ++done )
    {
        /* Pro každého klienta alokujeme a vyplníme instanci
         * struktury ‹client_state›. */

        struct client_state *client =
            calloc( 1, sizeof( struct client_state ) );

        client->upstream_fd = -1;
        client->result = -2;
        client->upstream_addr = *upstream;
        client->next = head;
        head = client;

        /* Přijmeme nové spojení. */

        if ( ( client->client_fd =
                    accept( sock_fd, NULL, NULL ) ) == -1 )
            goto end;

        /* Vytvoříme pracovní vlákno. */

        if ( pthread_create( &client->tid, NULL, client_thread,
                             client ) != 0 )
            goto end;

        /* A poznačíme si, že start proběhl úspěšně. */

        client->started = 1;

        /* Konečně provedeme úklid vláken. Provádět tento úklid
         * synchronně je dostatečné – uvolnění zdrojů se tím sice
         * může zdržet, ale provede se mezi každými dvěma požadavky
         * na zdroje nové.
         *
         * Sofistikovanější řešení by ovšem na tomto místě
         * neprocházelo celý seznam vláken v každé iteraci hlavního
         * cyklu – lepší strategii si můžete zkusit rozmyslet.
         * Ideální řešení úklid amortizuje tak, aby byla potřebná
         * práce na jednoho klienta konstantní a zároveň aby bylo
         * množství „mrtvých“ (alokovaných, ale nepoužívaných)
         * zdrojů omezeno konstantou. */

        if ( reap_threads( &head, 0 ) == -1 )
            goto end;
    }

    rv = 0;
end:
    if ( reap_threads( &head, 1 ) == -1 )
        rv = -1;

    return rv;
}

/* Zbývá podprogram ‹reap_threads› který uvolní zdroje spojené
 * s ukončenými vlákny. Je-li parametr ‹wait› nastaven na 1,
 * podprogram zároveň vyčká na ukončení vláken, které ještě běží.
 * Ukazatel na hlavu seznamu je zde vstupně-výstupním parametrem –
 * tato bude upravena tak, aby výsledný seznam obsahoval pouze
 * „živá“ vlákna. */

int reap_threads( struct client_state **head, int wait )
{
    struct client_state *client, *next, *new_head = NULL;
    int rv = 0;

    /* Jak bylo zmíněno výše, každá aktivace ‹reap_threads› bude
     * iterovat celý seznam aktivních vláken. To není příliš
     * hospodárné, ale pro účely této ukázky dostatečné. */

    for ( client = *head, next = NULL; client; client = next )
    {
        next = client->next;

        /* Vlákna, která se nepodařilo nastartovat, mají v seznamu
         * také svoje záznamy – tyto mají položku ‹started›
         * nastavenou na nulu, ale popisovač ‹client_fd› může být i
         * v takovém případě platný a je potřeba jej uzavřít. */

        if ( client->started )
        {
            /* Na zaneprázdněná vlákna budeme čekat pouze v případě,
             * že je ‹wait› nastaveno na 1. Jinak je přeřadíme do
             * nového seznamu živých vláken a pokračujeme další
             * položkou. Všimněte si, že nový seznam vláken bude
             * v opačném pořadí – na tomto pořadí ovšem nezáleží a
             * cyklus s otočením je jednodušší. */

            if ( !wait && client->result == -2 ) /* busy */
            {
                client->next = new_head;
                new_head = client;
                continue;
            }

            /* Vlákno již skončilo, nebo velmi brzo skončí. Voláním
             * ‹pthread_join› tedy zejména uvolníme zdroje s ním
             * spojené. */

            if ( pthread_join( client->tid, NULL ) != 0 )
                rv = -1;
        }

        /* Je-li to potřeba, zavřeme spojení s klientem. To je
         * potřeba pouze v případě, že jsme dříve narazili na chybu,
         * která znemožnila vytvoření vlákna. */

        if ( client->client_fd != -1 )
            if ( close( client->client_fd ) == -1 )
                rv = -1;

        /* Konečně uvolníme dynamickou paměť a pokračujeme na další
         * položku. */

        free( client );
    }

    /* Nezapomeneme nastavit novou hlavu seznamu. */

    *head = new_head;
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <signal.h>     /* signal, SIGPIPE, SIG_IGN */
#include <err.h>
#include <sys/wait.h>
#include <string.h>

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink" );
}

pid_t fork_proxy( int proxy_fd, int count,
                  struct sockaddr_un *upstream )
{
    pid_t pid = fork();
    alarm( 7 );

    if ( pid == -1 )
        err( 1, "creating proxy process" );

    if ( pid == 0 )
    {
        int rv = proxy_server( proxy_fd, count, upstream );
        close( proxy_fd );
        exit( rv == -1 ? 1 : 0 );
    }

    return pid;
}

static int reap( pid_t pid )
{
    int status;

    if ( waitpid( pid, &status, 0 ) == -1 )
        err( 2, "wait" );

    if ( WIFEXITED( status ) )
        return WEXITSTATUS( status );
    else
        return -1;
}

static int client_connect( struct sockaddr_un *addr )
{
    int fd = socket( AF_UNIX, SOCK_STREAM, 0 );

    if ( fd == -1 )
        err( 2, "socket" );

    if ( connect( fd, ( struct sockaddr * ) addr,
                  sizeof( struct sockaddr_un ) ) == -1 )
        return -1;

    return fd;
}

int main( void )
{
    if ( signal( SIGPIPE, SIG_IGN ) == SIG_ERR )
        err( 2, "signal" );

    struct sockaddr_un upstream =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.d2_upstream"
    };

    struct sockaddr_un proxy =
    {
        .sun_family = AF_UNIX,
        .sun_path = "zt.d2_proxy"
    };

    unlink_if_exists( upstream.sun_path );
    unlink_if_exists( proxy.sun_path );

    int upstream_fd, proxy_fd;

    /* … socket setup */

    if ( ( proxy_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 1, "creating proxy socket" );

    if ( bind( proxy_fd, ( struct sockaddr * ) &proxy,
               sizeof proxy ) == -1 )
        err( 1, "binding a socket to %s", proxy.sun_path );

    if ( listen( proxy_fd, 1 ) == -1 )
        err( 1, "listening on %s", proxy.sun_path );

    pid_t pid = fork_proxy( proxy_fd, 3, &upstream );

    close( proxy_fd );

    if ( ( upstream_fd = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
        err( 1, "creating upstream socket" );

    if ( bind( upstream_fd, ( struct sockaddr * ) &upstream,
               sizeof upstream ) == -1 )
        err( 1, "binding a socket to %s", upstream.sun_path );

    if ( listen( upstream_fd, 1 ) == -1 )
        err( 1, "listening on %s", upstream.sun_path );

    int c1 = client_connect( &proxy );
    int s1 = accept( upstream_fd, NULL, NULL );

    if ( s1 == -1 )
        err( 1, "accepting upstream connection" );

    char buf[ 64 ];

    assert( send( c1, "foo", 3, 0 ) == 3 );
    assert( recv( s1, buf, 3, MSG_WAITALL ) == 3 );
    assert( memcmp( buf, "foo", 3 ) == 0 );

    assert( send( s1, "bar", 3, 0 ) == 3 );
    assert( recv( c1, buf, 3, MSG_WAITALL ) == 3 );
    assert( memcmp( buf, "bar", 3 ) == 0 );

    close( c1 );
    assert( recv( s1, buf, 1, 0 ) == 0 );
    close( s1 );

    int c2 = client_connect( &proxy );
    int s2 = accept( upstream_fd, NULL, NULL );

    int c3 = client_connect( &proxy );
    int s3 = accept( upstream_fd, NULL, NULL );

    if ( s2 == -1 || s3 == -1 )
        err( 1, "accepting upstream connection" );

    assert( send( c2, "foo", 3, 0 ) == 3 );
    assert( send( s2, "bar", 3, 0 ) == 3 );
    assert( send( s3, "baz", 3, 0 ) == 3 );
    assert( send( c3, "quux", 4, 0 ) == 4 );

    assert( recv( s2, buf, 3, MSG_WAITALL ) == 3 );
    assert( memcmp( buf, "foo", 3 ) == 0 );
    assert( recv( c2, buf, 3, MSG_WAITALL ) == 3 );
    assert( memcmp( buf, "bar", 3 ) == 0 );
    assert( recv( s3, buf, 4, MSG_WAITALL ) == 4 );
    assert( memcmp( buf, "quux", 4 ) == 0 );
    assert( recv( c3, buf, 3, MSG_WAITALL ) == 3 );
    assert( memcmp( buf, "baz", 3 ) == 0 );

    close( s2 );
    assert( recv( c2, buf, 1, 0 ) == 0 );
    close( c2 );
    close( c3 );
    close( s3 );
    close( upstream_fd );

    assert( reap( pid ) == 0 );

    unlink_if_exists( upstream.sun_path );
    unlink_if_exists( proxy.sun_path );

    return 0;
}
