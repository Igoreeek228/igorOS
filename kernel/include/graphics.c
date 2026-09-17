#include "graphics.h"

static uint32_t* fb_address = 0;
static uint32_t screen_width = 0;
static uint32_t screen_height = 0;
static uint32_t screen_pitch = 0;

void init_graphics(uint32_t* fb, uint32_t width, uint32_t height, uint32_t pitch) {
    fb_address = fb;
    screen_width = width;
    screen_height = height;
    screen_pitch = pitch;
}

void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb_address || x >= screen_width || y >= screen_height) return;
    
    // Вычисление смещения с учетом pitch (строки могут иметь выравнивание)
    uint32_t* pixel = (uint32_t*)((uint8_t*)fb_address + y * screen_pitch) + x;
    *pixel = color;
}

void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t i = 0; i < h; i++) {
        for (uint32_t j = 0; j < w; j++) {
            put_pixel(x + j, y + i, color);
        }
    }
}

void clear_screen(uint32_t color) {
    draw_rect(0, 0, screen_width, screen_height, color);
}