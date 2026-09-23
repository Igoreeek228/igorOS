/*
 * calc_logic.c
 *
 * Портировано из OriginOS (kernel/calc.c). Это одна из "фич",
 * которых не хватало в IgorOS -- в оригинале OriginOS калькулятор
 * работал по этой же модели (fixed-point x100, аккумулятор +
 * отложенная операция), поведение сохранено 1:1, только убрана
 * зависимость от kstring.h (в IgorOS её нет) -- нужные две строковые
 * функции реализованы здесь же как маленькие статические хелперы.
 */

#include "calc_state.h"

#define SCALE 100

static uint32_t local_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void local_strcpy_shift_left(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void set_display_from_fixed(calc_state_t *c, int64_t v) {
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    int64_t whole = v / SCALE;
    int64_t frac = v % SCALE;

    char wbuf[16];
    uint32_t wi = 0;
    if (whole == 0) wbuf[wi++] = '0';
    while (whole > 0 && wi < sizeof(wbuf)) {
        wbuf[wi++] = (char)('0' + (whole % 10));
        whole /= 10;
    }

    uint32_t di = 0;
    if (neg) c->display[di++] = '-';
    while (wi > 0) c->display[di++] = wbuf[--wi];

    /* дробную часть показываем, только если она не нулевая (2 знака) */
    if (frac != 0) {
        c->display[di++] = '.';
        c->display[di++] = (char)('0' + (frac / 10));
        c->display[di++] = (char)('0' + (frac % 10));
    }
    c->display[di] = 0;
    c->disp_len = di;
}

/* парсит текущий текст на экране (набранный пользователем) в fixed-point
 * int64, масштаб x100 */
static int64_t parse_display(const calc_state_t *c) {
    int64_t whole = 0;
    int64_t frac = 0;
    int frac_digits = 0;
    int in_frac = 0;
    int neg = 0;
    uint32_t i = 0;

    if (c->disp_len > 0 && c->display[0] == '-') { neg = 1; i = 1; }

    for (; i < c->disp_len; i++) {
        char ch = c->display[i];
        if (ch == '.') { in_frac = 1; continue; }
        if (ch < '0' || ch > '9') continue;
        if (!in_frac) {
            whole = whole * 10 + (ch - '0');
        } else if (frac_digits < 2) {
            frac = frac * 10 + (ch - '0');
            frac_digits++;
        }
    }
    while (frac_digits < 2) { frac *= 10; frac_digits++; }

    int64_t result = whole * SCALE + frac;
    return neg ? -result : result;
}

void calc_init(calc_state_t *c) {
    c->display[0] = '0';
    c->display[1] = 0;
    c->disp_len = 1;
    c->accumulator = 0;
    c->pending_op = 0;
    c->start_new_entry = 1;
    c->div_by_zero = 0;
    c->last_pressed_key = 0;
    c->press_flash_t = 0;
    /* memory НЕ сбрасывается здесь: 'C' очищает только текущий ввод/
     * аккумулятор (поведение как в macOS Calculator) -- только явный
     * MC сбрасывает регистр памяти. */
}

static void apply_pending(calc_state_t *c) {
    if (c->pending_op == 0) {
        c->accumulator = parse_display(c);
        return;
    }
    int64_t rhs = parse_display(c);
    switch (c->pending_op) {
        case '+': c->accumulator = c->accumulator + rhs; break;
        case '-': c->accumulator = c->accumulator - rhs; break;
        case '*': c->accumulator = (c->accumulator * rhs) / SCALE; break;
        case '/':
            if (rhs == 0) { c->div_by_zero = 1; c->accumulator = 0; }
            else c->accumulator = (c->accumulator * SCALE) / rhs;
            break;
        default: break;
    }
}

void calc_feed_char(calc_state_t *c, char key) {
    if (c->div_by_zero && key != 'C') {
        calc_init(c);
    }

    if (key >= '0' && key <= '9') {
        if (c->start_new_entry) {
            c->display[0] = '0'; c->display[1] = 0; c->disp_len = 1;
            c->start_new_entry = 0;
        }
        if (c->disp_len == 1 && c->display[0] == '0') {
            c->display[0] = key; c->disp_len = 1;
        } else if (c->disp_len < sizeof(c->display) - 1) {
            c->display[c->disp_len++] = key;
            c->display[c->disp_len] = 0;
        }
        return;
    }

    if (key == '.') {
        if (c->start_new_entry) {
            c->display[0] = '0'; c->display[1] = '.'; c->display[2] = 0;
            c->disp_len = 2;
            c->start_new_entry = 0;
            return;
        }
        for (uint32_t i = 0; i < c->disp_len; i++) if (c->display[i] == '.') return;
        if (c->disp_len < sizeof(c->display) - 1) {
            c->display[c->disp_len++] = '.';
            c->display[c->disp_len] = 0;
        }
        return;
    }

    if (key == 'C') { calc_init(c); return; }

    if (key == '~') {
        if (c->disp_len > 0 && c->display[0] == '-') {
            local_strcpy_shift_left(c->display, c->display + 1, sizeof(c->display));
            c->disp_len--;
        } else if (!(c->disp_len == 1 && c->display[0] == '0')) {
            for (uint32_t i = c->disp_len; i > 0; i--) c->display[i] = c->display[i - 1];
            c->display[0] = '-';
            c->disp_len++;
            c->display[c->disp_len] = 0;
        }
        return;
    }

    if (key == 'm') { c->memory = 0; return; }
    if (key == 'p') { c->memory += parse_display(c); c->start_new_entry = 1; return; }
    if (key == 'q') { c->memory -= parse_display(c); c->start_new_entry = 1; return; }
    if (key == 'r') { set_display_from_fixed(c, c->memory); c->start_new_entry = 1; return; }

    if (key == '+' || key == '-' || key == '*' || key == '/') {
        apply_pending(c);
        c->pending_op = key;
        c->start_new_entry = 1;
        if (!c->div_by_zero) set_display_from_fixed(c, c->accumulator);
        return;
    }

    if (key == '=') {
        apply_pending(c);
        c->pending_op = 0;
        c->start_new_entry = 1;
        if (c->div_by_zero) {
            const char *err = "ERROR";
            uint32_t i = 0;
            while (err[i]) { c->display[i] = err[i]; i++; }
            c->display[i] = 0;
            c->disp_len = local_strlen(c->display);
        } else {
            set_display_from_fixed(c, c->accumulator);
        }
        return;
    }
}

void calc_backspace(calc_state_t *c) {
    if (c->div_by_zero) { calc_init(c); return; }
    if (c->disp_len > 1) {
        c->disp_len--;
        c->display[c->disp_len] = 0;
    } else {
        c->display[0] = '0';
        c->display[1] = 0;
        c->disp_len = 1;
        c->start_new_entry = 1;
    }
}
