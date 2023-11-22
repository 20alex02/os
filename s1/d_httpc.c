#define _POSIX_C_SOURCE 200809L

#include <stddef.h>     /* size_t */
#include <fcntl.h>      /* open */
#include <unistd.h>     /* pread */
#include <err.h>        /* err */
#include <string.h>     /* memcmp */
#include <assert.h>     /* assert */

/* Vaším úkolem bude naprogramovat klient protokolu HTTP 1.0.
 * Omezíme se na základní funkcionalitu – sestavení a odeslání
 * požadavku a přijetí a zpracování odpovědi. Pro komunikaci budeme
 * používat unixové sockety.¹ Řádky požadavku oddělujte sekvencí
 * ‹\r\n› a můžete předpokládat, že server bude dodržovat totéž. Jak
 * požadavek tak odpověď má tři části:
 *
 *  1. řádek požadavku (resp. stavový řádek pro odpověď),
 *     ◦ řádek požadavku má formu ‹METODA cesta HTTP/1.0›,
 *     ◦ stavový řádek má formu ‹HTTP/1.0 číselný_kód popis›,
 *  2. hlavičky – každé pole začíná jménem, následované dvojtečkou a
 *     textovou hodnotou, která pokračuje až do konce řádku,²
 *  3. tělo, oddělené od hlaviček prázdným řádkem.
 *
 * Obsah těla může být libovolný, nebudeme jej nijak interpretovat.
 * Pozor, může se jednat o binární data (tzn. tělo nemusí být nutně
 * textové).
 *
 * Všechny zde popsané podprogramy s návratovou hodnotou typu ‹int›
 * vrací v případě úspěchu nulu a v případě systémové chyby -1,
 * není-li uvedeno jinak. */

/* Jednotlivá pole hlavičky protokolu HTTP budeme reprezentovat
 * typem ‹http_header› a celou hlavičku pak typem
 * ‹http_header_list›. Jedná se o jednoduše zřetězený seznam dvojic
 * klíč-hodnota. Hodnota ‹value› nebude ukončena znakem nového řádku
 * (tzn. bude obsahovat pouze samotnou hodnotu příslušného pole). */

struct http_header
{
    char *name, *value;
};

struct http_header_list
{
    struct http_header header;
    struct http_header_list *next;
};

/* (Velmi zjednodušený) požadavek protokolu HTTP budeme
 * reprezentovat strukturou ‹http_request› – přitom budeme
 * podporovat pouze dvě metody, totiž ‹GET› a ‹HEAD›. Tělo požadavku
 * bude v obou případech prázdné.
 *
 * Prázdný seznam hlaviček je reprezentovaný nulovým ukazatelem
 * ‹headers›. */

enum http_method { HTTP_GET = 1,
                   HTTP_HEAD };

struct http_request
{
    enum http_method method;
    char *path;
    struct http_header_list *headers;
};

/* Pro zjednodušení tvorby požadavku implementujte následující dvě
 * funkce. Veškerá paměť spojená s požadavkem je vlastnictvím
 * požadavku – požadavek musí být platný i v situaci, kdy uživatel
 * do funkce předané ‹path› atp. později přepíše nebo uvolní.
 * Protože na pořadí hlaviček nezáleží, zvolte takové pořadí, aby
 * byla implementace efektivní. */

int http_request_set_path( struct http_request *request,
                           const char *path );
int http_request_add_header( struct http_request *request,
                             const char *field,
                             const char *value );

/* Následující funkce nechť požadavek zapíše do otevřeného
 * popisovače souboru. Tato funkce se Vám může hodit také
 * v implementaci procedury ‹http_request› níže. */

int http_request_write( struct http_request *request, int fd );

/* Konečně procedura ‹http_request_free› uvolní veškerou paměť
 * spojenou s požadavkem. Opětovné volání ‹http_request_free› na
 * stejný objekt nechť nemá žádný efekt. */

void http_request_free( struct http_request *request );

/* Pro reprezentaci odpovědi serveru použijeme strukturu
 * ‹http_response›, která bude obsahovat kód odpovědi, hlavičky a
 * tělo. Podobně jako u předchozích typů, hodnota typu
 * ‹http_response› bude vlastnit veškerou potřebnou paměť.
 * V seznamu ‹headers› budou hlavičky seřazeny v pořadí, ve kterém
 * je server odeslal (dejte si pozor na efektivitu!). */

struct http_response
{
    int code;
    struct http_header_list *headers;
    size_t body_length;
    char *body;
};

/* Procedura ‹http_response_read› přečte odpověď protokolu HTTP ze
 * zadaného popisovače a uloží ji do předané struktury
 * ‹http_response›. Výsledkem bude 0 proběhlo-li vše v pořádku, -1
 * při systémové chybě a -2 je-li odpověď špatně sestavená. */

int http_response_read( struct http_response *response, int fd_in );

/* Pro uvolnění veškeré paměti spojené s požadavkem slouží
 * následující procedura. Předaná hodnota ‹http_response› bude
 * uvedena do takového stavu, aby opětovné volání
 * ‹http_response_free› na stejné hodnotě neprovedlo žádnou akci. */

void http_response_free( struct http_response *response );

/* Konečně procedura ‹http_request› provede požadavek podle
 * parametru ‹request› na unixovém socketu ‹address› a odpověď
 * vyplní do předané hodnoty typu ‹response›. Návratová hodnota bude
 * 0 proběhlo-li vše v pořádku, -1 v případě systémové chyby a -2
 * v případě chybné odpovědi ze strany serveru. Není-li výsledek 0,
 * předaná hodnota ‹response› zůstane nedotčena. */

int http_request( const char *address,
                  struct http_request *request,
                  struct http_response *response );

/* ¹ Jako testovací server můžete použít například knihovnu
 *   ‹aiohttp› pro Python – umí pracovat s UNIXovými sockety
 *   (příklad serveru naleznete v ‹zz.httpd.py›). Úkol
 *   je ale rozvržen tak, že většinu funkcionality lze testovat i
 *   bez skutečného serveru – požadavky a odpovědi lze ukládat pro
 *   účely testování do obyčejných souborů.
 * ² Víceřádkové hlavičky pro zjednodušení nebudeme uvažovat. */

int main( int argc, const char **argv )
{
    int fd_req, fd_res;
    int bytes;
    struct http_request req = { .path = NULL, .headers = NULL };
    struct http_response res;
    char buffer[ 512 ];

    req.method = HTTP_GET;

    if ( http_request_set_path( &req, "/index" ) )
        err( 1, "setting request path" );
    if ( http_request_set_path( &req, "/index.html" ) )
        err( 1, "setting request path" );

    if ( http_request_add_header( &req, "Host", "www.fi.muni.cz" ) )
        err( 1, "setting request header" );

    if ( ( fd_req = open( "zt.request.txt",
                          O_CREAT | O_RDWR | O_TRUNC,
                          0666 ) ) == -1 )
        err( 1, "creating zt.request.txt" );

    if ( http_request_write( &req, fd_req ) == -1 )
        err( 1, "writing request to zt.request.txt" );

    if ( ( bytes = pread( fd_req, buffer, 512, 0 ) ) == -1 )
        err( 1, "reading request from zt.request.txt" );

    assert( strncmp( buffer, "GET /index.html HTTP/1.0\r\n"
                             "Host: www.fi.muni.cz\r\n\r\n",
                      bytes ) == 0 );

    http_request_free( &req );

    if ( ( fd_res = open( "zz.response.txt", O_RDONLY ) ) == -1 )
        err( 1, "opening zz.response.txt" );

    assert( http_response_read( &res, fd_res ) == 0 );
    assert( res.code == 200 );
    assert( res.body_length == 7 );
    assert( memcmp( res.body, "hello\r\n", 7 ) == 0 );
    assert( strcmp( res.headers->header.name,
                    "Content-Length" ) == 0 );
    assert( strcmp( res.headers->header.value, "7" ) == 0 );
    assert( strcmp( res.headers->next->header.name,
                    "Content-Type" ) == 0 );

    http_response_free( &res );
    return 0;
}
