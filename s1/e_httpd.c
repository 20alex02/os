#define _POSIX_C_SOURCE 200809L

#include <string.h>     /* strlen */
#include <assert.h>     /* assert */
#include <unistd.h>     /* unlink */
#include <errno.h>      /* errno, ENOENT */
#include <err.h>        /* err */

/* Základní popis protokolu HTTP 1.0 naleznete v předchozí úloze.
 * Nyní bude Vaším úkolem naprogramovat jednoduchý server tohoto
 * protokolu. Můžete předpokládat, že vyřízení jednoho požadavku
 * bude dostatečně rychlé a nemusíte tedy řešit souběžnou komunikaci
 * s více klienty.
 *
 * Je-li klientem vyžádaná cesta přítomna v seznamu souborů,
 * odpovídejte kódem 200 OK, jinak kódem 404 Not Found. Na neznámé
 * metody reagujte kódem 501 Not Implemented. */

struct file
{
    const char *path;
    const char *content_type;
    const char *data;
    size_t data_size;
};

struct file_list
{
    struct file file;
    struct file_list *next;
};

/* Tato úloha tvoří samostatný program, vstupním bodem bude
 * podprogram ‹httpd›, který spustí server na zadané adrese a bude
 * poskytovat soubory popsané druhým parametrem. Hledání souboru
 * můžete realizovat lineárním průchodem seznamu.
 *
 * Při fatální chybě ukončete program (s odpovídající chybovou
 * hláškou), jinak chybu zapište na chybový výstup a pokračujte
 * (je-li to potřeba, můžete přerušit komunikaci s aktuálním
 * klientem).
 *
 * Pro testování můžete použít např. příkazy:
 *
 *     $ ./e_httpd zt.httpd
 *     $ curl -v --unix-socket zt.httpd http://foo/hello.txt
 */

void httpd( const char *address, struct file_list *files );

static void unlink_if_exists( const char* file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlinking %s", file );
}

int main( int argc, const char **argv )
{
    if ( argc == 1 )
        return 0; /* no automated tests */

    if ( argc > 2 )
        errx( 1, "expected arguments: socket_path" );

    struct file_list hello, bye;

    hello.file.path = "/hello.txt";
    hello.file.content_type = "text/plain";
    hello.file.data = "hello world\n";
    hello.file.data_size = strlen( hello.file.data );
    hello.next = &bye;

    bye.file.path = "/bye.txt";
    bye.file.content_type = "text/plain";
    bye.file.data = "bye world\n";
    bye.file.data_size = strlen( bye.file.data );
    bye.next = NULL;

    unlink_if_exists( argv[ 1 ] );
    httpd( argv[ 1 ], &hello );

    return 1; /* httpd should never return */
}
