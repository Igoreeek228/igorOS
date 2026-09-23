#ifndef DOOM_APP_H
#define DOOM_APP_H

#include <stdint.h>

void toggle_doom_app(void);
void doom_app_close(void);
void doom_app_feed_key(char key);
void render_doom_app_window(uint32_t* buf, int scr_w, int scr_h,
                            int mx, int my, int btn, int click);
int doom_app_is_open(void);

#endif
