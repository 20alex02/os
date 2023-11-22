#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, close, unlinkat */
#include <fcntl.h>      /* openat */
#include <assert.h>     /* assert */
#include <string.h>     /* strlen, strcmp */
#include <errno.h>      /* errno */
#include <err.h>        /* err */
#include <stdio.h>
#include <stdlib.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

/* Implementujte proceduru ‹cut›, která na popisovač ‹out› vypíše
 * sloupec zadaný parametrem ‹field› ze souboru ‹file› (sloupce
 * číslujeme od 1).¹ Každý řádek na vstupu i výstupu bude zakončen
 * znakem ‹'\n'› a sloupce jsou na vstupu odděleny bajtem ‹delim›.
 * Soubor ‹file› hledejte ve složce určené popisovačem ‹dir›.
 *
 * Návratová hodnota: 0 – úspěch; -1 – systémová chyba.
 *
 * ¹ Toto chování je podobné standardnímu příkazu ‹cut -d delim -f
 *   field file›. */

char *readline(int fd) {
    int bytes_read;
    int offset = 0;
    int buff_size = 25;
    char *buff = malloc(buff_size * sizeof(char));
    if (buff == NULL) {
        return NULL;
    }
    char *tmp;
    while ((bytes_read = read(fd, buff + offset, 1)) > 0) {
        if (buff[offset] == '\n') {
            buff[offset] = '\0';
            return buff;
        }
        ++offset;
        if (offset >= buff_size) {
            buff_size *= 2;
            tmp = realloc(buff, buff_size * sizeof(char));
            if (tmp == NULL) {
                free(buff);
                return NULL;
            }
            buff = tmp;
        }
    }
    if (bytes_read == -1) {
        return NULL;
    }
    *buff = '\0';
    return buff;
}

int cut( int dir, const char* file, char delim, int field, int out ) {
    int rv = -1;
    int fd = openat(dir, file, O_RDONLY);
    if (fd == -1) {
        return -1;
    }

    char *token;
    char *line = NULL;
    int token_num = 0;
    char delim2[2] = {delim, 0};
    while ((line = readline(fd)) != NULL && *line != '\0') {
        token = strtok(line, delim2);
        ++token_num;
        while (token_num < field) {
            token = strtok(NULL, delim2);
            ++token_num;
        }
        if (token != NULL && dprintf(out, "%s", token) == -1) {
            goto error;
        }
        if (dprintf(out,"\n") == -1) {
            goto error;
        }
        free(line);
        token_num = 0;
    }
    if (line == NULL) {
        goto error;
    }

    rv = 0;
  error:
    free(line);
    close_or_warn(fd, file);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char* name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinkat '%s'", name );
}

static int create_file( int dir, const char *name )
{
    unlink_if_exists( dir, name );
    int fd;

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_WRONLY,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static int check_cut( int dir, const char* file, char delim, int field )
{
    int out_fd, result;
    const char *out_name = "zt.p5_test_out";

    out_fd = create_file( dir, out_name );
    result = cut( dir, file, delim, field, out_fd );
    close_or_warn( out_fd, out_name );

    return result;
}

static int check_output( const char* expected )
{
    const char *name = "zt.p5_test_out";
    char buffer[ 4096 + 1 ] = { 0 };

    int read_fd = openat( AT_FDCWD, name, O_RDONLY );

    if ( read_fd == -1 )
        err( 2, "opening %s", name );

    if ( read( read_fd, buffer, 4096 ) == -1 )
        err( 2, "reading %s", name );

    close_or_warn( read_fd, name );
    return strcmp( expected, buffer );
}

static void write_file( int dir, const char *name, const char *str )
{
    int fd = create_file( dir, name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

int main( void )
{
    int dir;

    if ( ( dir = open( ".", O_RDONLY ) ) == -1 )
        err( 2, "opening working directory" );

    write_file( dir, "zt.p5_csv_a", "a;b;c\n" );
    write_file( dir, "zt.p5_csv_b",
                "zkratka,celý název,kompatibilní s POSIX\n"
                "sh,Bourne shell,ano\n"
                "bash,Bourne Again Shell,ano\n"
                "ksh,KornShell,ano\n"
                "zsh,Z shell,ano\n"
                "fish,fish - the friendly interactive shell,ne\n"
                "powershell,PowerShell,ne\n" );

    unlink_if_exists( dir, "zt.p5_test_out" );

    assert( check_cut( dir, "zt.p5_csv_a", ';', 1 ) == 0 );
    assert( check_output( "a\n" ) == 0 );

    assert( check_cut( dir, "zt.p5_csv_a", ';', 2 ) == 0 );
    assert( check_output( "b\n" ) == 0 );

    assert( check_cut( dir, "zt.p5_csv_a", ';', 3 ) == 0 );
    assert( check_output( "c\n" ) == 0 );

    assert( check_cut( dir, "zt.p5_csv_b", ',', 1 ) == 0 );
    assert( check_output( "zkratka\n"
                          "sh\n"
                          "bash\n"
                          "ksh\n"
                          "zsh\n"
                          "fish\n"
                          "powershell\n" ) == 0 );

    assert( check_cut( dir, "zt.p5_csv_b", ',', 2 ) == 0 );
    assert( check_output( "celý název\n"
                          "Bourne shell\n"
                          "Bourne Again Shell\n"
                          "KornShell\n"
                          "Z shell\n"
                          "fish - the friendly interactive shell\n"
                          "PowerShell\n" ) == 0 );

    assert( check_cut( dir, "zt.p5_csv_b", ',', 3 ) == 0 );
    assert( check_output( "kompatibilní s POSIX\n"
                          "ano\n"
                          "ano\n"
                          "ano\n"
                          "ano\n"
                          "ne\n"
                          "ne\n" ) == 0 );

    unlink_if_exists( dir, "zt.p5_csv_a" );
    unlink_if_exists( dir, "zt.p5_csv_b" );
    unlink_if_exists( dir, "zt.p5_test_out" );
    close_or_warn( dir, "working directory" );

    return 0;
}
