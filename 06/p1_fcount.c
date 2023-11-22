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

/* Napište podprogram ‹fcount›, kterému je předán popisovač otevřené
 * složky, a který spočítá, kolik je (přímo) v této odkazů na
 * «obyčejné» soubory (je-li tentýž soubor odkázán více než jednou,
 * počítá se každý odkaz). Nezáporná návratová hodnota určuje počet
 * nalezených souborů, -1 pak indikuje systémovou chybu. */

int fcount(int dir_fd) {
    DIR *dir = NULL;
    int rv = -1, counter = 0;

    int dup_fd = dup(dir_fd);
    if (dup_fd == -1)
        goto out;

    dir = fdopendir(dup_fd);
    if (!dir)
        goto out;

    rewinddir(dir);
    struct dirent *ptr;
    struct stat st;
    while ((ptr = readdir(dir))) {
        if (fstatat(dir_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1)
            goto out;

        if (S_ISREG(st.st_mode))
            ++counter;
    }

    rv = counter;
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

int main(void) {
    int dir = open(".", O_RDONLY);
    if (dir == -1)
        err(2, "opening working directory");

    close_or_warn(dir, "working directory");
    assert(fcount(dir) == -1);

    unlink_if_exists(AT_FDCWD, "zt.p1_test_dir1", true);
    int dir1 = create_and_open_dir(AT_FDCWD, "zt.p1_test_dir1");

    assert(fcount(dir1) == 0);

    write_file(dir1, "zt.p1_a", "All I ever wanted");
    write_file(dir1, "zt.p1_b", "All I ever needed");
    write_file(dir1, "zt.p1_c", "Is here in my arms");
    write_file(dir1, "zt.p1_d", "Words are very unnecessary");
    write_file(dir1, "zt.p1_e", "They can only do harm");

    assert(fcount(dir1) == 5);
    assert(fcntl(dir1, F_GETFD) != -1);

    int dir2 = create_and_open_dir(dir1, "zt.p1_test_dir2");
    assert(fcount(dir1) == 5);

    if (linkat(dir1, "zt.p1_a", dir1, "zt.p1_f", 0) == -1)
        err(2, "creating a link to %s", "zt.p1_a");

    assert(fcount(dir1) == 6);

    if (symlinkat("zt.p1_b", dir1, "zt.p1_g") == -1)
        err(2, "creating a symlink to %s", "zt.p1_b");

    assert(fcount(dir1) == 6);
    assert(fcount(dir2) == 0);

    write_file(dir2, "zt.p1_h", "Life is a fight.");

    assert(fcount(dir2) == 1);

    unlink_if_exists(dir1, "zt.p1_a", false);
    unlink_if_exists(dir1, "zt.p1_b", false);
    unlink_if_exists(dir1, "zt.p1_c", false);
    unlink_if_exists(dir1, "zt.p1_d", false);
    unlink_if_exists(dir1, "zt.p1_e", false);
    unlink_if_exists(dir1, "zt.p1_f", false);
    unlink_if_exists(dir1, "zt.p1_g", false);
    unlink_if_exists(dir2, "zt.p1_h", false);

    close_or_warn(dir2, "zt.p1_test_dir2");
    unlink_if_exists(dir1, "zt.p1_test_dir2", true);
    close_or_warn(dir1, "zt.p1_test_dir1");
    unlink_if_exists(AT_FDCWD, "zt.p1_test_dir1", true);

    return 0;
}
