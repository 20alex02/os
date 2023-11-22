#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write */
#include <fcntl.h>      /* openat */
#include <sys/mman.h>   /* mmap, munmap */
#include <stdint.h>     /* uint8_t */
#include <string.h>     /* memset */
#include <stdio.h>      /* dprintf */
#include <err.h>
#include <assert.h>

/* Implementujte podprogram ‹flood_fill› s těmito parametry:
 *
 *  • ‹fd› je popisovač souboru ve formátu BMP, se kterým bude
 *    pracovat (a který lze mapovat do paměti),
 *  • ‹offset› index prvního pixelu v souboru,
 *  • ‹w›, ‹h› je šířka a výška obrázku v pixelech,
 *  • ‹x›, ‹y› jsou souřadnice pixelu, od kterého začne vyplňovat
 *    jednobarevnou plochu,
 *  • ‹color› je barva, na kterou plochu přebarví.
 *
 * Obrázek používá formát, kde jeden pixel je kódovaný jedním
 * bajtem a různé hodnoty představují různé barvy. Pixely jsou
 * uloženy po řádcích a každý řádek je zarovnán na celé 4 bajty
 * doleva (tzn. nepoužité bajty jsou na konci),
 *
 * Podprogram vrátí nulu proběhne-li vše úspěšně, -2 není-li vstup
 * v očekávaném formátu (nesouhlasí první dva bajty nebo je soubor
 * příliš krátký), nebo -1 nastane-li při zpracování systémová
 * chyba. */

int flood_fill( int fd, int offset, int w, int h,
                int x, int y, int color );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void write_buffer( int fd, const char *data, int nbytes )
{
    int written = write( fd, data, nbytes );

    if ( written == -1 )
        err( 2, "write" );
    if ( written < nbytes )
        errx( 2, "write couldn't write all data" );
}

static void write_le( int fd, int bytes, uint32_t num )
{
    assert( bytes <= 4 );
    char out[ 4 ];

    for ( int i = 0; i < bytes; ++i )
        out[ i ] = ( num >> ( i * 8 ) ) & 0xff;

    write_buffer( fd, out, bytes );
}

static const uint32_t header_size = 54 + 256 * 4;

static void write_header( int fd, uint32_t w, uint32_t h )
{
    const uint32_t bmap_size = w * h + ( w % 4 ) * h;
    const uint32_t total = header_size + bmap_size;

    /* file header */
    write_buffer( fd, "BM", 2 );
    write_le( fd, 4, total );
    write_le( fd, 4, 0 );
    write_le( fd, 4, header_size );

    /* DIB header */
    write_le( fd, 4, 0x28 ); /* size of the DIB header */
    write_le( fd, 4, w );
    write_le( fd, 4, h );
    write_le( fd, 2, 1 ); /* 1 layer */
    write_le( fd, 2, 8 ); /* 8 bits per pixel */
    write_le( fd, 4, 0 );
    write_le( fd, 4, bmap_size );
    write_le( fd, 4, 315 ); /* horizontal DPI */
    write_le( fd, 4, 315 ); /* vertical DPI */
    write_le( fd, 4, 0 );
    write_le( fd, 4, 0 );

    /* write the colour table */

    for ( int i = 0; i < 256; ++i )
    {
        write_le( fd, 1, i );
        write_le( fd, 1, i );
        write_le( fd, 1, i );
        write_le( fd, 1, 0 );
    }
}

static int create_file( const char *file )
{
    int fd = openat( AT_FDCWD, file,
                     O_CREAT | O_TRUNC | O_RDWR, 0666 );

    if ( fd == -1 )
        err( 2, "creating %s", file );

    return fd;
}

static int mk_bmp( const char *file, int w, int h )
{
    int fd = create_file( file );
    write_header( fd, w, h );
    return fd;
}

static int expect( int w, int h, int x, int y )
{
    if ( ( y == h / 4 || y == 3 * h / 4 ) && x >= w / 8 && x < 7 * w / 8 )
        return 255;

    if ( ( x == w / 4 || x == 3 * w / 4 ) && y > h / 8 && y < 7 * h / 8 )
        return 255;

    if ( x >= w / 4 && x < 3 * w / 4 && y >= h / 4 && y < 3 * h / 4 )
        return 64;

    return 32;
}

int main( void )
{
    const char *file_name = "zt.p3_square.bmp";
    int fd = mk_bmp( file_name, 3840, 2160 );

    int width = 3840, height = 2160;
    uint8_t line[ width ];

    for ( int row = 0; row < height; ++ row )
    {
        if ( row == height / 4 || row == 3 * height / 4 )
            memset( line + width / 8, 255, 3 * width / 4 );
        else
            memset( line, 0, width );

        if ( row > height / 8 && row < 7 * height / 8 )
        {
            line[ width / 4 ] = 255;
            line[ 3 * width / 4 ] = 255;
        }

        if ( write( fd, line, width ) == -1 )
            err( 1, "writing pixels into %s", file_name );
    }

    assert( flood_fill( fd, header_size, width, height,
                        width / 2, 37, 32 ) == 0 );
    assert( flood_fill( fd, header_size, width, height,
                        width / 2, height / 2, 64 ) == 0 );

    if ( lseek( fd, header_size, SEEK_SET ) == -1 )
        err( 1, "lseek" );

    for ( int y = 0; y < height; ++ y )
    {
        if ( read( fd, line, width ) == -1 )
            err( 1, "reading %d bytes from %s", width, file_name );

        for ( int x = 0; x < width; ++x )
        {
            int ex = expect( width, height, x, y );

            if ( line[ x ] != ex )
                dprintf( 2, "expected %d at [%d, %d] but got %d\n",
                            ex, x, y, line[ x ] );

            assert( line[ x ] == ex );
        }
    }

    if ( close( fd ) == -1 )
        warn( "closing %s", file_name );

    return 0;
}
