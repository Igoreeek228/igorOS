#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H

#include <stdint.h>

void toggle_terminal_app(void);
void render_terminal_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click);
void terminal_app_feed_key(char key);

#endif
