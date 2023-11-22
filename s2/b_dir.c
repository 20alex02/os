#define _POSIX_C_SOURCE 200809L

#include <stddef.h>     /* NULL */
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

/* Souborové systémy bývají organizovány jako hierarchie souborů
 * a složek. Vaším úkolem je naprogramovat procedury pro vyjádření
 * obsahu podstromu zadaného adresářem.
 *
 * Výstup se bude realizovat skrze strukturu ‹node›, která má
 * následující atributy:
 *  • ‹name› – ukazatel na nulou zakončený řetězec se jménem
 *    souboru;
 *  • ‹type› – hodnota výčtového typu níže popisující typ souboru;
 *  • ‹error› – hodnota ‹0›, nebo informace o nastalé chybě;
 *  • ‹dir› – v případě, že se jedná o adresář, obsahuje ukazatel na
 *    zřetězený seznam souborů, anebo ‹NULL›, pokud je adresář
 *    prázdný;
 *  • ‹next› – ukazatel na další prvek ve stejném adresáři, anebo
 *    ‹NULL›, pokud je poslední.
 *
 * Tato struktura reprezentuje jednu položku v adresářovém
 * podstromě. Položka je buď adresářem, nebo jiným souborem. Jelikož
 * se jedná o reprezentaci zřetězeným seznamem, struktura obsahuje
 * ukazatele na následníka a potomka (‹next› a ‹dir›).
 *
 * Atributy ‹name›, ‹dir› a ‹next› jsou ukazatele na dynamicky
 * alokovanou paměť, kterou následně musí být možné uvolnit
 * zavoláním ‹free›.
 *
 * Strukturu nemůžete nijak měnit.
 *
 * Výčtový typ ‹file_type› popisuje pět typů souborů, které budeme
 * rozlišovat: adresář, obyčejný soubor, symbolický odkaz, socket
 * a vše ostatní. */

enum file_type {
    t_directory,
    t_regular,
    t_symlink,
    t_socket,
    t_other,
};

struct node {
    char *name;
    enum file_type type;

    int error;

    struct node *dir;
    struct node *next;
};

/* Úkol spočívá v implementaci dvou procedur níže. První z nich je
 * ‹tree_create›, která bere parametry:
 *  • ‹at› – popisovač adresáře, ve kterém hledat složku, nebo
 *    konstanta ‹AT_FDCWD›;
 *  • ‹root_name› – název počátečního adresáře, který se má
 *    prohledávat;
 *  • ‹out› – výstupní ukazatel, do něhož má být uložen ukazatel
 *    na alokovanou zřetězenou strukturu.
 *
 * Její návratovou hodnotou bude:
 *  • ‹0› – úspěch;
 *  • ‹-1› – selhání pří přístupu k některému souboru či složce;
 *  • ‹-2› – selhání při alokaci, což je kritická chyba, a výstupní
 *    ‹out› nechť je nastaven na ‹NULL›.
 *
 * Jestliže je návratová hodnota ‹-1›, u dotčených souborů bude
 * atribut ‹error› nastaven na odpovídající hodnotu proměnné
 * ‹errno›. Jinak má být hodnota tohoto atributu ‹0›. */

enum file_type get_file_type(mode_t mode) {
    if (S_ISREG(mode)) {
        return t_regular;
    }
    if (S_ISDIR(mode)) {
        return t_directory;
    }
    if (S_ISSOCK(mode)) {
        return t_socket;
    }

    if (S_ISLNK(mode)) {
        return t_symlink;
    }
    return t_other;
}

bool set_node(struct node *node, const char *name, enum file_type type,
              int error, struct node *dir, struct node *next) {
    char *new_name = strdup(name);
    if (!new_name) {
        return false;
    }
    node->name = new_name;
    node->type = type;
    node->error = error;
    node->dir = dir;
    node->next = next;
    return true;
}

bool append_node(struct node **node, const char *name,
                 enum file_type type, int error, bool directory) {
    struct node *new_node = malloc(sizeof(struct node));
    if (!new_node) {
        return false;
    }
    if (!set_node(new_node, name, type, error, NULL, NULL)) {
        free(new_node);
        return false;
    }
    if (directory) {
        (*node)->dir = new_node;
    } else {
        (*node)->next = new_node;
    }
    *node = new_node;
    return true;
}

/* Je rovněž potřeba implementovat odpovídající uvolňovací
 * proceduru. Tou je zde ‹tree_free›, která musí být schopna
 * přijmout výstupní ukazatel z ‹tree_create› a ten uvolnit, včetně
 * všech přidělených zdrojů. */

void tree_free(struct node *tree) {
    if (tree == NULL) {
        return;
    }
    free(tree->name);

    tree_free(tree->dir);
    tree_free(tree->next);

    free(tree);
}

int tree_create_rec(int root_fd, struct node *node) {
    if (root_fd == -1) {
        node->error = errno;
        return -1;
    }

    int rv = -1;
    DIR *dir = fdopendir(root_fd);
    if (!dir) {
        node->error = errno;
        goto out;
    }

    rewinddir(dir);
    struct dirent *ptr;
    struct stat st;
    bool error = false;
    bool directory = true;
    while ((ptr = readdir(dir))) {
        if (strcmp(ptr->d_name, ".") == 0 ||
            strcmp(ptr->d_name, "..") == 0) {
            continue;
        }

        if (fstatat(root_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1) {
            if (!append_node(&node, ptr->d_name, t_other, errno, directory)) {
                rv = -2;
                goto out;
            }
            continue;
        }
        if (!append_node(&node, ptr->d_name, get_file_type(st.st_mode), 0, directory)) {
            rv = -2;
            goto out;
        }
        directory = false;
        if (!S_ISDIR(st.st_mode)) {
            continue;
        }
        int sub_fd = openat(root_fd, ptr->d_name,
                            O_DIRECTORY | O_RDONLY);
        rv = tree_create_rec(sub_fd, node);
        if (rv == -2) {
            goto out;
        }
        if (rv == -1) {
            error = true;
        }
    }

    rv = error ? -1 : 0;
    out:
    if (dir) {
        closedir(dir);
    }
    return rv;
}

int tree_create(int at, const char *root_name, struct node **out) {
    int rv = -1;
    int fd = -1;
    *out = malloc(sizeof(struct node));
    if (!(*out) || !set_node(*out, root_name, t_directory, 0, NULL, NULL)) {
        rv = -2;
        goto cleanup;
    }
    fd = openat(at, root_name, O_DIRECTORY);
    if (fd == -1) {
        (*out)->error = errno;
        goto cleanup;
    }
    rv = tree_create_rec(dup(fd), *out);
    cleanup:
    close(fd);
    if (rv == -2) {
        tree_free(*out);
        *out = NULL;
    }
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>     /* assert */
#include <sys/stat.h>   /* mkdirat */
#include <fcntl.h>      /* mkdirat, openat */
#include <unistd.h>     /* unlinkat, close */
#include <string.h>     /* strcmp */
#include <fcntl.h>      /* unlinkat */
#include <errno.h>      /* errno */
#include <err.h>        /* err, warn */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(int at, const char *name) {
    if (unlinkat(at, name, 0) == -1 && errno != ENOENT)
        err(2, "unlinking %s", name);
}

static void rmdir_if_exists(int at, const char *name) {
    if (unlinkat(at, name, AT_REMOVEDIR) == -1 && errno != ENOENT)
        err(2, "removing directory %s", name);
}

static int open_dir_at(int at, const char *name) {
    int fd = openat(at, name, O_RDONLY | O_DIRECTORY);

    if (fd == -1)
        err(2, "opening dir %s", name);

    return fd;
}

static void create_file(int at, const char *name) {
    int fd = openat(at, name, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd == -1)
        err(2, "creating %s", name);
    close_or_warn(fd, name);
}

static int create_dir(int at, const char *dir) {
    rmdir_if_exists(at, dir);

    if (mkdirat(at, dir, 0755) == -1)
        err(2, "creating directory %s", dir);

    return open_dir_at(at, dir);
}

int main(void) {
    unlink_if_exists(AT_FDCWD, "zt.b_root/folder/file_b");
    rmdir_if_exists(AT_FDCWD, "zt.b_root/folder");
    unlink_if_exists(AT_FDCWD, "zt.b_root/file_a");
    rmdir_if_exists(AT_FDCWD, "zt.b_root");

    int root = create_dir(AT_FDCWD, "zt.b_root");
    int folder = create_dir(root, "folder");
    create_file(root, "file_a");
    create_file(folder, "file_b");
    close_or_warn(folder, "folder");

    struct node *tree;
//    assert(tree_create(AT_FDCWD, "test_no_access", &tree) == -1);
//    assert(tree->error == EACCES);
//    tree_free(tree);
//
//    assert(tree_create(AT_FDCWD, "basic_test", &tree) == -1);
//    // todo other asserts
//    tree_free(tree);

    tree_create(AT_FDCWD, "zt.b_root", &tree);

    assert(tree->type == t_directory);
    assert(tree->dir != NULL);
    assert(tree->next == NULL);
    assert(strcmp(tree->name, "zt.b_root") == 0);
    assert(tree->dir->next != NULL);
    assert(tree->dir->next->next == NULL);

    assert(tree->dir->type == t_directory
           || tree->dir->next->type == t_directory);

    const struct node *tree_folder;
    const struct node *tree_file;
    if (tree->dir->type == t_directory) {
        tree_folder = tree->dir;
        tree_file = tree->dir->next;
    } else {
        tree_folder = tree->dir->next;
        tree_file = tree->dir;
    }

    assert(strcmp(tree_folder->name, "folder") == 0);
    assert(strcmp(tree_file->name, "file_a") == 0);
    assert(tree_folder->type == t_directory);
    assert(tree_folder->dir != NULL);
    assert(strcmp(tree_folder->dir->name, "file_b") == 0);

    tree_free(tree);

    close_or_warn(root, "zt.b_root");

    unlink_if_exists(AT_FDCWD, "zt.b_root/folder/file_b");
    rmdir_if_exists(AT_FDCWD, "zt.b_root/folder");
    unlink_if_exists(AT_FDCWD, "zt.b_root/file_a");
    rmdir_if_exists(AT_FDCWD, "zt.b_root");
    return 0;
}
