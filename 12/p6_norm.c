#define _POSIX_C_SOURCE 200809L

#include <string.h>     /* strcmp */
#include <stdlib.h>     /* free */
#include <assert.h>
#include <stdio.h>

/* Napište podprogram ‹path_normalize›, který odstraní duplikáty ze
 * zadané proměnné prostředí, která obsahuje seznam cest oddělených
 * dvojtečkami (jako např. proměnná ‹PATH›). Výsledkem bude ukazatel
 * na dynamicky alokované pole vhodné velikosti, ve kterém bude
 * uložen nulou ukončený řetězec. Nastane-li nějaká chyba, výsledkem
 * nechť je nulový ukazatel.
 *
 * Cesty srovnávejte jako řetězce – není potřeba sjednocovat např.
 * položky ‹/foo:/foo/› které sice označují stejný adresář, ale jsou
 * jinak zapsané. Výsledná hodnota musí být svým významem
 * ekvivalentní té původní. */


int is_token_in_result(const char *token, const char *result) {
    size_t token_len = strlen(token);
    char *ptr = strstr(result, token);
    while (ptr != NULL) {
        if ((ptr == result || ptr[-1] == ':') && ptr[token_len] == ':') {
            return 1;
        }
        ptr = strstr(ptr + 1, token);
    }
    return 0;
}

char *path_normalize(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    char *copy = strdup(path);
    if (copy == NULL) {
        return NULL;
    }
    char *result = malloc(strlen(path) + 2);
    if (result == NULL) {
        free(copy);
        return NULL;
    }
    result[0] = '\0';
    char *token = strtok(copy, ":");
    while (token != NULL) {
        if (!is_token_in_result(token, result)) {
            strcat(result, token);
            strcat(result, ":");
        }
        token = strtok(NULL, ":");
    }

    result[strlen(result) - 1] = '\0';

    free(copy);
    return result;
}

int main() {
    const char *p1 = "/foo:/bar:/foo",
            *p2 = "/foo",
            *p3 = "/foo/bar:/bar:/foo:/foo/bar",
            *p4 = "/foo/bar:/bar:/foo/bar:/foo",
            *p5 = "/foo/bar:/bar:/foo/bar:/bar",
            *p6 = "/foo:/foo:/foo:/foo";

    const char *e1 = "/foo:/bar",
            *e2 = p2,
            *e3 = "/foo/bar:/bar:/foo",
            *e4 = "/foo/bar:/bar:/foo",
            *e5 = "/foo/bar:/bar",
            *e6 = "/foo";

    char *n1 = path_normalize(p1),
            *n2 = path_normalize(p2),
            *n3 = path_normalize(p3),
            *n4 = path_normalize(p4),
            *n5 = path_normalize(p5),
            *n6 = path_normalize(p6);
    assert(strcmp(n1, e1) == 0);
    assert(strcmp(n2, e2) == 0);
    assert(strcmp(n3, e3) == 0);
    assert(strcmp(n4, e4) == 0);
    assert(strcmp(n5, e5) == 0);
    assert(strcmp(n6, e6) == 0);

    free(n1);
    free(n2);
    free(n3);
    free(n4);
    free(n5);
    free(n6);

    return 0;
}
