#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

/*
 * Загрузочный протокол — Limine (см. kernel/kernel.c и boot/limine/limine.conf).
 * Параметры экрана передаются в desktop_init() напрямую из ответа
 * limine_framebuffer_request, никакие структуры загрузчика здесь не нужны.
 */

/* Инициализация рабочего стола: фреймбуфер, ввод, обои, часы. */
void desktop_init(uint8_t* vram, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp);

/* Главный цикл отрисовки и обработки ввода. Не возвращает управление. */
void desktop_run(void);

/* Выключение системы (порт-заглушки QEMU/Bochs/VBox + halt). */
void sys_shutdown(void);

/* Графические примитивы (рисуют во внутренний бэкбуфер десктопа). */
void draw_pixel_buf(int x, int y, uint32_t color);
void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
uint32_t blend_colors(uint32_t bg, uint32_t fg, uint8_t alpha);

#endif
