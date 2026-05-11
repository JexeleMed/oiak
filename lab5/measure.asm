extern printf
global main

section .data
    fmt db "Zmierzone cykle (z narzutem stopera): %llu", 10, 0

section .text
main:
    ; =========================================================================
    ; 1. PROLOG I WYRÓWNANIE STOSU (ABI COMPLIANCE)
    ; =========================================================================
    ; System V ABI wymaga zachowania rejestrów callee-saved (RBX, R12-R15).
    ; Instrukcja CPUID bezwzględnie niszczy RBX, więc musimy go zabezpieczyć.
    ; Wrzucenie dokładnie 5 rejestrów (5 * 8 = 40 bajtów) idealnie wyrównuje 
    ; stos do 16 bajtów (Wejście: RSP % 16 == 8 -> po sub 40: RSP % 16 == 0).
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; =========================================================================
    ; 2. SERIALIZACJA PRZED STARTEM (CZYSZCZENIE POTOKU)
    ; =========================================================================
    xor eax, eax        ; Funkcja 0 dla CPUID
    cpuid               ; Twarda bariera: czeka, aż wszystkie wcześniejsze 
                        ; instrukcje całkowicie opuszczą potok procesora.

    ; =========================================================================
    ; 3. START POMIARU (CRITICAL PATH - FAZA WEJŚCIOWA)
    ; =========================================================================
    rdtsc               ; Odczyt licznika do EDX:EAX.
    
    ; MIKROARCHITEKTONICZNA OPTYMALIZACJA:
    ; W starym kodzie robiłeś SHL i OR wewnątrz mierzonego okna. To błąd!
    ; Traciłeś cenne cykle na mierzenie własnej logiki składania bitów.
    ; Zamiast tego robimy szybki zrzut do rejestrów 32-bitowych (1 cykl).
    ; Zapis do r12d automatycznie i darmowo zeruje górne 32 bity rejestru R12!
    mov r12d, eax       ; Zrzut dolnych 32 bitów STARTU
    mov r13d, edx       ; Zrzut górnych 32 bitów STARTU

    ; =========================================================================
    ; --- MIEJSCE NA TWÓJ MIERZONY KOD ---
    ; Aby zmierzyć czysty narzut (baseline) samego stopera, zostaw to puste!
    ; =========================================================================
    
    ; fsqrt             ; Odkomentuj, aby zmierzyć pojedynczą instrukcję

    ; =========================================================================
    ; 4. STOP POMIARU (CRITICAL PATH - FAZA WYJŚCIOWA)
    ; =========================================================================
    rdtscp              ; Instrukcja częściowo serializująca. Czeka, aż KOD 
                        ; powyżej fizycznie się zakończy. Wynik w EDX:EAX.
    mov r14d, eax       ; Zrzut dolnych 32 bitów KOŃCA
    mov r15d, edx       ; Zrzut górnych 32 bitów KOŃCA

    ; =========================================================================
    ; 5. SERIALIZACJA KOŃCOWA (BLOKADA OGONA)
    ; =========================================================================
    xor eax, eax
    cpuid               ; Gwarantuje, że instrukcje poniżej (składanie bitów,
                        ; printf) nie wejdą spekulatywnie w okno pomiarowe.

    ; =========================================================================
    ; 6. OBLICZENIA (POZA ŚCIEŻKĄ KRYTYCZNĄ)
    ; =========================================================================
    ; Teraz bezpiecznie składamy 64-bitowy START w rejestrze R12
    shl r13, 32
    or r12, r13

    ; Składamy 64-bitowy STOP w rejestrze R14
    shl r15, 32
    or r14, r15

    ; Obliczenie różnicy: R14 = STOP - START
    sub r14, r12

    ; =========================================================================
    ; 7. WYPISANIE WYNIKU I EPILOG
    ; =========================================================================
    mov rdi, fmt        ; Arg 1: wskaźnik na string
    mov rsi, r14        ; Arg 2: wynik w cyklach
    xor eax, eax        ; Czyszczenie EAX dla printf (0 funkcji wektorowych)
    call printf

    ; Przywrócenie rejestrów i powrót
    xor eax, eax        ; Kod ret = 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
