#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, unlinkat, … */
#include <fcntl.h>      /* openat, O_* */
#include <sys/stat.h>   /* fstat, struct stat */
#include <dirent.h>     /* DIR, fdopendir, readdir, … */
#include <string.h>     /* strlen, memcmp */
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/* Naprogramujte proceduru ‹find›, která obdrží:
 *
 *  • ‹root_fd› je popisovač otevřeného adresáře,
 *  • ‹suffix› je přípona jména hledaného odkazu.
 *
 * a která prohledá podstrom začínající v ‹root_fd› a nalezne
 * všechny odkazy, které mají tvar ‹*.suffix› – tzn. začínají
 * libovolným řetězcem, pokračují tečkou a končí řetězcem předaném
 * v parametru ‹suffix›. Počet nalezených odkazů vrátí. Nastane-li
 * při prohledávání chyba, výsledkem bude -1. */

bool ends_with(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return false;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

int count_suffix_rec(int root_fd, int *count, const char *suffix) {
    if (root_fd == -1)
        return -1;

    DIR *dir = fdopendir(root_fd);
    if (!dir) {
        close(root_fd);
        return -1;
    }

    int rv = -1;
    rewinddir(dir);
    struct dirent *ptr;
    struct stat st;
    while ((ptr = readdir(dir))) {
        if (strcmp(ptr->d_name, ".") == 0 ||
            strcmp(ptr->d_name, "..") == 0)
            continue;

        if (fstatat(root_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1)
            goto out;

        if (ends_with(ptr->d_name, suffix)) ++*count;

        if (!S_ISDIR(st.st_mode))
            continue;

        int sub_fd = openat(root_fd, ptr->d_name,
                            O_DIRECTORY | O_RDONLY);

        if (count_suffix_rec(sub_fd, count, suffix) == -1)
            goto out;
    }

    rv = 0;
    out:
    closedir(dir);

    return rv;
}

int count_suffix(int root_fd, const char *suffix) {
    int count = 0;

    if (count_suffix_rec(dup(root_fd), &count, suffix) == -1)
        return -1;

    return count;
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

static int mkdir_or_die(int dir_fd, const char *name) {
    int fd;

    if (mkdirat(dir_fd, name, 0777) == -1 && errno != EEXIST)
        err(1, "creating directory %s", name);
    if ((fd = openat(dir_fd, name, O_DIRECTORY)) == -1)
        err(1, "opening newly created directory %s", name);

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

static void write_file(int dir, const char *name, const char *str) {
    int fd = create_file(dir, name);

    if (write(fd, str, strlen(str)) == -1)
        err(2, "writing file %s", name);

    close_or_warn(fd, name);
}

int main() {
    int work_fd = mkdir_or_die(AT_FDCWD, "zt.p5_root");
    int subd_fd = mkdir_or_die(work_fd, "subdir");

    write_file(work_fd, "foo.txt", "x");
    write_file(subd_fd, "bar.csv", "y");
    unlink_if_exists(subd_fd, "foo");

    assert(count_suffix(work_fd, "txt") == 1);
    assert(count_suffix(work_fd, "csv") == 1);

    write_file(subd_fd, "foo.csv", "");
    assert(count_suffix(work_fd, "txt") == 1);
    assert(count_suffix(work_fd, "csv") == 2);

    write_file(work_fd, "foo.csv", "");
    assert(count_suffix(work_fd, "txt") == 1);
    assert(count_suffix(work_fd, "csv") == 3);

    close_or_warn(subd_fd, "zt.p5_root/subdir");
    close_or_warn(work_fd, "zt.p5_root");

    return 0;
}
