global get_tsc

section .text

; Function signature in C: 
; unsigned long long get_tsc(char mode);
get_tsc:
    ; The 'mode' argument (char) is passed in the DIL register (lowest 8 bits of RDI)
    
    cmp dil, '1'        ; Compare argument with character '1'
    je .use_rdtsc       ; If equal, jump to the standard rdtsc block
    
    cmp dil, '2'        ; Compare argument with character '2'
    je .use_rdtscp      ; If equal, jump to the rdtscp block
    
    ; Default case (if neither '1' nor '2')
    xor rax, rax        ; Return 0
    ret

.use_rdtsc:
    rdtsc               ; Reads Time Stamp Counter into EDX:EAX
    jmp .combine        ; Jump to combine EDX and EAX

.use_rdtscp:
    rdtscp              ; Reads TSC into EDX:EAX and TSC_AUX into ECX (serializes)
    jmp .combine

.combine:
    ; rdtsc/rdtscp stores the lower 32 bits in EAX and the upper 32 bits in EDX.
    ; We need to return a 64-bit value in RAX.
    
    shl rdx, 32         ; Shift the upper 32 bits from RDX to the left by 32 positions
    or rax, rdx         ; Combine RDX (upper 32 bits) and RAX (lower 32 bits)
    
    ret                 ; Return to C (Result is in RAX)
