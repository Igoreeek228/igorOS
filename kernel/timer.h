#ifndef IGOROS_TIMER_H
#define IGOROS_TIMER_H

#include <stdint.h>

/* Monotonic kernel clock driven by PIT IRQ0. */
void timer_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
uint64_t timer_millis(void);
void timer_wait_ticks(uint64_t ticks);
void timer_wait_ms(uint32_t ms);

#endif
