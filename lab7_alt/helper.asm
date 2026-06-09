.section .text
.global perf_open
.global perf_close
.global perf_close_x

perf_open:
    pushq   %rbx
    xorl    %eax, %eax
    cpuid
    rdtsc
    shlq    $32, %rdx
    orq     %rdx, %rax
    popq    %rbx
    ret

perf_close:
    pushq   %rbx
    rdtsc
    movq    %rax, %r8
    movq    %rdx, %r9
    xorl    %eax, %eax
    cpuid

    movq    %r9, %rdx
    movq    %r8, %rax
    shlq    $32, %rdx
    orq     %rdx, %rax
    popq    %rbx
    ret

perf_close_x:
    pushq   %rbx
    rdtscp
    movq    %rax, %r8
    movq    %rdx, %r9
    xorl    %eax, %eax
    cpuid

    movq    %r9, %rdx
    movq    %r8, %rax
    shlq    $32, %rdx
    orq     %rdx, %rax
    popq    %rbx
    ret
