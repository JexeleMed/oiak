section .text
    global f_asm
    global g_asm
    global get_fpu_cw
    global set_fpu_cw

; ---------------------------------------------------------
; Funkcja: f_asm (Niestabilna)
; Równanie: f(x) = sqrt(x^2 + 1) - 1
; ---------------------------------------------------------
f_asm:
    sub rsp, 8
    movsd [rsp], xmm0
    fld qword [rsp]
    fmul st0, st0
    fld1
    faddp st1, st0
    fsqrt
    fld1
    fsubp st1, st0
    fstp qword [rsp]
    movsd xmm0, [rsp]
    add rsp, 8
    ret

; ---------------------------------------------------------
; Funkcja: g_asm (Stabilna)
; Równanie: g(x) = x^2 / (sqrt(x^2 + 1) + 1)
; ---------------------------------------------------------
g_asm:
    sub rsp, 8
    movsd [rsp], xmm0
    fld qword [rsp]
    fmul st0, st0
    fld st0
    fld1
    faddp st1, st0
    fsqrt
    fld1
    faddp st1, st0
    fdivp st1, st0
    fstp qword [rsp]
    movsd xmm0, [rsp]
    add rsp, 8
    ret

; ---------------------------------------------------------
; Funkcja: get_fpu_cw
; Zwraca do C aktualny 16-bitowy Control Word FPU
; ---------------------------------------------------------
get_fpu_cw:
    sub rsp, 8
    fstcw [rsp]
    movzx rax, word [rsp]
    add rsp, 8
    ret

; ---------------------------------------------------------
; Funkcja: set_fpu_cw
; Ustawia FPU Control Word na podstawie argumentu z C
; ---------------------------------------------------------
set_fpu_cw:
    sub rsp, 8
    mov [rsp], di       ; Argument wchodzi przez rejestr DI (ABI)
    fldcw [rsp]
    add rsp, 8
    ret
