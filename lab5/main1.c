#include <stdio.h>

// Declare the external assembly function
extern unsigned long long get_tsc(char mode);

int main() {
    // Call the function with mode '1' (Standard RDTSC)
    unsigned long long tsc1 = get_tsc('1');
    printf("Wartosc TSC (metoda 1 - RDTSC): %llu\n", tsc1);

    // Call the function with mode '2' (Serialized RDTSCP)
    unsigned long long tsc2 = get_tsc('2');
    printf("Wartosc TSC (metoda 2 - RDTSCP): %llu\n", tsc2);

    return 0;
}
