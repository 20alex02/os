#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <errno.h>          /* errno */
#include <unistd.h>         /* read, write, fork, close */
#include <sys/socket.h>     /* send */
#include <arpa/inet.h>      /* htonl, ntohl */
#include <err.h>
#include <stdbool.h>

/* V této přípravě je Vaším úkolem naprogramovat datagramový server,
 * který od každého z ⟦N⟧ klientů obdrží jedno číslo, nalezne mezi
 * nimi minimum a toto vrátí. Čísla v rozsahu 0 až 2³¹ - 1 budou
 * klienty posílána jako čtyřbajtové pakety, nejvýznamnější bajt
 * první (tzn. big endian).
 *
 * Naprogramujte proceduru ‹uniqmin_server›, která jako parametry
 * dostane adresu unixového socketu, na kterém má poslouchat, a
 * kladné číslo ‹n›.
 *
 * Po ukončení protokolu (tzn. potom, co obdrží všech ‹n› čísel od
 * klientů) uklidí případné lokálně alokované zdroje a výsledné
 * minimum vrátí volajícímu.
 *
 * Nastane-li chyba protokolu (některý klient pošle paket
 * v nesprávném formátu), vrátí -2, ale až po přijetí paketů od
 * všech klientů. Nastane-li nějaká systémová chyba, vrátí -1 a
 * nastaví hodnotu ‹errno› odpovídajícím způsobem. */

uint32_t get_min(int size, const uint32_t arr[size]) {
    uint32_t min = arr[0];
    for (int i = 0; i < size; ++i) {
        if (arr[i] < min) {
            min = arr[i];
        }
    }
    return min;
}

int uniqmin_server( int fd, int n ) {
    uint32_t nums[n];
    ssize_t bytes;
    bool error = false;
    for (int i = 0; i < n; ++i) {
        if ( (bytes = recv( fd, &nums[i], 4, 0 )) == -1 ) {
            return -1;
        }
        if (bytes != 4) {
            error = true;
        }
    }
    if (error) {
        return -2;
    }
    for (int i = 0; i < n; ++i) {
        nums[i] = ntohl(nums[i]);
        if (nums[i] > 0x7fffffff) {
            return -2;
        }
    }
    return (int)get_min(n, nums);
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <sys/wait.h>
#include <fcntl.h>

static void close_or_warn( int fd, const char *name )
{
    if ( close( fd ) == -1 )
        warn( "closing %s", name );
}

void send_or_die( int fd, const void *buffer, int nbytes )
{
    int bytes_sent = send( fd, buffer, nbytes, 0 );

    if ( bytes_sent == -1 )
        err( 1, "sending %d bytes", nbytes );

    if ( bytes_sent != nbytes )
        errx( 1, "unexpected short send: %d/%d sent",
              bytes_sent, nbytes );
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

static void sender( int fd )
{
    uint32_t v1       = htonl( 1 );
    uint32_t v5       = htonl( 5 );
    uint32_t idlecode = htonl( 0x1d1ec0de );
    uint32_t big      = htonl( 0x7fff0000 );
    uint32_t toobig   = htonl( 0xfffffff0 );

    /* case 1 */
    send_or_die( fd, &v1, 4 );
    send_or_die( fd, &v1, 4 );
    send_or_die( fd, &v5, 4 );

    /* case 2 */
    send_or_die( fd, &v5, 4 );
    send_or_die( fd, &v5, 4 );
    send_or_die( fd, &v5, 4 );

    /* case 3 */
    send_or_die( fd, &big,      4 );
    send_or_die( fd, &idlecode, 4 );

    /* case 4 */
    send_or_die( fd, &big,      2 );
    send_or_die( fd, &idlecode, 4 );

    /* case 5 */
    send_or_die( fd, &big,      4 );
    send_or_die( fd, &idlecode, 4 );
    send_or_die( fd, &toobig,   4 );

    close( fd );
}

int main()
{
    int fds[ 2 ];

    if ( socketpair( AF_UNIX, SOCK_DGRAM, 0, fds ) == -1 )
        err( 1, "pipe" );

    int server_fd = fds[ 0 ],
        client_fd = fds[ 1 ];

    int client_pid = fork();

    if ( client_pid == -1 )
        err( 1, "fork" );

    if ( client_pid == 0 )
    {
        close_or_warn( server_fd, "server end of a socket" );
        sender( client_fd );
        return 0;
    }

    close_or_warn( client_fd, "client end of a socket" );

    assert( uniqmin_server( server_fd, 3 ) == 1 );
    assert( uniqmin_server( server_fd, 3 ) == 5 );
    assert( uniqmin_server( server_fd, 2 ) == 0x1d1ec0de );
    assert( uniqmin_server( server_fd, 2 ) == -2 );
    assert( uniqmin_server( server_fd, 3 ) == -2 );

    int fd_null = open( "/dev/null", O_WRONLY );

    if ( fd_null == -1 )
        err( 1, "opening /dev/null for writing" );

    assert( uniqmin_server( fd_null, 3 ) == -1 );
    assert( errno == ENOTSOCK );
    assert( reap( client_pid ) == 0 );

    close_or_warn( server_fd, "server end of a socket" );
    close_or_warn( fd_null, "/dev/null" );

    return 0;
}
