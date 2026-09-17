#include "graphics.h"
#include <stddef.h>

static size_t buffer_size_bytes;
static uint32_t *frontbuffer;
static uint32_t *backbuffer;
static uint32_t fb_width;
static uint32_t fb_height;
static uint32_t fb_pitch;
static size_t buffer_size_bytes;

void graphics_init(uint32_t *fb_addr, uint32_t width, uint32_t height, uint32_t pitch) {
    frontbuffer = fb_addr;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    buffer_size_bytes = (size_t)height * pitch;

    // Резервируем память под backbuffer (убедитесь, что регион свободен)
    backbuffer = (uint32_t*)0x2000000;
}

void put_pixel_backbuffer(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    backbuffer[y * (fb_pitch / 4) + x] = color;
}

void swap_buffers(void) {
    uint64_t *dst = (uint64_t*)frontbuffer;
    uint64_t *src = (uint64_t*)backbuffer;
    size_t count = buffer_size_bytes / 8;

    for (size_t i = 0; i < count; i++) {
        dst[i] = src[i];
    }
}