/*
 * terminal_app.c
 *
 * Приложение "Terminal" для IgorOS Nord.
 *
 * Набор команд вдохновлён OriginOS (kernel/terminal.c): help, clear,
 * pwd, whoami, echo, date, panic/crash. Команды, завязанные на
 * виртуальную файловую систему OriginOS (ls/cd/cat/mkdir/touch),
 * сюда НЕ перенесены -- у IgorOS другой стек хранения (FAT32/ATA,
 * см. src/drivers/system/fat32.c), и подделывать вывод несуществующей
 * команды было бы просто враньём в интерфейсе. Если позже понадобится
 * реальная файловая оболочка -- её стоит писать поверх fat32.c, а не
 * симулировать.
 *
 * UI написан заново под Big Sur: тёмное окно, скруглённые углы 18px,
 * приглушённые traffic lights, моноширинный вывод.
 */

#include "terminal_app.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"

#include <stdint.h>

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);

#define TERM_ROWS      12
#define TERM_LINE_MAX  64
#define TERM_INPUT_MAX 48
#define TERM_DOCK_INDEX 1

static int is_open = 0;
static int minimized = 0;
static int win_x = 0, win_y = 0;
static int win_w = 560, win_h = 360;
static int positioned = 0;
static int dragging = 0;
static int drag_ox = 0, drag_oy = 0;
static genie_state_t genie;

static char lines[TERM_ROWS][TERM_LINE_MAX];
static int line_count = 0;
static char input[TERM_INPUT_MAX];
static uint32_t input_len = 0;
static int booted = 0;

/* ---- крошечные строковые хелперы (своя копия, без kstring.h) ---- */

static uint32_t t_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void t_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int t_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void t_split_first_word(const char *line, char *cmd, uint32_t cmd_max,
                                char *rest, uint32_t rest_max) {
    uint32_t i = 0, ci = 0;
    while (line[i] == ' ') i++;
    while (line[i] && line[i] != ' ' && ci < cmd_max - 1) cmd[ci++] = line[i++];
    cmd[ci] = 0;
    while (line[i] == ' ') i++;
    uint32_t ri = 0;
    while (line[i] && ri < rest_max - 1) rest[ri++] = line[i++];
    rest[ri] = 0;
}

static void term_push_line(const char *line) {
    if (line_count < TERM_ROWS) {
        t_strcpy(lines[line_count], line, TERM_LINE_MAX);
        line_count++;
    } else {
        for (int i = 1; i < TERM_ROWS; i++) t_strcpy(lines[i - 1], lines[i], TERM_LINE_MAX);
        t_strcpy(lines[TERM_ROWS - 1], line, TERM_LINE_MAX);
    }
}

static void term_exec(const char *cmdline) {
    char prompt_line[TERM_LINE_MAX];
    t_strcpy(prompt_line, "~ $ ", TERM_LINE_MAX);
    /* аккуратно склеиваем без переполнения */
    uint32_t base = t_strlen(prompt_line);
    uint32_t i = 0;
    while (cmdline[i] && base + i < TERM_LINE_MAX - 1) { prompt_line[base + i] = cmdline[i]; i++; }
    prompt_line[base + i] = 0;
    term_push_line(prompt_line);

    char cmd[16], rest[TERM_INPUT_MAX];
    t_split_first_word(cmdline, cmd, sizeof(cmd), rest, sizeof(rest));

    if (t_strlen(cmd) == 0) {
        return;
    } else if (t_streq(cmd, "help")) {
        term_push_line("COMMANDS: HELP CLEAR PWD WHOAMI ECHO DATE");
    } else if (t_streq(cmd, "clear")) {
        line_count = 0;
    } else if (t_streq(cmd, "pwd")) {
        term_push_line("/");
    } else if (t_streq(cmd, "whoami")) {
        term_push_line("ROOT@IGOROS-NORD");
    } else if (t_streq(cmd, "echo")) {
        term_push_line(rest);
    } else if (t_streq(cmd, "date")) {
        term_push_line("IGOROS NORD DOES NOT TRACK REAL TIME YET");
    } else if (t_streq(cmd, "panic") || t_streq(cmd, "crash")) {
        __asm__ volatile (
            "xor %%eax, %%eax\n\t"
            "xor %%edx, %%edx\n\t"
            "xor %%ecx, %%ecx\n\t"
            "idiv %%ecx\n\t"
            : : : "eax", "edx", "ecx"
        );
    } else {
        char line[TERM_LINE_MAX];
        t_strcpy(line, "UNKNOWN COMMAND: ", TERM_LINE_MAX);
        uint32_t b2 = t_strlen(line);
        uint32_t j = 0;
        while (cmd[j] && b2 + j < TERM_LINE_MAX - 1) { line[b2 + j] = cmd[j]; j++; }
        line[b2 + j] = 0;
        term_push_line(line);
    }
}

void toggle_terminal_app(void)
{
    int dx, dy;
    genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);

    if (genie_is_animating(&genie))
        genie_cancel(&genie);

    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0;
        dragging = 0;
        return;
    }

    if (is_open) {
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        minimized = 0;
        dragging = 0;
        return;
    }

    is_open = 1;
    minimized = 0;
    dragging = 0;
    if (!booted) {
        term_push_line("IGOROS NORD TERMINAL - TYPE HELP");
        booted = 1;
    }
    genie_start_open(&genie, dx, dy);
}

void terminal_app_feed_key(char key)
{
    if (!is_open) return;

    if (key == '\r' || key == '\n') {
        input[input_len] = 0;
        term_exec(input);
        input_len = 0;
        input[0] = 0;
    } else if (key == 8 /* backspace */) {
        if (input_len > 0) input[--input_len] = 0;
    } else if (key >= 32 && key < 127) {
        if (input_len < TERM_INPUT_MAX - 1) {
            input[input_len++] = key;
            input[input_len] = 0;
        }
    }
}

void render_terminal_app_window(
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
        win_x = (scr_w - win_w) / 2 - 140;
        win_y = (scr_h - win_h) / 2 - 40;
        positioned = 1;
    }

    int mid_genie = genie_is_animating(&genie);

    int header_h = 34;
    int traffic_zone_w = 84;

    if (!mid_genie && btn && !dragging && win_drag_available() &&
        mx >= win_x && mx <= win_x + win_w - traffic_zone_w &&
        my >= win_y && my <= win_y + header_h)
    {
        dragging = 1;
        drag_ox = mx - win_x;
        drag_oy = my - win_y;
        win_drag_claim();
        win_set_focused(WIN_ID_TERMINAL_);
    }
    if (!btn) dragging = 0;
    if (dragging) { win_x = mx - drag_ox; win_y = my - drag_oy; }

    /* BAG: клик по кнопкам заголовка (close/minimize/zoom) реагировал
     * даже когда физически перекрыт другим, визуально более верхним
     * окном -- клик "проваливался" сквозь чужой title bar. Репортим
     * свой прямоугольник и глушим клик, если курсор сейчас над
     * окном выше нас по z-order. */
    win_report_rect(WIN_ID_TERMINAL_, win_x, win_y, win_w, win_h, is_open && !mid_genie);
    int occluded = win_click_occluded(WIN_ID_TERMINAL_, mx, my);

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

    /* Терминал -- почти чёрный, полупрозрачное "стекло" сверху, как
     * Big Sur Terminal.app в тёмной теме */
    draw_rounded_rect_buf(draw_x, draw_y, draw_w, draw_h, 18, 0x00151516);

    if (mid_genie) return; /* силуэт во время анимации, без деталей внутри */

    draw_rounded_rect_alpha(win_x, win_y, win_w, header_h, 18, 0x00FFFFFF, 10);
    draw_rect_buf(win_x, win_y + header_h / 2, win_w, header_h / 2, 0x00151516);

    draw_rounded_rect_buf(zoom_x, tl_y, tl_size, tl_size, 4,
        hover_zoom ? 0x0028C93F : 0x003A3A3C);
    draw_rounded_rect_buf(minimize_x, tl_y, tl_size, tl_size, 4,
        hover_minimize ? 0x00FFBD2E : 0x003A3A3C);
    draw_rounded_rect_buf(close_x, tl_y, tl_size, tl_size, 4,
        hover_close ? 0x00FF5F57 : 0x003A3A3C);

    if (click && hover_close && !occluded) {
        int dx, dy;
        genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        dragging = 0;
        return;
    }

    if (click && hover_minimize && !occluded) {
        int dx, dy;
        genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);
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

    /* ---- вывод ---- */
    int content_x = win_x + 14;
    int content_y = win_y + header_h + 10;
    int line_h = 18;

    for (int i = 0; i < line_count; i++) {
        uint32_t color = 0x0032D74B; /* зелёный, классика терминала */
        if (lines[i][0] == 'U' && lines[i][1] == 'N') color = 0x00FF6B5C; /* UNKNOWN COMMAND */
        draw_string(lines[i], content_x, content_y + i * line_h, color, buf, (uint32_t)scr_w);
    }

    /* ---- строка ввода с курсором ---- */
    char prompt[TERM_LINE_MAX + TERM_INPUT_MAX];
    t_strcpy(prompt, "~ $ ", sizeof(prompt));
    uint32_t base = t_strlen(prompt);
    uint32_t k = 0;
    while (input[k] && base + k < sizeof(prompt) - 1) { prompt[base + k] = input[k]; k++; }
    prompt[base + k] = 0;

    int prompt_y = content_y + line_count * line_h;
    draw_string(prompt, content_x, prompt_y, 0x0064D2FF, buf, (uint32_t)scr_w);

    int caret_x = content_x + font_text_width(prompt) + 2;
    draw_rect_buf(caret_x, prompt_y, 7, 14, 0x0064D2FF);
}
