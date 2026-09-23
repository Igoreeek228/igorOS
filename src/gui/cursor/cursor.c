#include "gui/cursor/cursor.h"

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} bmp_header_t;

typedef struct {
    uint32_t size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_pixels_per_m;
    int32_t  y_pixels_per_m;
    uint32_t colors_used;
    uint32_t colors_important;
} bmp_info_header_t;
#pragma pack(pop)

// Максимальный визуальный размер курсора на экране (28px - стандарт Windows)
#define MAX_CURSOR_SIZE 28

static inline void get_bmp_pixel(const uint8_t* pixel_data, int img_w, int img_h, int is_bottom_up, int bpp, int row_stride, int x, int y, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
    if (x < 0) x = 0;
    if (x >= img_w) x = img_w - 1;
    if (y < 0) y = 0;
    if (y >= img_h) y = img_h - 1;

    int py = is_bottom_up ? (img_h - 1 - y) : y;
    int bytes_per_pixel = bpp / 8;
    const uint8_t* p = pixel_data + py * row_stride + x * bytes_per_pixel;

    *b = p[0];
    *g = p[1];
    *r = p[2];
    *a = (bpp == 32) ? p[3] : 255;
}

// Смешивает цвет с уже нарисованным пикселем экрана по alpha (0-255).
// Раньше край курсора либо рисовался полностью непрозрачным, либо не
// рисовался вовсе (порог final_a < 32) — отсюда "лесенка" по контуру,
// даже несмотря на билинейную интерполяцию внутри самой картинки.
// Теперь используем реальное значение альфы из BMP для плавного края.
static inline uint32_t cursor_blend(uint32_t bg, uint32_t fg, uint8_t alpha) {
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    uint32_t inv = 255 - alpha;
    uint32_t bg_r = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bg_b = bg & 0xFF;
    uint32_t fg_r = (fg >> 16) & 0xFF, fg_g = (fg >> 8) & 0xFF, fg_b = fg & 0xFF;
    uint32_t r = (fg_r * alpha + bg_r * inv) / 255;
    uint32_t g = (fg_g * alpha + bg_g * inv) / 255;
    uint32_t b = (fg_b * alpha + bg_b * inv) / 255;
    return (r << 16) | (g << 8) | b;
}

void draw_cursor_bmp(int x, int y, uint32_t* backbuffer, uint32_t screen_width, uint32_t screen_height) {
    bmp_header_t* header = (bmp_header_t*)cursor_bmp_start;
    if (header->type != 0x4D42) return;

    bmp_info_header_t* info = (bmp_info_header_t*)(cursor_bmp_start + sizeof(bmp_header_t));
    
    int img_w = info->width;
    int img_h = info->height;
    int is_bottom_up = 1;

    if (img_h < 0) {
        img_h = -img_h;
        is_bottom_up = 0;
    }

    uint16_t bpp = info->bpp;
    const uint8_t* pixel_data = cursor_bmp_start + header->offset;
    int bytes_per_pixel = bpp / 8;
    int row_stride = ((img_w * bytes_per_pixel + 3) / 4) * 4;

    // Сохранение пропорций
    int render_w = img_w;
    int render_h = img_h;

    if (img_w > MAX_CURSOR_SIZE || img_h > MAX_CURSOR_SIZE) {
        if (img_w >= img_h) {
            render_w = MAX_CURSOR_SIZE;
            render_h = (img_h * MAX_CURSOR_SIZE) / img_w;
        } else {
            render_h = MAX_CURSOR_SIZE;
            render_w = (img_w * MAX_CURSOR_SIZE) / img_h;
        }
    }

    if (render_w < 1) render_w = 1;
    if (render_h < 1) render_h = 1;

    // Билинейная интерполяция
    for (int cy = 0; cy < render_h; cy++) {
        for (int cx = 0; cx < render_w; cx++) {
            int draw_x = x + cx;
            int draw_y = y + cy;

            if (draw_x < 0 || draw_x >= (int)screen_width || draw_y < 0 || draw_y >= (int)screen_height)
                continue;

            uint32_t gx = (cx * (img_w - 1) * 65536) / (render_w > 1 ? render_w - 1 : 1);
            uint32_t gy = (cy * (img_h - 1) * 65536) / (render_h > 1 ? render_h - 1 : 1);

            int x0 = gx >> 16;
            int y0 = gy >> 16;
            int x1 = (x0 < img_w - 1) ? x0 + 1 : x0;
            int y1 = (y0 < img_h - 1) ? y0 + 1 : y0;

            uint32_t fx = gx & 0xFFFF;
            uint32_t fy = gy & 0xFFFF;

            uint32_t w00 = ((65536 - fx) * (65536 - fy)) >> 16;
            uint32_t w10 = (fx * (65536 - fy)) >> 16;
            uint32_t w01 = ((65536 - fx) * fy) >> 16;
            uint32_t w11 = (fx * fy) >> 16;

            uint8_t r00, g00, b00, a00, r10, g10, b10, a10;
            uint8_t r01, g01, b01, a01, r11, g11, b11, a11;

            get_bmp_pixel(pixel_data, img_w, img_h, is_bottom_up, bpp, row_stride, x0, y0, &r00, &g00, &b00, &a00);
            get_bmp_pixel(pixel_data, img_w, img_h, is_bottom_up, bpp, row_stride, x1, y0, &r10, &g10, &b10, &a10);
            get_bmp_pixel(pixel_data, img_w, img_h, is_bottom_up, bpp, row_stride, x0, y1, &r01, &g01, &b01, &a01);
            get_bmp_pixel(pixel_data, img_w, img_h, is_bottom_up, bpp, row_stride, x1, y1, &r11, &g11, &b11, &a11);

            uint8_t final_r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
            uint8_t final_g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
            uint8_t final_b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;
            uint8_t final_a = (a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11) >> 16;

            // Мадженту (#FF00FF) — классический цвет-ключ прозрачности —
            // всегда пропускаем полностью, это не часть картинки.
            if (final_r > 240 && final_g < 15 && final_b > 240) continue;
            if (final_a == 0) continue; // полностью прозрачный пиксель — пропускаем совсем

            uint32_t color = (final_r << 16) | (final_g << 8) | final_b;
            uint32_t idx = draw_y * screen_width + draw_x;
            // Настоящий альфа-блендинг вместо жёсткой перезаписи — там, где
            // у исходного BMP плавный (антиалиased) край, он теперь и
            // остаётся плавным на экране, а не режется до "есть/нет пиксель".
            backbuffer[idx] = cursor_blend(backbuffer[idx], color, final_a);
        }
    }
}