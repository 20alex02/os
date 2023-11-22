#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <unistd.h>         /* read, write, fork, close */
#include <poll.h>
#include <err.h>

/* Na rozdíl od čtení, při blokujícím zápisu do socketu nebo roury
 * nemusíme řešit situaci, kdy by se operace provedla pouze částečně
 * (s nenulovým počtem bajtů, který je ale menší než požadovaný) –
 * blokující zápisy jsou v běžných situacích „všechno nebo nic“.
 *
 * V neblokujícím režimu je ale situace jiná – máme-li z nějakého
 * důvodu neblokující popisovač, do kterého potřebujeme odeslat
 * nějaké pevné množství dat (a zároveň jej z nějakého důvodu
 * nemůžeme ani dočasně přepnout do blokujícího režimu), musíme se
 * se situací vypořádat podobně, jako tomu bylo u operace ‹read›.¹
 *
 * Vaším úkolem je tedy naprogramovat proceduru ‹write_buffer›,
 * která zapíše do «neblokujícího» popisovače proudového socketu
 * (nebo roury) zadaný počet bajtů. Zamyslete se, jak efektivně
 * vyřešit situaci, kdy operace ‹write› skončí s výsledkem ‹EAGAIN›
 * – nemá totiž smysl ji okamžitě zkoušet znovu. Šance, že se mezi
 * iteracemi uvolní zdroje, je velmi malá, a takto napsaný program
 * by zcela zbytečně využíval neúměrné množství procesorového času.
 *
 * Výsledkem nechť je počet skutečně přečtených bajtů, nebo -1
 * v případě, že při zápisu došlo k fatální systémové chybě. */

int write_buffer( int fd, const char *buffer, int nbytes );

/* ¹ Na rozdíl od analogické konstrukce pro operaci ‹read› zde
 *   nevzniká přímé riziko uváznutí. To ale neznamená, že tento
 *   de-facto blokující zápis není bez rizik. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <fcntl.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int read_or_die( int fd, char *buffer, int nbytes )
{
    int bytes_read = read( fd, buffer, nbytes );

    if ( bytes_read == -1 )
        err( 1, "reading %d bytes", nbytes );

    return bytes_read;
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

static void reader( int reader_fd, int writer_fd )
{
    char buffer[ 1024 ];
    int i = 0, total = 0, bytes_read;

    while ( ( bytes_read =
                read_or_die( reader_fd, buffer, 1024 ) ) > 0 )
    {
        if ( total % ( 2 * 1024 * 1024 ) == 0 )
            sleep( 1 );

        if ( writer_fd >= 0 )
            assert( fcntl( writer_fd, F_GETFL, 0 ) & O_NONBLOCK );

        if ( total > 1024 * 1024 * 16 && writer_fd >= 0 )
        {
            close( writer_fd );
            writer_fd = -1;
        }

        for ( ; bytes_read; -- bytes_read )
        {
            assert( buffer[ i ] == i );
            i = ( i + 1 ) % 32;
            total ++;
        }
    }

    assert( total == 1024 * 1024 * 32 );
    close( reader_fd );
}

int main()
{
    int fds[ 2 ];

    if ( pipe( fds ) == -1 )
        err( 1, "pipe" );

    int reader_fd = fds[ 0 ],
        writer_fd = fds[ 1 ];

    if ( fcntl( writer_fd, F_SETFL, O_NONBLOCK ) != 0 )
        err( 1, "setting O_NONBLOCK on fd %d", writer_fd );

    int reader_pid = fork();
    alarm( 30 );

    if ( reader_pid == -1 )
        err( 1, "fork" );

    if ( reader_pid == 0 )
    {
        reader( reader_fd, writer_fd );
        return 0;
    }

    struct rlimit rlim;

    if ( getrlimit( RLIMIT_CPU, &rlim ) == -1 )
        err( 1, "getrlimit" );

    rlim.rlim_cur = 3;

    if ( setrlimit( RLIMIT_CPU, &rlim ) == -1 )
        err( 1, "setrlimit" );

    close_or_warn( reader_fd, "reader end of a pipe" );
    char buffer[ 2048 ];

    for ( int i = 0; i < 2048 ; ++i )
        buffer[ i ] = i % 32;

    for ( int i = 0; i < 16 * 1024; ++i )
        assert( write_buffer( writer_fd, buffer, 2048 ) == 2048 );

    close_or_warn( writer_fd, "writer end of a pipe" );
    assert( reap( reader_pid ) == 0 );

    int fd_rdonly = open( "/dev/null", O_RDONLY );

    if ( fd_rdonly == -1 )
        err( 1, "opening /dev/null for reading" );

    assert( write_buffer( reader_fd, buffer, 4 ) == -1 );
    assert( errno == EBADF );

    close_or_warn( fd_rdonly, "/dev/null" );

    return 0;
}
