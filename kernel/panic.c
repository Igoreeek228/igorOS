/*
 * panic.c
 *
 * Экран смерти IgorOS Nord.
 *
 * В исходном IgorOS такого экрана не было вообще -- ни одного
 * обработчика CPU-исключений не регистрировалось (см. kernel/isr_stubs.asm
 * и правки в kernel/kernel.c). Любой fault приводил к тихой triple-fault
 * перезагрузке без единой диагностики.
 *
 * Логика/структура сообщения портирована по духу из OriginOS
 * (kernel/idt.c: panic_reason_for, panic_draw_face, panic_screen) --
 * там это уже было полностью реализовано. Отрисовка написана заново,
 * потому что OriginOS рисует через gfx_circle_fill/gfx_line (модуль
 * gfx.c, которого у Igor нет), а здесь используется прямая запись в
 * фреймбуфер Igor (g_fb_vram/g_screen_pitch) -- специально в обход
 * desktop.c backbuffer, т.к. в момент фолта его состояние не гарантировано
 * консистентным.
 */

#include <stdint.h>

extern uint8_t* g_fb_vram;
extern uint32_t g_screen_w;
extern uint32_t g_screen_h;
extern uint32_t g_screen_pitch;

/* ---- прямой доступ к пикселю с учётом pitch (см. предупреждение в
 * desktop_swap_buffers про то, что pitch не всегда равен width*4) ---- */
static void panic_put_pixel(int x, int y, uint32_t color) {
    if (!g_fb_vram) return;
    if (x < 0 || y < 0 || (uint32_t)x >= g_screen_w || (uint32_t)y >= g_screen_h) return;
    uint32_t* row = (uint32_t*)(g_fb_vram + (uint64_t)y * g_screen_pitch);
    row[x] = color;
}

static void panic_fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            panic_put_pixel(x + i, y + j, color);
}

static void panic_fill_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r)
                panic_put_pixel(cx + x, cy + y, color);
        }
    }
}

static void panic_line(int x0, int y0, int x1, int y1, uint32_t color) {
    /* простой Брезенхем -- этого достаточно для короткого сегмента рта */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        panic_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

/* Минималистичный 5x7 текстовый рендер поверх panic_put_pixel -- panic
 * не может зависеть от gui/font.c (тот пишет в буфер по uint32_t* + width,
 * а не с учётом pitch фреймбуфера, и в момент фолта состояние desktop.c
 * не гарантировано валидным). Здесь -- предельно простой моноширинный
 * рендер: для читаемости кода ошибки/сообщения этого достаточно, экран
 * смерти не должен быть художественным, только читаемым. */
extern void draw_char(char c, int x, int y, uint32_t color, uint32_t* buf, uint32_t buf_width);

/* Переиспользуем существующий растровый шрифт Igor (gui/font.c), но
 * рисуем его через собственный панический putpixel, а не через
 * draw_string(..., buf, width) -- сначала рендерим строку во временный
 * маленький буфер нужной ширины, затем построчно копируем в реальный
 * фреймбуфер с учётом pitch. Так текст остаётся тем же самым шрифтом,
 * что и во всём остальном IgorOS Nord (стилистическая цельность), но
 * путь отрисовки не зависит от backbuffer'а desktop.c. */
extern int font_text_width(const char* str);

#define PANIC_TEXT_BUF_W 800
#define PANIC_TEXT_BUF_H 20
static uint32_t panic_text_scratch[PANIC_TEXT_BUF_W * PANIC_TEXT_BUF_H];

static void panic_draw_text_centered(const char* str, int cy, uint32_t color) {
    int w = font_text_width(str);
    if (w <= 0) return;
    if (w > PANIC_TEXT_BUF_W) w = PANIC_TEXT_BUF_W;

    for (int i = 0; i < PANIC_TEXT_BUF_W * PANIC_TEXT_BUF_H; i++)
        panic_text_scratch[i] = 0x00000000;

    /* draw_char/draw_string и так проверяют границы буфера по width,
     * поэтому просто рисуем всю строку в scratch с x=0 */
    extern void draw_string(const char* str, int x, int y, uint32_t color, uint32_t* buf, uint32_t buf_width);
    draw_string(str, 0, 2, color, panic_text_scratch, PANIC_TEXT_BUF_W);

    int dst_x = (int)(g_screen_w / 2) - w / 2;
    for (int y = 0; y < PANIC_TEXT_BUF_H; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = panic_text_scratch[y * PANIC_TEXT_BUF_W + x];
            if ((px & 0x00FFFFFF) != 0) {
                panic_put_pixel(dst_x + x, cy + y, px);
            }
        }
    }
}

/* Грустное лицо -- как в OriginOS panic_draw_face(), только через
 * собственные примитивы окружности/линии вместо gfx_circle_fill/gfx_line. */
static void panic_draw_face(int cx, int cy, uint32_t fg) {
    int r = 40;
    panic_fill_circle(cx, cy, r, 0x001C1C1E);
    panic_fill_circle(cx - 14, cy - 8, 5, fg);
    panic_fill_circle(cx + 14, cy - 8, 5, fg);

    int prev_x = cx - 16, prev_y = cy + 20;
    for (int i = 1; i <= 8; i++) {
        int x = cx - 16 + (32 * i) / 8;
        int t = (i - 4);
        int y = cy + 20 + (t * t) / 6; /* парабола вверх -> нахмуренный рот */
        panic_line(prev_x, prev_y, x, y, fg);
        prev_x = x; prev_y = y;
    }
}

/* Имена исключений x86 (векторы 0-31) -- список 1:1 из OriginOS idt.c,
 * там этот справочник уже был правильно составлен. */
static const char *exception_name[32] = {
    "Division by zero",
    "Debug exception",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection exception",
    "VMM communication exception",
    "Security exception",
    "Reserved",
};

static const char *panic_reason_for(uint64_t vector) {
    if (vector < 32) return exception_name[vector];
    return "Unknown fault";
}

static void itoa_simple(uint64_t v, char *out) {
    char tmp[24];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

/*
 * isr_common_handler
 *
 * Вызывается из kernel/isr_stubs.asm при любом CPU-исключении.
 * rsp в момент вызова указывает на: [vector][error_code][сохранённые
 * регистры...] (см. isr_common_stub) -- нам достаточно только первых
 * двух 64-битных слов, регистры для самого экрана смерти не нужны.
 */
void isr_common_handler(uint64_t *stack_ptr) {
    __asm__ volatile ("cli");

    /* стек уложен как: [r15..rax сохранены push'ами] [vector] [error_code] [rip...] 
     * -- 15 push'ов по 8 байт до vector/error_code */
    uint64_t vector = stack_ptr[15];
    uint64_t error_code = stack_ptr[16];
    (void)error_code;

    if (!g_fb_vram || g_screen_w == 0 || g_screen_h == 0) {
        /* Фреймбуфер ещё не готов -- деваться некуда, просто останавливаем
         * машину, чтобы не product дальнейшую порчу состояния. Полноценный
         * текстовый VGA-фоллбек (как в OriginOS) сюда не переносился:
         * IgorOS уже требует Limine-фреймбуфер для любого другого экрана
         * (загрузочного лого и т.д.), так что фолт до готовности фреймбуфера
         * означает, что сама платформа не даёт графики вообще -- в этом
         * случае никакой graceful-путь всё равно не поможет. */
        for (;;) { __asm__ volatile ("hlt"); }
    }

    /* Заливаем экран чёрным напрямую в фреймбуфер */
    panic_fill_rect(0, 0, (int)g_screen_w, (int)g_screen_h, 0x00000000);

    int cy = (int)(g_screen_h / 2);

    panic_draw_face((int)(g_screen_w / 2), cy - 90, 0x00EBEBEB);

    panic_draw_text_centered("IgorOS Nord ran into a problem and had to stop.", cy - 30, 0x00EBEBEB);

    char line[128];
    char num[24];
    const char *reason = panic_reason_for(vector);

    itoa_simple(vector, num);

    int p = 0;
    const char *prefix = "Error code: ";
    while (prefix[p]) { line[p] = prefix[p]; p++; }
    int q = 0;
    while (num[q]) { line[p++] = num[q++]; }
    line[p++] = ' '; line[p++] = ' '; line[p++] = '(';
    int r = 0;
    while (reason[r] && p < 120) { line[p++] = reason[r++]; }
    line[p++] = ')';
    line[p] = 0;

    panic_draw_text_centered(line, cy - 4, 0x00DC4646);

    panic_draw_text_centered("The system has been halted to prevent further damage.", cy + 30, 0x00969696);
    panic_draw_text_centered("IgorOS Nord", cy + 60, 0x00EBEBEB);

    for (;;) { __asm__ volatile ("hlt"); }
}
