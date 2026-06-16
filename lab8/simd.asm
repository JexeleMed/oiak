
default rel

global tsc_start
global tsc_stop
global simd_geo_sum
global simd_exp_sum

; =============================================================================
section .rodata
align 32
LANE_IDX: dq 0.0, 1.0, 2.0, 3.0
LOG2E:    times 4 dq 1.4426950408889634
LN2:      times 4 dq 0.6931471805599453
BIAS:     times 4 dq 0x3FF0000000000000  ; 1023 << 52 (bias wykladnika double)
; wspolczynniki 1/i! dla Taylora e^u, schemat Hornera
C10: times 4 dq 2.7557319223985893e-7
C9:  times 4 dq 2.7557319223985888e-6
C8:  times 4 dq 2.4801587301587302e-5
C7:  times 4 dq 1.9841269841269841e-4
C6:  times 4 dq 1.3888888888888889e-3
C5:  times 4 dq 8.3333333333333333e-3
C4:  times 4 dq 4.1666666666666666e-2
C3:  times 4 dq 0.16666666666666666
C2:  times 4 dq 0.5
C1:  times 4 dq 1.0
C0:  times 4 dq 1.0

; =============================================================================
section .text

; uint64_t tsc_start(void)
tsc_start:
    push rbx               
    xor  eax, eax
    cpuid
    rdtsc
    shl  rdx, 32
    or   rax, rdx
    pop  rbx
    ret

; uint64_t tsc_stop(void)
tsc_stop:
    push rbx
    rdtscp
    shl  rdx, 32
    or   rax, rdx
    mov  r8, rax
    xor  eax, eax
    cpuid
    mov  rax, r8
    pop  rbx
    ret

; double simd_geo_sum(const double *init, double factor, uint64_t iters)
;   rdi = init, xmm0 = factor (e^16h), rsi = iters
; e^(x+16h) = e^x * e^(16h) -- zamiast exp() w petli same mnozenia
; 4 niezalezne akumulatory zeby zerwac lancuch zaleznosci i wypelnic potok FPU
simd_geo_sum:
    vbroadcastsd ymm15, xmm0

    vmovupd ymm0, [rdi]
    vmovupd ymm1, [rdi + 32]
    vmovupd ymm2, [rdi + 64]
    vmovupd ymm3, [rdi + 96]

    vxorpd  ymm4, ymm4, ymm4
    vxorpd  ymm5, ymm5, ymm5
    vxorpd  ymm6, ymm6, ymm6
    vxorpd  ymm7, ymm7, ymm7

.petla:
    vaddpd  ymm4, ymm4, ymm0
    vaddpd  ymm5, ymm5, ymm1
    vaddpd  ymm6, ymm6, ymm2
    vaddpd  ymm7, ymm7, ymm3
    vmulpd  ymm0, ymm0, ymm15
    vmulpd  ymm1, ymm1, ymm15
    vmulpd  ymm2, ymm2, ymm15
    vmulpd  ymm3, ymm3, ymm15
    dec     rsi
    jnz     .petla

    vaddpd  ymm4, ymm4, ymm5
    vaddpd  ymm6, ymm6, ymm7
    vaddpd  ymm4, ymm4, ymm6
    vextractf128 xmm1, ymm4, 1
    vaddpd  xmm4, xmm4, xmm1
    vunpckhpd xmm1, xmm4, xmm4
    vaddsd  xmm4, xmm4, xmm1
    vmovapd xmm0, xmm4

    vzeroupper                      
    ret

; double simd_exp_sum(double x0, double h, uint64_t iters)
;   xmm0 = x0, xmm1 = h, rdi = iters
; redukcja zakresu: e^x = 2^k * e^u, gdzie |u| <= 0.35 -- Taylor zbiega szybko
simd_exp_sum:
    vbroadcastsd ymm15, [LOG2E]
    vbroadcastsd ymm14, [LN2]
    vmovapd      ymm13, [BIAS]
    vbroadcastsd ymm11, xmm0
    vbroadcastsd ymm12, xmm1
    vfmadd231pd  ymm11, ymm12, [LANE_IDX]   ; x = x0 + [0,1,2,3]*h
    vaddpd       ymm12, ymm12, ymm12
    vaddpd       ymm12, ymm12, ymm12         ; krok petli = 4h
    vxorpd       ymm10, ymm10, ymm10

.petla:
    vmulpd   ymm1, ymm11, ymm15     ; y = x * log2e
    vroundpd ymm2, ymm1, 0          ; k = round(y)
    vsubpd   ymm1, ymm1, ymm2       ; r = y - k
    vmulpd   ymm1, ymm1, ymm14      ; u = r * ln2

    ; Horner stopnia 10: e^u
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
    vfmadd213pd ymm3, ymm1, [C0]

    vcvtpd2dq xmm4, ymm2
    vpmovsxdq ymm4, xmm4
    vpsllq    ymm4, ymm4, 52
    vpaddq    ymm4, ymm4, ymm13
    vmulpd    ymm3, ymm3, ymm4      ; e^x = e^u * 2^k

    vaddpd    ymm10, ymm10, ymm3
    vaddpd    ymm11, ymm11, ymm12
    dec       rdi
    jnz       .petla

    vextractf128 xmm1, ymm10, 1
    vaddpd    xmm10, xmm10, xmm1
    vunpckhpd xmm1, xmm10, xmm10
    vaddsd    xmm10, xmm10, xmm1
    vmovapd   xmm0, xmm10

    vzeroupper
    ret
