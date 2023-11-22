#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>  /* mkdirat, openat, fchmodat */
#include <sys/types.h> /* mkdirat, openat */
#include <unistd.h>    /* unlinkat, close, write, fchmodat */
#include <fcntl.h>     /* mkdirat, unlinkat, openat, linkat, … */
#include <string.h>    /* strlen */
#include <stdbool.h>   /* bool */
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <dirent.h>

/* Naprogramujte proceduru ‹check_access›, která na vstupu dostane:
 *
 *  1. popisovač otevřené složky ‹dir_fd›,
 *  2. spojovaný seznam ‹names› jmen.
 *
 * Tato ze seznamu odstraní všechna jména souborů, které jsou
 * chráněny proti zápisu třetí stranou a ponechá ty, do kterých může
 * zapisovat kdokoliv jiný, než vlastník. Všechna jména jsou
 * relativní vůči složce ‹dir_fd›.
 *
 * Práva souboru zjistíte z položky ‹st_mode› struktury ‹stat›.
 * Přijdou Vám zřejmě vhod také hodnoty ‹S_IWGRP› (Stat Inode Write
 * GRouP – bitová maska odpovídající právu skupiny na zápis) a
 * ‹S_IWOTH› (Stat Inode Write OTHer – totéž, ale pro všechny
 * ostatní).
 *
 * Návratová hodnota:
 *
 *  • -2 v případě, kdy nebylo možné práva u některého jména ověřit
 *    (např. proto, že takový odkaz neexistuje),
 *  • -1 při neopravitelné systémové chybě (např. chyba při načítání
 *    položek adresáře),
 *  • 0 při úspěšném dokončení.
 *
 * Odkazy, u kterých se nepodařilo přístupová práva ověřit, ponechte
 * v seznamu. */

struct name_list_node {
    const char *name;
    struct name_list_node *next;
};

struct name_list {
    struct name_list_node *head;
};

//void remove_if_present(const char *name, struct name_list *names) {
//    struct name_list_node *node = names->head;
//    struct name_list_node *prev = NULL;
//    while (node) {
//        if (strcmp(node->name, name) == 0) {
//            if (prev) {
//                prev->next = node->next;
//            } else {
//                names->head = node->next;
//            }
//        }
//        prev = node;
//        node = node->next;
//    }
//}

int check_access(int dir_fd, struct name_list *names) {
    if (!names) {
        return 0;
    }
    DIR *dir = NULL;
    int rv = -1;

    int dup_fd = dup(dir_fd);
    if (dup_fd == -1) {
        goto out;
    }

    dir = fdopendir(dup_fd);
    if (!dir) {
        goto out;
    }

    struct dirent *ptr;
    struct stat st;
    struct name_list_node *node = names->head;
    struct name_list_node *prev = NULL;
    bool found_file = false;
    while (node) {
        rewinddir(dir);
        while ((ptr = readdir(dir))) {
            if (fstatat(dir_fd, ptr->d_name, &st,
                        AT_SYMLINK_NOFOLLOW) == -1) {
                goto out;
            }
            if (strcmp(ptr->d_name, node->name) == 0) {
                found_file = true;
                if (!(st.st_mode & S_IWOTH) && !(st.st_mode & S_IWGRP)) {
                    if (prev) {
                        prev->next = node->next;
                    } else {
                        names->head = node->next;
                    }
                }
                break;
            }
        }
        if (!found_file) {
            rv = -2;
            goto out;
        }
        found_file = false;
        prev = node;
        node = node->next;
    }

    rv = 0;
    out:
    if (dir)
        closedir(dir);
    else if (dup_fd != -1)
        close(dup_fd);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static int create_and_open_dir(int base_dir, const char *name) {
    if (mkdirat(base_dir, name, 0777) == -1)
        err(2, "creating directory %s", name);

    int fd = openat(base_dir, name, O_RDONLY | O_DIRECTORY, 0777);

    if (fd == -1)
        err(2, "opening directory %s", name);

    return fd;
}

static void unlink_if_exists(int fd, const char *name, bool is_dir) {
    if (unlinkat(fd, name, is_dir ? AT_REMOVEDIR : 0) == -1 &&
        errno != ENOENT)
        err(2, "unlinking %s", name);
}

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static int create_file(int dir_fd, const char *name, int mode) {
    int fd;

    if ((fd = openat(dir_fd, name,
                     O_CREAT | O_TRUNC | O_RDWR,
                     mode)) == -1)
        err(2, "creating %s", name);

    return fd;
}

static void write_file(int dir, const char *name, const char *str,
                       int mode) {
    int fd = create_file(dir, name, mode);

    if (write(fd, str, strlen(str)) == -1)
        err(2, "writing file %s", name);

    close_or_warn(fd, name);
}

static void test(int dir) {
    const char *names[] =
            {
                    "zt.p2_a", "zt.p2_b", "zt.p2_c",
                    "zt.p2_d", "zt.p2_e"
            };

    const char *contents[] =
            {
                    "Never let me down", "Never let me down",
                    "See the stars, they're shining bright",
                    "Never let me down", "Everything's alright tonight"
            };

    struct name_list_node
            fifth = {.name = names[4], .next = NULL},
            fourth = {.name = names[3], .next = &fifth},
            third = {.name = names[2], .next = &fourth},
            second = {.name = names[1], .next = &third},
            first = {.name = names[0], .next = &second};

    struct name_list lst = {.head = &first};

    mode_t prev_mask = umask(0000);

    for (int i = 0; i < 5; ++i)
        write_file(dir, names[i], contents[i], 0666);

    assert(check_access(-1, &lst) == -1);
    assert(check_access(dir, &lst) == 0);

    struct name_list_node *current = lst.head;
    for (int i = 0; i < 5; ++i) {
        assert(current != NULL);
        assert(strcmp(current->name, names[i]) == 0);
        current = current->next;
    }

    if (fchmodat(dir, names[4], 0644, 0) == -1)
        err(2, "fchmodat on %s", names[4]);

    assert(check_access(dir, &lst) == 0);
    current = lst.head;
    for (int i = 0; i < 4; ++i) {
        assert(current != NULL);
        assert(strcmp(current->name, names[i]) == 0);
        current = current->next;
    }
    assert(current == NULL);

    if (fchmodat(dir, names[1], 0644, 0) == -1)
        err(2, "fchmodat on %s", names[4]);

    assert(check_access(dir, &lst) == 0);
    assert(strcmp(lst.head->name, names[0]) == 0);
    assert(strcmp(lst.head->next->name, names[2]) == 0);
    assert(strcmp(lst.head->next->next->name, names[3]) == 0);
    assert(lst.head->next->next->next == NULL);

    struct name_list_node sixth =
            {.name = "zt.p2_f", .next = NULL};

    lst.head->next->next->next = &sixth;
    assert(check_access(dir, &lst) == -2);

    lst.head->next->next->next = NULL;
    sixth.next = lst.head->next;
    lst.head->next = &sixth;

    assert(check_access(dir, &lst) == -2);

    umask(prev_mask);
    for (int i = 0; i < 5; ++i) {
        unlink_if_exists(dir, names[i], false);
    }
}

int main(void) {
    assert(check_access(-1, NULL) == 0);

    unlink_if_exists(AT_FDCWD, "zt.p2_test_dir1", true);
    int dir1 = create_and_open_dir(AT_FDCWD, "zt.p2_test_dir1");

    assert(check_access(dir1, NULL) == 0);
    test(dir1);

    close_or_warn(dir1, "zt.p2_test_dir1");
    unlink_if_exists(AT_FDCWD, "zt.p2_test_dir1", true);

    return 0;
}
