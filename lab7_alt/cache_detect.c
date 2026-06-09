#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Funkcje z helper.asm: CPUID+RDTSC na starcie, RDTSCP+CPUID na końcu
 * — serializacja zapobiega reorderingowi instrukcji przez CPU */
extern uint64_t perf_open();
extern uint64_t perf_close_x();

#define DIM     8192                 /* bok macierzy 2D */
#define BUF_LEN (1024 * 1024 * 32)  /* 128 MB bufora dla testów linii/pojemności */

int main() {
    uint64_t t0, t1;
    volatile int val;  /* volatile zapobiega optymalizacji odczytu przez kompilator */

    /* Alokacja macierzy 2D jako tablicy wskaźników — wiersze nie są ciągłe w pamięci */
    int **grid = malloc(DIM * sizeof(int*));
    for(int i = 0; i < DIM; i++) grid[i] = malloc(DIM * sizeof(int));

    /* Bufor ciągły — używany w testach linii cache i pojemności */
    int *data = malloc(BUF_LEN * sizeof(int));

    /* Test 1: dostęp wierszami [i][j] vs kolumnami [j][i]
     * Wierszami = kolejne adresy → trafienia cache; kolumnami = skoki o wiersz → chybienia */
    printf("Odczyt dla [i][j] oraz [j][i]\n");

    t0 = perf_open();
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            val = grid[i][j];
    t1 = perf_close_x();
    printf("Odczyt wierszami [i][j]: %llu cykli\n", (unsigned long long)(t1 - t0));

    t0 = perf_open();
    for (int i = 0; i < DIM; i++)
        for (int j = 0; j < DIM; j++)
            val = grid[j][i];
    t1 = perf_close_x();
    printf("Odczyt kolumnami [j][i]: %llu cykli\n\n", (unsigned long long)(t1 - t0));

    /* Test 2: wykrywanie rozmiaru linii cache
     * Przy stride < rozmiar_linii/4 wiele odczytów korzysta z tej samej linii → mniej chybień.
     * Gdy stride >= rozmiar_linii/4 (tj. 16 dla linii 64B) każdy odczyt ładuje nową linię →
     * liczba cykli/odczyt przestaje rosnąć (plateau) — to wskazuje rozmiar linii. */
    printf("Ile komórek pamięci ma linia?\n");
    for (int stride = 1; stride <= 64; stride *= 2) {
        int cnt = 0;
        t0 = perf_open();
        for (int i = 0; i < BUF_LEN; i += stride) {
            val = data[i];
            cnt++;
        }
        t1 = perf_close_x();
        printf("Skok co %2d -> Srednio: %3llu cykli na odczyt\n", stride, (unsigned long long)(t1 - t0) / cnt);
    }

    /* Test 3: wykrywanie pojemności L1 cache
     * Dane wielokrotnie przepisywane (reps=100). Dopóki mieszczą się w L1 →
     * po pierwszym przejściu kolejne to trafienia L1 (szybko). Po przekroczeniu
     * rozmiaru L1 każde przejście generuje chybienia → skokowy wzrost cykli/odczyt. */
    printf("\nPojemność cache:\n");
    for (int sz_kb = 4; sz_kb <= 512; sz_kb *= 2) {
        int limit = (sz_kb * 1024) / 4;  /* liczba elementów int w sz_kb KB */
        int reps  = 100;

        t0 = perf_open();
        for (int k = 0; k < reps; k++)
            for (int i = 0; i < limit; i++)
                val = data[i];
        t1 = perf_close_x();
        printf("Rozmiar danych: %3d KB -> Srednio: %3llu cykli na odczyt\n",
               sz_kb, (unsigned long long)(t1 - t0) / (limit * reps));
    }

    for(int i = 0; i < DIM; i++) free(grid[i]);
    free(grid);
    free(data);

    return 0;
}
