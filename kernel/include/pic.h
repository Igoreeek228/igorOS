#ifndef PIC_H
#define PIC_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pic_remap(void);

/*
 * IgorOS Nord: EOI и управление масками PIC -- портировано по духу из
 * OriginOS (kernel/pic.c). Раньше в IgorOS этого не было вообще, потому
 * что не было ни одного зарегистрированного обработчика IRQ, которому
 * это могло бы понадобиться.
 */
void pic_send_eoi(uint8_t irq);
void pic_set_mask(uint8_t irq_line);
void pic_clear_mask(uint8_t irq_line);

/* Диспетчер IRQ (kernel/isr_stubs.asm -> irq_common_handler ->
 * зарегистрированный обработчик). Один обработчик на линию 0-15. */
typedef void (*irq_handler_t)(void);
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);

#endif