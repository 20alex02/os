#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>  /* mkdirat, openat */
#include <unistd.h>    /* unlinkat, close, write */
#include <fcntl.h>     /* mkdirat, unlinkat, openat, linkat */
#include <string.h>    /* strlen */
#include <stdlib.h>    /* free, malloc, calloc */
#include <string.h>    /* strncpy */
#include <stdbool.h>   /* bool */
#include <errno.h>
#include <assert.h>
#include <err.h>
#include <dirent.h>

/* Váš úkol v této přípravě je přímočarý – na vstupu dostanete
 * popisovač otevřené složky, ze kterého vytvoříte zřetězeny seznam
 * jmen. Jména budou uložena v dynamicky alokovaných polích vhodné
 * velikosti. Výsledkem je nulový ukazatel, dojde-li během
 * zpracování složky k chybě, nebo ukazatel na hlavu sestaveného
 * seznamu. */

struct name_list {
    char *name;
    struct name_list *next;
};

bool append(struct name_list **head, struct name_list **tail, const char *name) {
    unsigned long name_len = strlen(name) + 1;
    struct name_list *new_node = malloc(sizeof(struct name_list));
    char *new_name = malloc(sizeof(char) * name_len);
    if (!new_node || !new_name) {
        return false;
    }
    strncpy(new_name, name, name_len);
    new_node->name = new_name;
    new_node->next = NULL;
    if (*head) {
        (*tail)->next = new_node;
        *tail = (*tail)->next;
    } else {
        *tail = new_node;
        *head = new_node;
    }
    return true;
}

void destroy(struct name_list *list) {
    struct name_list *prev = NULL;
    while (list) {
        prev = list;
        list = list->next;
        free(prev->name);
        free(prev);
    }
}

struct name_list *list(int real_dir_fd) {
    DIR *dir = NULL;
    struct name_list *head = NULL, *tail = NULL;

    int dup_fd = dup(real_dir_fd);
    if (dup_fd == -1)
        goto out;

    dir = fdopendir(dup_fd);
    if (!dir)
        goto out;

    rewinddir(dir);
    struct dirent *ptr;
    struct stat st;
    while ((ptr = readdir(dir))) {
        if (fstatat(real_dir_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1) {
            goto out;
        }
        if (!append(&head, &tail, ptr->d_name)) {
            goto out;
        }
    }
    closedir(dir);
    return head;
    out:
    destroy(head);
    if (dir)
        closedir(dir);
    else if (dup_fd != -1)
        close(dup_fd);
    return NULL;
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

static void unlink_if_exists(int fd, const char *name,
                             bool is_dir) {
    if (unlinkat(fd, name, is_dir ? AT_REMOVEDIR : 0) == -1 &&
        errno != ENOENT)
        err(2, "unlinking %s", name);
}

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static int create_file(int dir_fd, const char *name) {
    int fd;

    if ((fd = openat(dir_fd, name,
                     O_CREAT | O_TRUNC | O_RDWR,
                     0666)) == -1)
        err(2, "creating %s", name);

    return fd;
}

static void write_file(int dir, const char *name, const char *str) {
    int fd = create_file(dir, name);

    if (write(fd, str, strlen(str)) == -1)
        err(2, "writing file %s", name);

    close_or_warn(fd, name);
}

static bool contains(struct name_list *node, const char *name) {
    while (node != NULL) {
        if (strcmp(node->name, name) == 0)
            return true;

        node = node->next;
    }
    return false;
}

static void check_content(struct name_list *node,
                          const char **names, int size) {
    if (node == NULL) {
        assert(size == 0);
        return;
    }

    for (int i = 0; i < size; ++i)
        assert(contains(node, names[i]));
}

static void cleanup(struct name_list *head) {
    while (head != NULL) {
        free(head->name);
        struct name_list *tmp = head->next;
        free(head);
        head = tmp;
    }
}

static void unlink_test_files() {
    int dir1 = openat(AT_FDCWD, "zt.p3_test_dir1", O_DIRECTORY);
    if (dir1 != -1) {
        const char *names[] =
                {
                        "zt.p3_a", "zt.p3_b", "zt.p3_c",
                        "zt.p3_d", "zt.p3_e"
                };

        for (int i = 0; i < 5; ++i)
            unlink_if_exists(dir1, names[i], false);

        close(dir1);
    }

    unlink_if_exists(AT_FDCWD, "zt.p3_test_dir1", true);
}

int main() {
    unlink_test_files();

    assert(list(-1) == NULL);

    int dir1 = create_and_open_dir(AT_FDCWD, "zt.p3_test_dir1");

    const char *in_empty[] = {".", ".."};
    struct name_list *result = list(dir1);
    check_content(result, in_empty, 2);
    cleanup(result);

    const char *names[] =
            {
                    ".", "..", "zt.p3_a", "zt.p3_b",
                    "zt.p3_c", "zt.p3_d", "zt.p3_e"
            };

    const char *contents[] =
            {
                    "Nobody said it was easy",
                    "It's such a shame for us to part",
                    "Nobody said it was easy",
                    "No one ever said it would be this hard",
                    "Oh, take me back to the start"
            };

    for (int i = 2; i < 7; ++i)
        write_file(dir1, names[i], contents[i - 2]);

    result = list(dir1);
    check_content(result, names, 7);
    cleanup(result);

    close_or_warn(dir1, "zt.p3_test_dir1");
    unlink_test_files();

    return 0;
}