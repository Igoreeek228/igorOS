; kernel/isr_stubs.asm
;
; IgorOS Nord: заглушки прерываний для CPU-исключений (векторы 0-31).
;
; В исходном IgorOS не было НИ ОДНОГО обработчика исключений -- idt_init()
; только ремапил PIC и включал прерывания, но isr_handler для vector 0-31
; никогда не регистрировался. Любой fault (деление на 0, page fault,
; general protection fault и т.д.) уходил в "default" запись IDT (которая
; тоже не была настроена), что на реальном железе/QEMU приводит к triple
; fault -- машина просто мгновенно перезагружается без единой диагностики.
;
; Здесь делается ровно то, чего не хватало: 32 отдельные точки входа,
; которые сохраняют регистры, номер вектора (и код ошибки, если CPU его
; кладёт сам), и зовут общий C-обработчик isr_common_handler(), который
; рисует экран смерти (см. kernel/panic.c).

section .text
bits 64

extern isr_common_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push qword 0        ; фиктивный код ошибки, чтобы кадр стека был единым
    push qword %1        ; номер вектора
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push qword %1        ; код ошибки уже положен CPU-ом, вектор добавляем сверху
    jmp isr_common_stub
%endmacro

; Векторы с кодом ошибки от CPU: 8, 10-14, 17, 21, 29, 30.
; Остальные из диапазона 0-31 -- без кода ошибки.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

isr_common_stub:
    ; Сохраняем регистры общего назначения перед вызовом C-кода.
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; передаём указатель на структуру регистров в C
    call isr_common_handler

    ; Экран смерти не возвращается (halt внутри), но на всякий случай
    ; аккуратно восстанавливаем стек, если C-часть всё же вернула
    ; управление (не должно происходить в нормальном режиме).
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16          ; убираем error_code + vector number
    iretq


; ============================================================
; IRQ stubs (векторы 32-47, IRQ0-15 после ремапа PIC)
;
; IgorOS Nord: полноценная interrupt-driven обработка железных
; прерываний. В исходном IgorOS PIC ремапился (kernel/pic.c), но
; НИ ОДНОГО обработчика IRQ не было зарегистрировано вообще -- ни
; таймера, ни клавиатуры (она читалась поллингом), ни тем более мыши.
; Реальный PS/2-драйвер мыши (см. drivers/system/mouse.c, портирован
; из OriginOS kernel/mouse.c) требует именно этого: пакет должен
; читаться в момент прерывания IRQ12, а не раз в кадр из главного
; цикла -- это и есть настоящая причина "дёрганого" курсора, которую
; двойной poll_mouse() за кадр (более ранний фикс) мог только
; смягчить, но не убрать полностью.
; ============================================================

extern irq_common_handler

%macro IRQ 2
global irq%1
irq%1:
    push qword 0
    push qword %2
    jmp irq_common_stub
%endmacro

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

irq_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call irq_common_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq
