#ifndef MUSIC_APP_H
#define MUSIC_APP_H

#include <stdint.h>

void toggle_music_app(void);
void render_music_app_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click);

#endif