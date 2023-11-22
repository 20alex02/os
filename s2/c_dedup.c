#define _POSIX_C_SOURCE 200809L

/* V této úloze budete programovat proceduru, která realizuje opačný
 * proces k tomu z první úlohy. Procedura ‹dedup› nalezne všechny
 * soubory v zadaném podstromě, které mají totožný obsah, a nahradí
 * je tvrdými odkazy na jedinou kopii. Bude mít dva parametry:
 *
 *  • ‹root_fd› je popisovač složky, která určuje podstrom, se
 *    kterým bude pracovat,
 *  • ‹attic_fd› je popisovač složky, která bude sloužit jako „půda“
 *    a kde se vytvoří odkazy na všechny redundantní soubory (tzn.
 *    ty, které byly nahrazeny tvrdým odkazem na jiný soubor).
 *
 * Složka ‹attic_fd› nesmí být uvnitř podstromu, který začíná
 * složkou ‹root_fd›. Předpokládejte, že zadaný podstrom obsahuje
 * pouze složky a odkazy na obyčejné soubory (tzn. neobsahuje žádné
 * měkké odkazy, ani jiné speciální typy souborů).
 *
 * Na každý soubor, který má totožný «obsah» (metadata nerozhodují)
 * jako některý jiný nalezený soubor, vytvořte ve složce ‹attic_fd›
 * odkaz, kterého jméno bude číslo i-uzlu. Nevytvářejte zde odkazy
 * na soubory, na které bude po skončení operace existovat odkaz ve
 * stromě ‹root_fd›.
 *
 * Selže-li nějaká operace, vstupní podstrom musí zůstat
 * v konzistentním stavu (jak vnitřně, tak vůči složce ‹attic_fd›).
 * Povolen je pouze jeden typ nekorektního stavu, a to pouze
 * v případě selhání několika systémových volání: ve složce
 * ‹attic_fd› může existovat přebytečný odkaz na soubor, který je
 * zároveň odkázán z podstromu ‹root_fd›.
 *
 * Bez ohledu na libovolná selhání musí každý odkaz ve stromě
 * ‹root_fd› ukazovat na stejný «obsah» jako před provedením
 * procedury ‹dedup›. Můžete předpokládat, že nikam do stromu
 * ‹root_fd› ani do složky ‹attic_fd› neprobíhá souběžný zápis.
 *
 * Návratová hodnota nechť je ‹0› v případě úspěchu, ‹-1› v případě,
 * že nastala chyba, ale vše je v korektním (i když nedokončeném)
 * stavu a konečně ‹-2› došlo-li k situaci, kdy nebylo možné úplnou
 * konzistenci zajistit. Podobně jako v úkolu A také platí, že každé
 * selhání považujeme za trvalé (nelze jej zvrátit opakovaným
 * voláním se stejnými parametry). */

int dedup( int root_fd, int attic_fd );

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <sys/stat.h>   /* mkdirat, fstatat */
#include <fcntl.h>      /* mkdirat, openat */
#include <dirent.h>     /* fdopendir, closedir, readdir, dirfd */
#include <unistd.h>     /* unlinkat, close */
#include <string.h>     /* strlen, strcmp */
#include <fcntl.h>      /* openat, unlinkat, fstatat */
#include <errno.h>      /* errno */
#include <err.h>        /* err, warn */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void unlink_if_exists( int at, const char *name )
{
    if ( unlinkat( at, name, 0 ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", name );
}

static void rmdir_if_exists( int at, const char *name )
{
    if ( unlinkat( at, name, AT_REMOVEDIR ) == -1 && errno != ENOENT )
        err( 2, "removing directory %s", name );
}

static int open_dir_at( int at, const char *name )
{
    int fd = openat( at, name, O_RDONLY | O_DIRECTORY );

    if ( fd == -1 )
        err( 2, "opening dir %s", name );

    return fd;
}

static void create_file( int at, const char *name, const char *contents )
{
    int fd = openat( at, name, O_CREAT | O_TRUNC | O_WRONLY, 0666 );
    if ( fd == -1 )
        err( 2, "creating %s", name );
    if ( write( fd, contents, strlen( contents ) ) == -1 )
        err( 2, "writing contents to %s", name );
    close_or_warn( fd, name );
}

static int create_dir( int at, const char *dir )
{
    rmdir_if_exists( at, dir );

    if ( mkdirat( at, dir, 0755 ) == -1 )
        err( 2, "creating directory %s", dir );

    return open_dir_at( at, dir );
}

static ino_t inode_num( int at, const char *file )
{
    struct stat st;
    if ( fstatat( at, file, &st, 0 ) == -1 )
        err( 2, "fstatat %s", file );
    return st.st_ino;
}

static void unlink_files_from_if_exists( int wd, const char* path )
{
    int fd_dir = openat( wd, path, O_RDONLY );
    if ( fd_dir == -1 && errno == ENOENT ) return;
    if ( fd_dir == -1 ) err( 2, "openat %s", path );

    DIR *dir = fdopendir( fd_dir );
    if ( !dir )
        err( 2, "fdopendir %s", path );

    struct dirent *ent = NULL;
    while ( ( ent = ( ( errno = 0 ), readdir( dir ) ) ) != NULL )
    {
        if ( strcmp( ent->d_name, "." ) == 0 || strcmp( ent->d_name, ".." ) == 0 )
            continue;

        unlink_if_exists( dirfd( dir ), ent->d_name );
    }
    if ( errno )
        err( 2, "readdir %s", path );

    if ( closedir( dir ) == -1 )
        err( 2, "closedir %s", path );
}

int main( void )
{
    unlink_files_from_if_exists( AT_FDCWD, "zt.c_attic" );
    int attic = create_dir( AT_FDCWD, "zt.c_attic" );

    unlink_if_exists( AT_FDCWD, "zt.c_data/folder/same_b" );
    rmdir_if_exists(  AT_FDCWD, "zt.c_data/folder" );
    unlink_if_exists( AT_FDCWD, "zt.c_data/same_a" );
    unlink_if_exists( AT_FDCWD, "zt.c_data/different_c" );
    rmdir_if_exists(  AT_FDCWD, "zt.c_data" );

    int root = create_dir( AT_FDCWD, "zt.c_data" );
    int folder = create_dir( root, "folder" );

    create_file( root,   "same_a",      "same thing" );
    create_file( folder, "same_b",      "same thing" );
    create_file( root,   "different_c", "different thing" );

    assert( dedup( root, attic ) == 0 );

    DIR *dir = fdopendir( attic );
    if ( !dir ) err( 2, "fdopendir attic" );
    struct dirent *ent;

    do
    {
        ent = readdir( dir );

    } while ( ent && ent->d_name[ 0 ] == '.' );
    assert( ent != NULL );

    ino_t a = inode_num( root, "same_a" );
    ino_t b = inode_num( folder, "same_b" );
    ino_t c = inode_num( dirfd( dir ), ent->d_name );
    assert( a == b );
    assert( b == c );

    if ( closedir( dir ) == -1 ) err( 2, "closing attic" );

    close_or_warn( folder, "folder" );
    close_or_warn( root, "root" );

    unlink_if_exists( AT_FDCWD, "zt.c_data/folder/same_b" );
    rmdir_if_exists(  AT_FDCWD, "zt.c_data/folder" );
    unlink_if_exists( AT_FDCWD, "zt.c_data/same_a" );
    unlink_if_exists( AT_FDCWD, "zt.c_data/different_c" );
    rmdir_if_exists(  AT_FDCWD, "zt.c_data" );
    return 0;
}
