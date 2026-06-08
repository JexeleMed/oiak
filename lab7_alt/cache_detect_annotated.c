/*
 * cache_detect.c – empirycznie mierzy rozmiar linii cache L1D i pojemność L1D.
 *
 * PROBLEM z prostą pętlą arr[i*stride]:
 *   Hardware stride-prefetcher CPU-a wykrywa regularny wzorzec dostępów i
 *   pobiera linie cache z wyprzedzeniem → każdy stride wygląda tak samo fast
 *   (~7 cykli), więc nie widać żadnej różnicy między stride=8 a stride=256.
 *
 * ROZWIĄZANIE – pointer chasing (ściganie wskaźników):
 *   p = *(char**)p
 *   Adres kolejnego dostępu zależy od wyniku POPRZEDNIEGO odczytu (data
 *   dependency). Prefetcher nie może przewidzieć adresu zanim nie skończy się
 *   poprzedni load → każda linijka cache jest naprawdę ładowana z pamięci.
 *
 * PROBLEM z czystym losowym pointer chasingiem:
 *   Przy buforze 512 KB (>> L1D 32 KB) każdy dostęp jest cache miss niezależnie
 *   od stride → nie widać różnicy między stride=8 a stride=64.
 *
 * ROZWIĄZANIE – grupowany obchód:
 *   Dzielimy bufor na grupy po GROUP_SZ=64 B (= 1 linia cache).
 *   Węzły WEWNĄTRZ grupy są łączone sekwencyjnie → jeden miss ładuje całą grupę.
 *   KOLEJNOŚĆ GRUP jest losowa → prefetcher nie widzi wzorca między grupami.
 *   Efekt: cycles/access ∝ stride dla stride < line_sz, potem plateau.
 *
 * Build:  gcc -O2 -o cache_detect cache_detect.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* 512 KB – rozmiar bufora do pomiaru stride (musi być >> L1D) */
#define MAX_ARRAY   (1 << 19)

/* 64 B – rozmiar fizycznej linii cache na x86-64; tak grupujemy węzły */
#define GROUP_SZ    64

/* ~1 mln pointer-chase'ów na pomiar – wystarczy dla stabilnego czasu */
#define N_CHASE     (1 << 20)

/* ── RDTSC ──────────────────────────────────────────────────────────────────
 * Odczyt licznika taktów procesora (Time Stamp Counter).
 *
 * Instrukcja RDTSC wpisuje:
 *   niższe 32 bity → EAX  (constraint "=a" wiąże EAX z `lo`)
 *   wyższe 32 bity → EDX  (constraint "=d" wiąże EDX z `hi`)
 * Składamy z nich 64-bitowy wynik: hi<<32 | lo.
 *
 * "volatile" w __asm__ volatile mówi kompilatorowi:
 *   "nie przenoś tej instrukcji przez inne operacje" –
 *   bez tego optymalizator mógłby przestawić RDTSC poza mierzoną pętlę.
 */
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ── build_list ─────────────────────────────────────────────────────────────
 *
 * Buduje kołową listę wskaźników w buforze `buf`.
 * Węzły (komórki, każda stride B) są podzielone na grupy po GROUP_SZ B.
 *
 *  stride ≤ GROUP_SZ:
 *    npl    = GROUP_SZ/stride   ← ile węzłów mieści się w jednej grupie
 *    ngroups = arr_bytes/GROUP_SZ ← ile grup po GROUP_SZ B
 *
 *  stride > GROUP_SZ (np. stride=128, 256):
 *    npl    = 1  ← 1 węzeł na "grupę" (po prostu losowa kolejność)
 *    ngroups = arr_bytes/stride
 *
 *  W obu przypadkach: ngroups * npl == arr_bytes/stride
 *  → żaden offset nie wychodzi poza bufor.
 *
 * Struktura listy:
 *   grupa[order[0]], węzeł 0 → węzeł 1 → ... → węzeł npl-1
 *                                                       ↓
 *   grupa[order[1]], węzeł 0 → węzeł 1 → ... → węzeł npl-1
 *                                                       ↓
 *   ...  (kolejność grup losowa z Fisher-Yates)
 *
 * Efekt przy obchodzeniu listy:
 *   – pierwsze wejście w nową grupę: cache miss (ładuje całą linię cache)
 *   – kolejne węzły w TEJ SAMEJ grupie: cache hit (są w tej samej linii)
 *   – skok do LOSOWEJ kolejnej grupy: prefetcher nie wie dokąd – cache miss
 */
static void build_list(char *buf, int arr_bytes, int stride)
{
    int npl, ngroups;

    /* Oblicz ile węzłów mieści się w jednej grupie i ile jest grup */
    if (stride <= GROUP_SZ) {
        npl     = GROUP_SZ / stride;  /* np. stride=8 → 8 węzłów/grupę      */
        ngroups = arr_bytes / GROUP_SZ;
    } else {
        npl     = 1;                  /* stride > 64B → 1 węzeł = 1 "grupa"  */
        ngroups = arr_bytes / stride;
    }

    /* Utwórz tablicę indeksów grup: order[0]=0, order[1]=1, ... */
    int *order = malloc(ngroups * sizeof(int));
    for (int i = 0; i < ngroups; i++) order[i] = i;

    /* Tasuj losowo algorytmem Fisher-Yates:
     * dla i = ngroups-1 downto 1:
     *   wybierz losowy j z [0, i]
     *   zamień order[i] ↔ order[j]
     * Wynik: każda permutacja grupy ma jednakowe prawdopodobieństwo. */
    for (int i = ngroups - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    /* Połącz węzły w listę kołową */
    for (int g = 0; g < ngroups; g++) {

        /* Indeks PIERWSZEGO węzła w grupie order[g] */
        int base = order[g] * npl;

        /* Łańcuch węzłów WEWNĄTRZ grupy: węzeł k → węzeł k+1
         * Wszystkie te węzły leżą w obrębie jednej linii cache (GROUP_SZ B),
         * więc po załadowaniu pierwszego reszta jest już w L1D – cache hit. */
        for (int k = 0; k < npl - 1; k++) {
            /* Adres węzła k w pamięci to: buf + (base+k)*stride */
            char **nd = (char **)(buf + (size_t)(base + k) * stride);
            /* Wpisz adres NASTĘPNEGO węzła jako wartość wskaźnika */
            *nd = buf + (size_t)(base + k + 1) * stride;
        }

        /* Ostatni węzeł grupy g → pierwszy węzeł NASTĘPNEJ grupy (losowej)
         * Ten skok trafia w inną, nieciągłą część pamięci → cache miss */
        int next  = order[(g + 1) % ngroups] * npl;
        char **last = (char **)(buf + (size_t)(base + npl - 1) * stride);
        *last = buf + (size_t)next * stride;
    }

    free(order);
}

/* ── measure ────────────────────────────────────────────────────────────────
 * Mierzy średnią liczbę cykli RDTSC na jeden pointer-chase.
 *
 * Pętla:
 *   p = *(char**)p
 *   to odczyt 8-bajtowego wskaźnika spod adresu p i nadpisanie p tą wartością.
 *   Jest to celowo SERIALIZOWANE – każda iteracja zależy od poprzedniej.
 *
 * 3 próby, zwracamy MINIMUM:
 *   Minimum usuwa zakłócenia systemu operacyjnego (przerwy, context switch).
 *   Dla tablic > L1D nawet minimum jest wolne, bo każda próba ma cache miss'y.
 */
static double measure(char *buf, int arr_bytes, int stride)
{
    /* Liczba węzłów w całej liście */
    int n    = arr_bytes / stride;

    /* Ile razy powtarzamy pełne przejście listy, aby ~N_CHASE łącznych operacji */
    int reps = N_CHASE / n;
    if (reps < 4) reps = 4;   /* minimum 4 powtórzenia dla stabilności */

    /* Zbuduj listę wskaźników dla tych parametrów */
    build_list(buf, arr_bytes, stride);

    uint64_t best = UINT64_MAX;  /* najlepszy (najmniejszy) wynik z 3 prób */

    for (int t = 0; t < 3; t++) {
        char *p = buf;               /* zaczyna od pierwszego węzła */

        uint64_t t0 = rdtsc();       /* odczyt TSC przed pętlą */

        for (int r = 0; r < reps; r++)
            for (int i = 0; i < n; i++)
                p = *(char **)p;     /* ścigaj wskaźnik – data dependency! */

        uint64_t t1 = rdtsc();       /* odczyt TSC po pętli */

        /* __asm__ volatile z "+r"(p) mówi kompilatorowi:
         * "zmienna p jest UŻYWANA" → kompilator nie może usunąć pętli
         * jako martwego kodu (dead code elimination). */
        __asm__ volatile ("" : : "r"(p));

        if (t1 - t0 < best) best = t1 - t0;   /* zachowaj minimum */
    }

    /* Podziel łączny czas przez łączną liczbę operacji → cykli/operację */
    return (double)best / ((double)n * reps);
}

/* ── stride_jump ────────────────────────────────────────────────────────────
 * Wykrywa początek plateau w pomiarach stride sweep.
 *
 * Idea: plateau (= pełna latencja cache miss) zaczyna się gdy stride ≥ LINE_SZ,
 * bo wtedy każdy węzeł jest w osobnej linii cache → 1 miss na 1 dostęp.
 *
 * Metoda "75% maksimum":
 *   Szukamy pierwszego indeksu gdzie cycles[i] ≥ 0.75 × max(cycles).
 *
 * Dlaczego NIE ratio:
 *   – Przy małych stride'ach ratio < 1.5, bo L1D-hit wewnątrz grupy "rozcieńcza"
 *     koszt missa → pierwsze progi 1.5 strzelają za wcześnie.
 *   – Metoda "minimum ratio" odpala na szumie przy dużych stride'ach.
 *   – Próg 75% jest stabilny: wartości sub-plateau leżą wyraźnie poniżej.
 */
static int stride_jump(double *v, int n)
{
    /* Znajdź maksymalną wartość (≈ latencja miss, czyli poziom plateau) */
    double vmax = 0.0;
    for (int i = 0; i < n; i++) if (v[i] > vmax) vmax = v[i];

    /* Zwróć indeks pierwszego pomiaru sięgającego 75% plateau */
    for (int i = 0; i < n; i++) if (v[i] >= 0.75 * vmax) return i;

    return n - 1;   /* fallback – nie powinien wystąpić przy sensownych danych */
}

/* ── size_jump ──────────────────────────────────────────────────────────────
 * Wykrywa granicę L1D w pomiarach size sweep.
 *
 * Metoda "pierwsze sąsiednie ratio > 1.5":
 *   Szukamy pierwszego indeksu k gdzie v[k] > 1.5 × v[k-1].
 *
 * Dlaczego NIE median split:
 *   Z krokiem = LINE_SZ dane mają TRZY poziomy: L1D (~4 cykli), L2 (~12),
 *   L3 (~35). Median split maksymalizuje kontrast przez całą tablicę i trafia
 *   na granicę L3/DRAM zamiast na L1D/L2 – nas interesuje PIERWSZA granica.
 *   Adjacent ratio zawsze trafia na pierwszy duży skok, czyli L1D → L2.
 */
static int size_jump(double *v, int n)
{
    for (int i = 1; i < n; i++)
        if (v[i] > 1.5 * v[i - 1]) return i;   /* pierwsze >1.5× przejście */
    return n - 1;
}

/* ── main ───────────────────────────────────────────────────────────────────*/
int main(void)
{
    srand(42);   /* stały seed → reprodukowalne wyniki (lista przetasowana tak samo) */

    /* aligned_alloc(64, ...) gwarantuje że buf zaczyna się na granicy 64B
     * (= granicy linii cache) → nie ma "split-line" przy pierwszym dostępie */
    char *buf = aligned_alloc(64, MAX_ARRAY);
    if (!buf) { perror("aligned_alloc"); return 1; }

    /* memset wymusza stronicowanie: Linux przydziela strony dopiero przy
     * pierwszym zapisie (copy-on-write). Bez tego pierwsze pomiary byłyby
     * zaburzone przez page fault'y. */
    memset(buf, 0, MAX_ARRAY);

    /* ── Sweep 1: różne stride'y, stały rozmiar bufora (512 KB) ──────────── */

    int    strides[] = { 8, 16, 32, 64, 128, 256 };
    int    ns = (int)(sizeof strides / sizeof strides[0]);
    double sv[16];   /* wyniki pomiarów dla każdego stride */

    /* Zbierz wszystkie pomiary przed detekcją (by poprawnie oznaczyć wiersz) */
    for (int i = 0; i < ns; i++)
        sv[i] = measure(buf, MAX_ARRAY, strides[i]);

    /* Wykryj indeks stride'a będącego rozmiarem linii cache */
    int li      = stride_jump(sv, ns);
    int line_sz = strides[li];

    printf("=== Stride sweep  (array = 512 KB, groups = %d B) ===\n", GROUP_SZ);
    printf("%-10s  %14s  %8s\n", "Stride(B)", "Cycles/acc", "Ratio");
    for (int i = 0; i < ns; i++) {
        double r = (i == 0) ? 1.0 : sv[i] / sv[i - 1];   /* ratio vs poprzedni */
        printf("%-10d  %14.2f  %7.2fx%s\n",
               strides[i], sv[i], r,
               i == li ? "  <-- line size" : "");         /* oznacz wykryty */
    }
    printf("  Detected cache line size: %d B\n\n", line_sz);

    /* ── Sweep 2: różne rozmiary bufora, stride = wykryty LINE_SZ ──────────
     *
     * Z krokiem równym rozmiarowi linii cache każdy węzeł leży w osobnej
     * linii cache → każdy dostęp to potencjalny miss.
     * Dla małych buforów (< L1D) po pierwszym przejściu wszystko jest w L1D
     * → minimum z 3 prób jest małe (~4 cykli).
     * Dla dużych buforów (> L1D) każde przejście ma missy
     * → minimum też jest duże (~12-35 cykli).
     */

    /* Generuj rozmiary: 1 KB, 2 KB, 4 KB, ..., 512 KB (potęgi 2) */
    int  sizes[20]; int nsz = 0;
    for (int s = 1024; s <= MAX_ARRAY; s <<= 1) sizes[nsz++] = s;

    double av[20];   /* wyniki pomiarów dla każdego rozmiaru */
    for (int i = 0; i < nsz; i++)
        av[i] = measure(buf, sizes[i], line_sz);

    /* Wykryj indeks pierwszego rozmiaru przekraczającego L1D */
    int si = size_jump(av, nsz);

    printf("=== Size sweep  (stride = %d B) ===\n", line_sz);
    printf("%-10s  %14s\n", "Size(KB)", "Cycles/acc");
    for (int i = 0; i < nsz; i++)
        printf("%-10d  %14.2f%s\n",
               sizes[i] >> 10, av[i],
               i == si ? "  <-- L1D boundary" : "");

    /* sizes[si]   = pierwszy rozmiar który NIE mieści się w L1D
     * sizes[si-1] = największy rozmiar który MIEŚCI się w L1D = pojemność L1D */
    int l1d_bytes = sizes[si - 1];
    printf("  L1D fills between %d KB and %d KB\n\n",
           l1d_bytes >> 10, sizes[si] >> 10);

    printf("=== Results ===\n");
    printf("  Line size      : %d B\n", line_sz);
    printf("  Number of lines: %d\n",   l1d_bytes / line_sz);

    free(buf);
    return 0;
}
