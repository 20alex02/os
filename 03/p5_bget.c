#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <unistd.h>         /* read, write, fork, close, fdatasync */
#include <stdint.h>         /* uint8_t */
#include <err.h>
#define BLOCK_SIZE 512

/* V této přípravě je Vaším úkolem naprogramovat klient pro
 * jednoduchý spojovaný protokol pro přenos bloků dat. Po připojení
 * klienta začne server ihned odesílat jednotlivé 512-bajtové bloky.
 *
 * Po odeslání každého bloku vyčká server na jednobajtovou
 * potvrzovací zprávu od klienta, že tento data přijal a bezpečně
 * uložil (aby bylo možné data na serveru smazat). Odpověď klienta
 * bude ‹0x01› proběhlo-li uložení bloku v pořádku a ‹0x02›
 * v případě opačném.
 *
 * V reakci na odpověď ‹0x01› server:
 *
 *  • pošle další blok, je-li nějaký k dispozici,
 *  • jinak zavře spojení.
 *
 * V reakci na odpověď ‹0x02› server ukončí spojení (již bez další
 * komunikace).
 *
 * Procedura ‹bget› obdrží popisovač spojovaného socketu a popisovač
 * souboru, do kterého bude data od serveru zapisovat. Po zapsání
 * každého bloku použije na popisovač systémové volání ‹fdatasync› –
 * úspěch tohoto volání pak potvrdí serveru odesláním bajtu ‹0x01›.
 *
 * Návratová hodnota ‹bget› bude:
 *
 *  • 0 v případě úspěchu (všechna data, která server odeslal, byla
 *    uložena),
 *  • -2 v případě, že byla komunikace ukončena kvůli selhání zápisu
 *    dat do souboru a
 *  • -1 v případě, že nastala chyba komunikace.
 *
 * Do výstupního parametru ‹block_count› pak uloží počet bloků,
 * které byly úspěšně uloženy do souboru, bez ohledu na celkový
 * výsledek. */

int bget( int sock_fd, int file_fd, int *block_count ) {
    const uint8_t RESP_OK = 0x01;
    const uint8_t RESP_ERR = 0x02;
    *block_count = 0;
    char buffer[BLOCK_SIZE];
    int bytes_read, offset = 0;
    while ((bytes_read = read(sock_fd, &buffer[offset], BLOCK_SIZE - offset)) > 0) {
        offset += bytes_read;
        if (offset == BLOCK_SIZE) {
            if (write(file_fd, buffer, BLOCK_SIZE) == -1) {
                return write(sock_fd, &RESP_ERR, 1) == -1 ? -1 : -2;
            }
            if (fdatasync(file_fd) == -1) {
                return write(sock_fd, &RESP_ERR, 1) == -1 ? -1 : -2;
            }
            ++(*block_count);
            if (write(sock_fd, &RESP_OK, 1) == -1) {
                return -1;
            }
            offset = 0;
        }
    }
    if (bytes_read == -1 || offset != 0) {
        return -1;
    }
    return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdlib.h>     /* exit */

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

static void unlink_if_exists( const char *file )
{
    if ( unlink( file ) == -1 && errno != ENOENT )
        err( 2, "unlink" );
}

void write_or_die( int fd, const uint8_t *buffer, int nbytes )
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

static int create_file( const char *name )
{
    unlink_if_exists( name );
    int fd;

    if ( ( fd = open( name,
                      O_CREAT | O_TRUNC | O_RDWR, 0666 ) ) == -1 )
        err( 2, "creating %s", name );

    return fd;
}

static int fork_server( int *client_fd, void( *server )( int ) )
{
    int fds[ 2 ];

    if ( socketpair( AF_UNIX, SOCK_STREAM, 0, fds ) == -1 )
        err( 1, "socketpair" );

    int server_fd = fds[ 0 ];
    *client_fd = fds[ 1 ];

    int server_pid = fork();

    if ( server_pid == -1 )
        err( 1, "fork" );

    if ( server_pid == 0 )
    {
        close_or_warn( *client_fd, "client end of the socket" );
        server( server_fd );
        close_or_warn( server_fd, "server end of the socket" );
        exit( 0 );
    }

    close_or_warn( server_fd, "server end of the socket" );
    return server_pid;
}

static void expect( int fd, uint8_t expected )
{
    uint8_t actual;
    int bytes_read;

    if ( ( bytes_read = read( fd, &actual, 1 ) ) == -1 )
        err( 1, "server reading an ack" );

    if ( bytes_read == 0 )
        errx( 1, "unexpected EOF from client" );

    if ( actual != expected )
        errx( 1, "expected %x but received %x from the client",
              expected, actual );
}

static void server_1( int fd )
{
    uint8_t buffer[ 512 ] = { 0 };

    buffer[ 0 ] = 1;
    write_or_die( fd, buffer, 512 );
    expect( fd, 0x01 );

    buffer[ 1 ] = 2;
    write_or_die( fd, buffer, 1 );
    write_or_die( fd, buffer + 1, 511 );
    expect( fd, 0x01 );

    buffer[ 0 ] = 0;
    buffer[ 3 ] = 0xff;

    for ( int i = 0; i < 512; i += 8 )
        write_or_die( fd, buffer + i, 8 );

    expect( fd, 0x01 );

    buffer[ 511 ] = 0xff;
    write_or_die( fd, buffer +  0,  33 );
    write_or_die( fd, buffer + 33,  44 );
    write_or_die( fd, buffer + 77, 435 );

    expect( fd, 0x01 );
}

static void server_2( int fd )
{
    uint8_t buffer[ 512 ] = { 7, 0 };
    write_or_die( fd, buffer, 200 );
}

static void server_3( int fd )
{
    uint8_t buffer[ 512 ] = { 0 };
    write_or_die( fd, buffer, 512 );
    expect( fd, 2 );
}

int main()
{
    const char *dump_name = "zt.p5_dump.bin";
    uint8_t buffer[ 2048 ];
    int client_fd, server_pid, block_count;
    int file_fd, null_fd_wr, null_fd_rd;
    int bytes_read;

    file_fd = create_file( dump_name );
    null_fd_rd = open( "/dev/null", O_RDONLY );
    null_fd_wr = open( "/dev/null", O_WRONLY );

    if ( null_fd_wr == -1 || null_fd_rd == -1 )
        err( 1, "opening /dev/null" );

    /* case 1 */
    server_pid = fork_server( &client_fd, server_1 );
    assert( bget( client_fd, file_fd, &block_count ) == 0 );
    assert( block_count == 4 );
    assert( reap( server_pid ) == 0 );
    bytes_read = pread( file_fd, buffer, 2048, 0 );
    close_or_warn( client_fd, "client side of the socket" );

    if ( ftruncate( file_fd, 0 ) != 0 )
        err( 1, "truncating %s", dump_name );

    /* data check */
    assert( bytes_read == 2048 );
    assert( buffer[    0 ] == 0x01 );
    assert( buffer[  512 ] == 0x01 );
    assert( buffer[  513 ] == 0x02 );
    assert( buffer[ 1024 ] == 0x00 );
    assert( buffer[ 1025 ] == 0x02 );
    assert( buffer[ 1027 ] == 0xff );
    assert( buffer[ 2047 ] == 0xff );

    /* case 2 */
    server_pid = fork_server( &client_fd, server_2 );
    assert( bget( client_fd, file_fd, &block_count ) == -1 );
    assert( block_count == 0 );
    assert( reap( server_pid ) == 0 );
    close_or_warn( client_fd, "client side of the socket" );

    /* case 3 */
    server_pid = fork_server( &client_fd, server_3 );
    assert( bget( client_fd, null_fd_rd, &block_count ) == -2 );
    assert( block_count == 0 );
    assert( reap( server_pid ) == 0 );
    close_or_warn( client_fd, "client side of the socket" );

    /* case 4 */
    assert( bget( null_fd_wr, file_fd, &block_count ) == -1 );

    close_or_warn( file_fd, dump_name );
    close_or_warn( null_fd_rd, "/dev/null" );
    close_or_warn( null_fd_wr, "/dev/null" );
    unlink_if_exists( dump_name );

    return 0;
}
