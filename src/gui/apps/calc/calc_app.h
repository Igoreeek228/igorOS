#ifndef CALC_APP_H
#define CALC_APP_H

#include <stdint.h>

void toggle_calc_app(void);
void render_calc_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click);

/* Клавиатурный ввод для калькулятора (передаётся из главного цикла,
 * как и остальной ввод с клавиатуры desktop_run). */
void calc_app_feed_key(char key);

#endif
