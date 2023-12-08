#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* exit */
#include <unistd.h>     /* write, read, close, … */
#include <poll.h>       /* poll */
#include <sys/socket.h> /* socket, AF_*, SOCK_* */
#include <sys/un.h>     /* sockaddr_un */
#include <errno.h>
#include <assert.h>
#include <err.h>

/* Předmětem této ukázky bude použití systémového volání ‹poll›
 * v kombinaci s pasivním socketem (takovým, který čeká na připojení
 * klienta) a přijímáním nových spojení.
 *
 * Podprogram ‹drain› bude představovat velmi jednoduchý „server“,
 * který přečte veškerá data, která mu nějaký klient odešle a
 * spočítá přitom celkový počet přijatých bajtů. Bude mít
 * 2 parametry:
 *
 *  1. ‹sock_fd› je popisovač pasivního socketu,
 *  2. ‹count› je počet připojení, které má akceptovat (další
 *     spojení už nebude přijímat, a jakmile se odpojí poslední
 *     klient, podprogram ‹drain› se vrátí).
 *
 * Výsledkem bude celkový počet přečtených bajtů, nebo -1 dojde-li
 * k nějaké chybě. */

/* Samotné volání ‹poll› a zpracováni výsledku delegujeme na
 * pomocný podprogram ‹drain_poll›, který definujeme níže. Význam
 * číselných vstupně-výstupních parametrů je následovný:
 *
 *  • ‹active› je počet aktivních popisovačů,
 *  • ‹total› je celkový počet načtených bajtů,
 *  • ‹count› je zbývající počet připojení, která je ještě potřeba
 *    akceptovat. */

int drain_poll( int sock_fd, struct pollfd *pfd, int pfd_size,
                int *active, int *total, int *count );

int drain( int sock_fd, int count )
{
    /* Prvním úkolem je nachystat pole struktur ‹pollfd› pro
     * systémové volání ‹poll›. Do tohoto pole zařadíme jednak
     * samotné ‹sock_fd›, jednak veškerá nová spojení od klientů.
     * Konkrétní velikost pole zvolíme podle toho, s kolika nejvýše
     * klienty chceme být schopni komunikovat souběžně. Zde zvolíme
     * relativně malé číslo, aby se nám podprogram lépe testoval. */

    struct pollfd pfd[ 4 ];
    const int pfd_size = 4;

    /* Nyní nastavíme položky ‹fd› a ‹events› – první struktura
     * ‹pollfd› bude vyhrazena pro ‹sock_fd›, zbytek prozatím
     * nastavíme na ‹-1› (takto nastavené ‹pollfd› bude voláni
     * ‹poll› ignorovat). Do ‹events› nastavíme ve všech případech
     * ‹POLLIN› – pro ‹sock_fd› událost ‹POLLIN› znamená, že lze bez
     * blokování provést ‹accept›, u těch ostatních to bude
     * znamenat, že lze provést ‹read›. */

    for ( int i = 0; i < pfd_size; ++ i )
    {
        pfd[ i ].fd = i == 0 ? sock_fd : -1;
        pfd[ i ].events = POLLIN;
    }

    /* Hlavní cyklus pak bude běžet tak dlouho, dokud existuje
     * nějaký aktivní popisovač, a to včetně samotného ‹sock_fd›.
     * O aktivaci a deaktivaci popisovačů se stará pomocný
     * podprogram ‹drain_poll›. Popisovač ‹sock_fd› je deaktivován
     * jakmile je přijato poslední spojení v zadaném limitu ‹count›.
     * Ostatní popisovače jsou aktivní, než je klient zavře.
     *
     * Do proměnné ‹total› budeme sčítat celkový počet přečtených
     * bajtů. */

    int total = 0;
    int active = 1;

    while ( active > 0 )
        if ( drain_poll( sock_fd, pfd, pfd_size,
                         &active, &total, &count ) == -1 )
            goto err;

    return total;

err:
    /* Samotné ‹sock_fd› není vlastněno podprogramem ‹drain›, proto
     * jej při zavírání popisovačů přeskočíme. */

    for ( int i = 1; i < pfd_size; ++i )
        if ( pfd[ i ].fd != -1 )
            close( pfd[ i ].fd );

    return -1;
}

/* Následuje de-facto hlavní část ukázky – zpracování výsledků
 * z volání ‹poll›. Za řídící proměnné lze považovat ‹empty› a
 * ‹*active›.
 *
 * Proměnná ‹empty› je buď nulová, jsou-li všechny struktury
 * ‹pollfd› obsazeny, nebo je to index některé neaktivní struktury.
 * Připomínáme, že ‹pfd[ 0 ]› je vyhrazeno pro popisovač ‹sock_fd›.
 *
 * Zejména platí, že je-li ‹empty› nula, nejsme schopni přijmout
 * další spojení – nejprve je nutné odbavit existující klienty.
 *
 * Proměnná ‹*active› je pak počet aktivních popisovačů, jak je
 * popsáno výše. Popisovač ‹sock_fd› považujeme za aktivní až do
 * doby, než vyčerpáme limit připojení, a to i v situaci, kdy je
 * jeho struktura ‹pollfd› neplatná. */

int drain_poll( int sock_fd, struct pollfd *pfd, int pfd_size,
                int *active, int *total, int *count )
{
    char buffer[ 32 ];
    int bytes;
    int empty = 0;

    /* Nejprve nastavíme ‹empty› – volání ‹poll› na tuto hodnotu
     * nebude mít žádný vliv. */

    for ( int i = 1; i < pfd_size; ++i )
        if ( pfd[ i ].fd == -1 )
            empty = i;

    /* Samotné pole ‹pfd› je při vstupu do ‹drain_poll› vždy
     * v konzistentním stavu, můžeme jej tedy rovnou předat
     * systémovému volání ‹poll›. */

    if ( poll( pfd, pfd_size, -1 ) == -1 )
        return -1;

    /* Je-li to možné, akceptujeme nové připojení – nezapomeneme při
     * tom snížit limit. Všimněte si, že při každém volání
     * ‹drain_poll› je otevřeno nejvýše jedno nové spojení. */

    if ( empty && ( pfd[ 0 ].revents & POLLIN ) )
    {
        if ( ( pfd[ empty ].fd =
               accept( sock_fd, NULL, NULL ) ) != -1 )
            -- *count;
        else
            return -1;
    }

    /* Nyní zpracujeme zbývající popisovače – je-li možné nějaká
     * data přečíst, tato přečteme a aktualizujeme hodnotu ‹bytes›.
     * Zjistíme-li, že některé spojení bylo klientem uzavřeno
     * (‹read› vrátí nulu), příslušný popisovač zavřeme a
     * odpovídající strukturu ‹pollfd› deaktivujeme. */

    for ( int i = 1; i < pfd_size; ++i )
    {
        if ( ( pfd[ i ].revents & ( POLLIN | POLLHUP ) ) == 0 )
            continue;

        if ( ( bytes = read( pfd[ i ].fd, buffer,
                             sizeof buffer ) ) == -1 )
            return -1;

        *total += bytes;

        if ( bytes == 0 )
        {
            close( pfd[ i ].fd );
            pfd[ i ].fd = -1;
        }
    }

    /* Konečně aktualizujeme hodnotu ‹*active› a nastavíme strukturu
     * ‹pollfd› popisovače ‹sock_fd› podle toho, je-li možné
     * přijmout další připojení. */

    *active = *count > 0;

    for ( int i = 1; i < pfd_size; ++i )
        if ( pfd[ i ].fd != -1 )
            ++ *active;

    pfd[ 0 ].fd = ( *count && *active < pfd_size ) ? sock_fd : -1;
    return 0;
}

int main( void ) /* demo */
{
    int sock_fd = socket( AF_UNIX, SOCK_STREAM, 0 );;
    char buffer[ 80 ] = { 3, 2, 1, 0 };

    struct sockaddr_un sun = { .sun_family = AF_UNIX,
                               .sun_path = "zt.p1_sock" };
    struct sockaddr *sa = ( struct sockaddr * ) &sun;

    if ( unlink( sun.sun_path ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", sun.sun_path );

    if ( bind( sock_fd, sa, sizeof sun ) )
        err( 1, "binding a unix socket to %s", sun.sun_path );

    if ( listen( sock_fd, 5 ) )
        err( 1, "listen on %s", sun.sun_path );

    int fd[ 3 ];

    for ( int i = 0; i < 3; ++i )
    {
        if ( ( fd[ i ] = socket( AF_UNIX, SOCK_STREAM, 0 ) ) == -1 )
            err( 1, "creating a unix socket" );
        if ( connect( fd[ i ], sa, sizeof sun ) == -1 )
            err( 1, "connecting to %s", sun.sun_path );
        if ( write( fd[ i ], buffer, sizeof buffer ) == -1 )
            err( 1, "writing to socket" );
        close( fd[ i ] );
    }

    assert( drain( sock_fd, 3 ) == 3 * 80 );
    close( sock_fd );

    return 0;
}
