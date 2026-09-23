#include "timer.h"
#include "include/pic.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_BASE_HZ  1193182U

static volatile uint64_t g_ticks = 0;
static uint32_t g_frequency = 250;

static inline void pit_outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void timer_irq_handler(void) {
    ++g_ticks;
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz < 20) frequency_hz = 20;
    if (frequency_hz > 1000) frequency_hz = 1000;

    uint32_t divisor = PIT_BASE_HZ / frequency_hz;
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;

    g_frequency = PIT_BASE_HZ / divisor;
    if (g_frequency == 0) g_frequency = frequency_hz;

    /* Channel 0, lobyte/hibyte, mode 3, binary. */
    pit_outb(PIT_COMMAND, 0x36);
    pit_outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    pit_outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;
    irq_install_handler(0, timer_irq_handler);
    pic_clear_mask(0);
}

uint64_t timer_ticks(void) {
    return g_ticks;
}

uint64_t timer_millis(void) {
    return (g_ticks * 1000ULL) / g_frequency;
}

void timer_wait_ticks(uint64_t ticks) {
    uint64_t target = g_ticks + ticks;
    while (g_ticks < target) {
        asm volatile ("hlt");
    }
}

void timer_wait_ms(uint32_t ms) {
    if (ms == 0) return;
    uint64_t ticks = ((uint64_t)ms * g_frequency + 999ULL) / 1000ULL;
    if (ticks == 0) ticks = 1;
    timer_wait_ticks(ticks);
}
