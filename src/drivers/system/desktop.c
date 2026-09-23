// src/desktop.c

#include "gui/desktop.h"
#include "gui/bmp_loader.h"
#include "gui/font.h"
#include "gui/wallpaper/wallpaper.h"
#include "gui/cursor/cursor.h"
#include "gui/apps/file/file_manager.h"
#include "gui/apps/music/music_app.h"
#include "gui/apps/about/about_app.h"
#include "drivers/system/keyboard.h"
#include "drivers/system/mouse.h"

extern void toggle_file_manager(void) __attribute__((weak));

extern const uint8_t file_icon_bmp_start[] __attribute__((weak));
extern const uint8_t music_icon_bmp_start[] __attribute__((weak));
extern const uint8_t start_icon_bmp_start[] __attribute__((weak));
extern const uint8_t volume_bmp_start[] __attribute__((weak));
extern const uint8_t wallpaper_bmp_start[] __attribute__((weak));

static uint32_t scr_width  = 1280;
static uint32_t scr_height = 720;
static uint32_t scr_pitch  = 5120;
static uint32_t scr_bpp    = 32;

static uint8_t* frontbuffer = (uint8_t*)0xA0000;

#define MAX_WIDTH  1920
#define MAX_HEIGHT 1080

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
    {"File",     file_icon_bmp_start,  0x00007AFF},
    {"Terminal", 0,                    0x001C1C1E},
    {"Editor",   0,                    0x00FF9500},
    {"Settings", 0,                    0x008E8E93},
    {"Music",    music_icon_bmp_start, 0x00FF2D55}
};

static int app_count =
    sizeof(dock_apps) / sizeof(dock_app_t);

static int prev_mouse_left = 0;

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

static int rtc_ticks = 0;


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
                if (
                    rx * rx +
                    ry * ry <=
                    r * r
                )
                {
                    draw_pixel_buf(
                        x + j,
                        y + i,
                        color
                    );
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

            if (
                rx != -1 &&
                ry != -1 &&
                rx * rx +
                ry * ry >
                r * r
            )
            {
                continue;
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
                        alpha
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
    if (!bmp_data)
        return;

    bmp_header_t* header =
        (bmp_header_t*)bmp_data;

    if (header->type != 0x4D42)
        return;

    bmp_info_header_t* info =
        (bmp_info_header_t*)
        (bmp_data + sizeof(bmp_header_t));

    int img_w = info->width;

    int img_h =
        info->height < 0
            ? -info->height
            : info->height;

    if (
        img_w <= 0 ||
        img_h <= 0
    )
    {
        return;
    }

    int is_bottom_up =
        info->height > 0;

    uint16_t bpp =
        info->bpp;

    const uint8_t* pixels =
        bmp_data + header->offset;

    int bytes_pp =
        bpp / 8;

    if (
        bytes_pp < 3 ||
        bytes_pp > 4
    )
    {
        return;
    }

    int row_stride =
        ((img_w * bytes_pp + 3) / 4) * 4;

    for (int cy = 0;
         cy < target_h;
         cy++)
    {
        for (int cx = 0;
             cx < target_w;
             cx++)
        {
            int rx = -1;
            int ry = -1;

            if (cx < r && cy < r)
            {
                rx = r - cx - 1;
                ry = r - cy - 1;
            }
            else if (
                cx >= target_w - r &&
                cy < r
            )
            {
                rx =
                    cx -
                    (target_w - r);

                ry =
                    r - cy - 1;
            }
            else if (
                cx < r &&
                cy >= target_h - r
            )
            {
                rx =
                    r - cx - 1;

                ry =
                    cy -
                    (target_h - r);
            }
            else if (
                cx >= target_w - r &&
                cy >= target_h - r
            )
            {
                rx =
                    cx -
                    (target_w - r);

                ry =
                    cy -
                    (target_h - r);
            }

            if (
                rx != -1 &&
                ry != -1 &&
                rx * rx +
                ry * ry >
                r * r
            )
            {
                continue;
            }

            int src_x =
                (cx * img_w) /
                target_w;

            int src_y =
                (cy * img_h) /
                target_h;

            int py =
                is_bottom_up
                    ? img_h - 1 - src_y
                    : src_y;

            const uint8_t* p =
                pixels +
                py * row_stride +
                src_x * bytes_pp;

            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r_val = p[2];

            /*
             * Старый прозрачный пурпурный цвет.
             */

            if (
                r_val > 240 &&
                g < 15 &&
                b > 240
            )
            {
                continue;
            }

            if (
                bpp == 32 &&
                p[3] < 32
            )
            {
                continue;
            }

            draw_pixel_buf(
                x + cx,
                y + cy,
                (r_val << 16) |
                (g << 8) |
                b
            );
        }
    }
}


/* ============================================================
   BUFFER SWAP
   ============================================================ */

static void desktop_swap_buffers(void)
{
    uint32_t* vram32 =
        (uint32_t*)frontbuffer;

    uint32_t total_pixels =
        scr_width * scr_height;

    if (
        total_pixels >
        (MAX_WIDTH * MAX_HEIGHT)
    )
    {
        total_pixels =
            MAX_WIDTH * MAX_HEIGHT;
    }

    for (
        uint32_t i = 0;
        i < total_pixels;
        i++
    )
    {
        vram32[i] =
            backbuffer[i];
    }
}


/* ============================================================
   BACKGROUND
   ============================================================ */

void render_layer_background(void)
{
    uint32_t total_pixels =
        scr_width * scr_height;

    if (
        total_pixels >
        (MAX_WIDTH * MAX_HEIGHT)
    )
    {
        total_pixels =
            MAX_WIDTH * MAX_HEIGHT;
    }

    for (
        uint32_t i = 0;
        i < total_pixels;
        i++
    )
    {
        backbuffer[i] =
            bg_buffer[i];
    }
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
    draw_rect_buf(
        0,
        0,
        scr_width,
        24,
        COLOR_TOPBAR
    );

    draw_rect_buf(
        0,
        23,
        scr_width,
        1,
        COLOR_TOPBAR_BORDER
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

    rtc_ticks++;

    if (rtc_ticks >= 60)
    {
        update_rtc_cache();

        rtc_ticks = 0;
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


/* ============================================================
   DOCK
   ============================================================ */

void render_layer_dock(int single_click)
{
    int icon_size = 38;
    int spacing = 16;

    int dock_w =
        app_count *
        (icon_size + spacing) +
        spacing;

    int dock_h = 54;

    int dock_x =
        (scr_width - dock_w) / 2;

    int dock_y =
        scr_height -
        dock_h -
        10;

    /*
     * Glass blur
     */

    blur_rect_buf(
        dock_x - 6,
        dock_y - 6,
        dock_w + 12,
        dock_h + 12,
        3
    );

    /*
     * Glass background
     */

    draw_rounded_rect_alpha(
        dock_x,
        dock_y,
        dock_w,
        dock_h,
        18,
        COLOR_DOCK_BG,
        150
    );

    /*
     * Border
     */

    draw_rounded_rect_alpha(
        dock_x - 1,
        dock_y - 1,
        dock_w + 2,
        dock_h + 2,
        19,
        COLOR_DOCK_BORDER,
        140
    );

    int hovered_idx = -1;

    /*
     * Icons
     */

    for (int i = 0;
         i < app_count;
         i++)
    {
        int icon_x =
            dock_x +
            spacing +
            i *
            (icon_size + spacing);

        int icon_y =
            dock_y + 8;

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

                if (
                    i == 0 &&
                    toggle_file_manager
                )
                {
                    toggle_file_manager();
                }

                /*
                 * Music
                 */

                else if (i == 4)
                {
                    toggle_music_app();
                }
            }
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
            dock_x +
            spacing +
            current_hover_idx *
            (icon_size + spacing);

        int tip_x =
            target_icon_x +
            icon_size / 2 -
            tip_w / 2;

        int tip_y =
            dock_y -
            tip_h -
            8;

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

    if (
        width > 0 &&
        width <= MAX_WIDTH
    )
    {
        scr_width = width;
    }

    if (
        height > 0 &&
        height <= MAX_HEIGHT
    )
    {
        scr_height = height;
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
    init_mouse();

    /*
     * RTC
     */

    update_rtc_cache();

    /*
     * Wallpaper
     */

    if (wallpaper_bmp_start)
    {
        draw_bmp_stretched(
            wallpaper_bmp_start,
            scr_width,
            scr_height,
            bg_buffer
        );
    }
    else
    {
        /*
         * Fallback background.
         */

        uint32_t total_pixels =
            scr_width *
            scr_height;

        if (
            total_pixels >
            MAX_WIDTH * MAX_HEIGHT
        )
        {
            total_pixels =
                MAX_WIDTH *
                MAX_HEIGHT;
        }

        for (
            uint32_t i = 0;
            i < total_pixels;
            i++
        )
        {
            bg_buffer[i] =
                0x001E1E22;
        }
    }
}


/* ============================================================
   DESKTOP MAIN LOOP
   ============================================================ */

void desktop_run(void)
{
    while (1)
    {
        /*
         * Mouse
         */

        poll_mouse();

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

        char key =
            keyboard_getchar();

        (void)key;

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

        render_file_manager_window(
            backbuffer,
            scr_width,
            scr_height,
            mouse_x,
            mouse_y,
            mouse_left_clicked,
            single_click
        );

        render_music_app_window(
            backbuffer,
            scr_width,
            scr_height,
            mouse_x,
            mouse_y,
            mouse_left_clicked,
            single_click
        );

        render_about_app_window(
            backbuffer,
            scr_width,
            scr_height,
            mouse_x,
            mouse_y,
            mouse_left_clicked,
            single_click
        );

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
    }
}