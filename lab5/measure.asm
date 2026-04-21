extern printf           ; Import printf from C standard library
global main             ; Export main so the C runtime can start here

section .data
    ; Format string for printf. 10 is the newline character (\n), 0 is the null-terminator.
    fmt db "Zmierzone cykle (roznica): %llu", 10, 0

section .text
main:
    ; Prologue: Align stack to 16 bytes. 
    ; Pushing a 64-bit register subtracts 8 from RSP.
    push r12            ; We also use R12 to safely store the start time (callee-saved)

    ; --- FIRST MEASUREMENT ---
    rdtsc               ; Read TSC into EDX:EAX
    shl rdx, 32         ; Shift EDX left to upper half
    or rax, rdx         ; Combine into RAX
    mov r12, rax        ; Store the start time in R12

    ; --- DUMMY WORKLOAD ---
    ; We run a simple loop to pass some time (cycles)
    mov rcx, 100000000  ; Loop counter
.work:
    dec rcx             ; Decrement counter
    jnz .work           ; Jump if not zero back to .work

    ; --- SECOND MEASUREMENT ---
    rdtsc               ; Read TSC again
    shl rdx, 32
    or rax, rdx         ; End time is now in RAX

    ; --- CALCULATE DIFFERENCE ---
    sub rax, r12        ; Subtract start time (R12) from end time (RAX). Result in RAX.

    ; --- CALL PRINTF ---
    ; System V ABI arguments: RDI (arg1), RSI (arg2)
    mov rdi, fmt        ; Argument 1: Pointer to the format string
    mov rsi, rax        ; Argument 2: The calculated difference
    xor rax, rax        ; Clear RAX (Required for printf to specify 0 floating-point arguments)
    
    call printf         ; Call the C standard library function

    ; Epilogue
    xor rax, rax        ; Return 0 from main
    pop r12             ; Restore R12 and align stack back
    ret                 ; Exit program
