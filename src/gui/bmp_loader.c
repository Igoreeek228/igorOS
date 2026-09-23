#include "bmp_loader.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;             // Сигнатура 'BM'
    uint32_t size;             // Размер файла
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;           // Смещение до пикселей
} BMPHeader;

typedef struct {
    uint32_t size;
    int32_t  width;            // Ширина изображения
    int32_t  height;           // Высота изображения
    uint16_t planes;
    uint16_t bpp;              // Глубина цвета (24 или 32 бит)
    uint32_t compression;
} BMPInfoHeader;
#pragma pack(pop)

void draw_bmp_stretched(const uint8_t* bmp_data, int dst_w, int dst_h, uint32_t* backbuffer) {
    if (!bmp_data || !backbuffer) return;

    BMPHeader* file_hdr = (BMPHeader*)bmp_data;
    if (file_hdr->type != 0x4D42) return; // Проверка 'BM'

    BMPInfoHeader* info_hdr = (BMPInfoHeader*)(bmp_data + sizeof(BMPHeader));
    int src_w = info_hdr->width;
    int src_h = info_hdr->height;
    int is_top_down = 0;

    if (src_h < 0) {
        src_h = -src_h;
        is_top_down = 1;
    }

    int bytes_per_pixel = info_hdr->bpp / 8;
    if (bytes_per_pixel < 3) return; // Поддержка только 24 и 32 bit

    int row_stride = ((src_w * bytes_per_pixel + 3) / 4) * 4;
    const uint8_t* pixels = bmp_data + file_hdr->offset;

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * src_h) / dst_h;
        int actual_sy = is_top_down ? sy : (src_h - 1 - sy);

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * src_w) / dst_w;
            const uint8_t* pixel = pixels + (actual_sy * row_stride) + (sx * bytes_per_pixel);

            uint8_t b = pixel[0];
            uint8_t g = pixel[1];
            uint8_t r = pixel[2];
            
            // Если 32 бит — берем альфа-канал, иначе 255 (непрозрачный)
            uint8_t a = (bytes_per_pixel == 4) ? pixel[3] : 255;

            // Полностью прозрачный пиксель пропускаем
            if (a == 0) continue;

            // Color Keying для 24-битного BMP (пропуск чисто черного цвета)
            if (bytes_per_pixel == 3 && r == 0 && g == 0 && b == 0) {
                continue;
            }

            int dst_idx = dy * dst_w + dx;

            if (a == 255) {
                // Полная непрозрачность
                backbuffer[dst_idx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                // Alpha Blending для полупрозрачности
                uint32_t bg_color = backbuffer[dst_idx];
                uint8_t bg_r = (bg_color >> 16) & 0xFF;
                uint8_t bg_g = (bg_color >> 8) & 0xFF;
                uint8_t bg_b = bg_color & 0xFF;

                uint8_t out_r = (r * a + bg_r * (255 - a)) / 255;
                uint8_t out_g = (g * a + bg_g * (255 - a)) / 255;
                uint8_t out_b = (b * a + bg_b * (255 - a)) / 255;

                backbuffer[dst_idx] = ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | out_b;
            }
        }
    }
}

/*
 * IgorOS Nord: draw_bmp_stretched_at.
 *
 * draw_bmp_stretched() выше всегда пишет с (0,0) на весь dst_w x dst_h
 * буфера -- удобно для полноэкранных обоев, но не годится для превью
 * миниатюр внутри окна (например, выбор обоев в Settings > Wallpaper).
 * Здесь та же логика растяжения, но с произвольным смещением (ox,oy)
 * внутри буфера произвольной ширины buf_w -- нужно для рисования
 * маленького квадрата-превью где-то в середине окна, а не поверх
 * всего экрана.
 */
void draw_bmp_stretched_at(const uint8_t* bmp_data, int ox, int oy, int dst_w, int dst_h, uint32_t* backbuffer, int buf_w) {
    if (!bmp_data || !backbuffer) return;

    BMPHeader* file_hdr = (BMPHeader*)bmp_data;
    if (file_hdr->type != 0x4D42) return;

    BMPInfoHeader* info_hdr = (BMPInfoHeader*)(bmp_data + sizeof(BMPHeader));
    int src_w = info_hdr->width;
    int src_h = info_hdr->height;
    int is_top_down = 0;

    if (src_h < 0) {
        src_h = -src_h;
        is_top_down = 1;
    }

    int bytes_per_pixel = info_hdr->bpp / 8;
    if (bytes_per_pixel < 3) return;

    int row_stride = ((src_w * bytes_per_pixel + 3) / 4) * 4;
    const uint8_t* pixels = bmp_data + file_hdr->offset;

    for (int dy = 0; dy < dst_h; dy++) {
        int sy = (dy * src_h) / dst_h;
        int actual_sy = is_top_down ? sy : (src_h - 1 - sy);

        for (int dx = 0; dx < dst_w; dx++) {
            int sx = (dx * src_w) / dst_w;
            const uint8_t* pixel = pixels + (actual_sy * row_stride) + (sx * bytes_per_pixel);

            uint8_t b = pixel[0];
            uint8_t g = pixel[1];
            uint8_t r = pixel[2];
            uint8_t a = (bytes_per_pixel == 4) ? pixel[3] : 255;

            if (a == 0) continue;
            if (bytes_per_pixel == 3 && r == 0 && g == 0 && b == 0) continue;

            int px = ox + dx;
            int py = oy + dy;
            if (px < 0 || py < 0) continue;

            int dst_idx = py * buf_w + px;

            if (a == 255) {
                backbuffer[dst_idx] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                uint32_t bg_color = backbuffer[dst_idx];
                uint8_t bg_r = (bg_color >> 16) & 0xFF;
                uint8_t bg_g = (bg_color >> 8) & 0xFF;
                uint8_t bg_b = bg_color & 0xFF;

                uint8_t out_r = (r * a + bg_r * (255 - a)) / 255;
                uint8_t out_g = (g * a + bg_g * (255 - a)) / 255;
                uint8_t out_b = (b * a + bg_b * (255 - a)) / 255;

                backbuffer[dst_idx] = ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | out_b;
            }
        }
    }
}