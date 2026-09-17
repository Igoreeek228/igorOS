// src/gui/font.h
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

void draw_char(char c, int x, int y, uint32_t color, uint32_t* buf, uint32_t buf_width);
void draw_string(const char* str, int x, int y, uint32_t color, uint32_t* buf, uint32_t buf_width);
int font_text_width(const char* str); // суммарная ширина строки в пикселях (для центрирования/тултипов)

#endif