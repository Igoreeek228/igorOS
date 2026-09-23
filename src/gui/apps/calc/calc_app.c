/*
 * calc_app.c
 *
 * Приложение "Калькулятор" для IgorOS Nord.
 *
 * Логика (calc_logic.c / calc_state.h) портирована 1:1 из OriginOS
 * (kernel/calc.c) -- этой фичи не было в IgorOS. Окно и вся отрисовка
 * написаны заново под стиль Big Sur вместо Mac OS X:
 *  - больший радиус скругления окна и кнопок (18 вместо 12),
 *  - плоский полупрозрачный заголовок без градиентной полосы,
 *  - монохромные (не цветные) traffic lights в неактивном состоянии,
 *    подсвечиваются только под курсором -- как в Big Sur,
 *  - кнопки-таблетки вместо плоских квадратов, с лёгкой тенью и
 *    подсветкой при наведении/нажатии.
 */

#include "calc_app.h"
#include "gui/desktop.h"
#include "calc_state.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"

#include <stdint.h>

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
extern void draw_pixel_buf(int x, int y, uint32_t color);

/* Индекс этого приложения в доке (см. desktop.c dock_apps[]) — нужен для
 * genie_dock_icon_point(), чтобы окно стекало именно в иконку Calculator. */
#define CALC_DOCK_INDEX 3

/* ==========================================
   Window state
   ========================================== */

static int is_open = 0;
static int minimized = 0;

static int win_x = 0;
static int win_y = 0;

static int win_w = 300;
static int win_h = 420;

static int positioned = 0;

static int dragging = 0;
static int drag_ox = 0;
static int drag_oy = 0;

static genie_state_t genie;

static calc_state_t calc;
static int calc_ready = 0;

void toggle_calc_app(void)
{
    int dx, dy;
    genie_dock_icon_point(CALC_DOCK_INDEX, &dx, &dy);

    /* If a genie is mid-flight, cancel it so we never get stuck. */
    if (genie_is_animating(&genie))
        genie_cancel(&genie);

    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0;
        dragging = 0;
        return;
    }

    if (is_open) {
        /* Dock click while open → close with genie (was instant before → felt like hang) */
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        minimized = 0;
        dragging = 0;
        return;
    }

    is_open = 1;
    minimized = 0;
    dragging = 0;
    if (!calc_ready) {
        calc_init(&calc);
        calc_ready = 1;
    }
    genie_start_open(&genie, dx, dy);
}

void calc_app_feed_key(char key)
{
    if (!is_open) return;

    if (key == 8 /* backspace */) {
        calc_backspace(&calc);
        return;
    }

    if ((key >= '0' && key <= '9') || key == '.' ||
        key == '+' || key == '-' || key == '*' || key == '/' ||
        key == '=' || key == '\r' || key == '\n')
    {
        calc_feed_char(&calc, (key == '\r' || key == '\n') ? '=' : key);
    }
}

/* ==========================================
   Button grid layout
   ========================================== */

typedef struct {
    const char *label;
    char key;          /* символ, скармливаемый calc_feed_char, 0 = нет действия */
    int is_accent;      /* оранжевая "операторная" кнопка, как на macOS Calculator */
    int is_muted;        /* серые верхние функциональные кнопки (C, +/-, %) */
} calc_btn_t;

static const calc_btn_t buttons[5][4] = {
    { {"C", 'C', 0, 1}, {"+/-", '~', 0, 1}, {"MC", 'm', 0, 1}, {"/", '/', 1, 0} },
    { {"7", '7', 0, 0}, {"8", '8', 0, 0}, {"9", '9', 0, 0}, {"*", '*', 1, 0} },
    { {"4", '4', 0, 0}, {"5", '5', 0, 0}, {"6", '6', 0, 0}, {"-", '-', 1, 0} },
    { {"1", '1', 0, 0}, {"2", '2', 0, 0}, {"3", '3', 0, 0}, {"+", '+', 1, 0} },
    { {"0", '0', 0, 0}, {".", '.', 0, 0}, {"MR", 'r', 0, 1}, {"=", '=', 1, 0} },
};

/* ==========================================
   Render window
   ========================================== */

void render_calc_app_window(
    uint32_t* buf,
    int scr_w,
    int scr_h,
    int mx,
    int my,
    int btn,
    int click
) {
    (void)buf;

    genie_tick(&genie);

    if (!is_open && !genie_is_animating(&genie))
        return;

    if (minimized && !genie_is_animating(&genie))
        return; /* полностью свёрнуто, ждём повторного клика по доку */

    if (!positioned) {
        win_x = (scr_w - win_w) / 2 + 120;
        win_y = (scr_h - win_h) / 2;
        positioned = 1;
    }

    /* Пока идёт genie-анимация (сворачивание/открытие), окно не
     * перетаскивается и не реагирует на клики по кнопкам -- как и в
     * OriginOS ("mid-genie windows aren't clickable"). */
    int mid_genie = genie_is_animating(&genie);

    /* ======================================
       Header drag (Big Sur: только верхняя полоса 38px)
       ====================================== */

    int header_h = 38;
    int traffic_zone_w = 84;

    if (
        !mid_genie &&
        btn &&
        !dragging &&
        win_drag_available() &&
        mx >= win_x &&
        mx <= win_x + win_w - traffic_zone_w &&
        my >= win_y &&
        my <= win_y + header_h
    ) {
        dragging = 1;
        drag_ox = mx - win_x;
        drag_oy = my - win_y;
        win_drag_claim();
        win_set_focused(WIN_ID_CALC_);
    }

    if (!btn)
        dragging = 0;

    if (dragging) {
        win_x = mx - drag_ox;
        win_y = my - drag_oy;
    }

    /* BAG: клики по кнопкам (traffic lights и сетка калькулятора)
     * реагировали, даже если курсор физически над другим, визуально
     * более верхним окном -- клик "проваливался" сквозь чужой title
     * bar. Репортим свой прямоугольник и глушим клик при перекрытии. */
    win_report_rect(WIN_ID_CALC_, win_x, win_y, win_w, win_h, is_open && !mid_genie);
    int occluded = win_click_occluded(WIN_ID_CALC_, mx, my);

    /* ======================================
       Genie: интерполированный прямоугольник для текущего кадра
       ====================================== */

    int draw_x, draw_y, draw_w, draw_h;
    genie_get_rect(&genie, win_x, win_y, win_w, win_h, &draw_x, &draw_y, &draw_w, &draw_h);

    /* Traffic lights рисуются по РЕАЛЬНОМУ (не искажённому genie)
     * положению шапки -- во время анимации всё окно уменьшено, отдельно
     * пересчитывать позиции кружочков под искажённый прямоугольник
     * бессмысленно, т.к. окно всё равно некликабельно в этот момент. */

    /* ======================================
       Traffic lights (Big Sur: приглушённые, ярче только под курсором)
       ====================================== */

    int tl_size = 12;
    int tl_gap = 8;
    int tl_right_margin = 14;

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

    /* ======================================
       Window shadow -- мягче и шире, как в Big Sur
       ====================================== */

    static const struct { int spread; int drop; uint8_t alpha; } shadows[] = {
        {14, 18, 8},
        {11, 15, 12},
        {8,  12, 16},
        {5,   9, 22},
        {2,   6, 30},
    };

    for (unsigned i = 0; i < sizeof(shadows) / sizeof(shadows[0]); i++) {
        int sp = shadows[i].spread;
        draw_rounded_rect_alpha(
            draw_x - sp / 2,
            draw_y - sp / 2 + shadows[i].drop,
            draw_w + sp,
            draw_h + sp,
            18 + sp / 2,
            0x00000000,
            shadows[i].alpha
        );
    }

    /* ======================================
       Main window body -- тёмная панель, как системный калькулятор
       в Big Sur (Dark Mode-подобный вид у всех "утилитных" окон)
       ====================================== */

    draw_rounded_rect_buf(draw_x, draw_y, draw_w, draw_h, 18, 0x001C1C1E);

    /* Во время genie-анимации внутренности окна (шапка/дисплей/кнопки)
     * не перерисовываются поверх искажённого прямоугольника -- как и в
     * OriginOS, там анимированное окно тоже просто "силуэт", детали не
     * успевают быть читаемыми за 14 кадров, и растягивать их под
     * искривлённую геометрию только испортило бы вид. */
    if (mid_genie) {
        goto after_genie_early_out;
    }

    /* Полупрозрачная плоская шапка без разделительной линии-градиента */
    draw_rounded_rect_alpha(win_x, win_y, win_w, header_h, 18, 0x00FFFFFF, 14);
    draw_rect_buf(win_x, win_y + header_h / 2, win_w, header_h / 2, 0x001C1C1E);

    /* Traffic lights: приглушённые тона, загораются под курсором */
    draw_rounded_rect_buf(zoom_x, tl_y, tl_size, tl_size, 4,
        hover_zoom ? 0x0028C93F : 0x003A3A3C);
    draw_rounded_rect_buf(minimize_x, tl_y, tl_size, tl_size, 4,
        hover_minimize ? 0x00FFBD2E : 0x003A3A3C);
    draw_rounded_rect_buf(close_x, tl_y, tl_size, tl_size, 4,

        hover_close ? 0x00FF5F57 : 0x003A3A3C);

    if (click && hover_close && !occluded) {
        int dx, dy;
        genie_dock_icon_point(CALC_DOCK_INDEX, &dx, &dy);
        genie_start_close(&genie, dx, dy);
        is_open = 0; /* genie сам доиграет анимацию закрытия и остановится */
        dragging = 0;
        return;
    }

    if (click && hover_minimize && !occluded) {
        int dx, dy;
        genie_dock_icon_point(CALC_DOCK_INDEX, &dx, &dy);
        genie_start_minimize(&genie, dx, dy);
        minimized = 1;
        dragging = 0;
        return;
    }

    if (click && hover_zoom && !occluded) {
        /* Zoom (зелёная кнопка): упрощённый maximize/restore -- разворачивает
         * окно почти на весь экран и обратно, без genie (в реальном macOS
         * зелёная кнопка тоже не играет genie, а плавно ресайзится, здесь --
         * мгновенно, ресайз-анимации отдельно не реализовывались). */
        static int pre_zoom_x, pre_zoom_y, pre_zoom_w, pre_zoom_h;
        static int is_zoomed = 0;
        if (!is_zoomed) {
            pre_zoom_x = win_x; pre_zoom_y = win_y; pre_zoom_w = win_w; pre_zoom_h = win_h;
            win_x = 40; win_y = 44; win_w = scr_w - 80; win_h = scr_h - 84;
            is_zoomed = 1;
        } else {
            win_x = pre_zoom_x; win_y = pre_zoom_y; win_w = pre_zoom_w; win_h = pre_zoom_h;
            is_zoomed = 0;
        }
    }

    /* ======================================
       Display
       ====================================== */

    int disp_x = win_x + 20;
    int disp_y = win_y + header_h + 18;
    int disp_w = win_w - 40;
    int disp_h = 56;

    draw_rounded_rect_buf(disp_x, disp_y, disp_w, disp_h, 10, 0x00151517);

    int text_w = font_text_width(calc.disp_len ? calc.display : "0");
    int text_x = disp_x + disp_w - text_w - 14;
    int text_y = disp_y + (disp_h - 20) / 2;

    draw_string(
        calc.disp_len ? calc.display : "0",
        text_x,
        text_y,
        0x00F5F5F7,
        buf,
        (uint32_t)scr_w
    );

    /* Индикатор памяти, если в M что-то лежит */
    if (calc.memory != 0) {
        draw_string("M", disp_x + 10, disp_y + 8, 0x00636366, buf, (uint32_t)scr_w);
    }

    /* ======================================
       Button grid
       ====================================== */

    int grid_x = win_x + 16;
    int grid_y = disp_y + disp_h + 16;
    int grid_w = win_w - 32;
    int gap = 10;
    int cell_w = (grid_w - gap * 3) / 4;
    int cell_h = 52;

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            const calc_btn_t *b = &buttons[row][col];

            int bx = grid_x + col * (cell_w + gap);
            int by = grid_y + row * (cell_h + gap);

            int hover =
                mx >= bx && mx <= bx + cell_w &&
                my >= by && my <= by + cell_h;

            uint32_t color;
            if (b->is_accent) {
                color = (hover && btn) ? 0x00E08A2E : (hover ? 0x00FF9F0A : 0x00FF9F0A);
                if (hover && btn) color = 0x00C67B22; /* нажатое состояние темнее */
            } else if (b->is_muted) {
                color = hover ? 0x004A4A4C : 0x003A3A3C;
                if (hover && btn) color = 0x00565658;
            } else {
                color = hover ? 0x00323234 : 0x002C2C2E;
                if (hover && btn) color = 0x00404042;
            }

            /* Кнопки-таблетки: радиус = половина высоты ячейки, как в
             * обновлённом Big Sur Calculator */
            draw_rounded_rect_buf(bx, by, cell_w, cell_h, cell_h / 2, color);

            int lbl_w = font_text_width(b->label);
            int lbl_x = bx + (cell_w - lbl_w) / 2;
            int lbl_y = by + (cell_h - 16) / 2;

            draw_string(b->label, lbl_x, lbl_y, 0x00FFFFFF, buf, (uint32_t)scr_w);

            if (click && hover && b->key && !occluded) {
                calc_feed_char(&calc, b->key);
            }
        }
    }

after_genie_early_out:
    return;
}
