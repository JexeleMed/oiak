section .data
    msg db 'Hello World!', 10      ; Our string with a newline at the end
    msg_len equ $ - msg            ; Length of the string

section .text
    global _start

_start:
    ; --- 1. PRINT ORIGINAL STRING ---
    mov rax, 1                     ; sys_write
    mov rdi, 1                     ; stdout
    mov rsi, msg                   ; pointer to string
    mov rdx, msg_len               ; length
    syscall

    ; --- 2. SETUP FOR ITERATION ---
    mov rbx, msg                   ; RBX = pointer to the current character
    mov rcx, msg_len               ; RCX = loop counter (number of characters to check)

.to_upper_loop:
    ; Check if we have processed all characters
    cmp rcx, 0
    je .print_modified             ; If RCX == 0, jump to printing the result (Jump if Equal)

    ; Load the current character into AL
    mov al, byte [rbx]

    ; --- 3. CHECK IF LOWERCASE ('a' <= AL <= 'z') ---
    cmp al, 'a'                    ; Compare current char with 'a' (ASCII 97)
    jl .next_char                  ; If less than 'a', it's not lowercase. Skip to next char.
    
    cmp al, 'z'                    ; Compare current char with 'z' (ASCII 122)
    jg .next_char                  ; If greater than 'z', it's not lowercase. Skip.

    ; --- 4. CONVERT TO UPPERCASE ---
    ; If we didn't jump, the character IS lowercase.
    sub al, 32                     ; Subtract 32 to get the uppercase ASCII value
    mov byte [rbx], al             ; Store the modified character back into memory

.next_char:
    ; --- 5. UPDATE POINTER AND COUNTER ---
    inc rbx                        ; Move pointer to the next character in memory
    dec rcx                        ; Decrement the loop counter
    jmp .to_upper_loop             ; Unconditional jump back to the start of the loop

.print_modified:
    ; --- 6. PRINT MODIFIED STRING ---
    mov rax, 1                     ; sys_write
    mov rdi, 1                     ; stdout
    mov rsi, msg                   ; pointer to our now-modified string
    mov rdx, msg_len               ; length
    syscall

    ; --- 7. EXIT CLEANLY ---
    mov rax, 60                    ; sys_exit
    mov rdi, 0                     ; success code
    syscall
