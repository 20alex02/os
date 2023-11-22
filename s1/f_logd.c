#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* unlink */
#include <fcntl.h>      /* open, O_* */
#include <errno.h>      /* errno, ENOENT, EEXIST */
#include <sys/stat.h>   /* mkdir */
#include <assert.h>
#include <err.h>

/* V této úloze bude Vaším úkolem implementovat jednoduchý logovací
 * server. K serveru se může připojit libovolný počet klientů,
 * přitom každý klient bude posílat zprávy pro uložení na serveru
 * (každá jednotlivá zpráva je ukončena znakem konce řádku).  Server
 * je pak bude v pořadí, ve kterém je obdržel, ukládat do dvou
 * souborů – jeden, který je specifický pro daného klienta, a jeden
 * společný (hlavní).
 *
 * Po připojení klient pošle řádek ‹log id\n›, kde ‹id› je libovolný
 * řetězec, který je platným názvem souboru. Server odpoví:
 *
 *  • ‹ok, logging as id\n› čím potvrdí, že požadavek přijal a je
 *    připraven zaznamenávat přijaté zprávy,
 *  • nebo ‹error <popis chyby>\n› narazí-li na nějaký problém
 *    (např. se nepodařilo otevřít logovací soubor).
 *
 * Připojí-li se víc klientů se stejným ‹id›, nejedná se o chybu,
 * logovací soubor budou sdílet. Když soubor se záznamy již
 * existuje, existující obsah nechť je zachován – nové záznamy bude
 * server přidávat na konec.
 *
 * Proběhne-li počáteční výměna v pořádku, server bez zbytečné
 * prodlevy uloží každou další zprávu, kterou od klienta obdrží, do
 * obou souborů.
 *
 * Hlavní soubor bude proceduře ‹logd› předán jako popisovač
 * otevřeného souboru ‹main_log_fd›, vedlejší soubory bude podle
 * potřeby vytvářet ve složce předané popisovačem otevřené složky
 * ‹log_dir_fd›. Soubory se budou jmenovat ‹id.log› kde ‹id› je
 * identifikátor odeslaný klientem.
 *
 * Narazí-li ‹logd› na fatální chybu, ukončí program s chybovým
 * kódem. Není-li server schopen zprávy některého klienta ukládat,
 * ukončí s tímto klientem spojení. */

void logd( const char *addr, int main_log_fd, int log_dir_fd );

int main( int argc, const char **argv )
{
    if ( argc == 1 )
        return 0; /* no automated tests */

    if ( argc < 4 )
        errx( 1, "arguments expected: "
                 "socket_path main_log log_dir" );

    int log_fd, dir_fd;
    const char *sock = argv[ 1 ],
               *log  = argv[ 2 ],
               *dir  = argv[ 3 ];

    if ( unlink( sock ) == -1 && errno != ENOENT )
        err( 1, "removing %s", sock );

    if ( mkdir( dir, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating directory %s", dir );

    if ( ( log_fd = open( log,
                          O_CREAT | O_TRUNC | O_RDWR,
                          0666 ) ) == -1 )
        err( 1, "creating %s", log );

    if ( ( dir_fd = open( dir, O_DIRECTORY ) ) == -1 )
        err( 1, "opening %s", dir );

    logd( sock, log_fd, dir_fd );

    return 1; /* logd should never return */
}
