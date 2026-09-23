#ifndef GENIE_ANIM_H
#define GENIE_ANIM_H

typedef enum {
    GENIE_IDLE = 0,
    GENIE_MINIMIZING,
    GENIE_CLOSING,
    GENIE_OPENING
} genie_anim_state_t;

typedef struct {
    genie_anim_state_t state;
    int t;                 /* текущий кадр анимации, 0..ANIM_FRAMES */
    int target_x, target_y; /* точка на иконке дока (экранные координаты) */
} genie_state_t;

void genie_start_minimize(genie_state_t *g, int target_x, int target_y);
void genie_start_close(genie_state_t *g, int target_x, int target_y);
void genie_start_open(genie_state_t *g, int from_x, int from_y);
void genie_cancel(genie_state_t *g);
void genie_tick(genie_state_t *g);
int  genie_is_animating(const genie_state_t *g);
int  genie_should_free_now(const genie_state_t *g);

/* Возвращает интерполированный прямоугольник окна для текущего кадра
 * анимации (real_x/y/w/h -- нормальные "целевые" координаты окна). */
void genie_get_rect(
    const genie_state_t *g,
    int real_x, int real_y, int real_w, int real_h,
    int *out_x, int *out_y, int *out_w, int *out_h
);

/* Экранная точка иконки дока по её индексу (см. desktop.c) -- окно
 * "стекает" именно сюда при сворачивании/закрытии. */
void genie_dock_icon_point(int dock_index, int *out_x, int *out_y);

#endif
