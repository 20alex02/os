#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* STDOUT_FILENO, STDERR_FILENO */
#include <stdio.h>      /* dprintf */
#include <err.h>        /* err, warn */
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <errno.h>

/* Uvažme program, který vypisuje řádky jak na svůj standardní
 * výstup, tak na svůj standardní chybový výstup, přičemž tyto mohou
 * být mezi sebou různě prokládány. Pokud tento program necháme
 * běžet v terminálu bez přesměrování, budou řádky obou výstupů
 * promíchané a nebudeme schopni rozlišit, který řádek pochází
 * z kterého. Jeho dva standardní výstupy jistě můžeme přesměrovat
 * každý na jiné místo (v shellu například použitím ‹">"› a ‹"2>"›),
 * nicméně v tom případě naopak ztratíme informaci o tom, jak byly
 * výstupy vzájemně proloženy.
 *
 * Cílem tohoto úkolu je implementovat proceduru ‹combine›, která
 * pro zadaný program s parametry přesměruje jeho výstupy na
 * společný popisovač, avšak zároveň podle prefixu odliší původ
 * každého řádku.
 *
 * Procedura ‹combine› bere následující parametry:
 *
 *  • ‹argv› – ukazatel na pole řetězců, zakončený ‹NULL›, jehož
 *    první prvek je název programu a ostatní jsou jeho parametry
 *    (název programu se však už neopakuje);
 *  • ‹fd_out› – popisovač, na který mají být přesměrovány oba
 *    výstupy;
 *  • ‹status› – ukazatel, na který bude zapsán výsledný status
 *    skončeného procesu.
 *
 * Programem pro spuštění může být i takový, který se nachází
 * pod některou cestou uvedenou v proměnné prostředí ‹PATH›.
 *
 * Hodnotu pod ukazatelem ‹status› bude možné předložit makrům
 * ‹WIFEXITED›, ‹WEXITSTATUS›, ‹WIFSIGNALED› a ‹WTERMSIG› pro
 * zjištění informací o tom, jak byl spuštěný program ukončen.
 *
 * Procedura ‹combine› spustí program se zadanými argumenty
 * a přesměruje jeho výstup tak, aby byl vypisován na objekt zadaný
 * popisovačem ‹fd_out› níže popsaným způsobem.
 *
 * Každý řádek standardního výstupu programu bude vypsán nezměněný,
 * pouze doplněný zepředu o prefix ‹"[STDOUT] "› (až do maximální
 * velikosti, viz popis níže). Stejným způsobem bude zpracován
 * jeho standardní chybový výstup, který bude doplňován o prefix
 * ‹"[STDERR] "›.
 *
 * Řádky na výstupu budou omezeny na délku 71 znaků (nepočítaje
 * prefix ani znak konce řádku). Tedy se započítáním prefixu bude
 * každý řádek možné zobrazit nejvýše 80 znaky. Znaky, které by
 * přečnívaly tento limit, budou zahozeny. Zároveň bude na řádku
 * následujícím po vypsaném uvedeno, že došlo k zahození znaků
 * a kolik takových bylo (ani zde se nebude počítat znak konce
 * řádku) v následujícím formátu:
 *
 *     ‹"STDOUT: cropped N characters"›
 *
 * Zde ‹N› označuje počet zahozených znaků a ‹"STDOUT"› bude
 * nahrazeno za ‹"STDERR"› pro chybový výstup.
 *
 * V závislosti na spouštěném programu může nastat situace, že
 * některý výstup bude ukončen bez toho, aby obsahoval zakončovací
 * znak ‹'\n'›. V takovém případě nechť je takový řádek vypsán
 * stejným způsobem jako výše, pouze doplněn o tento znak.
 * Zároveň nechť je informace o tomto uvedena na následujícím
 * samostatném řádku ve formátu:
 *
 *     ‹"STDOUT: no newline"›
 *
 * Podobně jako výše pro chybový výstup bude ‹"STDOUT"› nahrazeno za
 * ‹"STDERR"›. Pokud nastane situace, že daný neukončený řádek byl
 * zároveň zkrácen, nechť je informace o chybějícím zakončení
 * uvedena jako «druhá».
 *
 * Pro užitečnost procedury ‹combine› je nutné, aby byly řádky
 * vypisovány postupně a bez zbytečné prodlevy. Zejména to znamená,
 * že pokud na jeden z výstupů program zapsal data, není možné čekat
 * s jejich zpracováním ani v případě, že na druhý výstup je
 * neustále zapisováno.
 *
 * Za předpokladu, že všechny vstupní řádky jsou nejvýše zadané
 * délky (71 znaků), nechť platí, že mezi okamžikem, kdy je na jeden
 * výstup programem zapsán znak ‹'\n'›, a okamžikem vypsání
 * odpovídajícího řádku není více než 284 vypsaných znaků z druhého
 * výstupu.
 *
 * Podprogram ‹combine› skončí, jakmile je spouštěný program
 * ukončen.
 *
 * Jeho návratovou hodnotou nechť je 0 v případě úspěšného spuštění
 * zadaného programu a vyčkání na jeho ukončení. Pokud dojde
 * k nenapravitelné systémové chybě, návratová hodnota bude -1.
 *
 * Jedinou výjimkoujjk je případ, kdy k nenapravitelnému selhání dojde
 * ve vedlejším procesu. Protože takovou chybu by bylo obtížné
 * zachycovat z podprogramu ‹combine›, toto selhání se bude
 * indikovat pouze skrze hodnotu pod ukazatelem ‹status›. Na něj
 * nechť je v takovém případě zapsána taková hodnota, která odpovídá
 * ukončení procesu s návratovým kódem 100. */

#define EXIT_CHILD_FAILURE 100
#define MAX_LINE_LEN 71
#define PREFIX_LEN 9
#define NO_NEWLINE_LEN 19
#define BLOCK_SIZE 128

const char *std_out = "STDOUT";
const char *std_err = "STDERR";

struct buffer {
    char *data;
    int newline;
    int capacity;
    int len;
};

bool write_out(int fd, struct buffer *buf, const char *output_type) {
    char prefix[PREFIX_LEN + 1];
    char no_newline[NO_NEWLINE_LEN + 1];
    snprintf(prefix, PREFIX_LEN + 1, "[%s] ", output_type);
    snprintf(no_newline, NO_NEWLINE_LEN + 1, "%s: no newline\n", output_type);

    if (write(fd, prefix, PREFIX_LEN) == -1) {
        return false;
    }
    if (buf->len <= MAX_LINE_LEN + 1) {
        if (buf->newline != -1) {
            return write(fd, buf->data, buf->newline + 1) != -1;
        }
        buf->data[buf->len] = '\n';
        if (write(fd, buf->data, buf->len + 1) == -1) {
            return false;
        }
    } else {
        char tmp = buf->data[MAX_LINE_LEN];
        buf->data[MAX_LINE_LEN] = '\n';
        if (write(fd, buf->data, MAX_LINE_LEN + 1) == -1) {
            return false;
        }
        buf->data[MAX_LINE_LEN] = tmp;

        char cropped[MAX_LINE_LEN];
        snprintf(cropped, MAX_LINE_LEN, "%s: cropped %d characters\n", output_type,
                 buf->newline == -1 ? buf->len - MAX_LINE_LEN : buf->newline - MAX_LINE_LEN);
        if (write(fd, cropped, strlen(cropped)) == -1) {
            return false;
        }
    }
    if (buf->newline == -1) {
        buf->len = 0;
        return write(fd, no_newline, NO_NEWLINE_LEN) != -1;
    }
    return true;
}

bool realloc_double(struct buffer *buf) {
    buf->capacity *= 2;
    char *tmp = realloc(buf->data, buf->capacity * sizeof(char));
    if (!tmp) {
        return false;
    }
    buf->data = tmp;
    return true;
}

bool read_pipe(int fd, struct buffer *buf, bool *flag) {
    if (buf->len != 0) {
        buf->len -= buf->newline + 1;
        memmove(buf->data, buf->data + buf->newline + 1, buf->len);
    }
    char *newline = memchr(buf->data, '\n', buf->len);
    if (newline) {
        buf->newline = newline - buf->data;
        return true;
    }
    buf->newline = -1;
    ssize_t nread;
    while ((nread = read(fd, buf->data + buf->len, BLOCK_SIZE)) > 0) {
        buf->len += (int) nread;
        if ((newline = memchr(buf->data + buf->len - nread, '\n', nread))) {
            buf->newline = newline - buf->data;
            return true;
        }
        if (buf->capacity - buf->len + 1 < BLOCK_SIZE && !realloc_double(buf)) {
            return false;
        }
    }
    if (nread == -1 /*&& errno != EAGAIN*/) {
        perror("read");
        return false;
    }
    if (nread == 0) {
        *flag = true;
    }
    return true;
}

int combine(char **argv, int fd_out, int *status) {
    int rv = -1;
    int pipe_stdout[2];
    if (pipe(pipe_stdout) == -1) {
        perror("pipe");
        return -1;
    }
    int pipe_stderr[2];
    if (pipe(pipe_stderr) == -1) {
        perror("pipe");
        close(pipe_stdout[0]);
        close(pipe_stdout[1]);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipe_stdout[0]);
        close(pipe_stdout[1]);
        close(pipe_stderr[0]);
        close(pipe_stderr[1]);
        return -1;
    }
    if (pid == 0) {  // Child process
        int exit_status = EXIT_SUCCESS;
        if (dup2(pipe_stdout[1], STDOUT_FILENO) == -1
            || dup2(pipe_stderr[1], STDERR_FILENO) == -1) {
            perror("dup2");
            exit_status = EXIT_CHILD_FAILURE;
        }

        if (close(pipe_stdout[0]) == -1) exit_status = EXIT_CHILD_FAILURE;
        if (close(pipe_stdout[1]) == -1) exit_status = EXIT_CHILD_FAILURE;
        if (close(pipe_stderr[0]) == -1) exit_status = EXIT_CHILD_FAILURE;
        if (close(pipe_stderr[1]) == -1) exit_status = EXIT_CHILD_FAILURE;

        if (exit_status != EXIT_SUCCESS) {
            exit(exit_status);
        }

        execvp(argv[0], argv);
        perror("execvp");
        exit(EXIT_CHILD_FAILURE);
    }

    struct buffer buf_out = {0};
    struct buffer buf_err = {0};

    int close_rv = close(pipe_stdout[1]);
    if (close(pipe_stderr[1]) == -1 || close_rv == -1) {
        perror("close");
        goto err;
    }

    buf_out.capacity = buf_err.capacity = BLOCK_SIZE;
    buf_out.data = malloc(buf_out.capacity * sizeof(char));
    buf_err.data = malloc(buf_err.capacity * sizeof(char));
    if (!buf_out.data || !buf_err.data) {
        goto err;
    }

    bool stdout_done = false, stderr_done = false;
    while (!stdout_done || !stderr_done) {
        if (!stdout_done && !read_pipe(pipe_stdout[0], &buf_out, &stdout_done)) {
            goto err;
        }
        if (buf_out.len != 0 && !write_out(fd_out, &buf_out, std_out)) {
            goto err;
        }
        if (!stderr_done && !read_pipe(pipe_stderr[0], &buf_err, &stderr_done)) {
            goto err;
        }
        if (buf_err.len != 0 && !write_out(fd_out, &buf_err, std_err)) {
            goto err;
        }
    }

    if (waitpid(pid, status, 0) == -1) {
        goto err;
    }
    rv = 0;
    err:
    free(buf_out.data);
    free(buf_err.data);
    if (close(pipe_stdout[0]) == -1) rv = -1;
    if (close(pipe_stderr[0]) == -1) rv = -1;
    return rv;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

#include <assert.h>         /* assert */
#include <stdlib.h>         /* NULL, exit */
#include <string.h>         /* strlen */
#include <errno.h>          /* errno */
#include <unistd.h>         /* alarm, fork */
#include <sys/wait.h>       /* wait */
#include <fcntl.h>          /* open */

static void close_or_warn(int fd, const char *name) {
    if (close(fd) == -1)
        warn("closing %s", name);
}

static void unlink_if_exists(const char *name) {
    if (unlink(name) == -1 && errno != ENOENT)
        err(2, "unlinking %s", name);
}

static int run_combine(char **argv, int *status) {
    const char *name = "zt.c_out";

    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
        err(2, "opening %s", name);

    int rv = combine(argv, fd, status);
    close_or_warn(fd, name);
    return rv;
}

static int check_output(const char *expected) {
    const char *name = "zt.c_out";
    char buffer[255 + 1] = {0};

    int fd = open(name, O_RDONLY);
    if (fd == -1)
        err(2, "opening %s", name);

    if (read(fd, buffer, 255) == -1)
        err(2, "reading %s", name);
//    printf("content: %s\n", buffer);
    close_or_warn(fd, name);
    return strcmp(expected, buffer);
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        int status;
        int rv = combine(argv + 1, STDOUT_FILENO, &status);
        return rv;
    }

    int status;
    char *args1[] = {"echo", "hello world", NULL};
    assert(run_combine(args1, &status) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(check_output("[STDOUT] hello world\n") == 0);

    char *args2[] = {"printf", "ooga\\nbooga", NULL};
    assert(run_combine(args2, &status) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(check_output("[STDOUT] ooga\n"
                        "[STDOUT] booga\n"
                        "STDOUT: no newline\n") == 0);

    char *args3[] = {"sh", "-c", ">&2 echo some error", NULL};
    assert(run_combine(args3, &status) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(check_output("[STDERR] some error\n") == 0);

    char *args4[] = {"echo",
                     "1234512345123451234512345123451234512345123451234512345123451234512345123451234512345123451234512345",
                     NULL};
    assert(run_combine(args4, &status) == 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(check_output(
            "[STDOUT] 12345123451234512345123451234512345123451234512345123451234512345123451\n"
            "STDOUT: cropped 29 characters\n") ==
           0);

    unlink_if_exists("zt.c_out");
    return 0;
}
