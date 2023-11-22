#define _POSIX_C_SOURCE 200809L

#include <unistd.h>  /* read, write, pipe */
#include <string.h>  /* memcmp */
#include <poll.h> /* poll */
#include <stdlib.h>

/* Uvažme situaci, kdy potřebujeme číst data z několika zdrojů
 * zároveň, přitom tato data přichází různou rychlostí. Navrhněte a
 * naprogramujte podprogram ‹collect›, který obdrží:
 *
 *  • ‹count›   – počet popisovačů, se kterými bude pracovat,
 *  • ‹fds›     – ukazatel na pole popisovačů,
 *  • ‹buffers› – ukazatel na pole ukazatelů, kde každý ukazatel
 *    určuje paměť, do které se načtou data z odpovídajícího
 *    popisovače,
 *  • ‹sizes›   – ukazatel na pole čísel, které určí, kolik nejvýše
 *    bajtů se má z odpovídajícího popisovače načíst,
 *  • ‹pivot›   – index popisovače (význam viz níže).
 *
 * Uživatel bude podprogram volat opakovaně, vždy když bude
 * připraven zpracovat další data. Při každém volání podprogram
 * ‹collect›:
 *
 *  • načte data z jednoho popisovače,
 *  • posune odpovídající ukazatel v poli ‹buffers› za konec právě
 *    načtených dat,
 *  • sníží odpovídající hodnotu v poli ‹sizes› o počet načtených
 *    bajtů,
 *  • vrátí index dotčeného popisovače.
 *
 * Není-li žádný popisovač ke čtení připraven, funkce blokuje, než
 * se situace změní. Je-li připraven více než jeden popisovač,
 * použije se ten s nejnižším indexem větším nebo rovným hodnotě
 * ‹pivot›. Jsou-li všechny připravené popisovače na indexech
 * menších než ‹pivot›, použije se nejnižší z nich.
 *
 * Dojde-li k systémové chybě, podprogram vrátí hodnotu -1 a zařídí,
 * aby se budoucí volání se stejnými parametry pokusilo neprovedené
 * akce zopakovat. */

int read_from_fds(int start, int end, struct pollfd *pfds, char **buffers, int *sizes) {
    int bytes_read;
    for (int fd = start; fd < end; ++fd) {
        if (pfds[fd].revents & POLLIN) {
            if ((bytes_read = (int)read(pfds[fd].fd, buffers[fd], sizes[fd])) == -1) {
                return -1;
            }
            sizes[fd] -= bytes_read;
            buffers[fd] += bytes_read;
            return fd;
        }
    }
    return -2;
}

int collect( int count, int *fds, char **buffers,
             int *sizes, int pivot ) {
    int rv = -1;
    struct pollfd *pfds = malloc(count * sizeof(struct pollfd));
    if (pfds == NULL) {
        goto error;
    }
    for (int fd = 0; fd < count; ++fd) {
        pfds[fd].fd = fds[fd];
        pfds[fd].events = POLLIN;
    }
    if (poll(pfds, count, -1) == -1) {
        goto error;
    }
    if ((rv = read_from_fds(pivot, count, pfds, buffers, sizes)) != -2) {
        goto error;
    }
    rv = read_from_fds(0, pivot, pfds, buffers, sizes);
  error:
    free(pfds);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <err.h>            /* err */
#include <assert.h>         /* assert */

void set_buffer_ptrs( char* buffr_ptr, char** buffer_ptrs, int size )
{
    for ( int i = 0; i < size; i++ )
        buffer_ptrs[ i ] = buffr_ptr;
}

void write_or_die( int write_fd, const char* what, int bytes )
{
    if ( write( write_fd, what, bytes ) != bytes )
        err( 1, "unable to write to pipe" );
}

int main()
{
    char buffer[ 512 ] = { 0 };
    char* buffer_ptrs[ 3 ];
    int sizes[ 3 ];
    int fds_read[ 3 ], fds_write[ 3 ];

    for ( int i = 0; i < 3; i++ )
    {
        int p[ 2 ];
        if ( pipe( p ) != 0 ) err( 1, "unable to open pipes" );
        fds_read[ i ] = p[ 0 ];
        fds_write[ i ] = p[ 1 ];
        buffer_ptrs[ i ] = buffer;
        sizes[ i ] = 512;
    }

    write_or_die( fds_write[ 0 ], "hello", 5 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 0 );
    assert( memcmp( buffer, "hello", 5 ) == 0 );

    write_or_die( fds_write[ 0 ], "world", 5 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 0 );
    assert( memcmp( buffer, "helloworld", 10 ) == 0 );

    // for simplicity, all pointers point into the same buffer
    set_buffer_ptrs( buffer_ptrs[ 0 ], buffer_ptrs, 3 ); 

    write_or_die( fds_write[ 1 ], "lorem", 5 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 1 );
    assert( memcmp( buffer, "helloworldlorem", 15 ) == 0 );
    set_buffer_ptrs( buffer_ptrs[ 1 ], buffer_ptrs, 3 );

    write_or_die( fds_write[ 1 ], "ipsum", 5 );
    write_or_die( fds_write[ 2 ], "dolor", 5 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 1 );
    set_buffer_ptrs( buffer_ptrs[ 1 ], buffer_ptrs, 3 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 2 );
    set_buffer_ptrs( buffer_ptrs[ 2 ], buffer_ptrs, 3 );
    assert( memcmp( buffer, "helloworldloremipsumdolor", 25 ) == 0 );

    write_or_die( fds_write[ 0 ], "sit", 3 );
    write_or_die( fds_write[ 1 ], "amet", 4 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 0 );
    set_buffer_ptrs( buffer_ptrs[ 0 ], buffer_ptrs, 3 );
    assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == 1 );
    set_buffer_ptrs( buffer_ptrs[ 1 ], buffer_ptrs, 3 );
    assert( memcmp( buffer, "helloworldloremipsumdolorsitamet", 32 ) == 0 );

    for ( int i = 0; i < 3; i++ )
        write_or_die( fds_write[ i ], "cons", 4 );

    for ( int i = 0; i < 3; i++ )
    {
        assert( collect( 3, fds_read, buffer_ptrs, sizes, 0 ) == i );
        set_buffer_ptrs( buffer_ptrs[ i ], buffer_ptrs, 3 );
    }

    assert( memcmp( buffer, "helloworldloremipsumdolor"
                    "sitametconsconscons", 40 ) == 0 );

    for ( int i = 0; i < 3; i++ )
    {
        close( fds_read[ i ] );
        close( fds_write[ i ] );
    }

    return 0;
}
