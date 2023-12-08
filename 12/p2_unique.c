#define _POSIX_C_SOURCE 200809L

#include <assert.h>    /* assert */
#include <sys/stat.h>  /* mkdirat, openat */
#include <sys/types.h> /* mkdirat, openat */
#include <unistd.h>    /* unlinkat, linkat, close, write, … */
#include <fcntl.h>     /* mkdirat, unlinkat, openat, linkat, … */
#include <string.h>    /* strlen */
#include <stdbool.h>   /* bool */
#include <errno.h>
#include <err.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

/* Napište podprogram ‹count_unique›, kterému je předán popisovač
 * otevřeného adresáře, a který spočítá, kolik unikátních souborů
 * (i-uzlů, které nejsou složkami) je přímo tímto adresářem
 * odkazováno. Nezáporná návratová hodnota určuje počet nalezených
 * souborů, -1 indikuje systémovou chybu. */

bool realloc_double(ino_t **arr, int *capacity) {
    *capacity *= 2;
    ino_t *tmp = realloc(*arr, *capacity * sizeof(ino_t));
    if (!tmp) {
        return false;
    }
    *arr = tmp;
    return true;
}

bool is_duplicate(const ino_t *arr, int len, ino_t inode) {
    for (int i = 0; i < len; ++i) {
        if (arr[i] == inode) {
            return true;
        }
    }
    return false;
}

int count_unique(int dir_fd) {
    int rv = -1;
    DIR *dir = NULL;
    ino_t *inodes = NULL;
    int capacity = 10;
    int len = 0;

    int dup_fd = dup(dir_fd);
    if (dup_fd == -1)
        goto out;

    inodes = malloc(capacity * sizeof(ino_t));
    if (!inodes) {
        goto out;
    }

    dir = fdopendir(dup_fd);
    if (!dir)
        goto out;
    rewinddir(dir);
    struct dirent *ptr;
    struct stat st;
    while ((ptr = readdir(dir))) {
        if (fstatat(dir_fd, ptr->d_name, &st, AT_SYMLINK_NOFOLLOW) == -1)
            goto out;
        if (!S_ISDIR(st.st_mode) && !is_duplicate(inodes, len, st.st_ino)) {
            if (len >= capacity && !realloc_double(&inodes, &capacity)) {
                goto out;
            }
            inodes[len] = st.st_ino;
            ++len;
        }
    }
    rv = len;
    out:
    if (dir)
        closedir(dir);
    else if (dup_fd != -1)
        close(dup_fd);
    free(inodes);
    return rv;

}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void create_file(int dir_fd, const char *name) {
    int fd;

    if ((fd = openat(dir_fd, name,
                     O_CREAT | O_TRUNC | O_RDWR,
                     0666)) == -1)
        err(2, "creating %s", name);

    close_or_warn(fd, name);
}

static void link_file(int dir_fd, const char *old_name,
                      const char *new_name) {
    if (linkat(dir_fd, old_name, dir_fd, new_name, 0) == -1)
        err(2, "linking %s to %s", old_name, new_name);
}

static void unlink_if_exists(int fd, const char *name) {
    if (unlinkat(fd, name, 0) == -1 && errno != ENOENT)
        err(2, "unlinking %s", name);
}

static void unlink_test_files(int dir) {
    unlink_if_exists(dir, "a");
    unlink_if_exists(dir, "b");
    unlink_if_exists(dir, "c");
    unlink_if_exists(dir, "d");
    unlink_if_exists(dir, "x");
    unlink_if_exists(dir, "y");
    unlink_if_exists(dir, "z");
    unlink_if_exists(dir, "../zt.r2_link");
}

int main(void) {
    const char *root_name = "zt.r2_root";
    int root_fd;

    if (mkdirat(AT_FDCWD, root_name, 0777) == -1 &&
        errno != EEXIST)
        err(2, "creating directory %s", root_name);

    if ((root_fd = openat(AT_FDCWD, root_name,
                          O_RDONLY | O_DIRECTORY)) == -1)
        err(2, "opening directory %s", root_name);

    unlink_test_files(root_fd);

    assert(count_unique(-1) == -1);

    create_file(root_fd, "a");
    assert(count_unique(root_fd) == 1);

    create_file(root_fd, "b");
    assert(count_unique(root_fd) == 2);

    link_file(root_fd, "a", "x");
    assert(count_unique(root_fd) == 2);

    create_file(root_fd, "c");
    assert(count_unique(root_fd) == 3);

    link_file(root_fd, "a", "y");
    link_file(root_fd, "b", "z");
    assert(count_unique(root_fd) == 3);

    link_file(root_fd, "c", "../zt.r2_link");
    assert(count_unique(root_fd) == 3);

    if (symlinkat("a", root_fd, "d") == -1)
        err(2, "creating symlink d");

    assert(count_unique(root_fd) == 4);

    unlink_test_files(root_fd);
    close_or_warn(root_fd, root_name);

    return 0;
}
