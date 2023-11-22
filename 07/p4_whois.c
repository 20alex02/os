#define _POSIX_C_SOURCE 200809L

#include <assert.h>         /* assert */
#include <string.h>         /* memcmp */
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>

/* Systém WHOIS udržuje administrativní metadata o jménech vedených
 * ve veřejném systému DNS. Tato data nemají (globálně) žádný pevný
 * formát, z pohledu protokolu se jedná o volný text.
 *
 * Vaším úkolem je napsat proceduru ‹whois›, která:
 *
 *  1. získá adresu příslušného WHOIS serveru (jeho hostitelské
 *     jméno je ‹tld.whois-servers.net›, kde se za ‹tld› doplní
 *     příslušná doména nejvyšší úrovně,
 *  2. připojit se k tomuto serveru na port TCP 43,
 *  3. zjistit informace o zadané doméně ‹domain›.
 *
 * Samotný protokol je velmi jednoduchý – klient odešle doménové jméno
 * a konec řádku (CRLF, jak je na internetu běžné), server poté odešle
 * veškerá data o doméně a spojení ukončí. Pro jednoduchost odpověď
 * nijak neinterpretujte, pouze zkopírujte prvních ‹len› bajtů
 * odpovědi do předaného pole.
 *
 * Výsledek je 0 v případě úspěchu a -1 v případě chyby. */

int whois(const char *domain, char *buffer, int len) {
    // Determine the WHOIS server for the given TLD.
    char tld[256];
    int i = 0;

    while (domain[i] != '.' && domain[i] != '\0') {
        tld[i] = domain[i];
        i++;
    }
    tld[i] = '\0';

    struct addrinfo hints, *results, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // Allow both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(strcat(tld, ".whois-servers.net"), "43", &hints, &results) != 0) {
        return -1; // Unable to find the WHOIS server for the TLD.
    }

    int sockfd;

    for (rp = results; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            break; // Successfully connected to the WHOIS server.
        }

        close(sockfd);
    }

    if (rp == NULL) {
        freeaddrinfo(results);
        return -1; // Connection failed.
    }

    freeaddrinfo(results);

    // Send the domain name to the WHOIS server.
    char query[256];
    snprintf(query, sizeof(query), "%s\r\n", domain);
    send(sockfd, query, strlen(query), 0);

    // Receive the WHOIS response and copy it into the buffer.
    int bytes_received = recv(sockfd, buffer, len, 0);
    if (bytes_received == -1) {
        close(sockfd);
        return -1; // Failed to receive data.
    }

    close(sockfd);

    return 0; // Success.
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

int main(void) {
    char buf[512] = {0};

    assert(whois("example.com", buf, 512) == 0);
    assert(memcmp(buf, "   Domain Name: EXAMPLE.COM\r\n"
                       "   Registry Domain ID: 2336799_DOMAIN_COM-VRSN\r\n",
                  77) == 0);

    assert(whois("vim.org", buf, 512) == 0);
    assert(memcmp(buf, "Domain Name: vim.org\r\n"
                       "Registry Domain ID: 0fa5cba7e79a4325b8b47076eee87248-LROR\r\n",
                  81) == 0);

    assert(whois("third.example.com", buf, 512) == 0);
    assert(memcmp(buf, "No match for \"THIRD.EXAMPLE.COM\".\r\n>>> Last update", 50) == 0);

    assert(whois("this-domain-is.bad", buf, 512) == -1);
    assert(whois("this-domain-is-tld-only", buf, 512) == -1);

    return 0;
}
