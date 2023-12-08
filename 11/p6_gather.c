#define _POSIX_C_SOURCE 200809L

#include <unistd.h>     /* alarm */
#include <assert.h>
#include <stdint.h>
#include <pthread.h>

/* Tématem této přípravy budou asynchronní výpočty – Vaším úkolem
 * bude naprogramovat pomocný podprogram ‹gather›, který obdrží:
 *
 *  • ‹func› je ukazatel na výpočetní funkci, kterou bude spouštět,
 *  • ‹inputs› je pole vstupních hodnot,
 *  • ‹results› je pole výsledků,
 *  • ‹count› je velikost těchto polí.
 *
 * Podprogram ‹gather› spustí výpočet ‹func› pro každou vstupní
 * hodnotu (položku pole ‹inputs›) v samostatném vlákně a výsledky
 * uloží na odpovídající index pole ‹results›. Výsledkem bude 0
 * proběhne-li vše úspěšně, jinak -1. */

struct handle {
    uint64_t (*func)(uint64_t);

    uint64_t input;
    uint64_t *result;
};

void *thread_func(void *arg) {
    struct handle *info = (struct handle *) arg;
    *(info->result) = (info->func)(info->input);
    return NULL;
}

int gather(uint64_t ( *func )(uint64_t), uint64_t *inputs, uint64_t *results, int count) {
    pthread_t threads[count];
    struct handle handle[count];
    for (int i = 0; i < count; ++i) {
        handle[i].func = func;
        handle[i].input = inputs[i];
        handle[i].result = &results[i];
        if (pthread_create(&threads[i], NULL, thread_func, &handle[i]) != 0) {
            return -1;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (pthread_join(threads[i], NULL) != 0) {
            return -1;
        }
    }
    return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

uint64_t slowpoke(uint64_t val) {
    sleep(5);
    return val * 13;
}

int main(void) {
    alarm(7);

    uint64_t inputs[8] = {1, 2, 3, 4, 5, 6, 7, 8},
            results[8];

    assert(gather(slowpoke, inputs, results, 8) == 0);

    for (int i = 1; i <= 8; ++i)
        assert(results[i - 1] == (uint64_t) i * 13);

    return 0;
}
