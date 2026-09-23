/*
 * settings_app.c
 *
 * Приложение "Settings" для IgorOS Nord, стиль Big Sur (System
 * Preferences -> System Settings редизайн: боковая область слева,
 * контент справа, крупные скругления).
 *
 * Функциональные элементы: обои (Day/Night) и настройки увеличения
 * иконок дока (вкл/выкл + сила). Всё реально меняет поведение системы.
 */

#include "settings_app.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"

#include <stdint.h>

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
extern void draw_bmp_stretched_at(const uint8_t* bmp_data, int ox, int oy, int dst_w, int dst_h, uint32_t* backbuffer, int buf_w);

/* IgorOS Nord: выбор обоев -- переменная и три встроенных варианта
 * определены в desktop.c/kernel/resources.asm. */
extern int g_wallpaper_choice;
extern const uint8_t wallpaper_bmp_start[];
extern const uint8_t wallpaper_day_bmp_start[];
extern const uint8_t wallpaper_night_bmp_start[];

extern int g_dock_mag_enabled;
extern int g_dock_mag_level;


#define SETTINGS_DOCK_INDEX 4

static int is_open = 0;
static int minimized = 0;
static int win_x = 0, win_y = 0;
static int win_w = 520, win_h = 460;
static int positioned = 0;
static int dragging = 0;
static int drag_ox = 0, drag_oy = 0;
static genie_state_t genie;

static int dragging_slider = 0;  /* dock magnification level slider */

void toggle_settings_app(void)
{
    int dx, dy;
    genie_dock_icon_point(SETTINGS_DOCK_INDEX, &dx, &dy);

    if (genie_is_animating(&genie))
        genie_cancel(&genie);

    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0;
        dragging = 0;
        dragging_slider = 0;
        return;
    }

    if (is_open) {
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        minimized = 0;
        dragging = 0;
        dragging_slider = 0;
        return;
    }

    is_open = 1;
    minimized = 0;
    dragging = 0;
    dragging_slider = 0;
    genie_start_open(&genie, dx, dy);
}

void render_settings_app_window(
    uint32_t* buf,
    int scr_w,
    int scr_h,
    int mx,
    int my,
    int btn,
    int click
) {
    genie_tick(&genie);

    if (!is_open && !genie_is_animating(&genie))
        return;

    if (minimized && !genie_is_animating(&genie))
        return;

    if (!positioned) {
        win_x = (scr_w - win_w) / 2 + 60;
        win_y = (scr_h - win_h) / 2 + 60;
        positioned = 1;
    }

    int mid_genie = genie_is_animating(&genie);

    int header_h = 38;
    int traffic_zone_w = 84;

    if (!mid_genie && btn && !dragging && win_drag_available() &&
        mx >= win_x && mx <= win_x + win_w - traffic_zone_w &&
        my >= win_y && my <= win_y + header_h)
    {
        dragging = 1;
        drag_ox = mx - win_x;
        drag_oy = my - win_y;
        win_drag_claim();
        win_set_focused(WIN_ID_SETTINGS_);
    }
    if (!btn) dragging = 0;
    if (dragging) { win_x = mx - drag_ox; win_y = my - drag_oy; }

    /* BAG: клики по кнопкам заголовка, слайдеру громкости и превью
     * обоев реагировали, даже когда курсор физически над другим,
     * визуально более верхним окном -- клик "проваливался" сквозь
     * чужой title bar. Репортим свой прямоугольник и глушим клик при
     * перекрытии. */
    win_report_rect(WIN_ID_SETTINGS_, win_x, win_y, win_w, win_h, is_open && !mid_genie);
    int occluded = win_click_occluded(WIN_ID_SETTINGS_, mx, my);

    int draw_x, draw_y, draw_w, draw_h;
    genie_get_rect(&genie, win_x, win_y, win_w, win_h, &draw_x, &draw_y, &draw_w, &draw_h);

    int tl_size = 12, tl_gap = 8, tl_right_margin = 14;
    int tl_y = win_y + (header_h - tl_size) / 2;
    int close_x = win_x + win_w - tl_right_margin - tl_size;
    int minimize_x = close_x - tl_gap - tl_size;
    int zoom_x = minimize_x - tl_gap - tl_size;
    int hit_padding = 8;
    int hover_close = !mid_genie &&
        mx >= (close_x - hit_padding) && mx <= (close_x + tl_size + hit_padding) &&
        my >= (tl_y - hit_padding) && my <= (tl_y + tl_size + hit_padding);
    int hover_minimize = !mid_genie &&
        mx >= (minimize_x - hit_padding) && mx <= (minimize_x + tl_size + hit_padding) &&
        my >= (tl_y - hit_padding) && my <= (tl_y + tl_size + hit_padding);
    int hover_zoom = !mid_genie &&
        mx >= (zoom_x - hit_padding) && mx <= (zoom_x + tl_size + hit_padding) &&
        my >= (tl_y - hit_padding) && my <= (tl_y + tl_size + hit_padding);

    static const struct { int spread; int drop; uint8_t alpha; } shadows[] = {
        {14, 18, 8}, {11, 15, 12}, {8, 12, 16}, {5, 9, 22}, {2, 6, 30},
    };
    for (unsigned i = 0; i < sizeof(shadows) / sizeof(shadows[0]); i++) {
        int sp = shadows[i].spread;
        draw_rounded_rect_alpha(
            draw_x - sp / 2, draw_y - sp / 2 + shadows[i].drop,
            draw_w + sp, draw_h + sp, 18 + sp / 2, 0x00000000, shadows[i].alpha
        );
    }

    /* Основная панель -- светлая, как System Settings в Big Sur (в
     * отличие от тёмных Terminal/Calculator -- специально разный вид,
     * т.к. в реальном Big Sur системные настройки светлые по умолчанию) */
    draw_rounded_rect_buf(draw_x, draw_y, draw_w, draw_h, 18, 0x00F5F5F7);

    if (mid_genie) {
        dragging = 0;
        dragging_slider = 0;
        return; /* силуэт во время анимации */
    }

    draw_rounded_rect_alpha(win_x, win_y, win_w, header_h, 18, 0x00000000, 4);
    draw_rect_buf(win_x, win_y + header_h - 1, win_w, 1, 0x00E0E0E3);

    draw_rounded_rect_buf(zoom_x, tl_y, tl_size, tl_size, 4,
        hover_zoom ? 0x0028C93F : 0x00D0D0D3);
    draw_rounded_rect_buf(minimize_x, tl_y, tl_size, tl_size, 4,
        hover_minimize ? 0x00FFBD2E : 0x00D0D0D3);
    draw_rounded_rect_buf(close_x, tl_y, tl_size, tl_size, 4,
        hover_close ? 0x00FF5F57 : 0x00D0D0D3);

    if (click && hover_close && !occluded) {
        int dx, dy;
        genie_dock_icon_point(SETTINGS_DOCK_INDEX, &dx, &dy);
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        dragging = 0;
        return;
    }

    if (click && hover_minimize && !occluded) {
        int dx, dy;
        genie_dock_icon_point(SETTINGS_DOCK_INDEX, &dx, &dy);
        genie_start_minimize(&genie, dx, dy);
        minimized = 1;
        dragging = 0;
        return;
    }

    if (click && hover_zoom && !occluded) {
        static int pre_x, pre_y, pre_w, pre_h, is_zoomed = 0;
        if (!is_zoomed) {
            pre_x = win_x; pre_y = win_y; pre_w = win_w; pre_h = win_h;
            win_x = 40; win_y = 44; win_w = scr_w - 80; win_h = scr_h - 84;
            is_zoomed = 1;
        } else {
            win_x = pre_x; win_y = pre_y; win_w = pre_w; win_h = pre_h;
            is_zoomed = 0;
        }
        return;
    }

    /* ========== Dock magnification ========== */
    draw_string("Dock", win_x + 24, win_y + header_h + 20, 0x001D1D1F, buf, (uint32_t)scr_w);
    draw_string("Icon magnification", win_x + 24, win_y + header_h + 44, 0x006E6E73, buf, (uint32_t)scr_w);

    /* Toggle switch */
    int sw_x = win_x + win_w - 24 - 52;
    int sw_y = win_y + header_h + 40;
    int sw_w = 52, sw_h = 28;
    int sw_hover = mx >= sw_x && mx <= sw_x + sw_w && my >= sw_y && my <= sw_y + sw_h;

    draw_rounded_rect_buf(sw_x, sw_y, sw_w, sw_h, sw_h / 2,
        g_dock_mag_enabled ? 0x00007AFF : 0x00D0D0D3);
    int knob_d = 22;
    int knob_x = g_dock_mag_enabled ? (sw_x + sw_w - knob_d - 3) : (sw_x + 3);
    int knob_y = sw_y + (sw_h - knob_d) / 2;
    draw_rounded_rect_buf(knob_x, knob_y, knob_d, knob_d, knob_d / 2, 0x00FFFFFF);

    if (click && sw_hover && !occluded) {
        g_dock_mag_enabled = !g_dock_mag_enabled;
    }

    /* Strength slider (only meaningful when enabled, but always adjustable) */
    draw_string("Strength", win_x + 24, win_y + header_h + 88, 0x006E6E73, buf, (uint32_t)scr_w);

    int slider_x = win_x + 24;
    int slider_y = win_y + header_h + 114;
    int slider_w = win_w - 48;
    int slider_h = 6;

    draw_rounded_rect_buf(slider_x, slider_y, slider_w, slider_h, 3, 0x00D6D6DA);
    int fill_w = (slider_w * g_dock_mag_level) / 100;
    if (fill_w < 0) fill_w = 0;
    if (fill_w > slider_w) fill_w = slider_w;
    draw_rounded_rect_buf(slider_x, slider_y, fill_w, slider_h, 3,
        g_dock_mag_enabled ? 0x00007AFF : 0x00A0A0A5);

    int ksz = 18;
    int kx = slider_x + fill_w - ksz / 2;
    int ky = slider_y + slider_h / 2 - ksz / 2;
    draw_rounded_rect_alpha(kx - 2, ky - 1, ksz + 4, ksz + 4, (ksz + 4) / 2, 0x00000000, 40);
    draw_rounded_rect_buf(kx, ky, ksz, ksz, ksz / 2, 0x00FFFFFF);

    int knob_hit =
        mx >= slider_x - 10 && mx <= slider_x + slider_w + 10 &&
        my >= slider_y - 12 && my <= slider_y + slider_h + 12;

    if (btn && (dragging_slider || knob_hit) && !dragging && !occluded) {
        dragging_slider = 1;
        int rel = mx - slider_x;
        if (rel < 0) rel = 0;
        if (rel > slider_w) rel = slider_w;
        g_dock_mag_level = (rel * 100) / slider_w;
    }
    if (!btn) dragging_slider = 0;

    /* percent label */
    {
        char lbl[8];
        int v = g_dock_mag_level, i = 0, j = 0;
        char tmp[4];
        if (v == 0) tmp[i++] = '0';
        while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
        while (i > 0) lbl[j++] = tmp[--i];
        lbl[j++] = '%';
        lbl[j] = 0;
        draw_string(lbl, slider_x + slider_w - 34, slider_y - 22, 0x006E6E73, buf, (uint32_t)scr_w);
    }

    draw_rect_buf(win_x + 24, win_y + header_h + 150, win_w - 48, 1, 0x00E0E0E3);

    /* ========== Wallpaper (Day / Night only) ========== */
    draw_string("Wallpaper", win_x + 24, win_y + header_h + 168, 0x001D1D1F, buf, (uint32_t)scr_w);

    typedef struct {
        const uint8_t *bmp;
        const char *label;
        int choice_id;
    } wallpaper_option_t;

    wallpaper_option_t options[2] = {
        { wallpaper_day_bmp_start,   "Day",   1 },
        { wallpaper_night_bmp_start, "Night", 2 },
    };

    int thumb_w = 140, thumb_h = 78, thumb_gap = 20;
    int thumbs_y = win_y + header_h + 198;

    for (int i = 0; i < 2; i++) {
        int tx = win_x + 24 + i * (thumb_w + thumb_gap);

        int hover =
            mx >= tx && mx <= tx + thumb_w &&
            my >= thumbs_y && my <= thumbs_y + thumb_h;

        int selected = (g_wallpaper_choice == options[i].choice_id);

        uint32_t border_color = selected ? 0x00007AFF : (hover ? 0x00B0B0B5 : 0x00D6D6DA);
        int border_w = selected ? 3 : 1;

        draw_rounded_rect_buf(tx - border_w, thumbs_y - border_w,
            thumb_w + border_w * 2, thumb_h + border_w * 2, 10, border_color);

        if (options[i].bmp) {
            draw_bmp_stretched_at(options[i].bmp, tx, thumbs_y, thumb_w, thumb_h, buf, scr_w);
        } else {
            draw_rounded_rect_buf(tx, thumbs_y, thumb_w, thumb_h, 8, 0x00202024);
        }

        draw_string(options[i].label, tx, thumbs_y + thumb_h + 6, 0x006E6E73, buf, (uint32_t)scr_w);

        if (click && hover && !occluded) {
            g_wallpaper_choice = options[i].choice_id;
        }
    }

    draw_rect_buf(win_x + 24, thumbs_y + thumb_h + 30, win_w - 48, 1, 0x00E0E0E3);

    draw_string("IgorOS Nord", win_x + 24, thumbs_y + thumb_h + 48, 0x001D1D1F, buf, (uint32_t)scr_w);
    draw_string("Built on the IgorOS 64-bit kernel", win_x + 24, thumbs_y + thumb_h + 72, 0x006E6E73, buf, (uint32_t)scr_w);
}
