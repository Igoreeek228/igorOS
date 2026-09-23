#include "idt.h"
#include "pic.h"

static struct idt_entry idt[256];
static struct idtr idtr;

/*
 * IgorOS Nord: заглушки прерываний для CPU-исключений (kernel/isr_stubs.asm).
 * В исходном IgorOS ни один вектор 0-31 не регистрировался -- любой fault
 * приводил к тихой triple-fault перезагрузке без диагностики. Экран смерти
 * реализован в kernel/panic.c (isr_common_handler), вызывается из этих
 * заглушек.
 */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

/* IRQ0-15 (векторы 32-47 после ремапа PIC) -- см. kernel/isr_stubs.asm.
 * Нужны для настоящего interrupt-driven PS/2-драйвера мыши
 * (drivers/system/mouse.c), которого в исходном IgorOS не было. */
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags) {
    struct idt_entry *descriptor = &idt[vector];
    uint64_t addr = (uint64_t)isr;

    descriptor->isr_low    = addr & 0xFFFF;
    /*
     * НАСТОЯЩАЯ ПРИЧИНА, из-за которой курсор (и вообще любое аппаратное
     * прерывание) не работал: здесь было захардкожено kernel_cs = 0x08.
     *
     * IgorOS Nord грузится через протокол Limine, а не через собственный
     * GDT. Согласно официальному протоколу Limine (PROTOCOL.md, раздел
     * "Machine state at entry" -> x86-64), бутлоадер оставляет загруженным
     * СВОЙ временный GDT со следующими дескрипторами, начиная с offset 0:
     *   0: null
     *   1: 16-bit code   (селектор 0x08)
     *   2: 16-bit data   (селектор 0x10)
     *   3: 32-bit code   (селектор 0x18)
     *   4: 32-bit data   (селектор 0x20)
     *   5: 64-bit code   (селектор 0x28)  <-- нужен нам
     *   6: 64-bit data   (селектор 0x30)
     *
     * 0x08 в GDT Limine -- это 16-битный код, а не 64-битный. Пока
     * обработчик вызывается программно (sti просто выставляет флаг,
     * outb/inb никуда не прыгают) -- всё молчаливо "работает". Но как
     * только приходит первое настоящее аппаратное прерывание (у нас —
     * IRQ2 сразу после pic_clear_mask(2) в mouse_init(), которое ведёт
     * себя как первый демаскированный физический IRQ), CPU должен
     * совершить interrupt gate transfer через этот дескриптор в IDT.
     * С селектором 16-битного кода вместо 64-битного это -- General
     * Protection Fault на самом переключении сегмента, а поскольку
     * это происходит на аппаратном IRQ (а не при чистом software int),
     * дальнейшее состояние машины в QEMU/на реальном железе становится
     * недействительным -- выполнение зависает, не доходя до следующей
     * строчки C-кода после pic_clear_mask(2).
     *
     * Это и есть первопричина "курсор не двигается": PS/2-мышь и её
     * IRQ-обработчик были написаны верно, но CPU физически не мог
     * попасть в mouse_irq_handler(), потому что сама интерпретация
     * IDT-дескриптора была сломана на уровне сегмента кода.
     */
    descriptor->kernel_cs  = 0x28;
    descriptor->ist        = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid    = (addr >> 16) & 0xFFFF;
    descriptor->isr_high   = (addr >> 32) & 0xFFFFFFFF;
    descriptor->reserved   = 0;
}

static void idt_install_exception_handlers(void) {
    /* 0x8E = present, ring 0, 64-bit interrupt gate */
    void *stubs[32] = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    };
    for (int i = 0; i < 32; i++) {
        idt_set_descriptor((uint8_t)i, stubs[i], 0x8E);
    }
}

static void idt_install_irq_handlers(void) {
    void *stubs[16] = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    };
    for (int i = 0; i < 16; i++) {
        idt_set_descriptor((uint8_t)(32 + i), stubs[i], 0x8E);
    }
}

void idt_init(void) {
    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(struct idt_entry) * 256 - 1;

    idt_install_exception_handlers();
    idt_install_irq_handlers();

    pic_remap();

    asm volatile ("lidt %0" : : "m"(idtr));
    asm volatile ("sti");
}