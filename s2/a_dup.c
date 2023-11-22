#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, unlinkat, … */
#include <fcntl.h>      /* openat, O_* */
#include <sys/stat.h>   /* fstat, struct stat */
#include <string.h>     /* memcmp, strlen */
#include <errno.h>
#include <err.h>
#include <assert.h>

/* Vaším úkolem bude naprogramovat proceduru ‹dup_links›, která
 * dostane jako vstup popisovač složky (adresáře) a v celém takto
 * určeném «podstromě» provede „rozdělení“ tvrdých odkazů.
 *
 * Procedura zajistí, že
 *
 *  • ve výsledném stromě ukazuje na každý obyčejný soubor právě
 *    jeden odkaz, a to tak, že
 *  • obsah souboru, na který ukazuje více odkazů, nakopíruje do
 *    nového souboru a
 *  • původní sdílený odkaz nahradí odkazem na tento nový soubor.
 *
 * Soubory, na které vedl jediný odkaz již na začátku, se nesmí
 * touto procedurou nijak změnit. Selže-li některá operace, podstrom
 * musí zůstat v konzistentním stavu, zejména nesmí žádný odkaz
 * chybět.¹ Je dovoleno, aby při násobném selhání systémových volání
 * zůstal ve stromě nějaký odkaz nebo soubor navíc.
 *
 * Návratová hodnota:
 *
 *  • nezáporné číslo označuje úspěch a zároveň indikuje počet
 *    nových souborů (i-uzlů), které bylo potřeba vytvořit,
 *  • ‹-1› indikuje situaci, kdy došlo k chybě, ale strom je zcela
 *    konzistentní (tzn. každý odkaz je buď v nedotčeném původním
 *    stavu, nebo ukazuje na správně duplikovaný soubor),
 *  • ‹-2› indikuje situaci, kdy došlo k chybě jak při samotné práci
 *    procedury, tak při pokusu o uvedení stromu do konzistentního
 *    stavu, a tedy ve stromě přebývají nějaké odkazy nebo soubory.
 *
 * Systémové volání, které jednou selhalo, již se stejnými hodnotami
 * parametrů neopakujte – naopak předpokládejte, že každé «selhání»
 * je trvalé (bez ohledu na konkrétní hodnotu ‹errno›). */

int dup_links( int root_fd );

/* ¹ Pro zajištění těchto vlastností se Vám může hodit kombinace
 *   systémového volání ‹fchdir› a knihovního podprogramu ‹mkstemp›.
 *   Žel, vhodnější ‹mkstempat› není v tuto chvíli standardizované.
 *   Použijete-li ovšem ‹fchdir›, nezapomeňte před ukončením
 *   podprogramu vrátit nastavení pracovní složky do původního
 *   stavu. */

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists( int dir, const char *name )
{
    if ( unlinkat( dir, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", name );
}

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static int open_or_die( int dir_fd, const char *name )
{
    int fd = openat( dir_fd, name, O_RDONLY );

    if ( fd == -1 )
        err( 2, "opening %s", name );

    return fd;
}

static int mkdir_or_die( int dir_fd, const char *name )
{
    int fd;

    if ( mkdirat( dir_fd, name, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating directory %s", name );
    if ( ( fd = openat( dir_fd, name, O_DIRECTORY ) ) == -1 )
        err( 1, "opening newly created directory %s", name );

    return fd;
}

static int create_file( int dir_fd, const char *name )
{
    unlink_if_exists( dir_fd, name );
    int fd;

    if ( ( fd = openat( dir_fd, name,
                        O_CREAT | O_TRUNC | O_RDWR,
                        0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static void write_file( int dir, const char *name, const char *str )
{
    int fd = create_file( dir, name );

    if ( write( fd, str, strlen( str ) ) == -1 )
        err( 2, "writing file %s", name );

    close_or_warn( fd, name );
}

static int check_file( int dir, const char *name, const char *expected )
{
    int nbytes = strlen( expected );
    int rbytes;
    char buffer[ nbytes + 1 ];
    struct stat st;

    int read_fd = open_or_die( dir, name );

    if ( ( rbytes = read( read_fd, buffer, nbytes + 1 ) ) == -1 )
        err( 2, "reading %s", name );

    if ( fstat( read_fd, &st ) == -1 )
        err( 2, "stat-ing %s", name );

    close_or_warn( read_fd, name );

    return st.st_nlink == 1 &&
           rbytes == nbytes &&
           memcmp( expected, buffer, nbytes ) == 0;
}

int main()
{
    int work_fd = mkdir_or_die( AT_FDCWD, "zt.a_root" );
    write_file( work_fd, "foo", "x" );
    write_file( work_fd, "bar", "y" );
    unlink_if_exists( work_fd, "baz" );

    if ( linkat( work_fd, "foo", work_fd, "baz", 0 ) == -1 )
        err( 1, "linking foo to baz" );

    int sub_fd = mkdir_or_die( work_fd, "subdir" );
    write_file( sub_fd, "x", "x" );

    assert( dup_links( work_fd ) == 1 );
    assert( check_file( work_fd, "foo", "x" ) );
    assert( check_file( work_fd, "baz", "x" ) );
    assert( check_file( work_fd, "bar", "y" ) );

    close_or_warn( sub_fd, "zt.a_root/subdir" );
    close_or_warn( work_fd, "zt.a_root" );

    return 0;
}
