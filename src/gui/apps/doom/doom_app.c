/*
 * Real Doom (doomgeneric) window for IgorOS Nord.
 * Graphics and levels come from embedded DOOM.WAD.
 */
#include "doom_app.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"

#include "doomgeneric.h"
#include "doomkeys.h"

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
extern void draw_pixel_buf(int x, int y, uint32_t color);

extern void doom_igor_push_key(int pressed, unsigned char key);
extern void doom_igor_tick_ms(uint32_t dt);

#define DOOM_DOCK_INDEX 2
#define HEADER_H 28

static int is_open = 0, minimized = 0;
static int win_x = 40, win_y = 40;
static int win_w = 640, win_h = 420;
static int dragging = 0, drag_ox = 0, drag_oy = 0;
static genie_state_t genie;
static int positioned = 0;
static int engine_started = 0;
static int engine_failed = 0;
static int is_fullscreen = 0;
static int win_x_rest = 40, win_y_rest = 40, win_w_rest = 640, win_h_rest = 420;

static char *dg_argv[] = {
    "doom",
    "-iwad",
    "DOOM.WAD",
    0
};

void toggle_doom_app(void) {
    int dx, dy;
    genie_dock_icon_point(DOOM_DOCK_INDEX, &dx, &dy);
    if (genie_is_animating(&genie)) genie_cancel(&genie);
    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0;
        return;
    }
    if (is_open) {
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        minimized = 0;
        return;
    }
    is_open = 1;
    minimized = 0;
    positioned = 0;
    genie_start_open(&genie, dx, dy);
}

void doom_app_close(void) {
    if (!is_open) return;
    int dx, dy;
    genie_dock_icon_point(DOOM_DOCK_INDEX, &dx, &dy);
    genie_start_close(&genie, dx, dy);
    is_open = 0;
    minimized = 0;
    dragging = 0;
}

int doom_app_is_open(void) { return is_open && !minimized; }

static unsigned char map_key(char key) {
    switch (key) {
        case '\n': case '\r': return KEY_ENTER;
        case 27: return KEY_ESCAPE;
        case ' ': return KEY_FIRE;
        case '\t': return KEY_TAB;
        default: return (unsigned char)key;
    }
}

void doom_app_feed_key(char key) {
    if (!is_open || minimized || !engine_started) return;
    unsigned char dk = map_key(key);
    if (!dk) return;
    doom_igor_push_key(1, dk);
    doom_igor_push_key(0, dk);
}

/* Integer scale (pixel-perfect) when possible; otherwise nearest fill */
static void blit_doom_frame(int vx, int vy, int vw, int vh) {
    if (!DG_ScreenBuffer || vw <= 0 || vh <= 0) return;
    const int sw = DOOMGENERIC_RESX;
    const int sh = DOOMGENERIC_RESY;

    int scale = vw / sw;
    if (vh / sh < scale) scale = vh / sh;
    if (scale < 1) scale = 1;

    /* Prefer integer scale centered (no blurry stretch) */
    int dw = sw * scale;
    int dh = sh * scale;
    int ox = vx + (vw - dw) / 2;
    int oy = vy + (vh - dh) / 2;

    /* letterbox background */
    if (dw < vw || dh < vh)
        draw_rect_buf(vx, vy, vw, vh, 0x00000000);

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            uint32_t c = ((uint32_t)DG_ScreenBuffer[y * sw + x]) & 0x00FFFFFF;
            int px = ox + x * scale;
            int py = oy + y * scale;
            if (scale == 1)
                draw_pixel_buf(px, py, c);
            else
                draw_rect_buf(px, py, scale, scale, c);
        }
    }
}

void render_doom_app_window(uint32_t *buf, int scr_w, int scr_h,
                            int mx, int my, int btn, int click) {
    (void)buf;
    genie_tick(&genie);
    if (!is_open && !genie_is_animating(&genie)) return;
    if (minimized && !genie_is_animating(&genie)) return;

    if (!positioned) {
        /* adaptive: prefer 2x 320x200 + chrome, else fit screen */
        win_w = DOOMGENERIC_RESX * 2 + 8;
        win_h = DOOMGENERIC_RESY * 2 + HEADER_H + 8;
        if (win_w > scr_w - 20) win_w = scr_w - 20;
        if (win_h > scr_h - 40) win_h = scr_h - 40;
        win_x = (scr_w - win_w) / 2;
        win_y = (scr_h - win_h) / 2;
        if (win_y < 30) win_y = 30;
        positioned = 1;
    }

    /* start engine once when window is open */
    if (is_open && !engine_started && !engine_failed && !genie_is_animating(&genie)) {
        doomgeneric_Create(3, dg_argv);
        engine_started = DG_ScreenBuffer != 0;
        if (!engine_started) engine_failed = 1;
    }

    if (engine_started && is_open && !minimized) {
        doom_igor_tick_ms(16);
        doomgeneric_Tick();
    }

    int mid = genie_is_animating(&genie);
    int tz = 90;

    if (!mid && btn && !dragging && win_drag_available() &&
        mx >= win_x && mx <= win_x + win_w - tz &&
        my >= win_y && my <= win_y + HEADER_H) {
        dragging = 1;
        drag_ox = mx - win_x;
        drag_oy = my - win_y;
        win_drag_claim();
        win_set_focused(WIN_ID_DOOM_);
    }
    if (!btn) dragging = 0;
    if (dragging) {
        win_x = mx - drag_ox;
        win_y = my - drag_oy;
    }

    win_report_rect(WIN_ID_DOOM_, win_x, win_y, win_w, win_h, is_open && !mid);
    int occluded = win_click_occluded(WIN_ID_DOOM_, mx, my);

    int dx, dy, dw, dh;
    genie_get_rect(&genie, win_x, win_y, win_w, win_h, &dx, &dy, &dw, &dh);

    draw_rounded_rect_alpha(dx - 3, dy + 3, dw + 6, dh + 5, 10, 0, 28);
    draw_rounded_rect_buf(dx, dy, dw, dh, 8, 0x00101010);

    if (mid) return;

    draw_rect_buf(win_x, win_y, win_w, HEADER_H, 0x00202020);
    draw_string("DOOM", win_x + 12, win_y + 6, 0x00FF3333, buf, (uint32_t)scr_w);

    int min_c = 0, zoom_c = 0;
    if (win_chrome_traffic_lights(WIN_ID_DOOM_, win_x, win_y, win_w, HEADER_H,
                                  mx, my, click, occluded, &min_c, &zoom_c)) {
        doom_app_close();
        return;
    }
    if (min_c) {
        int gx, gy;
        genie_dock_icon_point(DOOM_DOCK_INDEX, &gx, &gy);
        genie_start_minimize(&genie, gx, gy);
        minimized = 1;
        return;
    }
    if (zoom_c) {
        if (!is_fullscreen) {
            win_x_rest = win_x; win_y_rest = win_y;
            win_w_rest = win_w; win_h_rest = win_h;
            /* almost whole screen; thin header only */
            win_x = 0; win_y = 0;
            win_w = scr_w; win_h = scr_h;
            is_fullscreen = 1;
        } else {
            win_x = win_x_rest; win_y = win_y_rest;
            win_w = win_w_rest; win_h = win_h_rest;
            is_fullscreen = 0;
        }
    }

    int vx = win_x + (is_fullscreen ? 0 : 4);
    int vy = win_y + HEADER_H + (is_fullscreen ? 0 : 2);
    int vw = win_w - (is_fullscreen ? 0 : 8);
    int vh = win_h - HEADER_H - (is_fullscreen ? 0 : 6);

    if (engine_failed) {
        draw_string("DOOM.WAD missing or failed to init", vx + 8, vy + 20,
                    0x00FFFFFF, buf, (uint32_t)scr_w);
        draw_string("Place src/gui/apps/doom/DOOM.WAD and rebuild", vx + 8, vy + 44,
                    0x00AAAAAA, buf, (uint32_t)scr_w);
    } else if (engine_started) {
        blit_doom_frame(vx, vy, vw, vh);
    } else {
        draw_string("Starting DOOM...", vx + 8, vy + 20, 0x00CCCCCC, buf, (uint32_t)scr_w);
    }
}
