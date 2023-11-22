#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <string.h>         /* memcmp */
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>

/* Řada internetových protokolů začíná uvítací zprávou serveru.
 * Vaším úkolem bude naprogramovat proceduru ‹banner›, která se
 * připojí k zadanému hostiteli na zadaný TCP port, tuto zprávu
 * získá a zapíše ji do předané paměti. Uvítací zpráva je vždy
 * ukončena znakem konce řádku (pozor, pokus o další čtení poté, co
 * byl přečten konec řádku, povede k uváznutí).
 *
 * Podaří-li se zprávu načíst, procedura ‹banner› vrátí nulu,
 * v případě chyby pak -1. */

//bool host(const char *hostname, struct addrinfo *address) {
//    struct addrinfo hints, *result, *rp;
//    memset(&hints, 0, sizeof(struct addrinfo));
////    hints.ai_family = AF_INET6;
////    hints.ai_socktype = SOCK_STREAM;
//
//    int status = getaddrinfo(hostname, NULL, &hints, &result);
//    if (status == EAI_NONAME) {
//        return -2;
//    }
//    if (status != 0) {
//        return -1;
//    }
//
//    bool found = false;
//    for (rp = result; rp != NULL; rp = rp->ai_next) {
//        if (rp->ai_family == AF_INET6) {
//            if (!found || memcmp(&((struct sockaddr_in6 *) rp->ai_addr)->sin6_addr, &address->s6_addr, 16) < 0) {
//                memcpy(&address->s6_addr, &((struct sockaddr_in6 *) rp->ai_addr)->sin6_addr, 16);
//                found = true;
//            }
//        }
//    }
//
//    freeaddrinfo(result);
//    if (!found) {
//        return -2;
//    }
//    return 0;
//}

int banner(const char *hostname, int port, char *buffer, int len) {
    int rv = -1;
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        return -1;
    }
    struct addrinfo hints, *serverinfo, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(hostname, port_str, &hints, &serverinfo) != 0) {
        goto out;
    }

    for (p = serverinfo; p != NULL; p = p->ai_next) {
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            continue;
        }
        break;
    }
    if (p == NULL) {
        goto out;
    }
    freeaddrinfo(serverinfo);

    int bytes_read = 0;
    char ch;
    ssize_t read_rv;
    while (bytes_read < len - 1 && (read_rv = read(sockfd, &ch, 1)) > 0) {
        buffer[bytes_read++] = ch;
        if (ch == '\n') {
            break;
        }
    }

    buffer[bytes_read] = '\0';
    if (bytes_read == 0 || read_rv == -1) {
        goto out;
    }
    rv = 0;
    out:
    close(sockfd);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

int main(void) {
    char buf[512] = {0};

    assert(banner("relay.ip4.fi.muni.cz", 25, buf, 512) == 0);
    assert(memcmp(buf, "220 anxur.fi.muni.cz ESMTP "
                       "NO UCE NO SPAM - CVT Vas lubi.\r\n", 60) == 0);

    assert(banner("aisa.ip6.fi.muni.cz", 143, buf, 512) == 0);
    assert(memcmp(buf, "* OK [CAP", 9) == 0);

    assert(banner("localhost", 79, buf, 512) == -1);

    return 0;
}
