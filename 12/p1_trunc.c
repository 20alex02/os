#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* close, write, pipe */
#include <errno.h>      /* errorno */
#include <assert.h>     /* assert */
#include <err.h>        /* err, warn */
#include <stdlib.h>     /* free */
#include <string.h>     /* strlen, memcmp */
#include <stdbool.h>

/* Napište podprogram ‹read_trunc›, který přečte ze vstupu jeden
 * záznam (každý záznam je ukončen bajtem ‹delim›), až do maximální
 * délky ‹size›. Je-li záznam delší, je načteno jen jeho prvních
 * ‹size› bajtů a zbytek je přeskočen – další volání ‹read_trunc›
 * vrátí další záznam (případně podobně zkrácený), atd. až než
 * narazí na konec souboru. Načtená data uloží do předaného pole
 * ‹out› (které má prostor minimálně pro ‹size› bajtů).
 *
 * Abychom mohli data načítat efektivně, bude volající proceduře
 * ‹read_trunc› navíc předávat ukazatel na strukturu ‹trunc_buffer›,
 * ve které lze uchovat data, která již byla načtena, ale nejsou
 * součástí aktuálního záznamu.
 *
 * Návratovou hodnotou bude v případě úspěchu skutečný počet bajtů,
 * které byly v záznamu uloženy, nebo -1 nepodaří-li se záznam
 * načíst. */

/* Strukturu ‹trunc_buffer› si můžete navrhnout dle vlastního
 * uvážení. Nesmí ale překročit velikost 512 bajtů. */

#define BUF_SIZE 256
struct trunc_buffer {
    char data[BUF_SIZE];
    int length;
};

//bool realloc_double(struct trunc_buffer *buf) {
//    buf->capacity *= 2;
//    char *tmp = realloc(buf->data, buf->capacity * sizeof(char));
//    if (!tmp) {
//        return false;
//    }
//    buf->data = tmp;
//    return true;
//}

//struct trunc_buffer {
//    char *memory;
//    int allocd;
//    int used;
//};

int read_trunc(int fd, char delim, int size, char *out, struct trunc_buffer *buffer) {
    int totalBytesRead = 0;
    if (buffer->length > 0) {
        int copyLength = buffer->length;
        if (copyLength > size) {
            copyLength = size;
        }

        memcpy(&out[totalBytesRead], buffer->data, copyLength);
        totalBytesRead += copyLength;

        // Update the buffer
        buffer->length -= copyLength;
        memmove(buffer->data, &buffer->data[copyLength], buffer->length);
    }

    while (totalBytesRead < size) {
        int bytesRead = read(fd, &out[totalBytesRead], size - totalBytesRead);

        if (bytesRead == -1) {
            return -1; // Error reading from the file descriptor
        } else if (bytesRead == 0) {
            break; // End of file
        }

        // Update totalBytesRead
        totalBytesRead += bytesRead;

        // Check for delimiter in the newly read data
        for (int i = 0; i < bytesRead; ++i) {
            if (out[i] == delim) {
                // Save the remaining data after the delimiter in the buffer
                buffer->length = bytesRead - (i + 1);
                for (int j = 0; j < buffer->length; ++j) {
                    buffer->data[j] = out[i + 1 + j];
                }
                totalBytesRead = i + 1;
                break;
            }
        }
    }

    return totalBytesRead;
}

int read_trunc(int fd, char delim, int size, char *out,
               struct trunc_buffer *buffer) {
    int offset = 0;
    if (buffer->data[0]) {
        char *delim_p = strchr(buffer->data, delim);
        if (delim_p && delim_p - buffer->data < size) {
            int token_len = (int) (delim_p - buffer->data);
            memcpy(out, buffer->data, token_len);
            if (token_len < BUF_SIZE - 1) {
                memmove(buffer->data, delim_p + 1, BUF_SIZE - token_len);
            }
            return token_len;
        }
        if (size <= BUF_SIZE) {
            memcpy(out, buffer->data, size);
            memmove(buffer->data, buffer->data + size, size);
            return size;
        }
        memcpy(out, buffer->data, BUF_SIZE);
        buffer->data[0] = '\0';
    }
    if (read(fd, buffer->data, BUF_SIZE))
        return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <fcntl.h>      /* openat */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *file) {
    if (unlink(file) == -1 && errno != ENOENT)
        err(2, "unlink");
}

static int create_file(const char *data) {
    const char *name = "zt.r3_split.txt";

    unlink_if_exists(name);
    int fd = openat(AT_FDCWD, name, O_CREAT | O_WRONLY, 0755);

    if (fd == -1)
        err(2, "creating %s", name);
    if (write(fd, data, strlen(data)) == -1)
        err(2, "writing into %s", name);

    close_or_warn(fd, name);

    fd = open(name, O_RDONLY);

    if (fd == -1)
        err(2, "opening %s for reading", name);

    return fd;
}

int main() {
    int fd;

    char rbuf[64];
    struct trunc_buffer *tbuf = calloc(1, 512);

    fd = create_file("f\nbar\nbazinga\n");

    assert(read_trunc(fd, '\n', 2, rbuf, tbuf) == 2);
    assert(memcmp(rbuf, "f\n", 2) == 0);
    assert(read_trunc(fd, '\n', 2, rbuf, tbuf) == 4);
    assert(memcmp(rbuf, "ba", 2) == 0);
    assert(read_trunc(fd, '\n', 3, rbuf, tbuf) == 8);
    assert(memcmp(rbuf, "baz", 3) == 0);
    assert(read_trunc(fd, '\n', 2, rbuf, tbuf) == 0);

    free(tbuf);
    close(fd);
    return 0;
}
