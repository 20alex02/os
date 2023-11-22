#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <unistd.h>         /* read, write, fork, close */
#include <err.h>

/* Při použití rour nebo spojovaných socketů musíme počítat s tím,
 * že systémové volání ‹read› načte menší počet bajtů, než kolik
 * jsme vyžádali, nebo než kolik by se nám hodilo. Naprogramujte
 * proceduru ‹read_buffer›, která z popisovače načte právě zadaný
 * počet bajtů ‹nbytes›. Procedura bude blokovat tak dlouho, než se
 * příslušný počet bajtů podaří přečíst, nebo načte všechny bajty,
 * které druhá strana zapsala před ukončením komunikace.
 *
 * Výsledkem nechť je počet skutečně přečtených bajtů, nebo -1
 * v případě, že při čtení došlo k systémové chybě.
 *
 * «Pozor!» Tuto proceduru je možné bezpečně použít pouze v situaci,
 * kdy je zaručeno, že druhá strana alespoň ‹nbytes› bajtů skutečně
 * zapíše, nebo komunikaci ukončí. Odešle-li druhá strana menší
 * počet bajtů a pak bude čekat na odpověď, povede použití procedury
 * ‹read_buffer› k uváznutí.¹
 *
 * ¹ Jak krátké čtení řešit u protokolů s proměnnou délkou zprávy si
 *   ukážeme v páté kapitole. */

int read_buffer( int fd, char *buffer, int nbytes ) {
    int offset = 0;
    int bytes_read;
    while ((bytes_read = read(fd, &buffer[offset], nbytes - offset)) > 0) {
        offset += bytes_read;
    }
    if (bytes_read == -1) {
        return -1;
    }
    return offset;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>
#include <fcntl.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

void write_or_die( int fd, const char *buffer, int nbytes )
{
    int bytes_written = write( fd, buffer, nbytes );

    if ( bytes_written == -1 )
        err( 1, "writing %d bytes", nbytes );

    if ( bytes_written != nbytes )
        errx( 1, "unexpected short write: %d/%d written",
              bytes_written, nbytes );
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

static void writer( int fd )
{
    char buffer[] = { 0,
                      0, 1, 2,
                      0, 1, 2, 3, 4, 5, 6,
                      7, 0, 3, 0 };

    write_or_die( fd, buffer +  0, 1 );
    write_or_die( fd, buffer +  1, 3 );
    write_or_die( fd, buffer +  4, 7 );
    write_or_die( fd, buffer + 11, 4 );

    close( fd );
}

int main()
{
    int fds[ 2 ];

    if ( pipe( fds ) == -1 )
        err( 1, "pipe" );

    int reader_fd = fds[ 0 ],
        writer_fd = fds[ 1 ];

    int writer_pid = fork();

    if ( writer_pid == -1 )
        err( 1, "fork" );

    if ( writer_pid == 0 )
    {
        close_or_warn( reader_fd, "reader end of a pipe" );
        writer( writer_fd );
        return 0;
    }

    close_or_warn( writer_fd, "writer end of a pipe" );
    char buffer[ 512 ];

    assert( read_buffer( reader_fd, buffer, 3 ) == 3 );
    assert( buffer[ 0 ] == 0 );
    assert( buffer[ 1 ] == 0 );
    assert( buffer[ 2 ] == 1 );

    assert( read_buffer( reader_fd, buffer, 3 ) == 3 );
    assert( buffer[ 0 ] == 2 );
    assert( buffer[ 1 ] == 0 );
    assert( buffer[ 2 ] == 1 );

    assert( read_buffer( reader_fd, buffer, 5 ) == 5 );
    assert( buffer[ 0 ] == 2 );
    assert( buffer[ 1 ] == 3 );
    assert( buffer[ 2 ] == 4 );
    assert( buffer[ 3 ] == 5 );
    assert( buffer[ 4 ] == 6 );

    assert( read_buffer( reader_fd, buffer, 4 ) == 4 );
    assert( buffer[ 0 ] == 7 );
    assert( buffer[ 1 ] == 0 );
    assert( buffer[ 2 ] == 3 );
    assert( buffer[ 3 ] == 0 );

    int fd_wronly = open( "/dev/null", O_WRONLY );

    if ( fd_wronly == -1 )
        err( 1, "opening /dev/null for writing" );

    assert( read_buffer( reader_fd, buffer, 4 ) == 0 );
    assert( read_buffer( fd_wronly, buffer, 4 ) == -1 );
    assert( errno == EBADF );
    assert( reap( writer_pid ) == 0 );

    close_or_warn( reader_fd, "reader end of a pipe" );
    close_or_warn( fd_wronly, "/dev/null" );

    return 0;
}
