#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <string.h>     /* memcmp */
#include <unistd.h>     /* write, read, lseek, close */
#include <stdint.h>     /* uint32_t */
#include <fcntl.h>      /* open */
#include <err.h>        /* err, errx, warn */
#include <stdlib.h>

/* Úkolem je naprogramovat proceduru ‹bwconv›, která převede obrázek
 * ve formátu BMP ze stupňů šedi do černé a bílé. Výsledný obrázek
 * tak bude obsahovat pixely pouze těchto dvou barev.
 *
 * Procedura přijímá parametry:
 *   • ‹fd_in› – popisovač pro vstupní bitmapová data;
 *   • ‹w› – šířka obrázku;
 *   • ‹h› – výška obrázku;
 *   • ‹fd_out› – výstupní popisovač;
 *   • ‹threshold› – mez rozdělující bílou a černou (hodnota 0–255).
 *
 * Na vstupu může procedura očekávat pouze bitmapová data (tedy
 * o hlavičku je postaráno se jinde) a stejně tak bude zapisovat
 * pouze výsledná bitmapová data bez hlavičky.
 *
 * Data budou takového formátu, že jeden bajt = jeden pixel (tedy 8
 * bitů na pixel). Formát BMP navíc definuje, že každý řádek musí
 * být uložen tak, aby jeho délka v bajtech byla dělitelná 4, tedy
 * po přečtení ‹w› bajtů budou na řádku další 0 až 3 bajty, které
 * výsledný obrázek nijak neovlivní. Můžete předpokládat, že na
 * vstupu bude hodnota těchto bajtů 0.
 *
 * Pro každý bajt určující barvu zapište na výstup černou (hodnotu
 * 0) je-li vstupní barva «menší nebo rovna» hodnotě ‹threshold› a
 * bílou (‹255›) jinak.
 *
 * «Pozor!» Když budete srovnávat vstupní bajty s hodnotou
 * ‹threshold›, dejte si pozor na implicitní konverze. Ujistěte se,
 * že konverze mezi použitými číselnými typy proběhne tak, jak
 * očekáváte.
 *
 * Návratová hodnota: ‹0› – úspěch; ‹-1› – systémová chyba.
 *
 * Při testování může přijít vhod příkaz ‹od -t x1› na prohlížení
 * jednotlivých bajtů obrázku. */

void convert_pixels(int line_length, uint8_t *pixels, int threshold) {
	for (int i = 0; i < line_length; ++i) {
		pixels[i] = pixels[i] <= threshold ? 0 : 255;
	}
}

int bwconv( int fd_in, int w, int h, int fd_out, int threshold ) {
    int rv = -1;
	// round up to the closes multiple of 4
    int line_length = (w + 3) & ~0x03;
	uint8_t *pixels = malloc(line_length);
    if (pixels == NULL) {
        goto error;
    }
	for (int line = 0; line < h; ++line) {
		if (read(fd_in, pixels, line_length) != line_length) {
			goto error;
		}
		convert_pixels(line_length, pixels, threshold);
		if (write(fd_out, pixels, line_length) != line_length) {
			goto error;
		}
	}
    rv = 0;
    error:
    free(pixels);
	return rv;
}

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

static void write_header( int fd, uint32_t w, uint32_t h )
{
    const uint32_t bmap_size = w * h * 3 + ( w % 4 ) * h;
    const uint32_t header_size = 54 + 256 * 4;
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

static int mk_bmp( const char *file, int w, int h,
                   const char *data, int len )
{
    int fd = create_file( file );

    write_header( fd, w, h );
    write_buffer( fd, data, len );

    if ( lseek( fd, -len, SEEK_CUR ) == -1 )
        err( 1, "seeking in %s", file );

    return fd;
}

static int run_bwconv( int fd_in, int w, int h, int threshold )
{
    const char *name = "zt.p2_out.bmp";
    int ret, fd_out;

    fd_out = create_file( name );

    write_header( fd_out, w, h );
    ret = bwconv( fd_in, w, h, fd_out, threshold );

    if ( close( fd_out ) )
        warn( "closing %s", name );

    return ret;
}

static int cmp_output( const char *expected, int len )
{
    const char *name = "zt.p2_out.bmp";
    char buffer[ len ];

    int fd = open( name, O_RDONLY );

    if ( fd == -1 )
        err( 2, "opening %s", name );

    lseek( fd, -len, SEEK_END );

    int bytes = read( fd, buffer, len );
    if ( bytes == -1 )
        err( 2, "reading %s", name );

    int cmp = len - bytes;
    if ( cmp == 0 )
        cmp = memcmp( expected, buffer, len );

    if ( close( fd ) )
        warn( "closing %s", name );

    return cmp;
}

int main( void )
{
    /* Testovací bitmapová data. Za povšimnutí stojí, že řádky jdou
     * odspodu nahoru, tedy v opačném pořadí, než jak je vidíme
     * v prohlížeči obrázků. */

    const char small[] =
    {
        0x7f, 0x41, 0x00, 0x00,
        0x00, 0xff, 0x00, 0x00
    };

    const char small_bw[] =
    {
        0xff, 0xff, 0x00, 0x00,
        0x00, 0xff, 0x00, 0x00
    };

    const char grad[] =
    {
        0x00, 0x21, 0x42, 0x63, 0x83, 0xa4, 0xff, 0x00,
        0x00, 0x1d, 0x3b, 0x58, 0x76, 0x93, 0xb1, 0x00,
        0x00, 0x17, 0x31, 0x4b, 0x65, 0x7e, 0x98, 0x00,
        0x00, 0x11, 0x27, 0x3d, 0x53, 0x68, 0x7e, 0x00,
        0x00, 0x0b, 0x1d, 0x2e, 0x41, 0x53, 0x65, 0x00,
        0x00, 0x06, 0x14, 0x21, 0x2f, 0x3c, 0x4b, 0x00,
        0x00, 0x03, 0x0a, 0x13, 0x1d, 0x26, 0x31, 0x00,
        0x00, 0x01, 0x03, 0x06, 0x0b, 0x11, 0x17, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    const char grad_bw_29[] =
    {
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    char grad_bw_255[ 8 * 9 ] = { 0 };
    grad_bw_255[ 6 ] = 0xff;

    int fd_small = mk_bmp( "zt.p2_small.bmp", 2, 2, small, 8 );
    assert( run_bwconv( fd_small, 2, 2, 64 ) == 0 );
    assert( cmp_output( small_bw, 8 ) == 0 );

    int fd_grad = mk_bmp( "zt.p2_grad.bmp", 7, 9, grad, 8 * 9 );
    assert( run_bwconv( fd_grad, 7, 9, 0xfe ) == 0 );
    assert( cmp_output( grad_bw_255, 8 * 9 ) == 0 );

    if ( lseek( fd_grad, -8 * 9, SEEK_END ) == -1 )
        err( 1, "seeking in zt.p2_grad.bmp" );

    assert( run_bwconv( fd_grad, 7, 9, 0x1c ) == 0 );
    assert( cmp_output( grad_bw_29, 8 * 9 ) == 0 );

    if ( close( fd_grad ) )
        warn( "closing zt.p2_grad.bmp" );
    if ( close( fd_small ) )
        warn( "closing zt.p2_small.bmp" );

    return 0;
}
