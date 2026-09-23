#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include <stdint.h>

void toggle_settings_app(void);
void render_settings_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click);

#endif
