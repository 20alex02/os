#define _POSIX_C_SOURCE 200809L

#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <unistd.h>     /* close, write, unlink */
#include <stdlib.h>     /* free */
#include <stdbool.h>
#include <string.h>

#define BLOCK_SIZE 128

/* Napište podprogram ‹read_until›, kterého úkolem je načíst ze
 * zadaného popisovače jeden záznam neznámé délky. Záznamy jsou od
 * sebe odděleny zadaným bajtem ‹delimiter›.
 *
 * Protože neznáme délku záznamu, může se lehce stát, že načteme víc
 * dat, než je potřeba. Tato data přitom nesmíme ztratit – jsou
 * součástí dalších záznamů, které budeme v budoucnu načítat (a
 * samozřejmě nemůžeme data načítat po jednom bajtu). Samotný
 * oddělovač chápeme jako součást předchozího záznamu.
 *
 * Tato přebytečná data budeme uchovávat ve struktuře ‹shift_buffer›
 * – každému volání ‹read_until› z daného popisovače bude předán
 * ukazatel na tutéž strukturu ‹shift_buffer›. Při prvním volání
 * ‹read_until› budou všechny položky vynulované. Správa zdrojů
 * spojených se strukturou ‹shift_buffer› je zcela v režii
 * podprogramu ‹read_until›.
 *
 * Narazí-li ‹read_until› na konec souboru, veškeré zdroje uvolní a
 * strukturu uvede do stavu, kdy ji lze použít pro další načítání
 * (např. z jiného popisovače). Výsledkem takového volání je pak
 * poslední záznam (potenciálně nulové velikosti, nikoliv ale nulový
 * ukazatel).
 *
 * Výsledkem podprogramu ‹read_until› je buď ‹NULL› nastane-li
 * nějaká systémová chyba, nebo ukazatel na dynamicky alokovanou
 * paměť, která obsahuje načtený záznam. Počet bajtů uložených
 * v načteném záznamu zapíše do výstupního parametru ‹length›. */

struct shift_buffer /* doplňte nebo upravte dle potřeby */
{
    char *memory;
    int allocd;
    int used;
};

bool realloc_double(struct shift_buffer *buf) {
    buf->allocd = buf->allocd == 0 ? BLOCK_SIZE : buf->allocd * 2;
    char *tmp = realloc(buf->memory, buf->allocd * sizeof(char));
    if (!tmp) {
        return false;
    }
    buf->memory = tmp;
    return true;
}

int get_delim(struct shift_buffer *buf, int offset, char delim) {
    for (int i = offset; i < buf->used; ++i) {
        if (buf->memory[i] == delim) {
            return i;
        }
    }
    return -1;
}

char *copy_from_buf(struct shift_buffer *buf, int size, int *length) {
    char *string = malloc(size * sizeof(char));
    if (!string) {
        return NULL;
    }
    *length = size;
    memcpy(string, buf->memory, size);
    buf->used -= size;
    memmove(buf->memory, &buf->memory[size], buf->used);
    return string;
}

char *read_until( int fd, char delimiter,
                  struct shift_buffer *buf, int *length ) {
    int nread;
    int delim_index, offset;
    char *rv = NULL;
    if (buf->allocd - buf->used < BLOCK_SIZE && !realloc_double(buf)) {
        goto error;
    }
    while ((nread = read(fd, buf->memory + buf->used, BLOCK_SIZE)) > 0) {
        offset = buf->used;
        buf->used += nread;
        if ((delim_index = get_delim(buf, offset, delimiter)) != -1) {
            return copy_from_buf(buf, delim_index + 1, length);
        }
        if (buf->allocd - buf->used < BLOCK_SIZE && !realloc_double(buf)) {
            goto error;
        }
    }
    if (nread == -1) {
        goto error;
    }
    if ((delim_index = get_delim(buf, 0, delimiter)) != -1) {
        return copy_from_buf(buf, delim_index + 1, length);
    }
    rv = malloc(buf->used * sizeof(char));
    if (!rv) {
        goto error;
    }
    memcpy(rv, buf->memory, buf->used);
    *length = buf->used;
  error:
    free(buf->memory);
    memset(buf, 0, sizeof(*buf));
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>
#include <fcntl.h>
#include <sched.h>
#include <errno.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void write_or_die( int fd, const char *buffer, int nbytes )
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
        err( 2, "waitpid %d", pid );

    if ( WIFEXITED( status ) )
        return WEXITSTATUS( status );
    else
        return -1;
}

static void writer( int fd )
{
    char buffer[] = { 0,
                      0, 1, 2,
                      0, 1, 2, 3, 4, 5, 6, 7,
                      0, 3, 8 };

    write_or_die( fd, buffer +  0, 1 ); sched_yield();
    write_or_die( fd, buffer +  1, 3 ); sched_yield();
    write_or_die( fd, buffer +  4, 8 ); sched_yield();
    write_or_die( fd, buffer + 12, 3 ); sched_yield();

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
    struct shift_buffer buffer = { 0 };
    char *result;
    int length;

    result = read_until( reader_fd, 0, &buffer, &length );
    assert( result );
    assert( length == 1 );
    assert( result[ 0 ] == 0 );
    free( result );

    result = read_until( reader_fd, 2, &buffer, &length );
    assert( result );
    assert( length == 3 );
    assert( result[ 0 ] == 0 );
    assert( result[ 1 ] == 1 );
    assert( result[ 2 ] == 2 );
    free( result );

    result = read_until( reader_fd, 8, &buffer, &length );
    assert( result );
    assert( length == 11 );
    for ( int i = 0; i <= 7; ++i )
        assert( result[ i ] == i );
    assert( result[ 8 ] == 0 );
    assert( result[ 9 ] == 3 );
    assert( result[ 10 ] == 8 );
    free( result );

    result = read_until( reader_fd, 8, &buffer, &length );
    assert( length == 0 );
    free( result );
    assert( reap( writer_pid ) == 0 );

    int fd_wronly = open( "/dev/null", O_WRONLY );

    if ( fd_wronly == -1 )
        err( 1, "opening /dev/null for writing" );

    assert( read_until( fd_wronly, 0, &buffer, &length ) == NULL );
    assert( errno == EBADF );

    close_or_warn( reader_fd, "reader end of a pipe" );
    close_or_warn( fd_wronly, "/dev/null" );

    return 0;
}
