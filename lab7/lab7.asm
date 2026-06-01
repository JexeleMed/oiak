; lab7.asm — Funkcje niskopoziomowe do pomiaru struktury L1D cache
; NASM x86-64, Linux System V AMD64 ABI
;
; Eksportowane funkcje:
;   uint64_t rdtsc_begin(void)
;   uint64_t rdtsc_end(void)
;   void     clflush_range(void *p, size_t n)
;   void     read_stride(const char *buf, size_t n, size_t stride)
;   uint64_t probe_set(const char *base, size_t stride, size_t k, size_t reps)

bits    64
section .text

global  rdtsc_begin
global  rdtsc_end
global  clflush_range
global  read_stride
global  pointer_chase

; -----------------------------------------------------------------------
; uint64_t rdtsc_begin(void)
;
; Pełna serializacja potoku (CPUID), potem odczyt licznika TSC.
; Zapewnia, że żadna wcześniejsza instrukcja nie "wyjdzie" za punkt pomiaru.
; Zwraca: RAX = 64-bitowa wartość TSC
; -----------------------------------------------------------------------
rdtsc_begin:
    push    rbx             ; CPUID nadpisuje RBX (callee-saved)
    xor     eax, eax        ; leaf 0
    cpuid                   ; serializacja; nadpisuje RAX/RBX/RCX/RDX
    rdtsc                   ; EDX:EAX = TSC
    shl     rdx, 32
    or      rax, rdx        ; RAX = 64-bit TSC
    pop     rbx
    ret

; -----------------------------------------------------------------------
; uint64_t rdtsc_end(void)
;
; RDTSCP czeka na ukończenie wszystkich poprzednich ładowań pamięci,
; następnie CPUID opróżnia potok (żadna późniejsza instrukcja nie "cofnie się").
; Zwraca: RAX = 64-bitowa wartość TSC
; -----------------------------------------------------------------------
rdtsc_end:
    rdtscp                  ; EDX:EAX = TSC; serializacja na ładowaniach
    shl     rdx, 32
    or      rax, rdx        ; RAX = 64-bit TSC
    push    rax             ; zachowaj wynik — CPUID go nadpisze
    push    rbx
    xor     eax, eax
    cpuid                   ; opróżnij potok (żadna późn. instr. nie "cofnie się")
    pop     rbx
    pop     rax             ; przywróć TSC
    ret

; -----------------------------------------------------------------------
; void clflush_range(void *p, size_t n)
;   rdi = p  — adres początku zakresu
;   rsi = n  — rozmiar w bajtach (zaokrąglony do wielokrotności 64)
;
; Unieważnia linie cache w zakresie [p, p+n). Używane do wymuszenia
; "zimnego" startu w eksperymencie 1.
; -----------------------------------------------------------------------
clflush_range:
    xor     rax, rax
.cfl_loop:
    clflush [rdi + rax]     ; wyrzuca linię z L1/L2/L3
    add     rax, 64
    cmp     rax, rsi
    jb      .cfl_loop
    mfence                  ; poczekaj na ukończenie wszystkich clflush
    ret

; -----------------------------------------------------------------------
; void read_stride(const char *buf, size_t n, size_t stride)
;   rdi = buf    — adres bazowy bufora
;   rsi = n      — rozmiar bufora [B]
;   rdx = stride — krok w bajtach
;
; Odczytuje kolejne bajty: buf[0], buf[stride], buf[2*stride], ...
; Generuje dokładnie (n/stride) dostępów do pamięci.
; -----------------------------------------------------------------------
read_stride:
    xor     rcx, rcx        ; offset = 0
.rs_loop:
    movzx   eax, byte [rdi + rcx]   ; odczyt — generuje potencjalny miss
    add     rcx, rdx        ; offset += stride
    cmp     rcx, rsi        ; offset < n?
    jb      .rs_loop
    ret

; -----------------------------------------------------------------------
; uint64_t pointer_chase(void *start, size_t steps)
;   rdi = start  — adres pierwszego wezla lancucha wskaznikow
;   rsi = steps  — liczba dereferencji do wykonania
;
; Algorytm: rax = *rax   (powtarzane 'steps' razy)
;
; Dlaczego pointer chasing, a nie petla z imul?
;   Kazda instrukcja `mov rax, [rax]` ZALEZY od wyniku poprzedniej.
;   Procesor NIE moze ich nakladac (brak Memory Level Parallelism).
;   Dlatego czas pomiaru rowna sie SUMIE latencji kolejnych dostepu.
;
;   k <= asocjatywnosc N (k adresow miesci sie w zestawie):
;     po rozgrzaniu lancucha -> trafienia L1 -> ~4 cykli/krok
;
;   k > asocjatywnosc N (thrashing: kazdy dostep wypiera poprzedni):
;     kazde `mov rax, [rax]` to chybienie -> ~40+ cykli/krok
;
; Wymaganie: bufor musi zawierac okragly lancuch wskaznikow ustawiony
; przez build_chain() w lab7.c PRZED wywolaniem tej funkcji:
;   buf[0*S] -> buf[1*S] -> ... -> buf[(k-1)*S] -> buf[0*S]
;
; Zwraca: RAX = laczna liczba cykli TSC
; Zachowane rejestry (callee-saved): RBX, R12, R13
; -----------------------------------------------------------------------
pointer_chase:
    push    r12
    push    r13
    push    rbx

    mov     r12, rdi        ; r12 = start address
    mov     r13, rsi        ; r13 = steps

    ; ---- Odczyt czasu startowego ----
    xor     eax, eax
    cpuid                   ; pelna serializacja; nadpisuje RAX/RBX/RCX/RDX
    rdtsc                   ; EDX:EAX = t_start
    shl     rdx, 32
    or      rax, rdx
    mov     rbx, rax        ; RBX = t_start

    ; ---- Petla pointer chasing ----
    mov     rax, r12        ; rax = biezacy wskaznik
    mov     rcx, r13        ; rcx = pozostale kroki
.pc_loop:
    mov     rax, [rax]      ; rax = *rax  (dereferencja -- KLUCZOWY dostep)
    dec     rcx
    jnz     .pc_loop

    ; ---- Odczyt czasu koncowego ----
    ; Po petli: rax = ostatni wskaznik (juz niepotrzebny)
    ; rdtscp nadpisze rax wartoscia TSC -- to jest zamierzone.
    rdtscp                  ; EDX:EAX = t_end; serializuje ladowania
    shl     rdx, 32
    or      rax, rdx        ; RAX = t_end
    sub     rax, rbx        ; RAX = elapsed = t_end - t_start

    pop     rbx
    pop     r13
    pop     r12
    ret
