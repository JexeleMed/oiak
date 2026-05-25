#include <stdio.h>
#include <stdint.h>

// Deklaracje funkcji z pliku .asm
extern double f_asm(double x);
extern double g_asm(double x);
extern uint16_t get_fpu_cw(void);
extern void set_fpu_cw(uint16_t cw);

// Maski bitowe sterujące zaokrąglaniem (Bity 10 i 11 w Rejestrze Kontrolnym)
#define FPU_RC_MASK    0xF3FF // Używamy by wyzerować bity 10 i 11 przed ustawieniem nowych
#define FPU_RC_NEAREST 0x0000 // 00 - Do najbliższej (Domyślny)
#define FPU_RC_DOWN    0x0400 // 01 - W dół (-nieskończoność)
#define FPU_RC_UP      0x0800 // 10 - W górę (+nieskończoność)
#define FPU_RC_TRUNC   0x0C00 // 11 - Obcinanie ułamka (w stronę zera)

// Funkcja pomocnicza - nakłada maskę i zmienia tryb
void set_rounding_mode(uint16_t mode) {
    uint16_t cw = get_fpu_cw();
    cw = (cw & FPU_RC_MASK) | mode; 
    set_fpu_cw(cw);
}

int main() {
    printf("Porownanie stabilnosci numerycznej FPU - Różne Tryby Zaokraglania\n");
    printf("-------------------------------------------------------------------------\n");
    printf("%-10s | %-12s | %-22s | %-22s\n", "x", "Tryb FPU", "f(x) [Niestabilne]", "g(x) [Stabilne]");
    printf("-------------------------------------------------------------------------\n");

    double x_values[] = {1.0, 1e-4, 1e-8, 1e-16};
    int num_values = sizeof(x_values) / sizeof(x_values[0]);

    const char* mode_names[] = {"Najblizsza", "W dol", "W gore", "Obcinanie"};
    uint16_t modes[] = {FPU_RC_NEAREST, FPU_RC_DOWN, FPU_RC_UP, FPU_RC_TRUNC};

    for (int i = 0; i < num_values; i++) {
        double x = x_values[i];
        
        // Pętla po wszystkich 4 trybach zaokrąglania na sprzęcie
        for (int m = 0; m < 4; m++) {
            set_rounding_mode(modes[m]);
            
            double res_f = f_asm(x);
            double res_g = g_asm(x);
            
            // Formatowanie: wypisz "x" tylko dla pierwszego wiersza z danej grupy
            if (m == 0) {
                printf("%-10.1e | %-12s | %-22.16e | %-22.16e\n", x, mode_names[m], res_f, res_g);
            } else {
                printf("%-10s | %-12s | %-22.16e | %-22.16e\n", "", mode_names[m], res_f, res_g);
            }
        }
        printf("-------------------------------------------------------------------------\n");
    }

    // Reset FPU do trybu domyślnego dla czystości
    set_rounding_mode(FPU_RC_NEAREST);

    return 0;
}
