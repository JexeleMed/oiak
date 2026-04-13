section .data
    ; Definiujemy pierwszą liczbę 128-bitową (maksymalna możliwa wartość samych jedynek)
    ; Dzielimy ją na dwie części po 64 bity (dq - define quadword)
    num1_low  dq 0xFFFFFFFFFFFFFFFF
    num1_high dq 0xFFFFFFFFFFFFFFFF

    ; Definiujemy drugą liczbę 128-bitową (wartość 1)
    num2_low  dq 0x0000000000000001
    num2_high dq 0x0000000000000000

section .bss
    ; Rezerwujemy 133 bajty na wynik (132 znaki '0'/'1' + 1 znak nowej linii)
    buffer resb 133

section .text
    global _start

_start:
    ; --- KROK 1: DODAWANIE ---
    
    ; Wczytujemy i dodajemy MŁODSZE 64 bity
    mov rax, [num1_low]
    add rax, [num2_low]    ; Zwykłe dodawanie, flaga CF (Carry Flag) zostanie ustawiona jeśli wystąpi przeniesienie
    
    ; Wczytujemy i dodajemy STARSZE 64 bity
    mov rbx, [num1_high]
    adc rbx, [num2_high]   ; ADC dodaje rbx + num2_high + flaga CF z poprzedniego kroku
    
    ; Zapisujemy ewentualny 129-ty bit (przeniesienie z najstarszej części) do rejestru RCX
    mov rcx, 0
    adc rcx, 0             ; Dodaje 0 + 0 + flaga CF. Teraz RCX ma wartość 0 lub 1.

    ; W tym momencie nasz 129-bitowy wynik jest w rejestrach:
    ; RCX (1 bit najwyższy), RBX (kolejne 64 bity), RAX (najmłodsze 64 bity)


    ; --- KROK 2: KONWERSJA NA ZNAKI BINARNE (ASCII) ---
    
    mov rdi, buffer        ; Wskaźnik na początek naszego bufora, gdzie będziemy wpisywać zera i jedynki

    ; PĘTLA 1: Przetwarzamy najstarsze 4 bity (żeby uzyskać 132 bity wyniku, o których mówił kolega)
    ; Bit 3, 2 i 1 będą zerami. Bit 0 to nasz potencjalny carry z RCX.
    mov r8, 3              ; Zaczynamy od bitu nr 3 w dół do 0
.loop_rcx:
    bt rcx, r8             ; bt (Bit Test) sprawdza bit na pozycji r8 i kopiuje go do flagi CF
    setc dl                ; Ustawia dl na 1 jeśli CF=1, w przeciwnym razie na 0
    add dl, '0'            ; Zamienia liczbę 0/1 na znak ASCII '0' lub '1'
    mov [rdi], dl          ; Wrzuca znak do bufora
    inc rdi                ; Przesuwa wskaźnik bufora o 1 bajt
    dec r8                 ; Zmniejsza licznik pętli
    jns .loop_rcx          ; Skacz, jeśli znak (Sign Flag) nie jest ustawiony (czyli dopóki r8 >= 0)

    ; PĘTLA 2: Przetwarzamy starsze 64 bity (rejestr RBX)
    mov r8, 63
.loop_rbx:
    bt rbx, r8
    setc dl
    add dl, '0'
    mov [rdi], dl
    inc rdi
    dec r8
    jns .loop_rbx

    ; PĘTLA 3: Przetwarzamy młodsze 64 bity (rejestr RAX)
    mov r8, 63
.loop_rax:
    bt rax, r8
    setc dl
    add dl, '0'
    mov [rdi], dl
    inc rdi
    dec r8
    jns .loop_rax

    ; --- KROK 3: WYPISANIE NA EKRAN ---
    
    mov byte [rdi], 10     ; Dodajemy znak nowej linii (kod ASCII 10) na sam koniec bufora

    mov rax, 1             ; Numer syscalla dla sys_write (1)
    mov rdi, 1             ; Deskryptor pliku 1 (stdout - ekran)
    mov rsi, buffer        ; Co wypisać (adres bufora)
    mov rdx, 133           ; Ile bajtów wypisać (132 znaki + 1 nowa linia)
    syscall                ; Wywołanie funkcji systemowej Linuxa

    ; --- KROK 4: ZAKOŃCZENIE PROGRAMU ---
    
    mov rax, 60            ; Numer syscalla dla sys_exit (60)
    xor rdi, rdi           ; Kod błędu 0 (xor rejestru na samym sobie to najszybszy sposób na wyzerowanie go)
    syscall
