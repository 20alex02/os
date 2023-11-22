#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <string.h>         /* memcmp */
#include <assert.h>         /* assert */
#include <stdint.h>         /* uint8_t */
#include <netinet/in.h>     /* struct in6_addr */
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>

/* Naprogramujte proceduru ‹host6›, která obdrží hostitelské jméno
 * ‹hostname› (formou ukazatele na nulou ukončený řetězec) a do
 * výstupního parametru ‹address› zapíše adresu protokolu IPv6
 * daného hostitele (nejvýznamnější bajt první). Má-li hostitel
 * takových adres víc, vyberte tu lexikograficky nejmenší (po
 * bajtech, nejvýznamnější bajt první). Výsledkem bude 0 v případě
 * úspěchu, -1 v případě blíže neurčené systémové chyby a -2
 * v případě, že hostitelské jméno neexistuje, nebo nemá žádné IPv6
 * adresy. */

int host6(const char *hostname, struct in6_addr *address) {
    struct addrinfo hints, *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(hostname, NULL, &hints, &result);
    if (status == EAI_NONAME || status == EAI_NODATA) {
        return -2;
    }
    if (status != 0) {
        return -1;
    }

    bool found = false;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET6) {
            if (!found || memcmp(&((struct sockaddr_in6 *) rp->ai_addr)->sin6_addr, &address->s6_addr, 16) < 0) {
                memcpy(&address->s6_addr, &((struct sockaddr_in6 *) rp->ai_addr)->sin6_addr, 16);
                found = true;
            }
        }
    }

    freeaddrinfo(result);
    if (!found) {
        return -2;
    }
    return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

int main(void) {
    struct in6_addr addr;

    uint8_t aisa_ip6[16] = {0x20, 0x01, 0x07, 0x18, 0x08, 0x01, 0x02, 0x30,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    /* jedna IPv6 adresa */
    assert(host6("aisa.fi.muni.cz", &addr) == 0);
    assert(memcmp(addr.s6_addr, aisa_ip6, 16) == 0);

    /* alias na aisu s jednou IPv6 adresou */
    assert(host6("www.fi.muni.cz", &addr) == 0);
    assert(memcmp(addr.s6_addr, aisa_ip6, 16) == 0);

    uint8_t cdns_ip6[16] = {0x2a, 0x01, 0x04, 0xf8, 0x01, 0x0a, 0x44, 0x8f,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40};
    /* vícero IPv6 adres */
    assert(host6("cloudns.net", &addr) == 0);
    assert(memcmp(addr.s6_addr, cdns_ip6, 16) == 0);

    /* jen IPv4 adresa */
    assert(host6("aisa.ip4.fi.muni.cz", &addr) == -2);
    /* žádné adresy */
    assert(host6("_imap._tcp.fi.muni.cz", &addr) == -2);
    /* žádný DNS záznam */
    assert(host6("neexistujici-hostname.fi.muni.cz", &addr) == -2);

    assert(host6("_imap._tcp.fi.muni.cz", &addr) == -2);

    return 0;
}
