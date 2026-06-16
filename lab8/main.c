#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

uint64_t tsc_start(void);
uint64_t tsc_stop(void);
double   simd_geo_sum(const double *init, double factor, uint64_t iters);
double   simd_exp_sum(double x0, double h, uint64_t iters);

#define LANES 16   /* 4 rejestry ymm * 4 double */
#define REPS  9

static double integrate_scalar(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;
    double sum = 0.0;
    for (uint64_t i = 0; i < n; i++)
        sum += exp(a + ((double)i + 0.5) * h);
    return sum * h;
}

static double integrate_simd_exp(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;
    return simd_exp_sum(a + 0.5 * h, h, n / 4) * h;
}

static double integrate_simd_geo(double a, double b, uint64_t n)
{
    double h = (b - a) / (double)n;

    double init[LANES];
    for (int i = 0; i < LANES; i++)
        init[i] = exp(a + ((double)i + 0.5) * h);

    double factor = exp((double)LANES * h);

    return simd_geo_sum(init, factor, n / LANES) * h;
}

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

/* volatile: bez tego -O2 usuwa obliczenia jako "nieuzywane" */
static volatile double sink;

int main(int argc, char **argv)
{
    double   a = 0.0, b = 1.0;
    uint64_t n = 1u << 24;

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
