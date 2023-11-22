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

/* Naprogramujte proceduru ‹disk_usage›, která prohledá zadaný
 * podstrom a sečte velikosti všech «obyčejných» souborů (v bajtech,
 * nikoliv alokovaných blocích), které se zde nachází. Výsledkem
 * nechť je tato celková velikost.
 *
 * Chyby:
 *
 *  • existuje-li se ve stromě obyčejný soubor, na který odkazuje
 *    více než jeden odkaz (nerozhoduje, zda je tento odkaz uvnitř
 *    nebo vně prohledávaného stromu), prohledávání ukončete
 *    s výsledkem -2,
 *  • dojde-li při zpracování podstromu k systémové chybě, výsledek
 *    bude -1. */

int disk_usage_rec(int root_fd, ssize_t *size) {
    if (root_fd == -1)
        return -1;

    int rv = -1;
    DIR *dir = fdopendir(root_fd);

    if (!dir)
        goto out;

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

        if (S_ISREG(st.st_mode)) {
            if (st.st_nlink != 1) {
                rv = 2;
                goto out;
            }
            *size += st.st_size;
        }

        if (S_ISDIR(st.st_mode)) {
            int sub_fd = openat(root_fd, ptr->d_name,
                                O_DIRECTORY | O_RDONLY);

            if (disk_usage_rec(sub_fd, size) == -1)
                goto out;
        }
    }

    rv = 0;
    out:
    if (dir)
        closedir(dir);

    return rv;
}

ssize_t disk_usage(int root_fd) {
    ssize_t size = 0;
    int rv;
    rv = disk_usage_rec(dup(root_fd), &size);
    if (rv != 0) {
        return rv;
    }
    return size;
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
    int work_fd = mkdir_or_die(AT_FDCWD, "zt.p6_root");
    int subd_fd = mkdir_or_die(work_fd, "subdir");

    unlink_if_exists(work_fd, "wibble");
    unlink_if_exists(subd_fd, "wibble");

    write_file(work_fd, "foo", "x");
    write_file(subd_fd, "bar", "y");
    write_file(subd_fd, "baz", "y");

    assert(disk_usage(work_fd) == 3);
    assert(disk_usage(-1) == -1);

    write_file(subd_fd, "wibble", "xy");

    if (symlinkat("foo", work_fd, "wibble") == -1)
        err(1, "creating a symlink");

    assert(disk_usage(work_fd) == 5);

    close_or_warn(subd_fd, "zt.p6_root/subdir");
    close_or_warn(work_fd, "zt.p6_root");

    return 0;
}
