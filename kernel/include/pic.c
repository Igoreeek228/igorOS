#include "pic.h"

void pic_remap(void) {
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, mask1);
    outb(0xA1, mask2);
}

/*
 * IgorOS Nord: EOI и маски -- портировано по духу из OriginOS
 * (kernel/pic.c: pic_send_eoi/pic_set_mask/pic_clear_mask), которых
 * в IgorOS не было вообще.
 */

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_set_mask(uint8_t irq_line) {
    uint16_t port = irq_line < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq_line < 8 ? irq_line : (uint8_t)(irq_line - 8);
    uint8_t value = (uint8_t)(inb(port) | (1 << bit));
    outb(port, value);
}

void pic_clear_mask(uint8_t irq_line) {
    uint16_t port = irq_line < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = irq_line < 8 ? irq_line : (uint8_t)(irq_line - 8);
    uint8_t value = (uint8_t)(inb(port) & ~(1 << bit));
    outb(port, value);
}

static irq_handler_t irq_routines[16];

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = handler;
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) irq_routines[irq] = 0;
}

/*
 * irq_common_handler
 *
 * Вызывается из kernel/isr_stubs.asm при срабатывании IRQ0-15.
 * Стек уложен так же, как для isr_common_handler: 15 push'ов, затем
 * [vector][error_code(=0, фиктивный)]. vector здесь -- вектор IDT
 * после ремапа PIC (32-47), из которого вычитаем 32, чтобы получить
 * номер линии IRQ (0-15) для поиска в irq_routines[].
 */
void irq_common_handler(uint64_t *stack_ptr) {
    uint64_t vector = stack_ptr[15]; /* 32-47 -- вектор IDT после ремапа */
    int irq_line = (int)(vector - 32);

    if (irq_line >= 0 && irq_line < 16 && irq_routines[irq_line]) {
        irq_routines[irq_line]();
    }

    pic_send_eoi((uint8_t)irq_line);
}