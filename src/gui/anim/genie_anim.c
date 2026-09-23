/*
 * genie_anim.c — macOS-like genie minimize/open/close.
 * Integer-only, safe state machine (no stuck animations).
 */

#include "genie_anim.h"

/* ~18 frames ≈ 0.3s at 60fps */
#define ANIM_FRAMES 18

static int ease_progress(int t, int frames, int reverse) {
    if (t <= 0) return 0;
    if (t >= frames) return 256;
    int lin = (t * 256) / frames;
    if (!reverse) {
        /* ease-out cubic */
        int inv = 256 - lin;
        int inv2 = (inv * inv) >> 8;
        int inv3 = (inv2 * inv) >> 8;
        return 256 - inv3;
    }
    /* ease-in cubic */
    int lin2 = (lin * lin) >> 8;
    return (lin2 * lin) >> 8;
}

void genie_start_minimize(genie_state_t *g, int target_x, int target_y) {
    if (!g) return;
    g->state = GENIE_MINIMIZING;
    g->t = 0;
    g->target_x = target_x;
    g->target_y = target_y;
}

void genie_start_close(genie_state_t *g, int target_x, int target_y) {
    if (!g) return;
    g->state = GENIE_CLOSING;
    g->t = 0;
    g->target_x = target_x;
    g->target_y = target_y;
}

void genie_start_open(genie_state_t *g, int from_x, int from_y) {
    if (!g) return;
    g->state = GENIE_OPENING;
    g->t = 0;
    g->target_x = from_x;
    g->target_y = from_y;
}

void genie_cancel(genie_state_t *g) {
    if (!g) return;
    g->state = GENIE_IDLE;
    g->t = 0;
}

void genie_tick(genie_state_t *g) {
    if (!g || g->state == GENIE_IDLE) return;
    g->t++;
    if (g->t >= ANIM_FRAMES) {
        g->state = GENIE_IDLE;
        g->t = 0;
    }
}

int genie_is_animating(const genie_state_t *g) {
    return g && g->state != GENIE_IDLE;
}

int genie_should_free_now(const genie_state_t *g) {
    return g && g->state == GENIE_IDLE && g->t == 0;
}

void genie_get_rect(
    const genie_state_t *g,
    int real_x, int real_y, int real_w, int real_h,
    int *out_x, int *out_y, int *out_w, int *out_h
) {
    if (!g || g->state == GENIE_IDLE) {
        *out_x = real_x; *out_y = real_y; *out_w = real_w; *out_h = real_h;
        return;
    }

    int p;
    if (g->state == GENIE_MINIMIZING || g->state == GENIE_CLOSING) {
        p = ease_progress(g->t, ANIM_FRAMES, 0);
    } else {
        p = 256 - ease_progress(g->t, ANIM_FRAMES, 1);
    }

    int w_shrink = (p * p) >> 8;
    int h_shrink = (p * 200) >> 8;
    if (h_shrink > 256) h_shrink = 256;

    *out_w = (real_w * (256 - w_shrink)) / 256;
    if (*out_w < 2) *out_w = 2;
    *out_h = (real_h * (256 - h_shrink)) / 256;
    if (*out_h < 2) *out_h = 2;

    int cx = real_x + real_w / 2;
    *out_x = cx + (((g->target_x - cx) * p) >> 8) - *out_w / 2;

    int bottom = real_y + real_h;
    int cur_bottom = bottom + (((g->target_y - bottom) * p) >> 8);
    *out_y = cur_bottom - *out_h;
}
