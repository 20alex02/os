#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>     /* exit */
#include <unistd.h>     /* write, read, close, … */
#include <poll.h>       /* poll */
#include <sys/socket.h> /* socket, AF_*, SOCK_* */
#include <sys/un.h>     /* sockaddr_un */
#include <errno.h>
#include <assert.h>
#include <err.h>

/* Předmětem této ukázky bude kombinace předchozích dvou, totiž
 * jednoduchý server, který je schopen komunikovat s několika
 * klienty souběžně. */

int main()
{
    return 0;
}
