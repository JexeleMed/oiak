section .text
    global f_asm
    global g_asm

; ---------------------------------------------------------
; Funkcja: f_asm
; Równanie: f(x) = sqrt(x^2 + 1) - 1
; Wejście C: xmm0 (rejestr 64-bit float)
; Wyjście C: xmm0
; ---------------------------------------------------------
f_asm:
    sub rsp, 8          ; Miejsce na stosie
    movsd [rsp], xmm0   ; xmm0 -> RAM

    fld qword [rsp]     ; ST(0) = x (RAM -> FPU)
    fmul st0, st0       ; ST(0) = x^2
    fld1                ; ST(0) = 1.0, ST(1) = x^2
    faddp st1, st0      ; ST(0) = x^2 + 1
    fsqrt               ; ST(0) = sqrt(x^2 + 1)
    fld1                ; ST(0) = 1.0, ST(1) = sqrt(x^2 + 1)
    fsubp st1, st0      ; ST(0) = sqrt(x^2 + 1) - 1.0

    fstp qword [rsp]    ; FPU -> RAM
    movsd xmm0, [rsp]   ; RAM -> xmm0
    add rsp, 8          ; Czyszczenie stosu
    ret

; ---------------------------------------------------------
; Funkcja: g_asm
; Równanie: g(x) = x^2 / (sqrt(x^2 + 1) + 1)
; Wejście C: xmm0
; Wyjście C: xmm0
; ---------------------------------------------------------
g_asm:
    sub rsp, 8
    movsd [rsp], xmm0

    fld qword [rsp]     ; ST(0) = x
    fmul st0, st0       ; ST(0) = x^2
    
    ; Kopia x^2 na stos
    fld st0             ; ST(0) = x^2, ST(1) = x^2
    
    fld1                ; ST(0) = 1.0, ST(1) = x^2, ST(2) = x^2
    faddp st1, st0      ; ST(0) = x^2 + 1.0, ST(1) = x^2
    fsqrt               ; ST(0) = sqrt(x^2 + 1.0), ST(1) = x^2
    fld1                ; ST(0) = 1.0, ST(1) = sqrt(x^2 + 1.0), ST(2) = x^2
    faddp st1, st0      ; ST(0) = sqrt(x^2 + 1.0) + 1.0, ST(1) = x^2

    ; ST(0) = mianownik = sqrt(x^2 + 1) + 1
    ; ST(1) = licznik   = x^2
    fdivp st1, st0      ; ST(0) = ST(1) / ST(0) -> x^2 / (sqrt(x^2 + 1) + 1)

    fstp qword [rsp]    ; Zapis
    movsd xmm0, [rsp]
    add rsp, 8
    ret
