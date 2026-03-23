section .data
    ; Our target string with a newline character (10) at the end.
    my_string db 'ReverseMe', 10
    
    ; Calculate total length of string including newline for printing
    str_len equ $ - my_string
    
    ; Calculate the index of the last character BEFORE the newline
    ; We don't want to swap the newline character (\n) to the front!
    last_char_idx equ str_len - 2 

section .text
    global _start

_start:
    ; --- 1. SET UP POINTERS FOR REVERSAL ---
    mov r8, my_string              ; R8 = pointer to the start of the string
    
    mov r9, my_string              ; R9 = pointer to the start of the string
    add r9, last_char_idx          ; R9 = pointer to the last printable character

.reverse_loop:
    ; --- 2. CHECK LOOP CONDITION ---
    cmp r8, r9                     ; Compare start pointer with end pointer
    jge .print_result              ; If R8 >= R9, we crossed the middle. Jump to end.

    ; --- 3. THE SWAP (Memory-to-Register, Register-to-Memory) ---
    ; CPU limitation: Cannot move memory to memory. 
    ; We use AL and BL (8-bit lower parts of RAX and RBX) as temporary buffers.
    mov al, byte [r8]              ; Load byte at [R8] into AL
    mov bl, byte [r9]              ; Load byte at [R9] into BL
    
    mov byte [r8], bl              ; Store BL into memory at [R8]
    mov byte [r9], al              ; Store AL into memory at [R9]

    ; --- 4. UPDATE POINTERS ---
    inc r8                         ; Move start pointer forward (R8++)
    dec r9                         ; Move end pointer backward (R9--)
    
    ; --- 5. REPEAT ---
    jmp .reverse_loop              ; Unconditional jump back to the start of the loop

.print_result:
    ; --- 6. PRINT THE REVERSED STRING ---
    mov rax, 1                     ; Syscall number for sys_write
    mov rdi, 1                     ; File descriptor 1 (stdout)
    mov rsi, my_string             ; Pointer to our string (now reversed in memory)
    mov rdx, str_len               ; Length of the string
    syscall                        ; Trigger interrupt to the Linux kernel

    ; --- 7. EXIT CLEANLY ---
    mov rax, 60                    ; Syscall number for sys_exit
    mov rdi, 0                     ; Exit code 0 (success)
    syscall                        ; Trigger interrupt to the Linux kernel
