; =============================================================================
; simd.asm -- czesc asemblerowa zadania (NASM, x86-64, Linux / System V ABI)
;
; Konwencja wywolan System V (wazne, zeby rozumiec interfejs C <-> asm):
;   - argumenty calkowite/wskazniki: rdi, rsi, rdx, rcx, r8, r9
;   - argumenty zmiennoprzecinkowe:  xmm0, xmm1, ...
;   - wynik calkowity w rax, wynik double w xmm0
;   - rejestry rbx, rbp, r12-r15 sa "callee-saved" -- jak ich uzywamy,
;     musimy je odlozyc i przywrocic (dotyczy nas przy CPUID, bo niszczy rbx)
; =============================================================================

default rel             ; adresowanie danych wzgledem RIP (wymagane przez PIE)

global tsc_start
global tsc_stop
global simd_geo_sum
global simd_exp_sum

section .rodata
align 32
; Stale dla wektorowego exp. Kazda powielona x4, zeby dalo sie ja brac
; bezposrednio jako 256-bitowy operand pamieciowy w instrukcjach ymm.
LANE_IDX: dq 0.0, 1.0, 2.0, 3.0          ; przesuniecia 4 "linii" wektora
LOG2E:    times 4 dq 1.4426950408889634  ; log2(e)
LN2:      times 4 dq 0.6931471805599453  ; ln(2)
BIAS:     times 4 dq 0x3FF0000000000000  ; 1023 << 52 (bias wykladnika double)
; wspolczynniki Taylora e^u = suma u^i / i!  (Horner od najwyzszej potegi)
C10: times 4 dq 2.7557319223985893e-7    ; 1/10!
C9:  times 4 dq 2.7557319223985888e-6    ; 1/9!
C8:  times 4 dq 2.4801587301587302e-5    ; 1/8!
C7:  times 4 dq 1.9841269841269841e-4    ; 1/7!
C6:  times 4 dq 1.3888888888888889e-3    ; 1/6!
C5:  times 4 dq 8.3333333333333333e-3    ; 1/5!
C4:  times 4 dq 4.1666666666666666e-2    ; 1/4!
C3:  times 4 dq 0.16666666666666666      ; 1/3!
C2:  times 4 dq 0.5                      ; 1/2!
C1:  times 4 dq 1.0
C0:  times 4 dq 1.0

section .text

; -----------------------------------------------------------------------------
; uint64_t tsc_start(void)
;
; Odczyt licznika cykli NA POCZATKU pomiaru.
; CPUID jest instrukcja serializujaca: procesor musi dokonczyc wszystko,
; co bylo przed nia, zanim pojdzie dalej. Dzieki temu RDTSC nie zostanie
; przestawione (out-of-order) PRZED kod, ktory dopiero chcemy zmierzyc,
; ani kod sprzed pomiaru nie "wjedzie" do srodka.
; RDTSC zwraca 64-bitowy licznik w parze EDX:EAX (gorne:dolne 32 bity).
; -----------------------------------------------------------------------------
tsc_start:
    push rbx                ; CPUID niszczy eax, ebx, ecx, edx -- rbx musimy uratowac
    xor  eax, eax           ; CPUID lisc 0 (dowolny, chodzi tylko o serializacje)
    cpuid
    rdtsc                   ; edx:eax = licznik cykli
    shl  rdx, 32
    or   rax, rdx           ; sklejamy w jeden 64-bitowy wynik w rax
    pop  rbx
    ret

; -----------------------------------------------------------------------------
; uint64_t tsc_stop(void)
;
; Odczyt licznika NA KONCU pomiaru. Tu kolejnosc jest odwrotna:
;   - RDTSCP czeka, az wszystkie wczesniejsze instrukcje sie wykonaja
;     (czyli mierzony kod na pewno juz skonczyl), dopiero wtedy czyta licznik,
;   - CPUID PO odczycie blokuje przestawienie kodu zza pomiaru przed odczyt.
; To jest klasyczny schemat z whitepapera Intela (Paoloni).
; -----------------------------------------------------------------------------
tsc_stop:
    push rbx
    rdtscp                  ; edx:eax = licznik (rdtscp nadpisuje tez ecx)
    shl  rdx, 32
    or   rax, rdx
    mov  r8, rax            ; ratujemy wynik, bo zaraz CPUID nadpisze rax/rdx
    xor  eax, eax
    cpuid                   ; bariera: nic zza pomiaru nie wskoczy przed rdtscp
    mov  rax, r8
    pop  rbx
    ret

; -----------------------------------------------------------------------------
; double simd_geo_sum(const double *init, double factor, uint64_t iters)
;          rdi = init   -- 16 poczatkowych wartosci funkcji: e^x0 ... e^x15
;          xmm0 = factor -- e^(16h), mnoznik przesuwajacy o 16 punktow w przod
;          rsi = iters  -- liczba iteracji petli (kazda przerabia 16 punktow)
;
; Sedno triku: e^(a + i*h) to ciag geometryczny, wiec NIE liczymy exp()
; w petli -- kazda nastepna wartosc dostajemy mnozac poprzednia przez stala.
;
; Trzymamy 4 niezalezne wektory wartosci (ymm0-ymm3, po 4 double kazdy = 16
; punktow) i 4 niezalezne akumulatory sum (ymm4-ymm7). Po co az 4?
; Bo v = v * q to LANCUCH ZALEZNOSCI -- kazde mnozenie czeka ~4 cykle na
; poprzednie. Cztery niezalezne lancuchy pozwalaja procesorowi wykonywac
; mnozenia rownolegle (superskalarnosc + potokowanie FPU).
;
; Zwraca surowa sume wartosci funkcji (bez mnozenia przez h -- to robi C).
; Zaklada iters >= 1 i n podzielne przez 16 (pilnuje tego strona C).
; -----------------------------------------------------------------------------
simd_geo_sum:
    vbroadcastsd ymm15, xmm0        ; ymm15 = [q, q, q, q], q = e^(16h)

    vmovupd ymm0, [rdi]             ; wartosci w punktach 0..3
    vmovupd ymm1, [rdi + 32]        ; punkty 4..7
    vmovupd ymm2, [rdi + 64]        ; punkty 8..11
    vmovupd ymm3, [rdi + 96]        ; punkty 12..15

    vxorpd  ymm4, ymm4, ymm4        ; akumulatory sum = 0
    vxorpd  ymm5, ymm5, ymm5
    vxorpd  ymm6, ymm6, ymm6
    vxorpd  ymm7, ymm7, ymm7

.petla:
    vaddpd  ymm4, ymm4, ymm0        ; dosumuj biezace 16 wartosci...
    vaddpd  ymm5, ymm5, ymm1
    vaddpd  ymm6, ymm6, ymm2
    vaddpd  ymm7, ymm7, ymm3
    vmulpd  ymm0, ymm0, ymm15       ; ...i przesun wszystkie o 16 punktow:
    vmulpd  ymm1, ymm1, ymm15       ;    v_i = v_i * e^(16h)
    vmulpd  ymm2, ymm2, ymm15
    vmulpd  ymm3, ymm3, ymm15
    dec     rsi
    jnz     .petla

    ; --- redukcja: 4 akumulatory -> 1 wektor -> 1 liczba (suma pozioma) ---
    vaddpd  ymm4, ymm4, ymm5
    vaddpd  ymm6, ymm6, ymm7
    vaddpd  ymm4, ymm4, ymm6        ; ymm4 = [s0, s1, s2, s3]
    vextractf128 xmm1, ymm4, 1      ; xmm1 = gorna polowka [s2, s3]
    vaddpd  xmm4, xmm4, xmm1        ; xmm4 = [s0+s2, s1+s3]
    vunpckhpd xmm1, xmm4, xmm4      ; xmm1 = [s1+s3, ...] (gorny double na dol)
    vaddsd  xmm4, xmm4, xmm1        ; xmm4[0] = suma calkowita
    vmovapd xmm0, xmm4              ; wynik double wraca w xmm0

    vzeroupper                      ; higiena AVX: czysci gorne polowki ymm,
                                    ; zeby nie spowalniac pozniejszego kodu SSE
    ret

; -----------------------------------------------------------------------------
; double simd_exp_sum(double x0, double h, uint64_t iters)
;          xmm0 = x0    -- pierwszy punkt (srodek pierwszego podprzedzialu)
;          xmm1 = h     -- krok miedzy punktami
;          rdi  = iters -- liczba iteracji (kazda przerabia 4 punkty)
;
; Wariant "uczciwy": e^x liczone NAPRAWDE dla kazdego punktu, tylko
; wektorowo (4 double naraz), bez triku z ciagiem geometrycznym.
; Tak mniej wiecej dziala kazda wektorowa implementacja exp (np. w libm).
;
; Uwaga z eksperymentu: rozwiniecie tej petli na 2 niezalezne strumienie
; (jak 4 akumulatory w simd_geo_sum) dalo tu tylko ~8%, bo waskim gardlem
; nie jest lancuch zaleznosci, lecz sama LICZBA operacji (~40 uopow FP na
; 4 punkty) -- procesor i tak przeplata kolejne, niezalezne iteracje petli.
;
; Algorytm dla kazdego x (redukcja zakresu + wielomian):
;   1. y = x * log2(e)            bo e^x = 2^y
;   2. k = round(y), r = y - k    rozbicie: 2^y = 2^k * 2^r, |r| <= 0.5
;   3. u = r * ln(2)              bo 2^r = e^u, |u| <= 0.35
;   4. e^u = wielomian Taylora stopnia 10 (schemat Hornera na FMA);
;      na tak malym przedziale to dokladnosc ~1e-13
;   5. 2^k budujemy BITOWO: w formacie double wykladnik to bity 52..62
;      z biasem 1023, wiec liczba (k+1023)<<52 zinterpretowana jako
;      double to dokladnie 2^k. Zadnego liczenia -- czysta manipulacja bitami.
;   6. wynik = 2^k * e^u
; -----------------------------------------------------------------------------
simd_exp_sum:
    ; --- przygotowanie stalych i stanu ---
    vbroadcastsd ymm15, [LOG2E]
    vbroadcastsd ymm14, [LN2]
    vmovapd      ymm13, [BIAS]
    vbroadcastsd ymm11, xmm0        ; [x0, x0, x0, x0]
    vbroadcastsd ymm12, xmm1        ; [h, h, h, h]
    vfmadd231pd  ymm11, ymm12, [LANE_IDX] ; x = x0 + [0,1,2,3]*h
    vaddpd       ymm12, ymm12, ymm12
    vaddpd       ymm12, ymm12, ymm12 ; krok petli = 4h
    vxorpd       ymm10, ymm10, ymm10 ; akumulator sumy = 0

.petla:
    ; --- redukcja zakresu ---
    vmulpd   ymm1, ymm11, ymm15     ; y = x * log2(e)
    vroundpd ymm2, ymm1, 0          ; k = round(y) (do najblizszej)
    vsubpd   ymm1, ymm1, ymm2       ; r = y - k,  |r| <= 0.5
    vmulpd   ymm1, ymm1, ymm14      ; u = r * ln2  (wiec e^u = 2^r)

    ; --- e^u wielomianem Taylora, schemat Hornera ---
    ; kazde vfmadd213pd robi: p = p * u + wspolczynnik
    vmovapd     ymm3, [C10]
    vfmadd213pd ymm3, ymm1, [C9]
    vfmadd213pd ymm3, ymm1, [C8]
    vfmadd213pd ymm3, ymm1, [C7]
    vfmadd213pd ymm3, ymm1, [C6]
    vfmadd213pd ymm3, ymm1, [C5]
    vfmadd213pd ymm3, ymm1, [C4]
    vfmadd213pd ymm3, ymm1, [C3]
    vfmadd213pd ymm3, ymm1, [C2]
    vfmadd213pd ymm3, ymm1, [C1]
    vfmadd213pd ymm3, ymm1, [C0]    ; ymm3 = e^u = 2^r

    ; --- 2^k przez wstawienie k w pole wykladnika double ---
    vcvtpd2dq xmm4, ymm2            ; 4 double -> 4 int32
    vpmovsxdq ymm4, xmm4            ; int32 -> int64 (z rozszerzeniem znaku)
    vpsllq    ymm4, ymm4, 52        ; k na pozycje wykladnika
    vpaddq    ymm4, ymm4, ymm13     ; + (1023<<52); te bity TO double 2^k
    vmulpd    ymm3, ymm3, ymm4      ; e^x = 2^r * 2^k

    vaddpd    ymm10, ymm10, ymm3    ; suma += 4 wartosci
    vaddpd    ymm11, ymm11, ymm12   ; x += 4h (nastepne 4 punkty)
    dec       rdi
    jnz       .petla

    ; --- suma pozioma akumulatora (jak w simd_geo_sum) ---
    vextractf128 xmm1, ymm10, 1
    vaddpd    xmm10, xmm10, xmm1
    vunpckhpd xmm1, xmm10, xmm10
    vaddsd    xmm10, xmm10, xmm1
    vmovapd   xmm0, xmm10

    vzeroupper
    ret
