#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>

// Инициализация параметров экрана
void init_graphics(uint32_t* fb, uint32_t width, uint32_t height, uint32_t pitch);

// Базовые графические примитивы
void put_pixel(uint32_t x, uint32_t y, uint32_t color);
void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void clear_screen(uint32_t color);

#endif