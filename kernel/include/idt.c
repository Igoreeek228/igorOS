// kernel/include/idt.c
//
// Таблица прерываний (IDT) и обработчики исключений CPU.
//
// До этого IDT не загружалась вовсе: любое исключение (#GP, #PF и т.д.)
// приводило к тихому тройного сбоя и перезагрузки. Теперь векторы 0-31
// перехватываются, печатают диагностику на serial и рисуют экран паники,
// после чего система останавливается контролируемо.

#include "idt.h"
#include "pic.h"
#include "../src/gui/font.h"

static struct idt_entry idt[256];
static struct idtr idtr;

/* Адреса заглушек из kernel/include/isr.asm (векторы 0-31). */
extern uint64_t isr_stub_table[32];

/* Глобальные переменные фреймбуфера из kernel/kernel.c. */
extern uint8_t*  g_fb_vram;
extern uint32_t  g_screen_w;
extern uint32_t  g_screen_h;
extern uint32_t  g_screen_pitch;
extern uint32_t  g_screen_bpp;

/* outb/inb приходят из pic.h. */

static void serial_print(const char *str) {
    while (*str) outb(0x3F8, *str++);
}

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags) {
    struct idt_entry *descriptor = &idt[vector];
    uint64_t addr = (uint64_t)isr;

    descriptor->isr_low    = addr & 0xFFFF;
    descriptor->kernel_cs  = 0x08;
    descriptor->ist        = 0;
    descriptor->attributes = flags;
    descriptor->isr_mid    = (addr >> 16) & 0xFFFF;
    descriptor->isr_high   = (addr >> 32) & 0xFFFFFFFF;
    descriptor->reserved   = 0;
}

void idt_init(void) {
    idtr.base = (uint64_t)&idt[0];
    idtr.limit = (uint16_t)(sizeof(struct idt_entry) * 256 - 1);

    pic_remap();

    /* Исключения CPU (векторы 0-31): present, DPL=0, 64-битный шлюз. */
    for (int i = 0; i < 32; i++) {
        idt_set_descriptor((uint8_t)i, (void*)isr_stub_table[i], 0x8E);
    }

    __asm__ volatile ("lidt %0" : : "m"(idtr));

    /* IRQ пока маскированы (disable_pic в _start), sti безопасно:
     * прерывания от устройств не придут, а исключения теперь ловим. */
    __asm__ volatile ("sti");
}

/* ---------- экран паники ---------- */

static const char* exception_name(uint64_t vector) {
    switch (vector) {
        case 0:  return "Division Error";
        case 1:  return "Debug";
        case 2:  return "NMI";
        case 3:  return "Breakpoint";
        case 4:  return "Overflow";
        case 5:  return "Bound Range Exceeded";
        case 6:  return "Invalid Opcode";
        case 7:  return "Device Not Available";
        case 8:  return "Double Fault";
        case 10: return "Invalid TSS";
        case 11: return "Segment Not Present";
        case 12: return "Stack-Segment Fault";
        case 13: return "General Protection Fault";
        case 14: return "Page Fault";
        case 16: return "x87 Floating-Point";
        case 17: return "Alignment Check";
        case 18: return "Machine Check";
        case 19: return "SIMD Floating-Point";
        default: return "Reserved/Unknown";
    }
}

static void hex16(char* out, uint64_t v) {
    static const char digits[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        *out++ = digits[v & 0xF];
        v >>= 4;
    }
    *out = 0;
}

static void uint_str(char* out, uint64_t v) {
    char tmp[24];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0;
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

/* Вызывается из isr.asm. Возврата не предполагается (заглушка крутит hlt). */
void isr_panic_handler(uint64_t vector, uint64_t err, uint64_t rip) {
    char buf[64];
    char num[24];

    serial_print("\n[KERNEL PANIC] Exception #");
    uint_str(num, vector);
    serial_print(num);
    serial_print(" (");
    serial_print(exception_name(vector));
    serial_print("), err=0x");
    hex16(buf, err);
    serial_print(buf);
    serial_print(", RIP=0x");
    hex16(buf, rip);
    serial_print(buf);
    serial_print("\n");

    /* Экран паники: рисуем во фреймбуфер, если он готов (32bpp). */
    if (g_fb_vram && g_screen_bpp == 32) {
        uint32_t* vram32 = (uint32_t*)g_fb_vram;
        uint32_t stride = g_screen_pitch / 4;

        int y = 40;
        draw_string(":( KERNEL PANIC", 40, y, 0x00FF3B30, vram32, stride);
        y += 30;

        draw_string("Exception #", 40, y, 0x00FFFFFF, vram32, stride);
        uint_str(num, vector);
        draw_string(num, 40 + font_text_width("Exception #"), y, 0x00FFFFFF, vram32, stride);
        draw_string(exception_name(vector),
                    40 + font_text_width("Exception #") + font_text_width(num) + 12,
                    y, 0x008E8E93, vram32, stride);
        y += 22;

        draw_string("err=0x", 40, y, 0x008E8E93, vram32, stride);
        hex16(buf, err);
        draw_string(buf, 40 + font_text_width("err=0x"), y, 0x008E8E93, vram32, stride);
        y += 22;

        draw_string("RIP=0x", 40, y, 0x008E8E93, vram32, stride);
        hex16(buf, rip);
        draw_string(buf, 40 + font_text_width("RIP=0x"), y, 0x008E8E93, vram32, stride);
        y += 30;

        draw_string("System halted. Details on serial (COM1).",
                    40, y, 0x008E8E93, vram32, stride);
    }

    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
