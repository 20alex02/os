#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* write, read, pread, close, unlinkat */
#include <fcntl.h>      /* openat */
#include <assert.h>     /* assert */
#include <string.h>     /* strlen, strcmp */
#include <errno.h>      /* errno */
#include <err.h>        /* err */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Soubory typu CSV (Comma-Separated Values) existují v řadě
 * variant, které se liší mimo jiné použitým oddělovačem. Ostatní
 * vlastnosti budeme pro účely tohoto příkladu uvažovat pevné:
 *
 *  • obsahuje-li v sobě některá hodnota právě používaný oddělovač,
 *    musí být uzavřena v uvozovkách (jinak jsou uvozovky kolem
 *    hodnot volitelné),
 *  • obsahuje-li hodnota uzavřená v uvozovkách znaky ‹"› nebo ‹\›,
 *    tyto musí být uvozeny znakem ‹\›,
 *  • znak nového řádku v hodnotě nepřipouštíme,
 *  • oddělovač, uvozovka, zpětné lomítko a znak nového řádku jsou
 *    každý kódovány jedním bajtem.
 *
 * Vaším úkolem bude naprogramovat proceduru, která na vstupu
 * (zadaném pomocí popisovače) obdrží soubor v tomto formátu a na
 * výstup (opět popisovač) zapíše novou verzi, která bude:
 *
 *  • používat zadaný oddělovač (může být jiný nebo stejný jako byl
 *    na vstupu),
 *  • právě ty hodnoty, které obsahují nový oddělovač, uzavře do
 *    uvozovek. */

bool even_preceding_backslash(const char *string, int index) {
    int backslash_count = 0;
    --index;
    while (index >= 0 && string[index] == '\\') {
        --index;
        ++backslash_count;
    }
    return backslash_count % 2 == 0;
}

int get_delim(const char *string, char delim) {
    char *delim_p = strchr(string, delim);
    int len = (int)strlen(string);
    if (delim_p == NULL) {
        return len - 1;
    }
    if (string[0] == '"') {
        for (int i = 1; string[i] != '\0'; ++i) {
            if (string[i] == '"' && string[i + 1] == delim && even_preceding_backslash(string, i)) {
                return i + 1;
            }
        }
    }
    return (int)(delim_p - string);
}

int get_token(char *string, char delim, int start) {
    if (start == -1) {
        return -2;
    }
    int delim_index = get_delim(string, delim);
    char c = string[delim_index];
    string[delim_index] = '\0';
    if (c == '\n') {
        return -1;
    }
    return delim_index;
}

bool realloc_double(char **string, int *capacity) {
    *capacity *= 2;
    char *tmp = realloc(*string, sizeof(char) * (*capacity));
    if (tmp == NULL) {
        warn("realloc");
        return false;
    }
    *string = tmp;
    return true;
}

char *add_quotes(const char *token, int token_len) {
    int capacity = token_len * 2 + 2;
    char *new_token = malloc(capacity * sizeof(char));
    if (new_token == NULL) {
        warn("malloc");
        return NULL;
    }
    new_token[0] = '"';
    int offset = 1;
    for (int i = 0; i < token_len; ++i) {
        if (offset + 3 > capacity && !realloc_double(&new_token, &capacity)) {
            return false;
        }
        if (token[i] == '"' || token[i] == '\\') {
            new_token[offset] = '\\';
            ++offset;
        }
        new_token[offset] = token[i];
        ++offset;
    }
    new_token[offset] = '"';
    new_token[offset + 1] = '\0';
    return new_token;
}

char *remove_quotes(const char *token, int token_len) {
    char *new_token = malloc(token_len * sizeof(char));
    if (new_token == NULL) {
        warn("malloc");
        return NULL;
    }
    int offset = 0;
    for (int i = 1; i < token_len - 1; ++i) {
        if (token[i] == '\\') {
            ++i;
        }
        new_token[offset] = token[i];
        ++offset;
    }
    new_token[offset] = '\0';
    return new_token;
}

bool print_token(int fd, char *token, char delim, bool line_start, bool line_end) {
    char *new_token = NULL;
    bool rv = false;
    char *delim_p = strchr(token, delim);
    int token_len = (int)strlen(token);
    if (token[0] != '"' && delim_p != NULL) {
        if ((new_token = add_quotes(token, (int)strlen(token))) == NULL) {
            return false;
        }
        token = new_token;
    } else if (token[0] == '"' && token[token_len - 1] == '"' && delim_p == NULL) {
        if ((new_token = remove_quotes(token, (int)strlen(token))) == NULL) {
            return false;
        }
        token = new_token;
    }
    if (!line_start && dprintf(fd, "%c", delim) == -1) {
        warn("dprintf");
        goto error;
    }
    if (dprintf(fd, "%s", token) == -1) {
        warn("dprintf");
        goto error;
    }
    if (line_end && dprintf(fd, "\n") == -1) {
        warn("dprintf");
        goto error;
    }
    rv = true;
error:
    free(new_token);
    return rv;
}

int reformat_csv( int fd_in, char old_delim,
                  char new_delim, int fd_out ) {
    int rv = 1;
    FILE *file = fdopen(fd_in, "r");
    if (file == NULL) {
        return 1;
    }
    int token_start, token_end;
    char *line = NULL;
    size_t capacity = 0;
    errno = 0;
    while (getline(&line, &capacity, file) != -1) {
        token_start = 0;
        while ((token_end = get_token(&line[token_start], old_delim, token_start)) != -2) {
            if (!print_token(fd_out, &line[token_start], new_delim, token_start == 0, token_end == -1)) {
                goto error;
            }
            token_start = token_end == -1 ? -1 : token_start + token_end + 1;
        }
    }
    if (errno != 0) {
        perror("readline");
        goto error;
    }
    rv = 0;
  error:
    free(line);
    if (fclose(file) != 0) {
        warn("fclose");
    }
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char* name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinkat '%s'", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int create_file( int dir, const char *name )
{
    unlink_if_exists( dir, name );
    int fd;

    if ( ( fd = openat( AT_FDCWD, name,
                        O_CREAT | O_TRUNC | O_RDWR,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static int check_output( int fd, const char *name,
                         int offset, const char *expected )
{
    char buffer[ 4096 + 1 ] = { 0 };

    if ( pread( fd, buffer, 4096, offset ) == -1 )
        err( 2, "reading %s (fd %d)", name, fd );

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
    int dir, fd_in, fd_out;
    const char *name_in  = "zt.a_input.csv",
            *name_out = "zt.a_output.csv";

    if ( ( dir = open( ".", O_RDONLY ) ) == -1 )
        err( 2, "opening working directory" );

    unlink_if_exists( dir, name_in );
    unlink_if_exists( dir, name_out );

    write_file( dir, name_in, "a;b,c\n" );

    fd_out = create_file( dir, name_out );

    if ( ( fd_in = openat( dir, name_in, O_RDONLY ) ) == -1 )
        err( 2, "opening %s", name_in );

    assert( reformat_csv( fd_in, ';', ',', fd_out ) == 0 );
    assert( check_output( fd_out, name_out, 0,
                          "a,\"b,c\"\n" ) == 0 );

    if ( lseek( fd_in, 0, SEEK_SET ) == -1 )
        err( 2, "seeking in %s", name_in );

    assert( reformat_csv( fd_in, ',', ';', fd_out ) == 0 );
    assert( check_output( fd_out, name_out, 8,
                          "\"a;b\";c\n" ) == 0 );

    unlink_if_exists( dir, name_in );
    unlink_if_exists( dir, name_out );
    close_or_warn( fd_in, name_in );
    close_or_warn( fd_out, name_out );
    close_or_warn( dir, "working directory" );

    return 0;
}
