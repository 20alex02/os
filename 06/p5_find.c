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

/* Naprogramujte proceduru ‹find›, která obdrží:
 *
 *  • ‹root_fd› je popisovač otevřeného adresáře,
 *  • ‹name› je jméno hledaného odkazu,
 *  • ‹flags› jsou příznaky, které předá volání ‹openat›.
 *
 * a která prohledá podstrom začínající v ‹root_fd›. Nalezne-li
 * v zadaném podstromě právě jeden odkaz hledaného jména, tento
 * otevře (s příznaky nastavenými na ‹flags›) a výsledný popisovač
 * vrátí. Nenalezne-li žádný nebo více než jeden vyhovující odkaz,
 * vrátí hodnotu -2. Dojde-li při prohledávání k chybě, vrátí -1.
 *
 * Nezapomeňte uvolnit veškeré zdroje, které jste alokovali
 * (s případnou výjimkou popisovače, který je funkcí vrácen). */

bool find_rec(int root_fd, const char *name, int flags, int *fd) {
    if (root_fd == -1)
        return false;

    int rv = false;
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

        if (strcmp(ptr->d_name, name) == 0) {
            if (*fd != -1) {
                goto out;
            }
            *fd = openat(root_fd, ptr->d_name, flags);
            if (*fd == -1) {
                goto out;
            }
        }

        if (fstatat(root_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1)
            goto out;

        if (!S_ISDIR(st.st_mode)) {
            continue;
        }
        int sub_fd = openat(root_fd, ptr->d_name, flags);
        if (!find_rec(sub_fd, name, flags, fd))
            goto out;
    }

    rv = true;
    out:
    if (dir)
        closedir(dir);
    return rv;
}

int find(int root_fd, const char *name, int flags) {
    int fd = -1;
    bool rv = find_rec(dup(root_fd), name, flags, &fd);
    if (!rv && fd == -1) {
        return -1;
    }
    if (!rv || fd == -1) {
        if (fd != -1) {
            close(fd);
        }
        return -2;
    }
    return fd;
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

static int check_file(int fd, const char *expected) {
    int nbytes = strlen(expected);
    int rbytes;
    char buffer[nbytes + 1];

    if ((rbytes = read(fd, buffer, nbytes + 1)) == -1)
        err(2, "reading %d bytes", nbytes + 1);

    return rbytes == nbytes &&
           memcmp(expected, buffer, nbytes) == 0;
}

int main() {
    int work_fd = mkdir_or_die(AT_FDCWD, "zt.p5_root");
    int subd_fd = mkdir_or_die(work_fd, "subdir");
    int file_fd;

    write_file(work_fd, "foo", "x");
    write_file(subd_fd, "bar", "y");
    unlink_if_exists(subd_fd, "foo");

    assert(find(work_fd, "foob", O_RDONLY) == -2);
    file_fd = find(work_fd, "foo", O_RDONLY);
    assert(file_fd != -1);
    assert(file_fd != -2);
    assert(check_file(file_fd, "x"));
    close_or_warn(file_fd, "file returned by find");

    file_fd = find(work_fd, "bar", O_RDONLY);
    assert(file_fd != -1);
    assert(file_fd != -2);
    assert(check_file(file_fd, "y"));
    close_or_warn(file_fd, "file returned by find");

    write_file(subd_fd, "foo", "");
    assert(find(work_fd, "foo", O_RDONLY) == -2);
    assert(find(file_fd, "foo", O_RDONLY) == -1);

    close_or_warn(subd_fd, "zt.p5_root/subdir");
    close_or_warn(work_fd, "zt.p5_root");

    return 0;
}
