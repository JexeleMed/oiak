/*
 * lab7.c -- Empiryczny pomiar struktury pamieci podrecznej L1D
 *
 * Trzy eksperymenty z uzyciem RDTSC (implementacja w lab7.asm):
 *
 *   Exp 1: Rozmiar linii cache [B]
 *          Zimny bufor, zmienny krok -> cpa rosnie liniowo, potem plateau
 *          Infleksja = rozmiar linii.
 *
 *   Exp 2: Rozmiar L1D -> liczba linii = L1_size / line_size
 *          Goraca pamiec, rosnacy bufor -> skok cpa = granica L1.
 *
 *   Exp 3: Asocjatywnosc (komórek w zestawie)
 *          Pointer chasing przez k adresow na ten sam zestaw:
 *          k <= N -> ~4 cykli/krok (trafienia L1)
 *          k >  N -> ~40+ cykli/krok (thrashing)
 *
 * Kompilacja:  make
 * Uruchomienie: ./lab7
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#define BUF_SIZE  (8 * 1024 * 1024)   /* 8 MB */

/* Deklaracje funkcji z lab7.asm */
extern uint64_t rdtsc_begin(void);
extern uint64_t rdtsc_end(void);
extern void     clflush_range(void *p, size_t n);
extern void     read_stride(const char *buf, size_t n, size_t stride);
extern uint64_t pointer_chase(void *start, size_t steps);

/* ====================================================================
 * Eksperyment 1 -- Rozmiar linii cache (metoda: losowy porzadek dostepu)
 *
 * Teoria:
 *   Dostep LOSOWY (przetasowana kolejnosc) eliminuje hardware prefetcher.
 *   Liczba cache missow zalezy wylacznie od liczby unikalnych LINII cache:
 *
 *   stride < line_size:  kilka adresow w tej samej linii -> 1 miss / (line_size/stride)
 *                        cpa = miss_penalty * stride / line_size  [liniowy wzrost]
 *
 *   stride >= line_size: kazdy dostep to inna linia -> 1 miss / dostep
 *                        cpa = miss_penalty  [PLATEAU]
 *
 *   Skok z liniowego wzrostu do plateau = rozmiar linii.
 *
 * Detekcja: szukamy pierwszego stride, gdzie ratio (cpa/cpa_poprzednia) < 1.5
 *           PO tym jak widzielismy juz ratio >= 1.8 (obszar liniowy).
 * ==================================================================== */
static size_t exp1_line_size(char *buf)
{
    /*
     * Uzywamy malego bufora (= COUNT * stride) aby uniknac za duzego
     * tablicy indeksow. COUNT = 8192 dostepow daje wystarczajaca dokladnosc.
     * Losowy dostep do tablicy indeksow jest sekwencyjny (indices[0..N]),
     * wiec prefetcher laduje indices[] z L2 - koszt stalY (~4 cykli),
     * niezalezny od stride. Roznica cpa miedzy stride'ami odzwierciedla
     * TYLKO zachowanie buf[].
     */
    const size_t COUNT = 8192;
    const int    RUNS  = 5;

    size_t *indices = malloc(COUNT * sizeof(size_t));
    if (!indices) return 64;

    printf("\n=== Eksperyment 1: Rozmiar linii cache ===\n");
    printf("  Dostep losowy (shuffle Fisher-Yates) | %zu krokow | %d prob\n",
           COUNT, RUNS);
    printf("  %-8s  %14s  %8s  %s\n",
           "Krok[B]", "Cykli/dostep", "Ratio", "");
    printf("  %-8s  %14s  %8s\n", "-------", "------------", "-----");

    double prev_cpa   = 0.0;
    int    saw_linear = 0;
    size_t detected   = 0;

    for (size_t stride = 1; stride <= 512; stride *= 2) {
        /* Wygeneruj COUNT adresow oddalonych o stride */
        for (size_t i = 0; i < COUNT; i++) indices[i] = i * stride;

        /* Przetasuj (Fisher-Yates) — eliminuje hardware prefetcher */
        for (size_t i = COUNT - 1; i > 0; i--) {
            size_t j   = (size_t)rand() % (i + 1);
            size_t tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }

        /* Zimna pamiec: wyrzuc zakres adresow uzywanych w tej iteracji */
        size_t span = COUNT * stride + 64;
        if (span > BUF_SIZE) span = BUF_SIZE;
        clflush_range(buf, span);

        /* Pomiar: dostep do buf[indices[i]] w losowej kolejnosci.
         * volatile zapobiega eliminacji petli przez kompilator. */
        volatile unsigned char *vbuf = (volatile unsigned char *)buf;
        uint64_t best = UINT64_MAX;

        for (int run = 0; run < RUNS; run++) {
            clflush_range(buf, span);

            uint64_t t0 = rdtsc_begin();
            for (size_t i = 0; i < COUNT; i++)
                (void)vbuf[indices[i]];
            uint64_t t1 = rdtsc_end();

            uint64_t elapsed = t1 - t0;
            if (elapsed < best) best = elapsed;
        }

        double cpa   = (double)best / (double)COUNT;
        double ratio = (prev_cpa > 0.0) ? cpa / prev_cpa : 0.0;
        const char *note = "";

        if (ratio >= 1.7)  saw_linear = 1;

        /*
         * Plateau: po obszarze liniowym — pierwsze stride, gdzie
         * podwojenie kroku ZMNIEJSZA cpa (cpa spada o > 5%).
         * Oznacza to, ze poprzedni stride byl na plateau,
         * wiec rozmiar linii = poprzedni stride = stride/2.
         *
         * Uwaga: "sub-liniowy wzrost" (ratio < 2 ale > 1) to nie plateau!
         * Plateau wykrywamy tylko gdy cpa realnie maleje.
         */
        if (saw_linear && ratio < 0.95 && detected == 0) {
            detected = stride / 2;
            note = " <-- plateau: cpa spada (linia = poprzedni krok)";
        }

        if (ratio > 0.0)
            printf("  %-8zu  %14.2f  %8.2f  %s\n", stride, cpa, ratio, note);
        else
            printf("  %-8zu  %14.2f  %8s  %s\n", stride, cpa, "-", note);

        prev_cpa = cpa;
    }

    free(indices);

    size_t line_size = detected ? detected : 64;
    printf("  => Wykryty rozmiar linii cache: %zu B\n\n", line_size);
    return line_size;
}

/* ====================================================================
 * Eksperyment 2 -- Rozmiar L1D i liczba linii
 *
 * Teoria (goraca pamiec, stride = line_size):
 *   B <= L1: po rozgrzaniu bufor siedzi caly w L1 -> ~4 cykli/dostep
 *   B >  L1: nie miesci sie -> missy L2 -> ~12 cykli/dostep
 *
 * Stride = line_size (= 64 B) -- jeden dostep = jedna linia cache.
 * Uzywamy dokladnie line_size zamiast stride=1, bo stride=1 pozwala
 * hardware prefetcherowi zamaskowac roznice miedzy L1 i L2 (prefetcher
 * liniowy dziala za dobrze przy kroku 1B).
 * ==================================================================== */
static size_t exp2_l1_size(const char *buf, size_t line_size)
{
    const int    REPS        = 3000;
    const size_t STRIDE_MEAS = 64;   /* stride staly = rozmiar linii x86-64 */

    printf("=== Eksperyment 2: Rozmiar L1D i liczba linii ===\n");
    printf("  Stride = %zu B (standard x86-64) | %d powtorzen (goraca pamiec)\n",
           STRIDE_MEAS, REPS);
    printf("  %-12s  %14s  %s\n", "Bufor[B]", "Cykli/dostep", "");
    printf("  %-12s  %14s\n", "---------", "------------");

    size_t detected = 0;
    double prev_cpa = 0.0;

    for (size_t sz = 2 * 1024; sz <= 512 * 1024; sz *= 2) {
        size_t n_acc = sz / STRIDE_MEAS;

        uint64_t t0 = rdtsc_begin();
        for (int r = 0; r < REPS; r++)
            read_stride(buf, sz, STRIDE_MEAS);
        uint64_t t1 = rdtsc_end();

        double cpa   = (double)(t1 - t0) / ((double)REPS * (double)n_acc);
        const char *note = "";

        if (prev_cpa > 0.0 && cpa > prev_cpa * 1.8 && detected == 0) {
            detected = sz / 2;
            note = " <-- skok (poprzedni rozmiar = L1)";
        }

        printf("  %-12zu  %14.2f  %s\n", sz, cpa, note);
        prev_cpa = cpa;
    }

    size_t l1 = detected ? detected : 32768;
    printf("\n  => Wykryty rozmiar L1D:     %zu B  (%zu KB)\n",
           l1, l1 >> 10);
    printf("  => Liczba linii w L1D:      %zu  (= %zu B / %zu B)\n\n",
           l1 / line_size, l1, line_size);
    return l1;
}

/* ====================================================================
 * build_chain -- buduje okragly lancuch wskaznikow
 *
 * Ustawia cykliczny lancuch wskaznikow w buforze:
 *   buf[0*S] -> buf[1*S] -> ... -> buf[(k-1)*S] -> buf[0*S]
 *
 * Kazdy wskaznik jest przechowywany na poczatku swojej linii cache
 * (pierwsze 8 bajtow kazdego wezla). Dlatego set_stride >= 8.
 *
 * Adresy oddalone o set_stride = num_sets * line_size mapuja na
 * TEN SAM zestaw cache (poniewaz set_index = (addr/64) mod num_sets).
 * ==================================================================== */
static void build_chain(char *buf, size_t set_stride, size_t k)
{
    for (size_t i = 0; i < k; i++) {
        char *src = buf + i * set_stride;
        char *dst = buf + ((i + 1) % k) * set_stride;
        *(char **)src = dst;
    }
}

/* ====================================================================
 * Eksperyment 3 -- Asocjatywnosc (komórek w zestawie)
 *
 * Teoria:
 *   Uzywamy pointer_chase() -- szeregowa latencja bez MLP.
 *   Latencja kroku = latencja JEDNEGO dostepu do pamieci.
 *
 *   k <= N (miesci sie):  steady state = trafienia L1 -> ~4 cykli/krok
 *   k >  N (thrashing):  kazdy krok wypiera poprzedni -> ~40+ cykli/krok
 *
 *   Prawidlowy set_stride = num_sets * line_size = L1_size / N
 *   Jesli set_stride jest wielokrotnoscia tej wartosci, adresy tez
 *   mapuja na ten sam zestaw -> efekt thrashingu widoczny.
 *
 * Szukamy set_stride, przy ktorym wystepuje wyrazny skok.
 * ==================================================================== */
static void exp3_assoc(char *buf, size_t l1_size, size_t line_size)
{
    (void)line_size;
    const size_t STEPS  = 2000000;  /* kroki lancucha (duzo > k dla stabilnosci) */
    const size_t MAX_K  = 20;

    printf("=== Eksperyment 3: Asocjatywnosc (komórek w zestawie) ===\n");
    printf("  Metoda: pointer chasing (lancuch wskaznikow)\n");
    printf("  Kroki na probe: %zu\n\n", STEPS);

    for (size_t set_stride = 512; set_stride <= l1_size; set_stride *= 2) {

        size_t max_k = BUF_SIZE / set_stride;
        if (max_k > MAX_K) max_k = MAX_K;
        if (max_k < 2)     continue;

        printf("  set_stride = %6zu B  (= L1 / %zu):\n",
               set_stride, l1_size / set_stride);
        printf("    %-5s  %14s\n", "k", "Cykli/krok");

        double prev_cpa   = 0.0;
        size_t detected_n = 0;

        for (size_t k = 1; k <= max_k; k++) {
            /*
             * Budujemy lancuch: 0 -> 1 -> 2 -> ... -> k-1 -> 0
             * Wszystkie k wezlow leza co set_stride bajtow => ten sam zestaw.
             */
            build_chain(buf, set_stride, k);

            /* Mierzymy z "goraca" pamiecia (bez flush):
             * - k <= N: po pierwszym cyklu lancucha wszystkie k linii
             *           sa w L1 -> kolejne kroki to trafienia
             * - k > N:  kazdy krok wypiera poprzedni -> ciagly thrashing
             * Przy STEPS >> k efekt startowy jest zaniedbywalny. */
            uint64_t cycles = pointer_chase(buf, STEPS);
            double   cpa    = (double)cycles / (double)STEPS;

            printf("    %-5zu  %14.2f", k, cpa);

            if (prev_cpa > 0.0 && cpa > prev_cpa * 1.8 && detected_n == 0) {
                detected_n = k - 1;
                printf("  <-- asocjatywnosc = %zu", detected_n);
            }
            printf("\n");

            prev_cpa = cpa;
        }
        printf("\n");
    }
}

/* ==================================================================== */
int main(void)
{
    char *buf = aligned_alloc(64, BUF_SIZE);
    if (!buf) {
        perror("aligned_alloc");
        return 1;
    }
    memset(buf, 0xAB, BUF_SIZE);   /* touch all pages (unikamy page fault w pomiarze) */
    srand(0x1234ABCD);             /* deterministyczny seed dla przetasowania w exp1 */

    printf("==================================================\n");
    printf("  Empiryczny pomiar struktury pamieci L1D\n");
    printf("  Metoda: RDTSC + NASM x86-64 (Linux)\n");
    printf("==================================================\n");

    size_t line_size = exp1_line_size(buf);
    size_t l1_size   = exp2_l1_size(buf, line_size);
               exp3_assoc(buf, l1_size, line_size);

    printf("==================================================\n");
    printf("  PODSUMOWANIE\n");
    printf("==================================================\n");
    printf("  Rozmiar linii cache   : %5zu B\n",           line_size);
    printf("  Rozmiar L1D           : %5zu B  (%zu KB)\n",
           l1_size, l1_size >> 10);
    printf("  Liczba linii w L1D    : %5zu\n",             l1_size / line_size);
    printf("  Asocjatywnosc         :        patrz Exp 3\n");
    printf("==================================================\n");

    free(buf);
    return 0;
}
