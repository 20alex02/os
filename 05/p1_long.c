#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* close, write, pipe */
#include <errno.h>      /* errorno */
#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <stdlib.h>     /* free */
#include <string.h>     /* strlen, memcmp */
#include <stdbool.h>
#include <stdio.h>

/* Napište podprogram ‹longest_line›, který přečte veškerá data ze
 * zadaného popisovače,¹ a nalezne v nich nejdelší řádek. Tento
 * řádek pak uloží do dynamicky alokovaného pole vhodné velikosti a
 * vrátí ukazatel na toto pole. Za řádky považujeme pouze sekvence
 * znaků ukončené znakem konce řádku (případný nekompletní řádek na
 * konci souboru ignorujeme).
 *
 * Program nesmí alokovat více dynamické paměti, než je trojnásobek
 * délky nalezeného nejdelšího řádku (+ 128 bajtů paměti k dobru).²
 * Smíte předpokládat, že vstup neobsahuje nulové bajty. V případě
 * neúspěchu podprogram vrátí nulový ukazatel (včetně situace, kdy
 * vstup neobsahuje žádný kompletní řádek).
 *
 * ¹ Můžete předpokládat, že na popisovači bude fungovat ‹lseek›.
 * ² Tento limit se vztahuje i na knihovní podprogramy, které
 *   využijete. Pozor, ‹fdopen› používá na mnoha systémech
 *   dynamickou paměť. */

bool realloc_line(char **string, size_t capacity) {
    char *tmp = realloc(*string, capacity * sizeof(char));
    if (!tmp) {
        return false;
    }
    *string = tmp;
    return true;
}

char *longest_line(int fd) {
    char *line = NULL;
    char *longest_line = NULL;
    FILE *file = fdopen(fd, "r");
    if (!file) {
        goto error;
    }
    size_t longest_len = 0;
    size_t len = 0;
    ssize_t nread;
    errno = 0;
    while ((nread = getline(&line, &len, file)) != -1) {
        if (line[nread - 1] != '\n') {
            break;
        }
        if ((size_t) nread + 1 > longest_len) {
            longest_len = nread + 1;
            if (!realloc_line(&longest_line, longest_len)) {
                goto error;
            }
            strncpy(longest_line, line, longest_len);
        }
    }
    if (errno) {
        goto error;
    }

    free(line);
//    fclose(file);
    return longest_line;
    error:
    free(line);
    free(longest_line);
//    fclose(file);
    return NULL;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <fcntl.h>      /* openat */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
}

static int check(const char *input, const char *expected) {
    const char *name = "zt.p1_long";

    unlink_if_exists(name);

    int fd = openat(AT_FDCWD, name, O_CREAT | O_WRONLY, 0755);

    if (fd == -1)
        err(2, "creating %s", name);
    if (write(fd, input, strlen(input)) == -1)
        err(2, "writing into %s", name);

    close_or_warn(fd, name);

    fd = open(name, O_RDONLY);

    if (fd == -1)
        err(2, "opening %s for reading", name);

    char *res = longest_line(fd);
    close_or_warn(fd, name);
    int rv;

    if (expected)
        rv = memcmp(res, expected, strlen(expected));
    else
        rv = (res != NULL);

    free(res);
    unlink_if_exists(name);
    return rv;
}

int main() {

    assert(check("hello\n", "hello\n") == 0);
    assert(check("Lorem\nipsum\n", "Lorem\n") == 0);
    assert(check("dolor\nsit\n", "dolor\n") == 0);
    assert(check("amet,\nconsectetur\nadipiscing\n",
                 "consectetur\n") == 0);
    assert(check("elit\nSed et nisi\negestas\n",
                 "Sed et nisi\n") == 0);
    assert(check("dapibus\nfelis\nquis, feugiat est.\n",
                 "quis, feugiat est.\n") == 0);
    assert(check("\n", "\n") == 0);
    assert(check("\n\n", "\n") == 0);
    assert(check("", NULL) == 0);

    assert(check("hello?", NULL) == 0);
    assert(check("anybody\nthere?", "anybody\n") == 0);

    const char *longish =
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.\n"
            "Sed et nisi egestas, dapibus felis quis, feugiat est.\n"
            "Phasellus pellentesque sem quam, "
            "ac ullamcorper erat pharetra id.\n"
            "Curabitur non suscipit sapien, nec mattis massa.\n"
            "Curabitur convallis vulputate nunc id rhoncus.\n"
            "Donec sit amet justo nulla.\n"
            "Sed posuere tincidunt lacus euismod faucibus.\n"
            "Pellentesque condimentum facilisis nibh, "
            "non rutrum turpis rhoncus vitae.\n"
            "Phasellus sodales tellus nec tincidunt blandit.\n";

    assert(check(longish,
                 "Pellentesque condimentum facilisis nibh, "
                 "non rutrum turpis rhoncus vitae.\n") == 0);

    return 0;
}
