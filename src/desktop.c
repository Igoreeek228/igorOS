// src/desktop.c

#include "gui/desktop.h"
#include "gui/bmp_loader.h"
#include "gui/font.h"
#include "gui/wallpaper/wallpaper.h"
#include "gui/cursor/cursor.h"
#include "gui/apps/file/file_manager.h"
#include "gui/apps/music/music_app.h"
#include "gui/apps/about/about_app.h"
#include "gui/apps/calc/calc_app.h"
#include "gui/apps/terminal/terminal_app.h"
#include "gui/apps/settings/settings_app.h"
#include "drivers/system/keyboard.h"
#include "drivers/system/mouse.h"
#include "timer.h"

extern void toggle_file_manager(void) __attribute__((weak));
extern void toggle_doom_app(void) __attribute__((weak));
extern void doom_app_close(void) __attribute__((weak));
extern void doom_app_feed_key(char key) __attribute__((weak));
extern void doom_app_feed_scancode(uint8_t sc, int ext, int pressed) __attribute__((weak));
extern int doom_app_is_open(void) __attribute__((weak));
extern void render_doom_app_window(uint32_t*, int, int, int, int, int, int) __attribute__((weak));

extern const uint8_t file_icon_bmp_start[] __attribute__((weak));
extern const uint8_t music_icon_bmp_start[] __attribute__((weak));
extern const uint8_t start_icon_bmp_start[] __attribute__((weak));
extern const uint8_t volume_bmp_start[] __attribute__((weak));
extern const uint8_t wallpaper_bmp_start[] __attribute__((weak));

/* IgorOS Nord: новые иконки в стиле Big Sur (см. kernel/resources.asm) */
extern const uint8_t doom_icon_bmp_start[] __attribute__((weak));
extern const uint8_t calc_icon_bmp_start[] __attribute__((weak));
extern const uint8_t notes_icon_bmp_start[] __attribute__((weak));
extern const uint8_t settings_icon_bmp_start[] __attribute__((weak));
extern const uint8_t terminal_icon_bmp_start[] __attribute__((weak));

/* IgorOS Nord: выбор обоев в Settings (см. kernel/resources.asm).
 * g_wallpaper_choice: 0 = Waves, 1 = Day (default), 2 = Night. */
extern const uint8_t wallpaper_day_bmp_start[] __attribute__((weak));
extern const uint8_t wallpaper_night_bmp_start[] __attribute__((weak));
int g_wallpaper_choice = 1;

/* Dock magnification (Settings → Dock).
 * enabled: 0 = off (fixed BASE size), 1 = on
 * level:   0..100 — how strong the grow is (default mild) */
int g_dock_mag_enabled = 1;
int g_dock_mag_level   = 55;  /* 0..100 */


const uint8_t* current_wallpaper_bmp(void) {
    switch (g_wallpaper_choice) {
        case 1: return wallpaper_day_bmp_start;
        case 2: return wallpaper_night_bmp_start;
        default: return wallpaper_bmp_start;
    }
}

static uint32_t scr_width  = 1280;
static uint32_t scr_height = 720;
static uint32_t scr_pitch  = 5120;
static uint32_t scr_bpp    = 32;

static uint8_t* frontbuffer = (uint8_t*)0xA0000;

#define MAX_WIDTH  2560
#define MAX_HEIGHT 1440

static uint32_t backbuffer[MAX_WIDTH * MAX_HEIGHT];
static uint32_t bg_buffer[MAX_WIDTH * MAX_HEIGHT];

static void desktop_swap_buffers(void);

#define COLOR_TOPBAR             0x00F6F6F6
#define COLOR_TOPBAR_BORDER     0x00D1D1D6
#define COLOR_BLACK             0x00000000
#define COLOR_WHITE             0x00FFFFFF
#define COLOR_DOCK_BG           0x00FFFFFF
#define COLOR_DOCK_BORDER       0x00E5E5EA
#define COLOR_TOOLTIP_BG        0x001C1C1E
#define COLOR_MENU_BG           0x00F0F0F3
#define COLOR_ACCENT            0x00007AFF
#define COLOR_MENU_ITEM_HOVER   0x00E1E1E6
#define COLOR_MENU_ITEM_PRESSED 0x00CACACF

typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} __attribute__((packed)) bmp_header_t;

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
} __attribute__((packed)) bmp_info_header_t;

typedef struct {
    const char* title;
    const uint8_t* bmp_data;
    uint32_t fallback_color;
} dock_app_t;

static dock_app_t dock_apps[] = {
    {"File",       file_icon_bmp_start,      0x00007AFF},
    {"Terminal",   terminal_icon_bmp_start,  0x001C1C1E},
    {"DOOM",       doom_icon_bmp_start,      0x00B22222},
    {"Calculator", calc_icon_bmp_start,      0x00FF9500},
    {"Settings",   settings_icon_bmp_start,  0x008E8E93},
    {"Music",      music_icon_bmp_start,     0x00FF2D55}
};

static int app_count =
    sizeof(dock_apps) / sizeof(dock_app_t);

static int prev_mouse_left = 0;

/*
 * Window drag arbitration.
 *
 * БАГ: раньше каждое окно (file manager / music / about / calc / terminal /
 * settings) независимо проверяло "мышь зажата и курсор над МОЕЙ шапкой" и
 * само себе выставляло dragging=1. Если шапки двух окон перекрывались на
 * экране (что нормально при нескольких открытых окнах), клик по видимой
 * верхней шапке одновременно попадал в проверку и у нижнего окна тоже --
 * оба окна одновременно начинали "тащиться" под одним и тем же курсором.
 * Так как рендерятся окна всегда в одном и том же фиксированном порядке
 * (file, music, about, calc, terminal, settings), а не в порядке
 * последнего клика, окно, которое визуально должно быть "сверху", могло
 * реально не быть тем, что реагирует -- отсюда ощущение "не все окна
 * перемещаются".
 *
 * Фикс: один общий флаг на кадр. Первое окно (в порядке рендера), у
 * которого курсор оказался над его шапкой в момент начала клика, "бронирует"
 * этот клик себе через win_drag_try_claim() -- остальные окна в этом же
 * кадре уже не могут начать перетаскивание, даже если их шапка тоже
 * технически перекрывает курсор.
 */
static int g_drag_claimed_this_press = 0;

/*
 * Window z-order.
 *
 * БАГ №2: даже после фикса драг-арбитража окна всё ещё рисовались в
 * фиксированном порядке (file, music, about, calc, terminal, settings),
 * так что settings всегда визуально поверх остальных, а окно, которое
 * пользователь только что схватил и тащит, могло оказаться под другим --
 * то самое "половина закрывает другую".
 *
 * Фикс: общий z-order массив из id окон. Рендерим окна в этом порядке
 * (первый в массиве -- в самом низу, последний -- поверх всех). Когда
 * окно "забирает" клик через win_drag_claim(), оно же поднимается в
 * конец массива z-order, т.е. становится активным и рисуется поверх
 * остальных -- как в любой нормальной ОС.
 *
 * g_current_render_id выставляется перед каждым вызовом render_*_window
 * в основном цикле (см. WIN_ID_* и render_windows_in_z_order() ниже),
 * чтобы win_drag_claim() знал, ЧЬЁ окно сейчас забирает клик, не меняя
 * сигнатуру существующих app-модулей.
 */
#define WIN_COUNT 7
enum { WIN_ID_FILE = 0, WIN_ID_MUSIC, WIN_ID_ABOUT, WIN_ID_CALC, WIN_ID_TERMINAL, WIN_ID_SETTINGS, WIN_ID_DOOM };

static int g_win_z_order[WIN_COUNT] = { WIN_ID_FILE, WIN_ID_MUSIC, WIN_ID_ABOUT, WIN_ID_CALC, WIN_ID_TERMINAL, WIN_ID_SETTINGS, WIN_ID_DOOM };
static int g_current_render_id = -1;

static void win_bring_to_front(int id)
{
    int pos = -1;
    for (int i = 0; i < WIN_COUNT; i++) {
        if (g_win_z_order[i] == id) { pos = i; break; }
    }
    if (pos < 0) return;
    if (pos != WIN_COUNT - 1) {
        for (int i = pos; i < WIN_COUNT - 1; i++) g_win_z_order[i] = g_win_z_order[i + 1];
        g_win_z_order[WIN_COUNT - 1] = id;
    }
    win_set_focused(id);
}


/* Вызывать в начале каждого кадра, до рендера окон. Не static -- нужна
 * только внутри desktop.c (единственное место вызова), но объявлена без
 * inline, чтобы не тянуть за собой отдельный static inline в .h. */
static void win_drag_frame_begin(int btn)
{
    if (!btn)
        g_drag_claimed_this_press = 0;
}

/* true, если клик ещё никем не занят в этом кадре -- окно может начать
 * dragging. Если окно решает начать перетаскивание, оно должно сразу же
 * вызвать win_drag_claim(). Объявлены в gui/desktop.h, вызываются из
 * всех app-модулей (file_manager.c, calc_app.c, и т.д.). */
int win_drag_available(void)
{
    return !g_drag_claimed_this_press;
}

void win_drag_claim(void)
{
    g_drag_claimed_this_press = 1;
    if (g_current_render_id >= 0)
        win_bring_to_front(g_current_render_id);
}

/*
 * Occlusion registry -- see the big comment on win_report_rect() /
 * win_click_occluded() in desktop.h for the bug this fixes.
 *
 * Каждое окно репортит свои границы РАЗ ЗА КАДР (когда оно реально
 * открыто) через win_report_rect(). Это накапливается в
 * g_win_rect[WIN_COUNT] вместе с z-позицией (индексом в g_win_z_order на
 * момент репорта). win_click_occluded(id, mx, my) затем ищет: есть ли
 * СРЕДИ ОТКРЫТЫХ окно, чей z-индекс строго больше, чем у "id", и чей
 * прямоугольник содержит (mx, my) -- если да, значит курсор физически
 * находится над визуально более верхним окном, и "id" не должен
 * реагировать на клик в этой точке в этом кадре.
 */
typedef struct { int x, y, w, h, open; } win_rect_t;
static win_rect_t g_win_rect[WIN_COUNT];

void win_report_rect(int id, int x, int y, int w, int h, int is_open)
{
    if (id < 0 || id >= WIN_COUNT) return;
    g_win_rect[id].x = x;
    g_win_rect[id].y = y;
    g_win_rect[id].w = w;
    g_win_rect[id].h = h;
    g_win_rect[id].open = is_open;
}

int win_is_open(int id)
{
    if (id < 0 || id >= WIN_COUNT) return 0;
    return g_win_rect[id].open;
}

static int g_focused_win = -1;

void win_set_focused(int id) { g_focused_win = id; }
int win_is_focused(int id) { return g_focused_win == id; }


static int win_z_index_of(int id)
{
    for (int i = 0; i < WIN_COUNT; i++)
        if (g_win_z_order[i] == id) return i;
    return -1;
}

int win_click_occluded(int id, int mx, int my)
{
    int my_z = win_z_index_of(id);
    if (my_z < 0) return 0;

    for (int i = 0; i < WIN_COUNT; i++) {
        int other = g_win_z_order[i];
        if (other == id) continue;
        if (i <= my_z) continue; /* окна с меньшим z-индексом ниже нас, не перекрывают */
        if (!g_win_rect[other].open) continue;

        int ox = g_win_rect[other].x, oy = g_win_rect[other].y;
        int ow = g_win_rect[other].w, oh = g_win_rect[other].h;

        if (mx >= ox && mx < ox + ow && my >= oy && my < oy + oh)
            return 1; /* курсор над более верхним открытым окном */
    }
    return 0;
}

static int current_hover_idx = -1;
static int tooltip_alpha = 0;

static int start_menu_open = 0;
static int volume_popup_open = 0;
static int current_volume = 70;

static int cached_h = 0;
static int cached_m = 0;
static int cached_s = 0;

static int cached_day = 1;
static int cached_month = 1;
static int cached_year = 2026;

static uint64_t next_rtc_update_ms = 0;


/* ============================================================
   PORT I/O
   ============================================================ */

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__(
        "outb %0, %1"
        :
        : "a"(val), "Nd"(port)
    );
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;

    __asm__ __volatile__(
        "inb %1, %0"
        : "=a"(ret)
        : "Nd"(port)
    );

    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ __volatile__(
        "outw %0, %1"
        :
        : "a"(val), "Nd"(port)
    );
}


/* ============================================================
   SHUTDOWN
   ============================================================ */

void sys_shutdown(void)
{
    uint32_t total_pixels =
        scr_width * scr_height;

    if (total_pixels >
        (uint32_t)(MAX_WIDTH * MAX_HEIGHT))
    {
        total_pixels =
            MAX_WIDTH * MAX_HEIGHT;
    }

    for (uint32_t i = 0; i < total_pixels; i++)
    {
        backbuffer[i] = 0x00000000;
    }

    const char* msg1 =
        "igorOS has shut down.";

    const char* msg2 =
        "It is now safe to close this window.";

    draw_string(
        msg1,
        ((int)scr_width -
         font_text_width(msg1)) / 2,
        (int)scr_height / 2 - 14,
        0x00FFFFFF,
        backbuffer,
        scr_width
    );

    draw_string(
        msg2,
        ((int)scr_width -
         font_text_width(msg2)) / 2,
        (int)scr_height / 2 + 6,
        0x008E8E93,
        backbuffer,
        scr_width
    );

    desktop_swap_buffers();

    /*
     * QEMU / Bochs / VirtualBox
     */

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    __asm__ __volatile__("cli");

    for (;;)
    {
        __asm__ __volatile__("hlt");
    }
}


/* ============================================================
   RTC
   ============================================================ */

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static uint8_t get_rtc_register(int reg)
{
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

static int is_rtc_updating(void)
{
    outb(CMOS_ADDRESS, 0x0A);

    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t bcd2bin(uint8_t val)
{
    return ((val / 16) * 10) +
           (val % 16);
}

static void update_rtc_cache(void)
{
    if (is_rtc_updating())
        return;

    uint8_t sec =
        get_rtc_register(0x00);

    uint8_t min =
        get_rtc_register(0x02);

    uint8_t hour =
        get_rtc_register(0x04);

    uint8_t d =
        get_rtc_register(0x07);

    uint8_t mo =
        get_rtc_register(0x08);

    uint8_t yr =
        get_rtc_register(0x09);

    uint8_t regB =
        get_rtc_register(0x0B);

    if (!(regB & 0x04))
    {
        sec  = bcd2bin(sec);
        min  = bcd2bin(min);
        hour = bcd2bin(hour);
        d    = bcd2bin(d);
        mo   = bcd2bin(mo);
        yr   = bcd2bin(yr);
    }

    cached_h = hour;
    cached_m = min;
    cached_s = sec;

    cached_day = d;
    cached_month = mo;
    cached_year = 2000 + yr;
}


/* ============================================================
   COLOR BLENDING
   ============================================================ */

uint32_t blend_colors(
    uint32_t bg,
    uint32_t fg,
    uint8_t alpha
)
{
    uint8_t r_bg =
        (bg >> 16) & 0xFF;

    uint8_t g_bg =
        (bg >> 8) & 0xFF;

    uint8_t b_bg =
        bg & 0xFF;

    uint8_t r_fg =
        (fg >> 16) & 0xFF;

    uint8_t g_fg =
        (fg >> 8) & 0xFF;

    uint8_t b_fg =
        fg & 0xFF;

    uint8_t r =
        (r_fg * alpha +
         r_bg * (255 - alpha)) / 255;

    uint8_t g =
        (g_fg * alpha +
         g_bg * (255 - alpha)) / 255;

    uint8_t b =
        (b_fg * alpha +
         b_bg * (255 - alpha)) / 255;

    return
        (r << 16) |
        (g << 8) |
        b;
}


/* ============================================================
   PIXELS
   ============================================================ */

void draw_pixel_buf(
    int x,
    int y,
    uint32_t color
)
{
    if (x < 0 ||
        x >= (int)scr_width ||
        y < 0 ||
        y >= (int)scr_height)
    {
        return;
    }

    uint32_t idx =
        y * scr_width + x;

    if (idx <
        (MAX_WIDTH * MAX_HEIGHT))
    {
        backbuffer[idx] = color;
    }
}

// Смешивает пиксель с уже нарисованным фоном по alpha — в отличие от
// draw_pixel_buf (жёсткая перезапись), используется на краях скруглённых
// фигур, чтобы получить сглаживание (antialiasing) вместо "лесенки".
static void draw_pixel_blend(int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || x >= (int)scr_width || y < 0 || y >= (int)scr_height) return;
    uint32_t idx = y * scr_width + x;
    if (idx < (MAX_WIDTH * MAX_HEIGHT)) {
        backbuffer[idx] = blend_colors(backbuffer[idx], color, alpha);
    }
}


/* ============================================================
   RECTANGLE
   ============================================================ */

void draw_rect_buf(
    int x,
    int y,
    int w,
    int h,
    uint32_t color
)
{
    if (w <= 0 || h <= 0)
        return;

    for (int i = 0; i < h; i++)
    {
        for (int j = 0; j < w; j++)
        {
            draw_pixel_buf(
                x + j,
                y + i,
                color
            );
        }
    }
}


/* ============================================================
   ROUNDED RECTANGLE
   ============================================================ */

void draw_rounded_rect_buf(
    int x,
    int y,
    int w,
    int h,
    int r,
    uint32_t color
)
{
    if (w <= 0 || h <= 0)
        return;

    if (r < 1)
        r = 1;

    if (r * 2 > w)
        r = w / 2;

    if (r * 2 > h)
        r = h / 2;

    for (int i = 0; i < h; i++)
    {
        for (int j = 0; j < w; j++)
        {
            int rx = -1;
            int ry = -1;

            if (j < r && i < r)
            {
                rx = r - j - 1;
                ry = r - i - 1;
            }
            else if (
                j >= w - r &&
                i < r
            )
            {
                rx = j - (w - r);
                ry = r - i - 1;
            }
            else if (
                j < r &&
                i >= h - r
            )
            {
                rx = r - j - 1;
                ry = i - (h - r);
            }
            else if (
                j >= w - r &&
                i >= h - r
            )
            {
                rx = j - (w - r);
                ry = i - (h - r);
            }

            if (rx != -1 &&
                ry != -1)
            {
                int dist_sq = rx * rx + ry * ry;
                int r_sq = r * r;
                if (dist_sq <= r_sq)
                {
                    draw_pixel_buf(
                        x + j,
                        y + i,
                        color
                    );
                }
                else
                {
                    // Полупрозрачное кольцо в 1px сразу за жёсткой границей
                    // круга — дешёвая замена честному антиалиасингу: без
                    // него угол выглядит зубчатым ("лесенка" из пикселей).
                    int r_outer_sq = (r + 1) * (r + 1);
                    if (dist_sq <= r_outer_sq)
                    {
                        draw_pixel_blend(x + j, y + i, color, 110);
                    }
                }
            }
            else
            {
                draw_pixel_buf(
                    x + j,
                    y + i,
                    color
                );
            }
        }
    }
}


/* ============================================================
   ALPHA ROUNDED RECTANGLE
   ============================================================ */

void draw_rounded_rect_alpha(
    int x,
    int y,
    int w,
    int h,
    int r,
    uint32_t color,
    uint8_t alpha
)
{
    if (w <= 0 || h <= 0)
        return;

    for (int i = 0; i < h; i++)
    {
        for (int j = 0; j < w; j++)
        {
            int px = x + j;
            int py = y + i;

            if (
                px < 0 ||
                px >= (int)scr_width ||
                py < 0 ||
                py >= (int)scr_height
            )
            {
                continue;
            }

            int rx = -1;
            int ry = -1;

            if (j < r && i < r)
            {
                rx = r - j - 1;
                ry = r - i - 1;
            }
            else if (
                j >= w - r &&
                i < r
            )
            {
                rx = j - (w - r);
                ry = r - i - 1;
            }
            else if (
                j < r &&
                i >= h - r
            )
            {
                rx = r - j - 1;
                ry = i - (h - r);
            }
            else if (
                j >= w - r &&
                i >= h - r
            )
            {
                rx = j - (w - r);
                ry = i - (h - r);
            }

            int dist_sq = -1;
            if (rx != -1 && ry != -1) dist_sq = rx * rx + ry * ry;

            uint8_t pixel_alpha = alpha;
            if (dist_sq != -1)
            {
                int r_sq = r * r;
                if (dist_sq > r_sq)
                {
                    int r_outer_sq = (r + 1) * (r + 1);
                    if (dist_sq > r_outer_sq)
                    {
                        continue; // за пределами даже сглаженного кольца
                    }
                    pixel_alpha = (uint8_t)(alpha / 2); // сглаживающее полукольцо
                }
            }

            uint32_t idx =
                py * scr_width + px;

            if (
                idx <
                (MAX_WIDTH * MAX_HEIGHT)
            )
            {
                backbuffer[idx] =
                    blend_colors(
                        backbuffer[idx],
                        color,
                        pixel_alpha
                    );
            }
        }
    }
}


/* ============================================================
   BMP DRAW
   ============================================================ */

static void draw_scaled_bmp_rounded(
    const uint8_t* bmp_data,
    int x,
    int y,
    int target_w,
    int target_h,
    int r
)
{
    /* Fast nearest-neighbor scale + simple rounded mask.
     * Bilinear was O(w*h*4 samples) every frame on every dock icon → heavy lag. */
    if (!bmp_data || target_w <= 0 || target_h <= 0)
        return;

    bmp_header_t* header = (bmp_header_t*)bmp_data;
    if (header->type != 0x4D42)
        return;

    bmp_info_header_t* info =
        (bmp_info_header_t*)(bmp_data + sizeof(bmp_header_t));

    int img_w = info->width;
    int img_h = info->height < 0 ? -info->height : info->height;
    if (img_w <= 0 || img_h <= 0)
        return;

    int is_bottom_up = info->height > 0;
    uint16_t bpp = info->bpp;
    const uint8_t* pixels = bmp_data + header->offset;
    int bytes_pp = bpp / 8;
    if (bytes_pp < 3 || bytes_pp > 4)
        return;

    int row_stride = ((img_w * bytes_pp + 3) / 4) * 4;
    if (r < 0) r = 0;
    if (r * 2 > target_w) r = target_w / 2;
    if (r * 2 > target_h) r = target_h / 2;

    for (int cy = 0; cy < target_h; cy++) {
        int src_y = (cy * img_h) / target_h;
        if (src_y >= img_h) src_y = img_h - 1;
        int py = is_bottom_up ? (img_h - 1 - src_y) : src_y;
        const uint8_t* row = pixels + py * row_stride;

        for (int cx = 0; cx < target_w; cx++) {
            if (r > 0) {
                int rx = -1, ry = -1;
                if (cx < r && cy < r) { rx = r - cx - 1; ry = r - cy - 1; }
                else if (cx >= target_w - r && cy < r) { rx = cx - (target_w - r); ry = r - cy - 1; }
                else if (cx < r && cy >= target_h - r) { rx = r - cx - 1; ry = cy - (target_h - r); }
                else if (cx >= target_w - r && cy >= target_h - r) { rx = cx - (target_w - r); ry = cy - (target_h - r); }
                if (rx >= 0 && ry >= 0 && rx * rx + ry * ry > r * r)
                    continue;
            }

            int src_x = (cx * img_w) / target_w;
            if (src_x >= img_w) src_x = img_w - 1;
            const uint8_t* p = row + src_x * bytes_pp;
            uint8_t b = p[0], g = p[1], rv = p[2];
            if (rv > 240 && g < 15 && b > 240) continue;
            if (bpp == 32 && p[3] < 32) continue;
            draw_pixel_buf(x + cx, y + cy, ((uint32_t)rv << 16) | ((uint32_t)g << 8) | b);
        }
    }
}

/* ============================================================
   BUFFER SWAP
   ============================================================ */

static void desktop_swap_buffers(void)
{
    uint32_t bytes_per_pixel = scr_bpp / 8;
    if (bytes_per_pixel == 0) bytes_per_pixel = 4;

    uint32_t copy_height = scr_height;
    uint32_t copy_width = scr_width;
    if (copy_height > MAX_HEIGHT) copy_height = MAX_HEIGHT;
    if (copy_width > MAX_WIDTH) copy_width = MAX_WIDTH;

    if (bytes_per_pixel == 4) {
        uint32_t copy_words = copy_width;
        uint32_t copy_qwords = copy_words / 2;
        for (uint32_t row = 0; row < copy_height; ++row) {
            uint8_t *dst = frontbuffer + (uint64_t)row * scr_pitch;
            const uint32_t *src = backbuffer + (uint64_t)row * scr_width;
            uint64_t *dst64 = (uint64_t *)dst;
            const uint64_t *src64 = (const uint64_t *)src;

            for (uint32_t i = 0; i < copy_qwords; ++i)
                dst64[i] = src64[i];

            if (copy_words & 1U)
                ((uint32_t *)dst)[copy_words - 1] = src[copy_words - 1];
        }
    } else if (bytes_per_pixel == 3) {
        for (uint32_t row = 0; row < copy_height; ++row) {
            uint8_t *dst = frontbuffer + (uint64_t)row * scr_pitch;
            const uint32_t *src = backbuffer + (uint64_t)row * scr_width;
            for (uint32_t col = 0; col < copy_width; ++col) {
                uint32_t px = src[col];
                dst[col * 3 + 0] = (uint8_t)(px & 0xFF);
                dst[col * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);
                dst[col * 3 + 2] = (uint8_t)((px >> 16) & 0xFF);
            }
        }
    }
}


/* ============================================================
   BACKGROUND
   ============================================================ */

void render_layer_background(void)
{
    uint32_t total_pixels = scr_width * scr_height;
    if (total_pixels > MAX_WIDTH * MAX_HEIGHT)
        total_pixels = MAX_WIDTH * MAX_HEIGHT;

    uint32_t qwords = total_pixels / 2;
    uint64_t *dst64 = (uint64_t *)backbuffer;
    const uint64_t *src64 = (const uint64_t *)bg_buffer;
    for (uint32_t i = 0; i < qwords; ++i)
        dst64[i] = src64[i];

    if (total_pixels & 1U)
        backbuffer[total_pixels - 1] = bg_buffer[total_pixels - 1];
}


/* ============================================================
   START MENU
   ============================================================ */

void render_start_menu(int single_click)
{
    if (!start_menu_open)
        return;

    int menu_w = 180;
    int menu_h = 90;

    int menu_x = 4;
    int menu_y = 28;

    draw_rounded_rect_buf(
        menu_x,
        menu_y,
        menu_w,
        menu_h,
        8,
        COLOR_MENU_BG
    );

    draw_rounded_rect_alpha(
        menu_x - 1,
        menu_y - 1,
        menu_w + 2,
        menu_h + 2,
        9,
        COLOR_TOPBAR_BORDER,
        180
    );

    int item_x =
        menu_x + 6;

    int item_w =
        menu_w - 12;

    int item_h = 30;
    int gap = 6;

    int about_y =
        menu_y + 6;

    int shutdown_y =
        about_y +
        item_h +
        gap;

    int mouse_over_about =
        (
            mouse_x >= item_x &&
            mouse_x <= item_x + item_w &&
            mouse_y >= about_y &&
            mouse_y <= about_y + item_h
        );

    int mouse_over_shutdown =
        (
            mouse_x >= item_x &&
            mouse_x <= item_x + item_w &&
            mouse_y >= shutdown_y &&
            mouse_y <= shutdown_y + item_h
        );

    /*
     * About
     */

    if (mouse_over_about)
    {
        uint32_t bg =
            mouse_left_clicked
                ? COLOR_MENU_ITEM_PRESSED
                : COLOR_MENU_ITEM_HOVER;

        draw_rounded_rect_buf(
            item_x,
            about_y,
            item_w,
            item_h,
            10,
            bg
        );
    }

    draw_string(
        "About System",
        item_x + 8,
        about_y + 7,
        COLOR_BLACK,
        backbuffer,
        scr_width
    );

    /*
     * Shutdown
     */

    if (mouse_over_shutdown)
    {
        uint32_t bg =
            mouse_left_clicked
                ? COLOR_MENU_ITEM_PRESSED
                : COLOR_MENU_ITEM_HOVER;

        draw_rounded_rect_buf(
            item_x,
            shutdown_y,
            item_w,
            item_h,
            10,
            bg
        );
    }

    draw_string(
        "Shut Down...",
        item_x + 8,
        shutdown_y + 7,
        0x00FF3B30,
        backbuffer,
        scr_width
    );

    if (single_click)
    {
        if (mouse_over_about)
        {
            start_menu_open = 0;

            toggle_about_app();
            win_bring_to_front(WIN_ID_ABOUT);
        }
        else if (mouse_over_shutdown)
        {
            sys_shutdown();
        }
    }
}


/* ============================================================
   VOLUME POPUP
   ============================================================ */

void render_volume_popup(void)
{
    if (!volume_popup_open)
        return;

    int pop_w = 180;
    int pop_h = 50;

    int pop_x =
        scr_width - 240;

    int pop_y = 28;

    draw_rounded_rect_buf(
        pop_x,
        pop_y,
        pop_w,
        pop_h,
        8,
        COLOR_MENU_BG
    );

    draw_rounded_rect_alpha(
        pop_x - 1,
        pop_y - 1,
        pop_w + 2,
        pop_h + 2,
        9,
        COLOR_TOPBAR_BORDER,
        180
    );

    draw_string(
        "Vol:",
        pop_x + 10,
        pop_y + 18,
        COLOR_BLACK,
        backbuffer,
        scr_width
    );

    int track_x =
        pop_x + 48;

    int track_y =
        pop_y + 22;

    int track_w = 100;
    int track_h = 6;

    draw_rounded_rect_buf(
        track_x,
        track_y,
        track_w,
        track_h,
        3,
        COLOR_TOPBAR_BORDER
    );

    int fill_w =
        (track_w * current_volume) /
        100;

    if (fill_w > 0)
    {
        draw_rounded_rect_buf(
            track_x,
            track_y,
            fill_w,
            track_h,
            3,
            COLOR_ACCENT
        );
    }

    int thumb_x =
        track_x +
        fill_w -
        4;

    draw_rounded_rect_buf(
        thumb_x,
        track_y - 3,
        10,
        12,
        4,
        COLOR_BLACK
    );

    if (mouse_left_clicked)
    {
        if (
            mouse_x >= track_x &&
            mouse_x <= track_x + track_w &&
            mouse_y >= pop_y + 5 &&
            mouse_y <= pop_y + pop_h - 5
        )
        {
            int new_vol =
                (
                    (mouse_x - track_x) *
                    100
                ) / track_w;

            if (new_vol < 0)
                new_vol = 0;

            if (new_vol > 100)
                new_vol = 100;

            current_volume =
                new_vol;
        }
    }
}


/* ============================================================
   TOP BAR
   ============================================================ */

void render_layer_topbar(int single_click)
{
    /*
     * Big Sur: строка меню полупрозрачная (просвечивает обоями), без
     * жёсткой линии-разделителя внизу -- в отличие от Catalina/Mac OS X,
     * где топбар был сплошным и отделялся тонкой чёткой границей.
     */

    draw_rounded_rect_alpha(
        0,
        0,
        scr_width,
        24,
        0,
        COLOR_TOPBAR,
        190
    );

    /*
     * Start icon
     */

    int start_btn_x = 8;
    int start_btn_y = 3;

    int start_btn_w = 18;
    int start_btn_h = 18;

    if (start_icon_bmp_start)
    {
        draw_scaled_bmp_rounded(
            start_icon_bmp_start,
            start_btn_x,
            start_btn_y,
            start_btn_w,
            start_btn_h,
            5
        );
    }
    else
    {
        draw_rounded_rect_buf(
            start_btn_x,
            start_btn_y,
            start_btn_w,
            start_btn_h,
            5,
            COLOR_BLACK
        );
    }

    draw_string(
        "igorOS",
        32,
        4,
        COLOR_BLACK,
        backbuffer,
        scr_width
    );

    /*
     * Start button click
     */

    if (
        single_click &&
        mouse_x >= 0 &&
        mouse_x <= 90 &&
        mouse_y >= 0 &&
        mouse_y <= 24
    )
    {
        start_menu_open =
            !start_menu_open;

        volume_popup_open = 0;
    }

    /*
     * RTC
     */

    uint64_t now_ms = timer_millis();
    if (now_ms >= next_rtc_update_ms)
    {
        update_rtc_cache();
        next_rtc_update_ms = now_ms + 1000ULL;
    }

    char datetime_str[20];

    datetime_str[0] =
        '0' +
        (cached_day / 10);

    datetime_str[1] =
        '0' +
        (cached_day % 10);

    datetime_str[2] = '.';

    datetime_str[3] =
        '0' +
        (cached_month / 10);

    datetime_str[4] =
        '0' +
        (cached_month % 10);

    datetime_str[5] = ' ';

    datetime_str[6] =
        '0' +
        (cached_h / 10);

    datetime_str[7] =
        '0' +
        (cached_h % 10);

    datetime_str[8] = ':';

    datetime_str[9] =
        '0' +
        (cached_m / 10);

    datetime_str[10] =
        '0' +
        (cached_m % 10);

    datetime_str[11] = 0;

    int clock_x =
        scr_width - 125;

    draw_string(
        datetime_str,
        clock_x,
        4,
        COLOR_BLACK,
        backbuffer,
        scr_width
    );

    /*
     * Volume
     */

    int vol_btn_x =
        scr_width - 205;

    int vol_btn_y = 3;

    int vol_btn_w = 18;
    int vol_btn_h = 18;

    if (volume_bmp_start)
    {
        draw_scaled_bmp_rounded(
            volume_bmp_start,
            vol_btn_x,
            vol_btn_y,
            vol_btn_w,
            vol_btn_h,
            5
        );
    }
    else
    {
        draw_rounded_rect_buf(
            vol_btn_x,
            vol_btn_y,
            vol_btn_w,
            vol_btn_h,
            5,
            0x008E8E93
        );
    }

    char vol_str[8];

    int idx = 0;

    vol_str[idx++] = ' ';

    if (current_volume == 100)
    {
        vol_str[idx++] = '1';
        vol_str[idx++] = '0';
        vol_str[idx++] = '0';
    }
    else
    {
        if (current_volume >= 10)
        {
            vol_str[idx++] =
                '0' +
                (current_volume / 10);
        }

        vol_str[idx++] =
            '0' +
            (current_volume % 10);
    }

    vol_str[idx++] = '%';
    vol_str[idx] = 0;

    draw_string(
        vol_str,
        vol_btn_x + 22,
        4,
        COLOR_BLACK,
        backbuffer,
        scr_width
    );

    /*
     * Volume click
     */

    if (
        single_click &&
        mouse_x >= vol_btn_x &&
        mouse_x <= vol_btn_x + 65 &&
        mouse_y >= 0 &&
        mouse_y <= 24
    )
    {
        volume_popup_open =
            !volume_popup_open;

        start_menu_open = 0;
    }
}


/* ============================================================
   BLUR
   ============================================================ */

static void blur_rect_buf(
    int x,
    int y,
    int w,
    int h,
    int radius
)
{
    if (radius <= 0)
        return;

    if (x < 0)
    {
        w += x;
        x = 0;
    }

    if (y < 0)
    {
        h += y;
        y = 0;
    }

    if (
        x + w >
        (int)scr_width
    )
    {
        w =
            (int)scr_width - x;
    }

    if (
        y + h >
        (int)scr_height
    )
    {
        h =
            (int)scr_height - y;
    }

    if (
        w <= 0 ||
        h <= 0 ||
        w > MAX_WIDTH ||
        h > MAX_HEIGHT
    )
    {
        return;
    }

    static uint32_t scratch[
        MAX_WIDTH
    ];

    /*
     * Horizontal pass
     */

    for (int row = 0;
         row < h;
         row++)
    {
        uint32_t* line =
            &backbuffer[
                (y + row) *
                scr_width +
                x
            ];

        for (int col = 0;
             col < w;
             col++)
        {
            int r = 0;
            int g = 0;
            int b = 0;
            int cnt = 0;

            for (
                int k = -radius;
                k <= radius;
                k++
            )
            {
                int sx =
                    col + k;

                if (
                    sx < 0 ||
                    sx >= w
                )
                {
                    continue;
                }

                uint32_t p =
                    line[sx];

                r +=
                    (p >> 16) & 0xFF;

                g +=
                    (p >> 8) & 0xFF;

                b +=
                    p & 0xFF;

                cnt++;
            }

            scratch[col] =
                ((uint32_t)(r / cnt) << 16) |
                ((uint32_t)(g / cnt) << 8) |
                (uint32_t)(b / cnt);
        }

        for (int col = 0;
             col < w;
             col++)
        {
            line[col] =
                scratch[col];
        }
    }

    /*
     * Vertical pass
     */

    for (int col = 0;
         col < w;
         col++)
    {
        for (int row = 0;
             row < h;
             row++)
        {
            scratch[row] =
                backbuffer[
                    (y + row) *
                    scr_width +
                    x +
                    col
                ];
        }

        for (int row = 0;
             row < h;
             row++)
        {
            int r = 0;
            int g = 0;
            int b = 0;
            int cnt = 0;

            for (
                int k = -radius;
                k <= radius;
                k++
            )
            {
                int sy =
                    row + k;

                if (
                    sy < 0 ||
                    sy >= h
                )
                {
                    continue;
                }

                uint32_t p =
                    scratch[sy];

                r +=
                    (p >> 16) & 0xFF;

                g +=
                    (p >> 8) & 0xFF;

                b +=
                    p & 0xFF;

                cnt++;
            }

            backbuffer[
                (y + row) *
                scr_width +
                x +
                col
            ] =
                ((uint32_t)(r / cnt) << 16) |
                ((uint32_t)(g / cnt) << 8) |
                (uint32_t)(b / cnt);
        }
    }
}


/*
 * blur_rect_rounded_buf: то же самое размытие, что и blur_rect_buf, но
 * со скруглёнными углами -- за пределами скруглённой маски (в самих
 * уголках прямоугольника) исходные пиксели фона восстанавливаются
 * НЕразмытыми, поэтому визуально получается ровно "стеклянная панель со
 * скруглёнными углами", а не просто прямоугольное матовое пятно.
 *
 * Раньше у дока был именно прямоугольный blur_rect_buf() без всякого
 * скругления -- отсюда и жалоба "док не скруглён, не такой" при
 * сравнении с реальным macOS доком (там всегда rounded glass panel).
 */
static void blur_rect_rounded_buf(
    int x,
    int y,
    int w,
    int h,
    int radius,
    int corner_r
)
{
    if (w <= 0 || h <= 0)
        return;

    if (corner_r < 0)
        corner_r = 0;
    if (corner_r * 2 > w)
        corner_r = w / 2;
    if (corner_r * 2 > h)
        corner_r = h / 2;

    /* Сохраняем НЕразмытые пиксели только из 4 угловых квадратов --
     * остальная area (края/центр) в сохранении не нуждается, скругление
     * там не режет. Квадрат стороны corner_r в каждом углу. */
    static uint32_t corner_backup[4][160 * 160];
    int cr = corner_r;
    if (cr > 160) cr = 160; /* защитный потолок под статический буфер выше */

    int x0 = x, y0 = y;
    int x1 = x + w, y1 = y + h;

    /* Клэмп углов к экрану -- если панель частично за краем экрана,
     * просто не восстанавливаем те углы (blur_rect_buf сам обрежется). */
    for (int cy = 0; cy < cr; cy++) {
        for (int cx = 0; cx < cr; cx++) {
            int px_tl = x0 + cx, py_tl = y0 + cy;
            int px_tr = x1 - cr + cx, py_tr = y0 + cy;
            int px_bl = x0 + cx, py_bl = y1 - cr + cy;
            int px_br = x1 - cr + cx, py_br = y1 - cr + cy;

            if (px_tl >= 0 && px_tl < (int)scr_width && py_tl >= 0 && py_tl < (int)scr_height)
                corner_backup[0][cy * 160 + cx] = backbuffer[py_tl * scr_width + px_tl];
            if (px_tr >= 0 && px_tr < (int)scr_width && py_tr >= 0 && py_tr < (int)scr_height)
                corner_backup[1][cy * 160 + cx] = backbuffer[py_tr * scr_width + px_tr];
            if (px_bl >= 0 && px_bl < (int)scr_width && py_bl >= 0 && py_bl < (int)scr_height)
                corner_backup[2][cy * 160 + cx] = backbuffer[py_bl * scr_width + px_bl];
            if (px_br >= 0 && px_br < (int)scr_width && py_br >= 0 && py_br < (int)scr_height)
                corner_backup[3][cy * 160 + cx] = backbuffer[py_br * scr_width + px_br];
        }
    }

    blur_rect_buf(x, y, w, h, radius);

    /* Восстанавливаем сохранённые пиксели там, где угол ВНЕ скруглённой
     * четверти окружности радиуса cr -- т.е. режем блюр по кругу, оставляя
     * под ним оригинальный фон (тот же приём, что и alpha-скругление в
     * draw_rounded_rect_alpha, только тут "цвет" -- это сам фон, а не
     * константа). */
    for (int cy = 0; cy < cr; cy++) {
        for (int cx = 0; cx < cr; cx++) {
            /* Расстояние от текущего пикселя до центра скругления данного
             * угла -- если больше радиуса, значит пиксель снаружи дуги,
             * его нужно вернуть к оригиналу (не блюренному). */
            int dx = cr - cx - 1;
            int dy = cr - cy - 1;
            int outside = (dx * dx + dy * dy) > (cr * cr);

            if (!outside)
                continue;

            int px_tl = x0 + cx, py_tl = y0 + cy;
            int px_tr = x1 - cr + cx, py_tr = y0 + cy;
            int px_bl = x0 + cx, py_bl = y1 - cr + cy;
            int px_br = x1 - cr + cx, py_br = y1 - cr + cy;

            if (px_tl >= 0 && px_tl < (int)scr_width && py_tl >= 0 && py_tl < (int)scr_height)
                backbuffer[py_tl * scr_width + px_tl] = corner_backup[0][cy * 160 + cx];
            if (px_tr >= 0 && px_tr < (int)scr_width && py_tr >= 0 && py_tr < (int)scr_height)
                backbuffer[py_tr * scr_width + px_tr] = corner_backup[1][cy * 160 + cx];
            if (px_bl >= 0 && px_bl < (int)scr_width && py_bl >= 0 && py_bl < (int)scr_height)
                backbuffer[py_bl * scr_width + px_bl] = corner_backup[2][cy * 160 + cx];
            if (px_br >= 0 && px_br < (int)scr_width && py_br >= 0 && py_br < (int)scr_height)
                backbuffer[py_br * scr_width + px_br] = corner_backup[3][cy * 160 + cx];
        }
    }
}


/* ============================================================
   DOCK
   ============================================================ */

/*
 * IgorOS Nord: macOS-style dock magnification.
 *
 * Портировано по духу из OriginOS (kernel/kernel.c: dock_icon_size_for_dist,
 * draw_dock) -- там иконки увеличивались в зависимости от расстояния
 * курсора до их "исходной" позиции, соседние иконки тоже вздувались,
 * затухая с расстоянием. В оригинальном IgorOS все иконки дока были
 * фиксированного размера без какой-либо реакции на курсор.
 *
 * Как и в Origin: целочисленная математика, без float (ядро не готовит
 * FPU для безопасного использования). Упрощено по сравнению с полной
 * версией Origin (там ещё была настройка силы эффекта из Settings и
 * авто-скрытие дока) -- IgorOS Nord пока не имеет своего Settings >
 * Dock раздела, so эффект зашит на "стандартную" силу.
 */

/* macOS 12-style dock — compact bar, icons grow upward out of it */
#define DOCK_BASE_ICON   36
#define DOCK_MAX_ICON_CAP 72   /* roomy range for Settings strength */
#define DOCK_INFLUENCE   80
#define DOCK_PAD_X       12
#define DOCK_PAD_Y        5
#define DOCK_SPACING     10

static int dock_iabs(int v) { return v < 0 ? -v : v; }

static int dock_max_icon_now(void) {
    if (!g_dock_mag_enabled) return DOCK_BASE_ICON;
    int lvl = g_dock_mag_level;
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    /* level 0 → almost no grow; 100 → CAP */
    return DOCK_BASE_ICON + ((DOCK_MAX_ICON_CAP - DOCK_BASE_ICON) * lvl) / 100;
}

/* Smooth quadratic falloff; respects Settings enable + level. */
static int dock_icon_size_for_dist(int dist) {
    int base = DOCK_BASE_ICON;
    int max_i = dock_max_icon_now();
    if (max_i <= base) return base;
    if (dist >= DOCK_INFLUENCE) return base;
    int t = DOCK_INFLUENCE - dist;
    int bump = (t * t) / DOCK_INFLUENCE;
    int size = base + ((max_i - base) * bump) / DOCK_INFLUENCE;
    if (size > max_i) size = max_i;
    if (size < base) size = base;
    return size;
}

/*
 * IgorOS Nord: точка дока для genie-анимации.
 *
 * Приложениям (calc/terminal/settings) нужна экранная точка их иконки в
 * доке, чтобы окно "стекало" именно туда при сворачивании/закрытии, как
 * в OriginOS. Дозаполняется в конце каждого кадра render_layer_dock()
 * (см. ниже, static int icon_positions[] там же) -- эти три массива
 * хранят последнюю известную позицию/размер каждой иконки, доступную
 * извне через genie_dock_icon_point().
 */

static int g_dock_icon_x[16];
static int g_dock_icon_y_top = 0;
static int g_dock_icon_size[16];
static int g_dock_icon_count = 0;

void genie_dock_icon_point(int dock_index, int *out_x, int *out_y)
{
    if (dock_index < 0 || dock_index >= g_dock_icon_count || g_dock_icon_count == 0) {
        /* пока дока не было отрисовано ни разу (например, окно открыли
         * до первого кадра) -- падаем в разумную точку внизу по центру
         * экрана, чтобы анимация всё равно выглядела осмысленно */
        *out_x = (int)scr_width / 2;
        *out_y = (int)scr_height - 10;
        return;
    }
    *out_x = g_dock_icon_x[dock_index] + g_dock_icon_size[dock_index] / 2;
    *out_y = g_dock_icon_y_top;
}

void render_layer_dock(int single_click)
{
    int spacing = DOCK_SPACING;

    /* Resting layout: fixed centers, fixed panel width (macOS behaviour).
     * Icons scale around these centers; the glass bar never stretches. */
    int rest_w = app_count * (DOCK_BASE_ICON + spacing) + spacing;
    int rest_x = (scr_width - rest_w) / 2;

    int centers[16];
    for (int i = 0; i < app_count && i < 16; i++) {
        centers[i] = rest_x + spacing + i * (DOCK_BASE_ICON + spacing) + DOCK_BASE_ICON / 2;
    }

    /* Panel sits at the bottom; height is based on BASE icons only.
     * Magnified icons grow upward and protrude above the glass. */
    int dock_h = DOCK_BASE_ICON + DOCK_PAD_Y * 2;
    int dock_y = (int)scr_height - dock_h - 6;  /* closer to bottom edge */

    /* Bottom edge of icons is locked to the inside bottom of the glass. */
    int icon_bottom = dock_y + dock_h - DOCK_PAD_Y;

    /* Only magnify when cursor is near the dock strip */
    int mouse_near =
        mouse_y >= dock_y - (DOCK_MAX_ICON_CAP - DOCK_BASE_ICON) - 24 &&
        mouse_y < (int)scr_height;

    /*
     * Fast smooth magnification.
     * Approach target by ~40% of remaining delta each frame → settles in
     * ~6-8 frames, feels responsive without visible stepping or lag.
     * Snap when within 1 px so it never crawls the last pixel forever.
     */
    static int anim_sizes[16];
    static int anim_sizes_init = 0;
    if (!anim_sizes_init) {
        for (int i = 0; i < 16; i++) anim_sizes[i] = DOCK_BASE_ICON;
        anim_sizes_init = 1;
    }

    int target_sizes[16];
    for (int i = 0; i < app_count && i < 16; i++) {
        int dist = mouse_near ? dock_iabs((int)mouse_x - centers[i]) : DOCK_INFLUENCE;
        target_sizes[i] = dock_icon_size_for_dist(dist);

        int diff = target_sizes[i] - anim_sizes[i];
        if (diff > 1 || diff < -1) {
            /* ~2/5 of remaining distance per frame */
            anim_sizes[i] += (diff * 2) / 5;
        } else {
            anim_sizes[i] = target_sizes[i]; /* snap last pixel */
        }
    }

    int sizes[16];
    for (int i = 0; i < app_count && i < 16; i++) sizes[i] = anim_sizes[i];

    /*
     * BAG: dock_x/dock_w раньше считались от total_icons_w -- реальной,
     * "гуляющей" суммы текущих (увеличенных magnification'ом) размеров
     * иконок. Из-за этого сама панель дока и стартовая точка отрисовки
     * icon_x двигались каждый кадр, а centers[i] (используемые же для
     * расчёта, какая иконка ближе к курсору) оставались завязаны на
     * неподвижную "отдыхающую" сетку rest_x/rest_w. Два разных источника
     * истины для одной и той же геометрии -- отсюда "иконки не ровно в
     * доке": при наведении сетка отрисовки расходилась с сеткой centers.
     *
     * Фикс: и панель, и icon_x строятся от ОДНОЙ и той же неподвижной
     * сетки (rest_x/rest_w/centers[]), как в реальном macOS -- сама
     * панель дока не "дышит" при magnification, растут только сами
     * иконки, вверх, вокруг своего неподвижного центра.
     */
    /* Fixed panel geometry — never stretches with magnification. */
    int dock_w = rest_w;
    int dock_x = rest_x;
    /* dock_h already computed above from DOCK_BASE_ICON + padding */

    /*
     * Glass blur — скруглённая стеклянная панель дока (радиус скругления
     * заметно больше, чем сила самого блюра, чтобы уголки были чёткими и
     * действительно круглыми, как в реальном macOS доке, а не просто
     * прямоугольным матовым пятном).
     */

    /* Glass extends a few px beyond the icon row for the classic macOS look. */
    int dock_panel_x = dock_x - DOCK_PAD_X;
    int dock_panel_y = dock_y;
    int dock_panel_w = dock_w + DOCK_PAD_X * 2;
    int dock_panel_h = dock_h;
    int dock_corner_r = 18;

    /*
     * BAG: у дока не было настоящей мягкой drop-shadow -- только сама
     * стеклянная панель (blur + 1px обводка). На плоском ярком фоне
     * (например, море со скриншота) это выглядит как резкий
     * прямоугольный полупрозрачный блок без всякого объёма -- отсюда
     * жалоба "тень ужасна". Реальный macOS док отбрасывает мягкую тень,
     * растушёванную на 10-16px за пределами самой панели, с падающей
     * от края к краю альфой.
     *
     * Фикс: несколько скруглённых alpha-прямоугольников, каждый чуть
     * больше предыдущего и с меньшей альфой, рисуются ПОД панелью (т.е.
     * до blur/заливки) -- получается плавно растушёванная тень вместо
     * жёсткого края.
     */
    /* Cheap dock chrome: 2 soft shadow layers + flat frosted panel.
     * Full box-blur every frame was a major FPS killer. */
    draw_rounded_rect_alpha(
        dock_panel_x - 6, dock_panel_y - 2 + 6,
        dock_panel_w + 12, dock_panel_h + 10,
        dock_corner_r + 4, 0x00000000, 28);
    draw_rounded_rect_alpha(
        dock_panel_x - 2, dock_panel_y + 2,
        dock_panel_w + 4, dock_panel_h + 4,
        dock_corner_r + 1, 0x00000000, 40);

    draw_rounded_rect_alpha(
        dock_panel_x - 1, dock_panel_y - 1,
        dock_panel_w + 2, dock_panel_h + 2,
        dock_corner_r + 1, COLOR_DOCK_BORDER, 50);
    draw_rounded_rect_alpha(
        dock_panel_x, dock_panel_y,
        dock_panel_w, dock_panel_h,
        dock_corner_r, COLOR_DOCK_BG, 70);

    int hovered_idx = -1;
    static int icon_positions[16]; /* реальный x каждой иконки в этом кадре, для тултипа */

    /*
     * Icons
     */

    for (int i = 0;
         i < app_count;
         i++)
    {
        int icon_size = sizes[i];
        /*
         * BAG (см. комментарий у dock_w/dock_x выше): раньше icon_x
         * накапливался последовательно через running_x += icon_size +
         * spacing -- т.е. позиция КАЖДОЙ иконки зависела от текущего
         * (анимированного) размера ВСЕХ предыдущих иконок в этом кадре.
         * Как только magnification увеличивал одну иконку слева, все
         * иконки правее неё сдвигались, хотя их центр должен был
         * оставаться на месте -- отсюда неровный, "гуляющий" интервал
         * между иконками дока.
         *
         * Фикс: icon_x центрируем вокруг неподвижного centers[i] (та же
         * сетка, что используется для расчёта magnification), а не
         * накапливаем от соседей -- иконка растёт строго вокруг своего
         * собственного центра, соседи не сдвигаются.
         */
        int icon_x = centers[i] - icon_size / 2;
        icon_positions[i] = icon_x;
        g_dock_icon_x[i] = icon_x;
        g_dock_icon_size[i] = icon_size;

        /* Иконка растёт вверх: нижний край всегда на icon_bottom,
         * верхний край поднимается тем выше, чем крупнее иконка. */
        int icon_y =
            icon_bottom - icon_size;

        /*
         * Icon
         */

        if (dock_apps[i].bmp_data)
        {
            draw_scaled_bmp_rounded(
                dock_apps[i].bmp_data,
                icon_x,
                icon_y,
                icon_size,
                icon_size,
                10
            );
        }
        else
        {
            draw_rounded_rect_buf(
                icon_x,
                icon_y,
                icon_size,
                icon_size,
                10,
                dock_apps[i].fallback_color
            );
        }

        /*
         * Hover
         */

        if (
            mouse_x >= icon_x &&
            mouse_x <
                icon_x + icon_size &&
            mouse_y >= icon_y &&
            mouse_y <
                icon_y + icon_size
        )
        {
            hovered_idx = i;

            if (single_click)
            {
                /*
                 * File Manager
                 */

                if (i == 0 && toggle_file_manager) {
                    toggle_file_manager();
                    win_bring_to_front(WIN_ID_FILE);
                } else if (i == 1) {
                    toggle_terminal_app();
                    win_bring_to_front(WIN_ID_TERMINAL);
                } else if (i == 2 && toggle_doom_app) {
                    toggle_doom_app();
                    win_bring_to_front(WIN_ID_DOOM);
                } else if (i == 3) {
                    toggle_calc_app();
                    win_bring_to_front(WIN_ID_CALC);
                } else if (i == 4) {
                    toggle_settings_app();
                    win_bring_to_front(WIN_ID_SETTINGS);
                } else if (i == 5) {
                    toggle_music_app();
                    win_bring_to_front(WIN_ID_MUSIC);
                }
            }
        }
    }

    g_dock_icon_count = app_count;
    g_dock_icon_y_top = icon_bottom - DOCK_BASE_ICON; /* resting icon top for genie */

    /* Rounded underline under open apps (tray-style indicator) */
    {
        static const int dock_to_win[] = {
            WIN_ID_FILE, WIN_ID_TERMINAL, WIN_ID_DOOM, WIN_ID_CALC, WIN_ID_SETTINGS, WIN_ID_MUSIC
        };
        for (int i = 0; i < app_count && i < 6; i++) {
            int wid = dock_to_win[i];
            if (wid < 0 || wid >= WIN_COUNT) continue;
            if (!g_win_rect[wid].open) continue;
            int ix = g_dock_icon_x[i];
            int isz = g_dock_icon_size[i];
            int ux = ix + isz / 2 - 4;
            int uy = icon_bottom + 3;
            draw_rounded_rect_buf(ux, uy, 8, 3, 2, 0x00FFFFFF);
        }
    }


    /*
     * Tooltip state
     */

    if (hovered_idx != -1)
    {
        if (
            current_hover_idx !=
            hovered_idx
        )
        {
            current_hover_idx =
                hovered_idx;

            tooltip_alpha = 0;
        }

        if (tooltip_alpha < 255)
            tooltip_alpha += 35;

        if (tooltip_alpha > 255)
            tooltip_alpha = 255;
    }
    else
    {
        if (tooltip_alpha > 0)
            tooltip_alpha -= 35;

        if (tooltip_alpha < 0)
            tooltip_alpha = 0;
    }

    /*
     * Tooltip
     */

    if (
        tooltip_alpha > 10 &&
        current_hover_idx >= 0
    )
    {
        const char* label =
            dock_apps[
                current_hover_idx
            ].title;

        int tip_w =
            font_text_width(label) +
            16;

        int tip_h = 22;

        int target_icon_x =
            icon_positions[current_hover_idx];

        int hovered_icon_size =
            sizes[current_hover_idx];

        int tip_x =
            target_icon_x +
            hovered_icon_size / 2 -
            tip_w / 2;

        /* Label floats just above the current (magnified) icon top */
        int tip_y =
            icon_bottom - hovered_icon_size - tip_h - 4;

        uint8_t a =
            (uint8_t)tooltip_alpha;

        draw_rounded_rect_alpha(
            tip_x,
            tip_y,
            tip_w,
            tip_h,
            6,
            COLOR_TOOLTIP_BG,
            a
        );

        draw_string(
            (char*)label,
            tip_x + 8,
            tip_y + 3,
            COLOR_WHITE,
            backbuffer,
            scr_width
        );
    }
}


/* Запоминаем, какие обои были нарисованы в bg_buffer последний раз, чтобы
 * не перерисовывать их каждый кадр зря -- только когда g_wallpaper_choice
 * реально поменялся (например, кликом в Settings). Раньше bg_buffer
 * заполнялся один раз в desktop_init() и больше никогда не обновлялся,
 * поэтому смена обоев в Settings меняла только переменную-выбор, а сам
 * фон оставался прежним до перезагрузки. */
static int g_wallpaper_drawn_choice = -1;

void refresh_wallpaper(void)
{
    if (g_wallpaper_choice == g_wallpaper_drawn_choice)
        return;

    if (current_wallpaper_bmp())
    {
        draw_bmp_stretched(
            current_wallpaper_bmp(),
            (int)scr_width,
            (int)scr_height,
            bg_buffer
        );
    }
    else
    {
        uint32_t total_pixels = scr_width * scr_height;
        if (total_pixels > MAX_WIDTH * MAX_HEIGHT)
            total_pixels = MAX_WIDTH * MAX_HEIGHT;

        for (uint32_t i = 0; i < total_pixels; i++)
            bg_buffer[i] = 0x001E1E22;
    }

    g_wallpaper_drawn_choice = g_wallpaper_choice;
}


/* ============================================================
   DESKTOP INIT
   ============================================================ */

void desktop_init(
    uint8_t* vram,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint32_t bpp
)
{
    if (vram)
        frontbuffer = vram;

    // Раньше при превышении MAX_WIDTH/MAX_HEIGHT разрешение просто
    // игнорировалось целиком, и scr_width/scr_height оставались на старом
    // значении по умолчанию (1280x720), а frontbuffer/pitch при этом уже
    // указывали на РЕАЛЬНОЕ (большее) разрешение экрана — рассинхрон буфера
    // и реального дисплея. Теперь вместо игнора — обрезаем (clamp) до
    // максимума, который выдерживают статические буферы. Это не "резиновая"
    // поддержка любого разрешения (буферы всё ещё фиксированного размера),
    // но хотя бы не ломает рендер целиком, если экран окажется больше 4K.
    if (width > 0) {
        scr_width = (width > MAX_WIDTH) ? MAX_WIDTH : width;
    }

    if (height > 0) {
        scr_height = (height > MAX_HEIGHT) ? MAX_HEIGHT : height;
    }

    scr_pitch =
        pitch
            ? pitch
            : scr_width * 4;

    scr_bpp =
        bpp
            ? bpp
            : 32;

    /*
     * Input
     */

    init_keyboard();
    /*
     * ФИКС: раньше здесь был повторный init_mouse().
     * Мышь уже инициализируется один раз в kernel.c (mouse_set_bounds +
     * init_mouse() во время boot-прогресса). Повторная инициализация
     * здесь заново гоняла полный PS/2-протокол сброса (0xF6/0xF3/0xE8/0xF4)
     * поверх уже активного, стримящего пакеты контроллера -- из-за этого
     * могла ломаться синхронизация mouse_cycle / зависать mouse_wait(),
     * и курсор переставал двигаться после старта desktop_run().
     * Заново выставляем только границы экрана (актуальный scr_width/scr_height
     * известен только здесь), не трогая уже работающий PS/2-канал.
     */
    mouse_set_bounds(scr_width, scr_height);

    /*
     * RTC
     */

    update_rtc_cache();
    next_rtc_update_ms = timer_millis() + 1000ULL;

    /*
     * Wallpaper
     *
     * force redraw: сбрасываем кэш "что было нарисовано", т.к. scr_width/
     * scr_height только что определились (или изменились) -- старая
     * растянутая картинка под новое разрешение уже не годится.
     */

    g_wallpaper_drawn_choice = -1;
    refresh_wallpaper();
}


/* ============================================================
   DESKTOP MAIN LOOP
   ============================================================ */

void desktop_run(void)
{
    /* 250 Hz PIT, 4 ticks per frame ~= 62.5 FPS.  Use a deadline rather
     * than sleeping a fixed amount after rendering, so a heavy frame does
     * not add another full sleep interval and make the UI feel sluggish. */
    uint64_t next_frame_tick = timer_ticks();

    while (1)
    {
        /*
         * Mouse
         *
         * ФИКС: раньше мышь опрашивалась ровно один раз за целый кадр.
         * render_layer_background() перерисовывает весь фреймбуфер с нуля
         * (без dirty-rect), поэтому чем тяжелее кадр (больше окон/эффектов),
         * тем реже реально читался PS/2-порт -- курсор ощутимо "тупил" и
         * дёргался под нагрузкой, хотя сам PS/2-драйвер (mouse.c) был
         * корректным. Теперь опрашиваем мышь несколько раз за кадр:
         * один раз в начале (для логики кликов) и ещё раз прямо перед
         * отрисовкой курсора, чтобы сама точка курсора всегда бралась
         * из самых свежих координат, даже если рендер окон затянулся.
         */

        int single_click =
            (
                mouse_left_clicked &&
                !prev_mouse_left
            );

        prev_mouse_left =
            mouse_left_clicked;

        /*
         * Keyboard
         */

        /*
         * Клавиатура теперь событийная (IRQ1): забираем ВСЕ накопившиеся
         * события за кадр. Если в фокусе окно Doom -- оно получает сырые
         * скан-коды (стрелки, WASD, удержание клавиш), остальные приложения
         * при этом ничего не печатают. Иначе -- как раньше, символы
         * калькулятору и терминалу.
         */
        kbd_event_t kev;
        int doom_has_focus = doom_app_is_open && doom_app_feed_scancode &&
                             doom_app_is_open() && win_is_focused(WIN_ID_DOOM);

        if (keyboard_consume_alt_f4()) {
            /* Close focused window (Alt+F4) */
            int fid = -1;
            /* win_is_focused walks ids */
            for (int wi = 0; wi < WIN_COUNT; wi++) {
                if (win_is_focused(wi) && win_is_open(wi)) { fid = wi; break; }
            }
            if (fid < 0) {
                /* fallback: top of z-order that is open */
                for (int zi = WIN_COUNT - 1; zi >= 0; zi--) {
                    int id = g_win_z_order[zi];
                    if (win_is_open(id)) { fid = id; break; }
                }
            }
            if (fid == WIN_ID_FILE && toggle_file_manager) toggle_file_manager();
            else if (fid == WIN_ID_TERMINAL) toggle_terminal_app();
            else if (fid == WIN_ID_CALC) toggle_calc_app();
            else if (fid == WIN_ID_SETTINGS) toggle_settings_app();
            else if (fid == WIN_ID_MUSIC) toggle_music_app();
            else if (fid == WIN_ID_ABOUT) toggle_about_app();
            else if (fid == WIN_ID_DOOM && doom_app_close) doom_app_close();
        }

        while (keyboard_poll_event(&kev)) {
            if (doom_has_focus) {
                doom_app_feed_scancode(kev.scancode, kev.extended, kev.pressed);
                continue;
            }
            char key = keyboard_event_to_char(&kev);
            if (key) {
                calc_app_feed_key(key);
                terminal_app_feed_key(key);
            }
        }

        /*
         * Wallpaper
         *
         * Дёшево -- сравнение int'а, реальная перерисовка bg_buffer
         * происходит только когда пользователь реально сменил обои
         * в Settings (g_wallpaper_choice != то, что уже нарисовано).
         */

        refresh_wallpaper();

        /*
         * Window drag arbitration -- reset the per-frame claim before any
         * window gets a chance to grab it. See win_drag_frame_begin().
         */

        win_drag_frame_begin(mouse_left_clicked);

        /*
         * Background
         */

        render_layer_background();

        /*
         * Application windows
         *
         * IMPORTANT:
         * Applications are rendered before
         * the system UI.
         */

        /*
         * Рендерим окна в порядке g_win_z_order (низ -> верх), а не в
         * фиксированном порядке file/music/about/calc/terminal/settings.
         * g_current_render_id выставляется перед каждым вызовом, чтобы
         * win_drag_claim() внутри app-модуля знал, чьё окно поднимать.
         * См. комментарий у объявления g_win_z_order выше.
         */
        for (int __wz = 0; __wz < WIN_COUNT; __wz++) {
            g_current_render_id = g_win_z_order[__wz];
            switch (g_current_render_id) {
                case WIN_ID_FILE:
                    render_file_manager_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_MUSIC:
                    render_music_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_ABOUT:
                    render_about_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_CALC:
                    render_calc_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_TERMINAL:
                    render_terminal_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_SETTINGS:
                    render_settings_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
                case WIN_ID_DOOM:
                    if (render_doom_app_window)
                        render_doom_app_window(backbuffer, scr_width, scr_height, mouse_x, mouse_y, mouse_left_clicked, single_click);
                    break;
            }
        }
        g_current_render_id = -1;

        /*
         * System UI
         */

        render_layer_topbar(
            single_click
        );

        render_layer_dock(
            single_click
        );

        /*
         * Popups
         */

        render_start_menu(
            single_click
        );

        render_volume_popup();

        /*
         * Cursor
         *
         * Второй опрос мыши прямо перед отрисовкой курсора: если кадр
         * получился тяжёлым (много окон/эффектов), курсор всё равно
         * рисуется по самой свежей позиции, а не по той, что была в
         * начале кадра. Это устраняет ощущение "залипания" мыши при
         * открытых нескольких приложениях.
         */

        draw_cursor_bmp(
            mouse_x,
            mouse_y,
            backbuffer,
            scr_width,
            scr_height
        );

        /*
         * Present frame
         */

        desktop_swap_buffers();

        next_frame_tick += 4;
        uint64_t now_tick = timer_ticks();
        if (now_tick < next_frame_tick) {
            timer_wait_ticks(next_frame_tick - now_tick);
        } else if (now_tick > next_frame_tick + 8) {
            /* We fell far behind (e.g. a heavy app frame).  Resync instead
             * of trying to render a backlog of missed frames. */
            next_frame_tick = now_tick;
        }
    }
}