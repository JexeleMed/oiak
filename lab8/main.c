/*
 * main.c -- numeryczne calkowanie f(x) = e^x na [a, b] metoda prostokatow
 * (punkt srodkowy), z pomiarem czasu w cyklach procesora (RDTSC).
 *
 * Trzy wersje:
 *   1) integrate_scalar   -- naiwna petla, exp() z libm dla kazdego punktu
 *   2) integrate_simd_exp -- petla AVX, e^x liczone uczciwie dla kazdego
 *      punktu, ale wektorowo (redukcja zakresu + wielomian, simd.asm);
 *      pokazuje, ile daje SAMA wektoryzacja
 *   3) integrate_simd_geo -- petla AVX + trik algorytmiczny: e^(a+ih) to
 *      ciag geometryczny, wiec exp() liczymy tylko 17 razy na starcie,
 *      a w petli sa juz same mnozenia
 *
 * Weryfikacja poprawnosci jest darmowa: dokladna wartosc to e^b - e^a.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

/* funkcje z simd.asm */
uint64_t tsc_start(void);
uint64_t tsc_stop(void);
double   simd_geo_sum(const double *init, double factor, uint64_t iters);
double   simd_exp_sum(double x0, double h, uint64_t iters);

#define LANES 16   /* tyle punktow przerabia jedna iteracja petli SIMD */
#define REPS  9    /* powtorzenia pomiaru -- bierzemy najlepszy wynik */

/* ---- wersja liniowa (baseline): exp() w kazdej iteracji ---- */
static double integrate_scalar(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;
    double sum = 0.0;
    for (uint64_t i = 0; i < n; i++)
        sum += exp(a + ((double)i + 0.5) * h);   /* punkt srodkowy podprzedzialu */
    return sum * h;
}

/* ---- wersja SIMD "uczciwa": wektorowy exp, 4 punkty na iteracje ---- */
static double integrate_simd_exp(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;
    return simd_exp_sum(a + 0.5 * h, h, n / 4) * h;
}

/* ---- wersja SIMD + trik geometryczny: C tylko przygotowuje dane ---- */
static double integrate_simd_geo(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;

    /* 16 pierwszych wartosci funkcji -- punkty startowe dla 4 wektorow ymm */
    double init[LANES];
    for (int i = 0; i < LANES; i++)
        init[i] = exp(a + ((double)i + 0.5) * h);

    /* mnoznik przesuwajacy o 16 punktow: e^(x+16h) = e^x * e^(16h) */
    double factor = exp((double)LANES * h);

    return simd_geo_sum(init, factor, n / LANES) * h;
}

/* Narzut samego pomiaru (tsc_start + tsc_stop bez niczego w srodku).
 * Mierzymy go wiele razy i bierzemy minimum, potem odejmujemy od wynikow. */
static uint64_t measure_overhead(void)
{
    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 10000; i++) {
        uint64_t t0 = tsc_start();
        uint64_t t1 = tsc_stop();
        if (t1 - t0 < best)
            best = t1 - t0;
    }
    return best;
}

/* zapobiega wyrzuceniu obliczen przez optymalizator */
static volatile double sink;

int main(int argc, char **argv)
{
    double   a = 0.0, b = 1.0;
    uint64_t n = 1u << 24;            /* 16M punktow, podzielne przez 16 */

    if (argc == 4) {
        a = atof(argv[1]);
        b = atof(argv[2]);
        n = strtoull(argv[3], NULL, 10);
    }
    if (n % LANES != 0) {
        fprintf(stderr, "n musi byc podzielne przez %d\n", LANES);
        return 1;
    }

    uint64_t overhead = measure_overhead();
    double   exact    = exp(b) - exp(a);

    /* kazda wersje liczymy REPS razy i bierzemy najszybszy przebieg --
     * minimum najlepiej przybliza "czysty" czas bez zaklocen od systemu */
    struct {
        const char *name;
        double (*fn)(double, double, uint64_t);
        double result;
        uint64_t cycles;
    } v[] = {
        { "skalarnie (exp z libm)      ", integrate_scalar,   0, UINT64_MAX },
        { "SIMD, wektorowy exp         ", integrate_simd_exp, 0, UINT64_MAX },
        { "SIMD + ciag geometryczny    ", integrate_simd_geo, 0, UINT64_MAX },
    };
    int nv = (int)(sizeof v / sizeof v[0]);

    for (int i = 0; i < nv; i++) {
        for (int r = 0; r < REPS; r++) {
            uint64_t t0 = tsc_start();
            v[i].result = v[i].fn(a, b, n);
            uint64_t t1 = tsc_stop();
            sink = v[i].result;
            if (t1 - t0 - overhead < v[i].cycles)
                v[i].cycles = t1 - t0 - overhead;
        }
    }

    printf("Calka e^x na [%g, %g], n = %llu, narzut pomiaru = %llu cykli\n\n",
           a, b, (unsigned long long)n, (unsigned long long)overhead);
    printf("dokladnie (e^b - e^a): %.15f\n\n", exact);

    for (int i = 0; i < nv; i++) {
        printf("%s %.15f  (blad wzgl. %.2e)\n",
               v[i].name, v[i].result, fabs(v[i].result - exact) / exact);
        printf("%29s %llu cykli, %.2f cykla/punkt, przyspieszenie %.1fx\n\n", "",
               (unsigned long long)v[i].cycles,
               (double)v[i].cycles / (double)n,
               (double)v[0].cycles / (double)v[i].cycles);
    }
    return 0;
}
