#define _POSIX_C_SOURCE 200809L

#include <stdio.h>      /* dprintf */
#include <assert.h>     /* assert */
#include <string.h>     /* strcmp, memset */
#include <unistd.h>     /* unlinkat, close */
#include <fcntl.h>      /* open, openat */
#include <errno.h>      /* errno */
#include <err.h>        /* NONPOSIX: err */
#include <stdbool.h>
#include <stdlib.h>

#define BLOCK_SIZE 128
/* Naprogramujte proceduru ‹cat›, která obdrží tyto 3 parametry:
 *
 *   • ‹dir_fd› – popisovač adresáře, ve kterém bude hledat všechny
 *     níže zmíněné soubory,
 *   • ‹list_fd› – popisovač, ze kterého podprogram ‹cat› přečte
 *     jména souborů,
 *   • ‹out_fd› – «výstupní» popisovač.
 *
 * Vstup ‹list_fd› bude obsahovat na každém řádku jméno souboru
 * (samotný seznam nemusí být nutně zapsán v souboru). Procedura
 * ‹cat› zapíše obsahy všech těchto souborů (v zadaném pořadí) do
 * popisovače ‹out›. Za řádek považujeme posloupnost znaků
 * «zakončenou» ‹'\n'› (nikoliv tedy ‹"\r\n"› nebo ‹'\r'›).
 *
 * Nepodaří-li se nějaký soubor otevřít, přeskočte jej. Návratová
 * hodnota:
 *
 *  • 0 – podařilo se otevřít, přečíst a zapsat všechny soubory,
 *  • -1 – chyba čtení z popisovače ‹list_fd›, chyba zápisu do
 *    ‹out_fd›, nebo jiná fatální systémová chyba,
 *  • -2 – vše proběhlo v pořádku, ale některé soubory byly
 *    přeskočeny, protože se je nepodařilo otevřít. */

bool copy_file_content(int fd_in, int fd_out) {
    int bytes_transferred;
    char buffer[BLOCK_SIZE];
    while ((bytes_transferred = read(fd_in, buffer, BLOCK_SIZE)) > 0) {
        if (write(fd_out, buffer, bytes_transferred) == -1) {
            return false;
        }
    }
    if (bytes_transferred == -1) {
        return false;
    }
    return true;
}

int cat(int dir_fd, int list_fd, int out_fd) {
    int rv = -1;
    FILE *list_file = fdopen(list_fd, "r");
    if (!list_file) {
        return rv;
    }
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    int file = -1;
    errno = 0;
    while ((nread = getline(&line, &len, list_file)) != -1) {
        if (line[nread - 1] == '\n') {
            line[nread - 1] = '\0';
        }
        file = openat(dir_fd, line, O_RDONLY);
        if (file == -1) {
            rv = -2;
            continue;
        }
        if (!copy_file_content(file, out_fd)) {
            goto error;
        }
        close(file);
        file = -1;
    }
    if (errno) {
        goto error;
    }
    if (rv != -2) {
        rv = 0;
    }
    error:
    free(line);
//    fclose(list_file);
    if (file != -1) {
        close(file);
    }
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void unlink_if_exists(int dir, const char *name) {
    if (unlinkat(dir, name, 0) == -1 && errno != ENOENT)
        err(2, "unlinking %s", name);
}

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static int open_or_die(int dir_fd, const char *name) {
    int fd = openat(dir_fd, name, O_RDONLY);

    if (fd == -1)
        err(2, "opening %s", name);

    return fd;
}

static int create_file(int dir_fd, const char *name) {
    unlink_if_exists(dir_fd, name);
    int fd;

    if ((fd = openat(dir_fd, name,
                     O_CREAT | O_TRUNC | O_RDWR,
                     0666)) == -1)
        err(2, "creating %s", name);

    return fd;
}

static int check_cat(int dir_fd, int list_fd) {
    int out_fd, result;
    const char *out_name = "zt.p6_test_out";

    if (lseek(list_fd, 0, SEEK_SET) != 0)
        err(1, "lseek");

    out_fd = create_file(dir_fd, out_name);
    result = cat(dir_fd, list_fd, out_fd);
    close_or_warn(out_fd, out_name);

    return result;
}

static int check_output(const char *expected) {
    const char *name = "zt.p6_test_out";
    char buffer[4096 + 1] = {0};

    int read_fd = open_or_die(AT_FDCWD, name);

    if (read(read_fd, buffer, 4096) == -1)
        err(2, "reading %s", name);

    close_or_warn(read_fd, name);
    return strcmp(expected, buffer);
}

static void write_file(int dir, const char *name, const char *str) {
    int fd = create_file(dir, name);

    if (write(fd, str, strlen(str)) == -1)
        err(2, "writing file %s", name);

    close_or_warn(fd, name);
}

int main(void) {
    int dir;

    if ((dir = open(".", O_RDONLY)) == -1)
        err(2, "opening working directory");

    write_file(dir, "zt.p6_a", "contents of zt.p6_a\n");
    write_file(dir, "zt.p6_b", "contents of zt.p6_b\n");
    write_file(dir, "zt.p6_c", "contents of zt.p6_c\n");
    write_file(dir, "zt.p6_d", "contents of zt.p6_d\n");

    unlink_if_exists(dir, "zt.p6_test_out");
    unlink_if_exists(dir, "zt.p6_lst1");
    unlink_if_exists(dir, "zt.p6_lst2");
    unlink_if_exists(dir, "zt.p6_lst3");
    unlink_if_exists(dir, "zt.p6_lst4");

    int lst1 = create_file(dir, "zt.p6_lst1");
    int lst2 = create_file(dir, "zt.p6_lst2");
    int lst3 = create_file(dir, "zt.p6_lst3");
    int lst4 = create_file(dir, "zt.p6_lst4");

    dprintf(lst1, "%s\n", "zt.p6_a");
    dprintf(lst2, "%s\n%s\n%s\n", "zt.p6_a", "zt.p6_b", "zt.p6_c");
    dprintf(lst3, "%s\n%s\n%s\n%s\n%s\n%s\n",
            "zt.p6_a", "zt.p6_b", "zt.p6_c",
            "zt.p6_d", "zt.p6_a", "zt.p6_b");

    assert(check_cat(dir, lst1) == 0);
    assert(check_output("contents of zt.p6_a\n") == 0);

    assert(check_cat(dir, lst2) == 0);
    assert(check_output("contents of zt.p6_a\n"
                        "contents of zt.p6_b\n"
                        "contents of zt.p6_c\n") == 0);

    assert(check_cat(dir, lst3) == 0);
    assert(check_output("contents of zt.p6_a\n"
                        "contents of zt.p6_b\n"
                        "contents of zt.p6_c\n"
                        "contents of zt.p6_d\n"
                        "contents of zt.p6_a\n"
                        "contents of zt.p6_b\n") == 0);

    unlink_if_exists(dir, "zt.p6_a");

    assert(check_cat(dir, lst3) == -2);
    assert(check_output("contents of zt.p6_b\n"
                        "contents of zt.p6_c\n"
                        "contents of zt.p6_d\n"
                        "contents of zt.p6_b\n") == 0);

    assert(check_cat(dir, lst1) == -2);
    assert(check_output("") == 0);

    unlink_if_exists(dir, "zt.p6_a");
    unlink_if_exists(dir, "zt.p6_b");
    unlink_if_exists(dir, "zt.p6_c");
    unlink_if_exists(dir, "zt.p6_d");
    unlink_if_exists(dir, "zt.p6_lst1");
    unlink_if_exists(dir, "zt.p6_lst2");
    unlink_if_exists(dir, "zt.p6_lst3");
    unlink_if_exists(dir, "zt.p6_lst4");
    unlink_if_exists(dir, "zt.p6_test_out");
    close_or_warn(dir, "working directory");

    if (close(lst1) || close(lst2) ||
        close(lst3) || close(lst4)) {
        err(2, "close");
    }

    return 0;
}
