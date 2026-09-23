#ifndef BMP_LOADER_H
#define BMP_LOADER_H

#include <stdint.h>

void draw_bmp_stretched(const uint8_t* bmp_data, int dst_w, int dst_h, uint32_t* backbuffer);
void draw_bmp_stretched_at(const uint8_t* bmp_data, int ox, int oy, int dst_w, int dst_h, uint32_t* backbuffer, int buf_w);

#endif