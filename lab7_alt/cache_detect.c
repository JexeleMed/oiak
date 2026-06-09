/*
 * Empirical L1D cache parameter detection using RDTSC and pointer chasing.
 *
 * Sweep 1 – stride (512 KB array, strides 8-256 B):
 *   Grouped traversal: cache-line-sized groups visited in random order,
 *   nodes within a group chained sequentially. This defeats the hardware
 *   prefetcher while preserving spatial locality within each cache line.
 *   cycles/access ~ stride/LINE_SZ for stride < line_sz, then plateau.
 *   Detection: first index reaching 75% of plateau maximum.
 *
 * Sweep 2 – array size (stride = detected line size, 1-512 KB):
 *   Each access touches a fresh cache line. Arrays fitting in L1D warm up
 *   after first pass; larger arrays always miss. Detection: first adjacent
 *   ratio > 1.5x (catches L1D->L2 boundary, not the deeper L3 boundary).
 *
 * Build: gcc -O2 -o cache_detect cache_detect.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_ARRAY  (1 << 19)   /* 512 KB — must be >> L1D (typ. 32-64 KB) */
#define N_CHASE    (1 << 20)   /* target pointer chases per sample */

#define STRIDE_THRESH  0.75    /* fraction of plateau to declare line-size hit */
#define SIZE_THRESH    1.50    /* adjacent-ratio jump signalling L1D boundary  */

static inline uint64_t rdtsc(void)
{
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t val;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
#error "Unsupported architecture: no cycle counter available"
#endif
}

static void build_list(char *buf, int arr_bytes, int stride, int group_sz)
{
    int npl, ngroups;
    if (stride <= group_sz) {
        npl     = group_sz / stride;
        ngroups = arr_bytes / group_sz;
    } else {
        npl     = 1;
        ngroups = arr_bytes / stride;
    }

    int *order = malloc(ngroups * sizeof(int));
    for (int i = 0; i < ngroups; i++) order[i] = i;
    for (int i = ngroups - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }

    for (int g = 0; g < ngroups; g++) {
        int base = order[g] * npl;
        for (int k = 0; k < npl - 1; k++) {
            char **nd = (char **)(buf + (size_t)(base + k) * stride);
            *nd = buf + (size_t)(base + k + 1) * stride;
        }
        int next    = order[(g + 1) % ngroups] * npl;
        char **last = (char **)(buf + (size_t)(base + npl - 1) * stride);
        *last = buf + (size_t)next * stride;
    }
    free(order);
}

static double measure(char *buf, int arr_bytes, int stride, int group_sz)
{
    int n    = arr_bytes / stride;
    int reps = N_CHASE / n;
    if (reps < 4) reps = 4;

    build_list(buf, arr_bytes, stride, group_sz);

    uint64_t best = UINT64_MAX;
    for (int t = 0; t < 3; t++) {
        char *p = buf;
        uint64_t t0 = rdtsc();
        for (int r = 0; r < reps; r++)
            for (int i = 0; i < n; i++)
                p = *(char **)p;
        uint64_t t1 = rdtsc();
        __asm__ volatile ("" : : "r"(p));
        if (t1 - t0 < best) best = t1 - t0;
    }
    return (double)best / ((double)n * reps);
}

/* First index where v[i] >= 75% of the plateau maximum. */
static int stride_jump(double *v, int n)
{
    double vmax = 0.0;
    for (int i = 0; i < n; i++) if (v[i] > vmax) vmax = v[i];
    for (int i = 0; i < n; i++) if (v[i] >= STRIDE_THRESH * vmax) return i;
    return n - 1;
}

/* First index where v[i] > 1.5 * v[i-1]. */
static int size_jump(double *v, int n)
{
    for (int i = 1; i < n; i++)
        if (v[i] > SIZE_THRESH * v[i - 1]) return i;
    return n - 1;
}

int main(void)
{
    srand(42);
    char *buf = aligned_alloc(64, MAX_ARRAY);
    if (!buf) { perror("aligned_alloc"); return 1; }
    memset(buf, 0, MAX_ARRAY);

    /* Stride sweep */
    /* max stride sets upper bound on detectable line size */
    int    strides[] = { 8, 16, 32, 64, 128, 256 };
    int    ns = (int)(sizeof strides / sizeof strides[0]);
    double sv[16];

    /* group_sz = max stride: guaranteed >= actual line size on any known CPU,
     * so spatial locality within a group is determined by hardware, not group_sz */
    int group_sz = strides[ns - 1];

    for (int i = 0; i < ns; i++)
        sv[i] = measure(buf, MAX_ARRAY, strides[i], group_sz);
    int li      = stride_jump(sv, ns);
    int line_sz = strides[li];

    printf("=== Stride sweep  (array = 512 KB, group_sz = %d B) ===\n", group_sz);
    printf("%-10s  %14s  %8s\n", "Stride(B)", "Cycles/acc", "Ratio");
    for (int i = 0; i < ns; i++) {
        double r = (i == 0) ? 1.0 : sv[i] / sv[i - 1];
        printf("%-10d  %14.2f  %7.2fx%s\n",
               strides[i], sv[i], r, i == li ? "  <-- line size" : "");
    }
    printf("  Detected cache line size: %d B\n\n", line_sz);

    /* Size sweep */
    int  sizes[20]; int nsz = 0;
    for (int s = 1024; s <= MAX_ARRAY; s <<= 1) sizes[nsz++] = s;
    double av[20];
    for (int i = 0; i < nsz; i++)
        av[i] = measure(buf, sizes[i], line_sz, line_sz);
    int si = size_jump(av, nsz);

    printf("=== Size sweep  (stride = %d B) ===\n", line_sz);
    printf("%-10s  %14s\n", "Size(KB)", "Cycles/acc");
    for (int i = 0; i < nsz; i++)
        printf("%-10d  %14.2f%s\n",
               sizes[i] >> 10, av[i], i == si ? "  <-- L1D boundary" : "");

    int l1d_bytes = sizes[si - 1];
    printf("  L1D fills between %d KB and %d KB\n\n",
           l1d_bytes >> 10, sizes[si] >> 10);

    printf("=== Results ===\n");
    printf("  Line size      : %d B\n", line_sz);
    printf("  Number of lines: %d\n",   l1d_bytes / line_sz);

    free(buf);
    return 0;
}
