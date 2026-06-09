.section .text
.global perf_open
.global perf_close
.global perf_close_x

/* Zwraca timestamp startu (cykle TSC) jako uint64_t w RAX.
 * CPUID przed RDTSC = bariera serializująca: CPU kończy wszystkie
 * wcześniejsze instrukcje zanim odczyta licznik. */
perf_open:
    pushq   %rbx            /* CPUID niszczy RBX — zachowaj */
    xorl    %eax, %eax      /* EAX=0: wybór liścia CPUID (basic info) */
    cpuid                   /* bariera serializacji out-of-order */
    rdtsc                   /* EDX:EAX = bieżąca wartość licznika TSC */
    shlq    $32, %rdx       /* przesuń górne 32 bity na pozycję [63:32] */
    orq     %rdx, %rax      /* złącz: RAX = pełne 64-bitowe TSC */
    popq    %rbx
    ret

/* Zwraca timestamp końca — wariant z RDTSC (bez wbudowanej serializacji).
 * Kolejność: najpierw RDTSC, potem CPUID jako bariera "zamykająca". */
perf_close:
    pushq   %rbx
    rdtsc                   /* odczytaj TSC zanim cokolwiek po nim się wykona */
    movq    %rax, %r8       /* zachowaj wynik — CPUID zaraz nadpisze RAX/RDX */
    movq    %rdx, %r9
    xorl    %eax, %eax
    cpuid                   /* bariera: upewnia się że RDTSC nie był spekulatywny */

    movq    %r9, %rdx       /* przywróć zachowany wynik TSC */
    movq    %r8, %rax
    shlq    $32, %rdx       /* złóż 64-bitową wartość tak samo jak w perf_open */
    orq     %rdx, %rax
    popq    %rbx
    ret

/* Jak perf_close, ale używa RDTSCP zamiast RDTSC.
 * RDTSCP ma wbudowaną częściową serializację: gwarantuje że wszystkie
 * poprzednie odczyty pamięci zakończyły się przed pomiarem. */
perf_close_x:
    pushq   %rbx
    rdtscp                  /* EDX:EAX = TSC, ECX = ID rdzenia (IA32_TSC_AUX) */
    movq    %rax, %r8       /* zachowaj wynik przed CPUID */
    movq    %rdx, %r9
    xorl    %eax, %eax
    cpuid                   /* bariera zamykająca — blokuje instrukcje po RDTSCP */

    movq    %r9, %rdx
    movq    %r8, %rax
    shlq    $32, %rdx
    orq     %rdx, %rax
    popq    %rbx
    ret
