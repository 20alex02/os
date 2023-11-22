#define _POSIX_C_SOURCE 200809L

#include <stddef.h>     /* size_t */
#include <fcntl.h>      /* open */
#include <unistd.h>     /* pread */
#include <err.h>        /* err */
#include <string.h>     /* memcmp */
#include <assert.h>     /* assert */
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

/* Vaším úkolem bude naprogramovat klient protokolu HTTP 1.0.
 * Omezíme se na základní funkcionalitu – sestavení a odeslání
 * požadavku typu GET nebo HEAD a přijetí a zpracování odpovědi.
 *
 * Řádky požadavku oddělujte sekvencí ‹\r\n› a můžete předpokládat,
 * že server bude dodržovat totéž. Jak požadavek tak odpověď má tři
 * části:
 *
 *  1. řádek požadavku (resp. stavový řádek pro odpověď),
 *     ◦ řádek požadavku má formu ‹METODA cesta HTTP/1.0›,
 *     ◦ stavový řádek má formu ‹HTTP/1.0 číselný_kód popis›,
 *  2. hlavičky – každé pole začíná jménem, následované dvojtečkou a
 *     textovou hodnotou, která pokračuje až do konce řádku,¹
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

struct http_header {
    char *name, *value;
};

struct http_header_list {
    struct http_header header;
    struct http_header_list *next;
};

/* Zjednodušený požadavek protokolu HTTP budeme reprezentovat
 * strukturou ‹http_request› – přitom budeme podporovat pouze dvě
 * metody, totiž ‹GET› a ‹HEAD›. Tělo požadavku bude v obou
 * případech prázdné. Prázdný seznam hlaviček je reprezentovaný
 * nulovým ukazatelem ‹headers›. */

enum http_method {
    HTTP_GET = 1,
    HTTP_HEAD
};

struct http_request {
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

int http_request_set_path(struct http_request *request,
                          const char *path) {
    if (request == NULL || path == NULL) {
        return -1;
    }
    request->path = strdup(path);
    if (request->path == NULL) {
        return -1;
    }
    return 0;
}

int http_request_add_header(struct http_request *request,
                            const char *field,
                            const char *value) {
    if (request == NULL || field == NULL || value == NULL) {
        return -1;
    }

    struct http_header *new_header = (struct http_header *) malloc(sizeof(struct http_header));
    if (new_header == NULL) {
        return -1;
    }

    new_header->name = strdup(field);
    new_header->value = strdup(value);
    if (new_header->name == NULL || new_header->value == NULL) {
        free(new_header->name);
        free(new_header->value);
        free(new_header);
        return -1;
    }

    struct http_header_list *new_node = malloc(sizeof(struct http_header_list));
    if (new_node == NULL) {
        free(new_header->name);
        free(new_header->value);
        free(new_header);
        return -1;
    }

    new_node->header = *new_header;
    new_node->next = request->headers;
    request->headers = new_node;
    return 0;
}

/* Následující funkce nechť požadavek zapíše do otevřeného
 * popisovače souboru. Tato funkce se Vám může hodit také
 * v implementaci procedury ‹http_request› níže. */

int http_request_write(struct http_request *request, int fd) {
    if (request == NULL || fd < 0) {
        return -1;
    }

    const char *method_str = (request->method == HTTP_GET) ? "GET" : "HEAD";
    if (dprintf(fd, "%s %s HTTP/1.0\r\n", method_str, request->path) < 0) {
        return -1;
    }

    struct http_header_list *current = request->headers;
    while (current != NULL) {
        if (dprintf(fd, "%s: %s\r\n", current->header.name, current->header.value) < 0) {
            return -1;
        }
        current = current->next;
    }

    if (dprintf(fd, "\r\n") < 0) {
        return -1;
    }

    return 0;
}

/* Konečně procedura ‹http_request_free› uvolní veškerou paměť
 * spojenou s požadavkem. Opětovné volání ‹http_request_free› na
 * stejný objekt nechť nemá žádný efekt. */

void http_request_free(struct http_request *request) {
    if (request == NULL) {
        return;
    }
    free(request->path);
    struct http_header_list *current = request->headers;
    while (current != NULL) {
        struct http_header_list *next = current->next;
        free(current->header.name);
        free(current->header.value);
        free(current);
        current = next;
    }
    memset(request, 0, sizeof(struct http_request));
}

/* Pro reprezentaci odpovědi serveru použijeme strukturu
 * ‹http_response›, která bude obsahovat kód odpovědi, hlavičky a
 * tělo. Podobně jako u předchozích typů, hodnota typu
 * ‹http_response› bude vlastnit veškerou potřebnou paměť.
 * V seznamu ‹headers› budou hlavičky seřazeny v pořadí, ve kterém
 * je server odeslal (dejte si pozor na efektivitu!). */

struct http_response {
    int code;
    struct http_header_list *headers;
    size_t body_length;
    char *body;
};

/* Procedura ‹http_response_read› přečte odpověď protokolu HTTP ze
 * zadaného popisovače a uloží ji do předané struktury
 * ‹http_response›. Výsledkem bude 0 proběhlo-li vše v pořádku, -1
 * při systémové chybě a -2 je-li odpověď špatně sestavená. */

struct buffer {
    char *data;
    int capacity;
    int len;
    int line_len;
};

bool realloc_double(char **string, int *capacity) {
    *capacity *= 2;
    char *tmp = malloc(sizeof(char) * *capacity);
    if (!tmp) {
        return false;
    }
    *string = tmp;
    return true;
}

#define BLOCK_SIZE 128

bool get_newline(struct buffer *buf) {
    for (int i = 0; i < buf->len - 1; ++i) {
        if (buf->data[i] == '\r' && buf->data[i + 1] == '\n') {
            buf->line_len = i + 2;
            return true;
        }
    }
    return false;
}

bool read_line(int fd, struct buffer *buf) {
    if (buf->line_len != 0) {
        buf->len -= buf->line_len;
        memmove(buf->data, &buf->data[buf->line_len], buf->len);
    }
    if (buf->capacity - buf->len - 1 < BLOCK_SIZE && !realloc_double(&buf->data, &buf->capacity)) {
        return false;
    }
    ssize_t nread;
    while ((nread = read(fd, &buf->data[buf->len], BLOCK_SIZE)) > 0) {
        buf->len += (int) nread;
        buf->data[buf->len] = '\0';
        if (get_newline(buf)) {
            return true;
        }
        if (buf->capacity - buf->len - 1 < BLOCK_SIZE && !realloc_double(&buf->data, &buf->capacity)) {
            return false;
        }
    }
    if (nread == -1) {
        return false;
    }
    buf->data[buf->len] = '\0';
    buf->line_len = 0;
    return true;
}

int http_response_read_code(struct http_response *response, int fd, struct buffer *buf) {
    if (!read_line(fd, buf)) {
        return -1;
    }
    char *http = "HTTP/1.0 ";
    if (buf->line_len <= strlen(http)) {
        return -2;
    }
    char *end;
    errno = 0;
    long code = strtol(buf->data, &end, 10);
    if (end == buf->data || *end != '\0' ||
        ((code == LONG_MIN || code == LONG_MAX) && errno == ERANGE)) {
        return -2;
    }
    response->code = (int) code;
    return 0;
}

int http_response_read_headers(struct http_response *response, int fd, struct buffer *buf) {
    while (true) {
        if (!read_line(fd, buf)) {
            return -1;
        }
        if (buf->line_len == 0) {
            return -2;
        }
        if (buf->line_len == 2 && buf->data[0] == '\r' && buf->data[1] == '\n') {
            return 0;
        }
        char *colon = strchr(buf->data, ':');
        if (colon == NULL) {
            return -2;
        }
        *colon = '\0';
        char *name = buf->data;
        char *value = colon + 1;
        struct http_header_list *new_header = malloc(sizeof(struct http_header_list));
        if (new_header == NULL) {
            return -1;
        }

        new_header->next = response->headers;
        response->headers = new_header;
        new_header->header.name = strdup(name);
        new_header->header.value = strdup(value);
        if (!new_header->header.name || !new_header->header.value) {
            return -1;
        }
    }
}

int http_response_read_body(struct http_response *response, int fd) {
    int capacity = BLOCK_SIZE;
    response->body = malloc(sizeof(char) * capacity);
    if (!response->body) {
        return -1;
    }
    size_t nread;
    while ((nread = read(fd, response->body + response->body_length, BLOCK_SIZE)) > 0) {
        response->body_length += nread;
        if (capacity - response->body_length < BLOCK_SIZE && !realloc_double(&response->body, &capacity)) {
            return -1;
        }
    }
    if (nread == -1) {
        return -1;
    }
    return 0;
}

int http_response_read(struct http_response *response, int fd_in) {
    if (response == NULL || fd_in < 0) {
        return -1;
    }
    memset(response, 0, sizeof(struct http_response));
    struct buffer buf = {.capacity = BLOCK_SIZE};
    buf.data = malloc(buf.capacity);
    if (!buf.data) {
        return -1;
    }
    int rv;
    if ((rv = http_response_read_code(response, fd_in, &buf)) != 0) {
        goto out;
    }
    if ((rv = http_response_read_headers(response, fd_in, &buf)) != 0) {
        goto out;
    }
    rv = http_response_read_body(response, fd_in);
    out:
    free(buf.data);
//    free(response);
    return rv;
}

/* Pro uvolnění veškeré paměti spojené s požadavkem slouží
 * následující procedura. Předaná hodnota ‹http_response› bude
 * uvedena do takového stavu, aby opětovné volání
 * ‹http_response_free› na stejné hodnotě neprovedlo žádnou akci. */

void http_response_free(struct http_response *response) {
    if (response == NULL) {
        return;
    }

    struct http_header_list *current = response->headers;
    while (current != NULL) {
        struct http_header_list *next = current->next;
        free(current->header.name);
        free(current->header.value);
        free(current);
        current = next;
    }
    free(response->body);
    memset(response, 0, sizeof(struct http_response));
}

/* Procedura ‹http_request› provede požadavek podle parametru
 * ‹request›. Začíná-li adresa tečkou nebo lomítkem, je
 * interpretována jako adresa unixového socketu. V opačném případě
 * je to hostitelské jméno počítače, ke kterému se má připojit,
 * případně následované dvojtečkou a číslem portu. Není-li port
 * uveden, použije standardní port 80.
 *
 * Procedura vyplní odpověď serveru do předané hodnoty typu
 * ‹response›. Návratová hodnota bude 0 proběhlo-li vše v pořádku,
 * -1 v případě systémové chyby a -2 v případě chybné odpovědi ze
 * strany serveru. Není-li výsledek 0, předaná hodnota ‹response›
 * zůstane nedotčena. */

int get_sock_un(const char *address) {
    int rv = -1;
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    struct sockaddr_un sockaddr = {.sun_family = AF_UNIX};
    if (strlen(address) >= sizeof sockaddr.sun_path - 1) {
        goto out;
    }
    snprintf(sockaddr.sun_path, sizeof sockaddr.sun_path, "%s", address);
    if (connect(sock, (struct sockaddr *) &sockaddr, sizeof(struct sockaddr_un)) == -1) {
        goto out;
    }
    rv = sock;
    out:
    close(sock);
    return rv;
}

int get_sock_in(const char *address) {
    int rv = -1;
    char *address_copy = strdup(address);
    if (address_copy == NULL) {
        return -1;
    }
    char *colon = strchr(address_copy, ':');
    char *port = "80";
    if (colon != NULL) {
        *colon = '\0';
        port = colon + 1;
    }

    struct addrinfo *result = NULL;
    struct addrinfo hints = {
            .ai_family = AF_INET,
            .ai_socktype = SOCK_STREAM,
            .ai_protocol = 0
    };
    int status = getaddrinfo(address_copy, port, &hints, &result);
    if (status != 0) {
        goto out;
    }

    int sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == -1) {
        goto out;
    }

    if (connect(sock, result->ai_addr, result->ai_addrlen) == -1) {
        close(sock);
        goto out;
    }

    rv = sock;
    out:
    free(address_copy);
    if (result) {
        freeaddrinfo(result);
    }
    return rv;
}

int http_request(const char *address,
                 struct http_request *request,
                 struct http_response *response) {
    int sock = (address[0] == '/' || address[0] == '.') ? get_sock_un(address) : get_sock_in(address);
    if (sock == -1) {
        return -1;
    }

    // TODO check for req free
    if (http_request_write(request, sock) == -1) {
        close(sock);
        return -1;
    }
    // TODO Není-li výsledek 0, předaná hodnota ‹response› zůstane nedotčena
    int rv = http_response_read(response, sock);
    close(sock);
    return rv;
}

/* Konečně procedura ‹http_get› navíc zařídí sestavení požadavku a
 * uložení těla odpovědi do souboru. Na rozdíl od procedury
 * ‹http_request› musí ‹http_get› korektně pracovat i s velkými
 * soubory (takovými, které se nevejdou do paměti celé najednou).
 * V sestavené hlavičce vyplní pole ‹Host› tak, aby odpovídalo
 * zadanému hostitelskému jménu vzdáleného počítače. Soubor pak
 * uloží do složky ‹dir_fd› pod jménem, které odpovídá poslední
 * položce cesty ‹path›. */

int http_get(const char *host, const char *path, int dir_fd) {
    int rv = -1;
    struct http_request request = {.method = HTTP_GET};
    struct http_response response = {0};
    struct buffer buf = {.capacity = BLOCK_SIZE};
    int fd = -1;
    int sock = -1;
    if (http_request_set_path(&request, path) == -1 || http_request_add_header(&request, "Host", host)) {
        goto out;
    }

    sock = (host[0] == '/' || host[0] == '.') ? get_sock_un(host) : get_sock_in(host);
    if (sock == -1) {
        goto out;
    }
    if (http_request_write(&request, sock) == -1) {
        goto out;
    }

    buf.data = malloc(buf.capacity);
    if (!buf.data) {
        return -1;
    }
    int status = http_response_read_code(&response, sock, &buf);
    if (status != 0) {
        rv = status;
        goto out;
    }
    status = http_response_read_headers(&response, sock, &buf);
    if (status != 0) {
        rv = status;
        goto out;
    }

    const char *filename = strrchr(path, '/');
    if (filename == NULL) {
        filename = path;
    } else {
        filename++;
    }
    fd = openat(dir_fd, filename, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        goto out;
    }
    size_t nread;
    while ((nread = read(fd, buf.data, BLOCK_SIZE)) > 0) {
        if (write(fd, buf.data, nread) == -1) {
            goto out;
        }
    }
    if (nread == -1) {
        goto out;
    }
    rv = 0;
    out:
    free(buf.data);
    http_request_free(&request);
    http_response_free(&response);
    if (fd != -1) {
        close(fd);
    }
    if (sock != -1) {
        close(sock);
    }
    return rv;
}

/* ¹ Víceřádkové hlavičky pro zjednodušení nebudeme uvažovat. */

int main(void) {
    int fd_req, fd_res;
    int bytes;
    struct http_request req = {.path = NULL, .headers = NULL};
    struct http_response res;
    char buffer[512];

    req.method = HTTP_GET;

    if (http_request_set_path(&req, "/index"))
        err(1, "setting request path");
    if (http_request_set_path(&req, "/index.html"))
        err(1, "setting request path");

    if (http_request_add_header(&req, "Host", "www.fi.muni.cz"))
        err(1, "setting request header");

    if ((fd_req = open("zt.request.txt",
                       O_CREAT | O_RDWR | O_TRUNC,
                       0666)) == -1)
        err(1, "creating zt.request.txt");

    if (http_request_write(&req, fd_req) == -1)
        err(1, "writing request to zt.request.txt");

    if ((bytes = pread(fd_req, buffer, 512, 0)) == -1)
        err(1, "reading request from zt.request.txt");

    assert(strncmp(buffer, "GET /index.html HTTP/1.0\r\n"
                           "Host: www.fi.muni.cz\r\n\r\n",
                   bytes) == 0);

    http_request_free(&req);

    if ((fd_res = open("zz.response.txt", O_RDONLY)) == -1)
        err(1, "opening zz.response.txt");

    assert(http_response_read(&res, fd_res) == 0);
    assert(res.code == 200);
    assert(res.body_length == 7);
    assert(memcmp(res.body, "hello\r\n", 7) == 0);
    assert(strcmp(res.headers->header.name,
                  "Content-Length") == 0);
    assert(strcmp(res.headers->header.value, "7") == 0);
    assert(strcmp(res.headers->next->header.name,
                  "Content-Type") == 0);

    http_response_free(&res);
    return 0;
}
