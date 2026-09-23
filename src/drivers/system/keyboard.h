#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/*
 * PS/2 клавиатура (scan code set 1), работает по IRQ1.
 * Драйвер отдаёт события "нажата / отпущена" с учётом расширенных
 * клавиш (стрелки, правый Ctrl/Alt, Home/End...), а не только символы.
 */

#define KBD_MOD_SHIFT 0x01
#define KBD_MOD_CTRL  0x02
#define KBD_MOD_ALT   0x04
#define KBD_MOD_CAPS  0x08

typedef struct {
    uint8_t scancode;   /* скан-код set 1 без бита 0x80 (make/break) */
    uint8_t extended;   /* 1 если был префикс 0xE0 (стрелки и т.п.)  */
    uint8_t pressed;    /* 1 = нажата (или автоповтор), 0 = отпущена */
    uint8_t mods;       /* KBD_MOD_* на момент события               */
} kbd_event_t;

void init_keyboard(void);

/* Взять следующее событие из очереди. 0 если очередь пуста. */
int keyboard_poll_event(kbd_event_t *ev);

/* Символ для события нажатия (0 если это не печатная клавиша). */
char keyboard_event_to_char(const kbd_event_t *ev);

/* Совместимость со старым кодом: следующий печатный символ или 0. */
char keyboard_getchar(void);

/* 1 if Alt+F4 was pressed since last call (edge-triggered). */
int keyboard_consume_alt_f4(void);

#endif
