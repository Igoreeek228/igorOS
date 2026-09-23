#ifndef CALC_STATE_H
#define CALC_STATE_H

#include <stdint.h>

/*
 * Состояние калькулятора.
 *
 * Портировано из OriginOS (kernel/wm.h + kernel/calc.c): классическая
 * модель "аккумулятор + отложенная операция + строка текущего ввода".
 * Фиксированная точка (значение * 100) вместо float/double -- в
 * freestanding-ядре нет мягкого float, а с fixed-point арифметика
 * остаётся точной и предсказуемой.
 */
typedef struct {
    char display[24];    /* что показывается / сейчас набирается */
    uint32_t disp_len;
    int64_t accumulator;  /* fixed point, x100 */
    int64_t memory;        /* fixed point, x100 -- MC/M+/M-/MR */
    char pending_op;       /* 0 = нет, иначе '+','-','*','/' */
    int start_new_entry;   /* следующая цифра должна начать новый ввод */
    int div_by_zero;       /* защёлкнутая ошибка деления на 0 */

    char last_pressed_key;
    int press_flash_t;
} calc_state_t;

void calc_init(calc_state_t *c);
void calc_feed_char(calc_state_t *c, char key);
void calc_backspace(calc_state_t *c);

#endif
