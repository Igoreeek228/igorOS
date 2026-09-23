; kernel/include/isr.asm
;
; Заглушки обработчиков исключений CPU (векторы 0-31).
; Стек на входе в общий обработчик:
;   [rsp]    = номер вектора        (протолкнут заглушкой)
;   [rsp+8]  = код ошибки            (от CPU или 0)
;   [rsp+16] = RIP прерванного кода  (автоматически от CPU)

section .text

%macro ISR_NOERR 1
isr%1:
    push qword 0          ; код ошибки отсутствует
    push qword %1         ; вектор
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr%1:
    push qword %1         ; вектор (код ошибки уже лежит от CPU)
    jmp isr_common
%endmacro

ISR_NOERR 0               ; #DE Division Error
ISR_NOERR 1               ; #DB Debug
ISR_NOERR 2               ; NMI
ISR_NOERR 3               ; #BP Breakpoint
ISR_NOERR 4               ; #OF Overflow
ISR_NOERR 5               ; #BR Bound Range
ISR_NOERR 6               ; #UD Invalid Opcode
ISR_NOERR 7               ; #NM Device Not Available
ISR_ERR   8               ; #DF Double Fault
ISR_NOERR 9               ; Coprocessor Segment Overrun
ISR_ERR   10              ; #TS Invalid TSS
ISR_ERR   11              ; #NP Segment Not Present
ISR_ERR   12              ; #SS Stack-Segment Fault
ISR_ERR   13              ; #GP General Protection
ISR_ERR   14              ; #PF Page Fault
ISR_NOERR 15              ; Reserved
ISR_NOERR 16              ; #MF x87 FP
ISR_ERR   17              ; #AC Alignment Check
ISR_NOERR 18              ; #MC Machine Check
ISR_NOERR 19              ; #XM SIMD FP
ISR_NOERR 20              ; #VE Virtualization
ISR_ERR   21              ; #CP Control Protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28              ; #HV Hypervisor
ISR_ERR   29              ; #VC VMM Communication
ISR_ERR   30              ; #SX Security
ISR_NOERR 31

extern isr_panic_handler

isr_common:
    ; System V AMD64: rdi, rsi, rdx - первые три аргумента.
    mov rdi, [rsp]        ; вектор
    mov rsi, [rsp + 8]    ; код ошибки
    mov rdx, [rsp + 16]   ; RIP прерванного кода
    call isr_panic_handler

.hang:
    cli
    hlt
    jmp .hang

; Таблица адресов заглушек для установки в IDT из C.
section .data
align 8
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dq isr %+ i
%assign i i+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
