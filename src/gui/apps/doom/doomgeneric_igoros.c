#include "engine/doomgeneric.h"
#include "engine/doomkeys.h"
#include <stdint.h>

/* DG_ScreenBuffer lives in doomgeneric.c */

#define KEYQ_SIZE 128
static unsigned short key_q[KEYQ_SIZE];
static unsigned k_r = 0, k_w = 0;
static uint32_t ticks_ms = 0;

void doom_igor_push_key(int pressed, unsigned char key) {
    unsigned n = (k_w + 1) & (KEYQ_SIZE - 1);
    if (n == k_r) return;
    key_q[k_w] = (unsigned short)((pressed ? 0x0100 : 0) | key);
    k_w = n;
}

void doom_igor_tick_ms(uint32_t dt) { ticks_ms += dt; }

void DG_Init(void) {}

void DG_DrawFrame(void) {
    /* blit happens in doom_app after doomgeneric_Tick / D_Display */
}

/* Doom waits for the next tic in a loop (TryRunTics -> I_Sleep). The clock only
 * advances between frames, so Sleep must advance it or the whole OS spins here. */
void DG_SleepMs(uint32_t ms) { ticks_ms += ms; }

uint32_t DG_GetTicksMs(void) { return ticks_ms; }

int DG_GetKey(int *pressed, unsigned char *doom_key) {
    if (k_r == k_w) return 0;
    unsigned short k = key_q[k_r];
    k_r = (k_r + 1) & (KEYQ_SIZE - 1);
    *pressed = (k & 0x100) ? 1 : 0;
    *doom_key = (unsigned char)(k & 0xff);
    return 1;
}

void DG_SetWindowTitle(const char *title) { (void)title; }


/* ------------------------------------------------------------------ *
 * Раскладка: физические клавиши (скан-коды set 1) -> клавиши Doom.
 *
 *   W / S            вперёд / назад        (в меню: вверх / вниз)
 *   A / D            шаг влево / вправо    (в меню: влево / вправо)
 *   стрелки          вверх/вниз идти, влево/вправо поворот
 *   E или Пробел     ИСПОЛЬЗОВАТЬ -- открывать двери, нажимать кнопки
 *                    (в меню: Enter)
 *   Ctrl             огонь
 *   Shift            бег
 *   Alt              удерживать + поворот = шаг вбок
 *   1-7              оружие,  Tab карта,  Esc меню,  Enter выбор
 *
 * Буквы W/A/S/D/E в игре дополнительно уходят как обычные буквы, чтобы
 * работали чит-коды (iddqd, idkfa...). В меню они превращаются только
 * в стрелки / Enter, иначе срабатывали бы горячие буквы пунктов меню.
 * ------------------------------------------------------------------ */

#include "engine/doomkeys.h"

extern unsigned int menuactive;   /* m_menu.c: открыто ли меню Doom */

static const char letter_row_q[] = "qwertyuiop";   /* 0x10..0x19 */
static const char letter_row_a[] = "asdfghjkl";    /* 0x1E..0x26 */
static const char letter_row_z[] = "zxcvbnm";      /* 0x2C..0x32 */

/* out[0], out[1] -- до двух клавиш Doom на одну физическую клавишу */
static void map_key(int ext, unsigned sc, int menu, unsigned char out[2]) {
    out[0] = out[1] = 0;

    if (ext) {
        switch (sc) {
            case 0x48: out[0] = KEY_UPARROW;    break;
            case 0x50: out[0] = KEY_DOWNARROW;  break;
            case 0x4B: out[0] = KEY_LEFTARROW;  break;
            case 0x4D: out[0] = KEY_RIGHTARROW; break;
            case 0x1D: out[0] = KEY_FIRE;       break;   /* правый Ctrl  */
            case 0x38: out[0] = KEY_RALT;       break;   /* правый Alt   */
            case 0x1C: out[0] = KEY_ENTER;      break;   /* Enter цифр.  */
            case 0x47: out[0] = KEY_HOME;       break;
            case 0x4F: out[0] = KEY_END;        break;
            case 0x49: out[0] = KEY_PGUP;       break;
            case 0x51: out[0] = KEY_PGDN;       break;
            case 0x52: out[0] = KEY_INS;        break;
            case 0x53: out[0] = KEY_DEL;        break;
        }
        return;
    }

    char letter = 0;
    if (sc >= 0x10 && sc <= 0x19) letter = letter_row_q[sc - 0x10];
    else if (sc >= 0x1E && sc <= 0x26) letter = letter_row_a[sc - 0x1E];
    else if (sc >= 0x2C && sc <= 0x32) letter = letter_row_z[sc - 0x2C];

    if (letter) {
        switch (letter) {
            case 'w': if (menu) out[0] = KEY_UPARROW;    else { out[0] = KEY_UPARROW;   out[1] = 'w'; } return;
            case 's': if (menu) out[0] = KEY_DOWNARROW;  else { out[0] = KEY_DOWNARROW; out[1] = 's'; } return;
            case 'a': if (menu) out[0] = KEY_LEFTARROW;  else { out[0] = KEY_STRAFE_L;  out[1] = 'a'; } return;
            case 'd': if (menu) out[0] = KEY_RIGHTARROW; else { out[0] = KEY_STRAFE_R;  out[1] = 'd'; } return;
            case 'e': if (menu) out[0] = KEY_ENTER;      else { out[0] = KEY_USE;       out[1] = 'e'; } return;
            default:  out[0] = (unsigned char)letter; return;
        }
    }

    if (sc >= 0x02 && sc <= 0x0A) { out[0] = (unsigned char)('1' + (sc - 0x02)); return; }
    if (sc >= 0x3B && sc <= 0x44) { out[0] = (unsigned char)(KEY_F1 + (sc - 0x3B)); return; }

    switch (sc) {
        case 0x01: out[0] = KEY_ESCAPE;    break;
        case 0x0B: out[0] = '0';           break;
        case 0x0C: out[0] = KEY_MINUS;     break;
        case 0x0D: out[0] = KEY_EQUALS;    break;
        case 0x0E: out[0] = KEY_BACKSPACE; break;
        case 0x0F: out[0] = KEY_TAB;       break;
        case 0x1A: out[0] = '[';           break;
        case 0x1B: out[0] = ']';           break;
        case 0x1C: out[0] = KEY_ENTER;     break;
        case 0x1D: out[0] = KEY_FIRE;      break;        /* левый Ctrl  */
        case 0x27: out[0] = ';';           break;
        case 0x28: out[0] = '\'';         break;
        case 0x29: out[0] = '`';           break;
        case 0x2A: case 0x36: out[0] = KEY_RSHIFT; break;
        case 0x2B: out[0] = '\\';         break;
        case 0x33: out[0] = ',';           break;
        case 0x34: out[0] = '.';           break;
        case 0x35: out[0] = '/';           break;
        case 0x38: out[0] = KEY_RALT;      break;        /* левый Alt   */
        case 0x39: out[0] = menu ? KEY_ENTER : KEY_USE; break;   /* Пробел */
        case 0x57: out[0] = KEY_F11;       break;
        case 0x58: out[0] = KEY_F12;       break;
    }
}

/* Что сейчас зажато: чтобы отпускание ушло ровно тем же клавишам Doom,
 * даже если между нажатием и отпусканием открылось меню. */
static unsigned char held[2][128][2];
static int held_count = 0;

void doom_igor_feed_scancode(unsigned sc, int ext, int pressed) {
    if (sc >= 128) return;
    unsigned char *h = held[ext ? 1 : 0][sc];

    if (pressed) {
        if (h[0]) {
            /* автоповтор железа: в меню нужен (прокрутка), в игре нет */
            if (menuactive) {
                doom_igor_push_key(1, h[0]);
                if (h[1]) doom_igor_push_key(1, h[1]);
            }
            return;
        }
        unsigned char out[2];
        map_key(ext, sc, menuactive != 0, out);
        if (!out[0]) return;
        h[0] = out[0]; h[1] = out[1];
        held_count++;
        doom_igor_push_key(1, h[0]);
        if (h[1]) doom_igor_push_key(1, h[1]);
    } else {
        if (!h[0]) return;
        doom_igor_push_key(0, h[0]);
        if (h[1]) doom_igor_push_key(0, h[1]);
        h[0] = h[1] = 0;
        held_count--;
    }
}

/* Окно потеряло фокус / закрылось -- отпустить всё, иначе персонаж
 * так и побежит вперёд сам. */
void doom_igor_release_all(void) {
    for (int e = 0; e < 2; e++)
        for (int sc = 0; sc < 128; sc++) {
            unsigned char *h = held[e][sc];
            if (!h[0]) continue;
            doom_igor_push_key(0, h[0]);
            if (h[1]) doom_igor_push_key(0, h[1]);
            h[0] = h[1] = 0;
        }
    held_count = 0;
}

int doom_igor_held_count(void) { return held_count; }
