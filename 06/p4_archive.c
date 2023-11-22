#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* read, write, unlinkat, … */
#include <fcntl.h>      /* openat, O_* */
#include <sys/stat.h>   /* fstat, struct stat */
#include <sys/sendfile.h>
#include <dirent.h>     /* DIR, fdopendir, readdir, … */
#include <string.h>     /* strlen, memcmp */
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

/* Napište proceduru ‹archive_files›, která na vstupu obdrží:
 *
 *  1. ‹work_fd› – popisovač složky, ze které bude staré soubory
 *     přesouvat do archivu,
 *  2. ‹archive_fd› – popisovač archivní složky (do této složky
 *     budou staré soubory přesunuty),
 *  3. ‹threshold› – hodnota ‹struct timespec›, která určuje
 *     nejstarší soubor, který má být ve složce ‹work_fd› zachován.
 *
 * Dále:
 *
 *  • pro určení stáří souboru použijeme časové razítko posledního
 *    zápisu (‹mtime›),
 *  • dojde-li ke kolizi jmen ve složce ‹archive_fd›, existující
 *    soubory budou nahrazeny (jinak ale existující soubory
 *    zůstávají nedotčené),
 *  • dojde-li během zpracování k chybě a povaha chyby to umožňuje,
 *    procedura ‹archive_files› se pokusí zbývající položky přesto
 *    zpracovat.
 *
 * Návratová hodnota:
 *
 *  • 0 značí, že vše proběhlo bez chyb,
 *  • -1 že operace byla z důvodu chyby přerušena,
 *  • -2 že některé soubory nebylo možné zkontrolovat nebo
 *    přesunout. */

//bool archive_files_rec(int work_fd, int archive_fd,
//                       struct timespec threshold) {
//    if (work_fd == -1)
//        return -1;
//
//    int rv = false;
//    DIR *dir = fdopendir(work_fd);
//    if (!dir)
//        goto out;
//
//    rewinddir(dir);
//
//    struct dirent *ptr;
//    struct stat st;
//    while ((ptr = readdir(dir))) {
//        if (strcmp(ptr->d_name, ".") == 0 ||
//            strcmp(ptr->d_name, "..") == 0)
//            continue;
//
//        if (strcmp(ptr->d_name, name) == 0) {
//            if (*fd != -1) {
//                goto out;
//            }
//            *fd = openat(work_fd, ptr->d_name, flags);
//            if (*fd == -1) {
//                goto out;
//            }
//        }
//
//        if (fstatat(work_fd, ptr->d_name, &st,
//                    AT_SYMLINK_NOFOLLOW) == -1)
//            goto out;
//
//        if (!S_ISDIR(st.st_mode)) {
//            continue;
//        }
//        int sub_fd = openat(work_fd, ptr->d_name, O_RDONLY);
//        if (!archive_files_rec(sub_fd, archive_fd, threshold))
//            goto out;
//    }
//
//    rv = true;
//    out:
//    if (dir)
//        closedir(dir);
//    return rv;
//}

bool is_newer(struct timespec a, struct timespec b) {
    return a.tv_sec > b.tv_sec ||
           (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec);
}

int archive_files(int work_fd, int archive_fd,
                  struct timespec threshold) {
    DIR *dir = NULL;
    int rv = -1;

    int dup_fd = dup(work_fd);
    if (dup_fd == -1) {
        goto out;
    }

    dir = fdopendir(dup_fd);
    if (!dir) {
        goto out;
    }

    struct dirent *ptr;
    struct stat st;
    rewinddir(dir);
    while ((ptr = readdir(dir))) {
        if (fstatat(work_fd, ptr->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) == -1) {
            rv = -2;
            continue;
        }
        if (S_ISREG(st.st_mode) && is_newer(threshold, st.st_mtim)) {
            if (unlinkat(archive_fd, ptr->d_name, 0) == -1 && errno != ENOENT) {
                rv = -2;
                continue;
            }
            int in_fd = openat(work_fd, ptr->d_name, O_RDONLY);
            if (in_fd == -1) {
                rv = -2;
                continue;
            }
            int out_fd = openat(archive_fd, ptr->d_name, O_CREAT | O_WRONLY);
            if (out_fd == -1) {
                rv = -2;
                close(in_fd);
                continue;
            }
            if (sendfile(out_fd, in_fd, NULL, st.st_size) == -1 ||
                unlinkat(work_fd, ptr->d_name, 0) == -1) {
                rv = -2;
                close(in_fd);
                close(out_fd);
                continue;
            }
            close(in_fd);
            close(out_fd);
        }
    }

    if (rv != -2)
        rv = 0;
    out:
    if (dir)
        closedir(dir);
    else if (dup_fd != -1)
        close(dup_fd);
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <time.h>

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

static int check_file(int dir, const char *name, const char *expected) {
    int nbytes = strlen(expected);
    int rbytes;
    char buffer[nbytes + 1];
    struct stat st;

    int read_fd = open_or_die(dir, name);

    if ((rbytes = read(read_fd, buffer, nbytes + 1)) == -1)
        err(2, "reading %s", name);

    if (fstat(read_fd, &st) == -1)
        err(2, "stat-ing %s", name);

    close_or_warn(read_fd, name);

    return st.st_nlink == 1 &&
           rbytes == nbytes &&
           memcmp(expected, buffer, nbytes) == 0;
}

static void set_mtime(int dir_fd, const char *name, struct timespec time) {
    struct timespec utime[2] = {time, time};

    if (utimensat(dir_fd, name, utime, 0) == -1)
        err(1, "setting mtime of %s", name);
}

int main() {
    struct timespec now, earlier;

    if (clock_gettime(CLOCK_REALTIME, &now) == -1)
        err(1, "getting current time");

    earlier = now;
    earlier.tv_sec -= 60;

    int work_fd = mkdir_or_die(AT_FDCWD, "zt.p4_root");
    int arch_fd = mkdir_or_die(AT_FDCWD, "zt.p4_arch");

    write_file(work_fd, "foo", "x");
    write_file(work_fd, "bar", "y");
    unlink_if_exists(arch_fd, "foo");
    unlink_if_exists(arch_fd, "bar");

    set_mtime(work_fd, "bar", earlier);
    set_mtime(work_fd, "foo", now);

    assert(archive_files(work_fd, arch_fd, now) == 0);

    assert(check_file(arch_fd, "bar", "y"));
    assert(check_file(work_fd, "foo", "x"));
    assert(faccessat(arch_fd, "foo", 0, 0) == -1);
    assert(errno == ENOENT);
    assert(faccessat(work_fd, "bar", 0, 0) == -1);
    assert(errno == ENOENT);

    close_or_warn(work_fd, "zt.p4_root");
    close_or_warn(arch_fd, "zt.p4_arch");

    return 0;
}
