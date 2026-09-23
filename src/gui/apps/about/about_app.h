#ifndef ABOUT_APP_H
#define ABOUT_APP_H

#include <stdint.h>

void toggle_about_app(void);
void render_about_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click);

#endif
