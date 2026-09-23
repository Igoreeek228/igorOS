#ifndef CURSOR_H
#define CURSOR_H

#include <stdint.h>

extern const uint8_t cursor_bmp_start[];
extern const uint8_t cursor_bmp_end[];

void draw_cursor_bmp(int x, int y, uint32_t* backbuffer, uint32_t screen_width, uint32_t screen_height);

#endif