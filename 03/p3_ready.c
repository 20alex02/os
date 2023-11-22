#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* exit */
#include <unistd.h>     /* pipe, write, read, close, dup2,
                         * STDIN_FILENO */
#include <assert.h>     /* assert */
#include <poll.h>       /* poll */
#include <errno.h>


/* Vaším úkolem je implementovat podprogram ‹readyfd›, který obdrží
 * tyto dva parametry:
 *
 *  • ‹bits› je ukazatel na bitové pole,
 *  • ‹count› je velikost takto předaného pole v bitech.
 *
 * Každý nenulový bit v předaném bitovém poli označuje popisovač, na
 * který se volající dotazuje. Funkce vrátí hodnotu nejnižšího
 * z dotazovaných popisovačů, z něhož je možné «číst». Není-li možné
 * číst z žádného z nich, program zablokuje do doby, než to možné
 * bude.
 *
 * Bitového pole je předáno v ⌈count/8⌉ bajtech, přitom jednotlivé
 * bity jsou indexovány následovně:
 *
 *   • bajt s nejnižší adresou odpovídá popisovačům 0–7,
 *   • následující bajt popisovače 8–15, atd.,
 *   • uvnitř každého bajtu odpovídá nejméně významný bit popisovači
 *     s nejnižším číslem.
 *
 * Návratová hodnota je číslo popisovače připraveného ke čtení, nebo
 * -1 nastane-li chyba. Krom systémových chyb ošetřete i případ, kdy
 * je vstupní množina popisovačů prázdná – v takové situaci nastavte
 * ‹errno› na ‹EDEADLK›. */

int readyfd( unsigned char* bits, int count ) {
    assert(count > 0);
    if (bits == NULL) {
        errno = EDEADLK;
        return -1;
    }
    int rv = -1;
    struct pollfd *pfds = NULL;
    pfds = malloc(count * sizeof(struct pollfd));
    if (pfds == NULL) {
        goto error;
    }
    int offset = 0;
    for (int byte = 0; byte < count / 8; ++byte) {
        for (int bit = 0; bit < 8; ++bit) {
            if ((bits[byte] & ( 1 << bit )) >> bit) {
                pfds[offset].fd = 8 * byte + bit;
                pfds[offset].events = POLLIN;
                ++offset;
            }
        }
    }
    if (offset == 0) {
        errno = EDEADLK;
        goto error;
    }
    if (poll(pfds, offset, -1) == -1) {
        goto error;
    }
    for (int i = 0; i < offset; ++i) {
        if (pfds[i].revents & POLLIN) {
            rv = pfds[i].fd;
            break;
        }
    }
  error:
    free(pfds);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void prepare( int* pipes, int count )
{
    for ( int i = 3; i < count; ++i )
        if ( dup2( STDIN_FILENO, i ) == -1 )
            exit( 2 );

    for ( int i = 3; i < count; ++i )
    {
        if ( pipe( pipes + i * 2 ) == -1 )
            exit( 2 );

        if ( dup2( pipes[ i * 2 ], i ) == -1 )
            exit( 2 );
    }
}

static void cleanup( int* pipes, int count )
{
    for ( int i = 3; i < count; ++i )
        close( i );

    for ( int i = 3; i < count; ++i )
    {
        close( pipes[ i * 2 ] );
        close( pipes[ i * 2 + 1 ] );
    }
}

static void ready( int i, int* pipes )
{
    if ( write( pipes[ i * 2 + 1 ], "something", 9 ) == -1 )
        exit( 2 );
}

static void unready( int i, int* pipes )
{
    char buf[ 9 ];
    if ( read( pipes[ i * 2 ], &buf, 9 ) == -1 ) exit( 2 );
}

int main( void )
{
    const int count = 32;
    int pipes[ count * 2 ];

    prepare( pipes, count );

    unsigned char  all[] = { 0xA0, 0x00, 0xE1, 0x83, };

    ready(  5, pipes );
    ready(  7, pipes );
    ready( 16, pipes );
    ready( 21, pipes );
    ready( 22, pipes );
    ready( 23, pipes );
    ready( 24, pipes );
    ready( 25, pipes );
    ready( 31, pipes );

    assert( readyfd( all, count ) ==  5 );

    unready(  5, pipes );
    assert( readyfd( all, count ) ==  7 );

    unready(  7, pipes );
    assert( readyfd( all, count ) == 16 );

    unready( 16, pipes );
    assert( readyfd( all, count ) == 21 );

    unready( 21, pipes );
    assert( readyfd( all, count ) == 22 );

    unready( 22, pipes );
    assert( readyfd( all, count ) == 23 );

    unready( 23, pipes );
    assert( readyfd( all, count ) == 24 );

    unready( 24, pipes );
    assert( readyfd( all, count ) == 25 );

    unready( 25, pipes );
    assert( readyfd( all, count ) == 31 );

    cleanup( pipes, count );
}
