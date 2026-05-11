#include <stdio.h>

extern double f_asm(double x);
extern double g_asm(double x);

int main() {
    printf("Porownanie stabilnosci numerycznej FPU (x87)\n");
    printf("------------------------------------------------------------\n");
    printf("%-10s | %-22s | %-22s\n", "x", "f(x) [Niestabilne]", "g(x) [Stabilne]");
    printf("------------------------------------------------------------\n");

    // coraz mniejsze x
    double x_values[] = {1.0, 0.1, 0.01, 1e-4, 1e-6, 1e-8, 1e-9};
    int num_values = sizeof(x_values) / sizeof(x_values[0]);

    for (int i = 0; i < num_values; i++) {
        double x = x_values[i];
        double res_f = f_asm(x);
        double res_g = g_asm(x);
        
        // %e notacja naukowa
        printf("%-10.1e | %-22.16e | %-22.16e\n", x, res_f, res_g);
    }

    return 0;
}
