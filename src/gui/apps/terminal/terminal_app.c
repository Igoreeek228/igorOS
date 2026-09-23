// src/gui/apps/terminal/terminal_app.c
//
// Терминал igorOS: окно с построчным буфером вывода, строкой ввода,
// мигающим курсором и набором команд (help, clear, echo, date,
// sysinfo, ls, cat). Доступ к диску — только чтение (fat32).

#include <stdint.h>

#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/apps/terminal/terminal_app.h"
#include "drivers/system/fat32.h"
#include "drivers/system/sound_manager.h"

/* ---------- геометрия окна ---------- */

#define TERM_WIN_W      620
#define TERM_WIN_H      430
#define TERM_TITLE_H    32
#define TERM_TEXT_X     12
#define TERM_TEXT_TOP   (TERM_TITLE_H + 10)
#define TERM_LINE_H     18

/* ---------- буфер строк ---------- */

#define TERM_MAX_LINES  200
#define TERM_LINE_LEN   96
#define TERM_INPUT_LEN  (TERM_LINE_LEN - 1)

/* ---------- цвета ---------- */

#define COL_BG      0x000F0F12
#define COL_TITLE   0x002C2C2E
#define COL_TEXT    0x00E8E8E8
#define COL_PROMPT  0x0030D158
#define COL_ERR     0x00FF453A
#define COL_WARN    0x00FFD60A

/* ---------- состояние ---------- */

static int is_open = 0;
static int welcomed = 0;

static char     lines[TERM_MAX_LINES][TERM_LINE_LEN];
static uint32_t line_colors[TERM_MAX_LINES];
static int      line_count = 0;

static char input[TERM_INPUT_LEN + 1];
static int  input_len = 0;

static int blink_counter = 0;

static int scr_w_cached = 1280;
static int scr_h_cached = 720;

static int disk_state = -1; /* -1: ещё не опрашивали */

#define TERM_PROMPT "igor@igoros:~$ "

/* ---------- порт I/O (RTC) ---------- */

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ---------- строки без libc ---------- */

static int tstr_len(const char* s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static void tstr_copy(char* dst, const char* src, int max)
{
    int i = 0;
    while (i < max && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int tstr_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void uint_to_str(uint32_t v, char* out)
{
    char tmp[12];
    int i = 0;

    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    int j = 0;
    while (i > 0)
        out[j++] = tmp[--i];
    out[j] = 0;
}

static void two_digits(char* out, int v)
{
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = 0;
}

/* ---------- вывод ---------- */

/* Добавляет строку в историю; при переполнении буфера старые строки
 * сдвигаются вверх (без использования библиотечного memmove). */
static void print_line(const char* text, uint32_t color)
{
    int idx;

    if (line_count < TERM_MAX_LINES) {
        idx = line_count++;
    } else {
        for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
            for (int j = 0; j < TERM_LINE_LEN; j++)
                lines[i][j] = lines[i + 1][j];
            line_colors[i] = line_colors[i + 1];
        }
        idx = TERM_MAX_LINES - 1;
    }

    tstr_copy(lines[idx], text, TERM_LINE_LEN - 1);
    line_colors[idx] = color;
}

/* ---------- RTC ---------- */

static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)((v / 16) * 10 + (v % 16));
}

static void cmd_date(void)
{
    outb(0x70, 0x0B);
    uint8_t regB = inb(0x71);

    outb(0x70, 0x00); uint8_t sec  = inb(0x71);
    outb(0x70, 0x02); uint8_t min  = inb(0x71);
    outb(0x70, 0x04); uint8_t hour = inb(0x71);
    outb(0x70, 0x07); uint8_t day  = inb(0x71);
    outb(0x70, 0x08); uint8_t mon  = inb(0x71);
    outb(0x70, 0x09); uint8_t yr   = inb(0x71);

    if (!(regB & 0x04)) {
        sec = bcd2bin(sec);
        min = bcd2bin(min);
        hour = bcd2bin(hour);
        day = bcd2bin(day);
        mon = bcd2bin(mon);
        yr = bcd2bin(yr);
    }

    char buf[32];
    char part[4];
    int p = 0;

    two_digits(part, day);  buf[p++] = part[0]; buf[p++] = part[1]; buf[p++] = '.';
    two_digits(part, mon);  buf[p++] = part[0]; buf[p++] = part[1]; buf[p++] = '.';
    tstr_copy(buf + p, "20", 2); p += 2;
    two_digits(part, yr);   buf[p++] = part[0]; buf[p++] = part[1]; buf[p++] = ' ';
    two_digits(part, hour); buf[p++] = part[0]; buf[p++] = part[1]; buf[p++] = ':';
    two_digits(part, min);  buf[p++] = part[0]; buf[p++] = part[1]; buf[p++] = ':';
    two_digits(part, sec);  buf[p++] = part[0]; buf[p++] = part[1];
    buf[p] = 0;

    print_line(buf, COL_TEXT);
}

/* ---------- диск (только чтение) ---------- */

static void ensure_disk(void)
{
    if (disk_state < 0)
        disk_state = fat32_init();
}

static void cmd_ls(void)
{
    ensure_disk();

    if (!disk_state) {
        print_line("ls: FAT32-раздел не найден (нужен ATA-диск)", COL_ERR);
        return;
    }

    static fat32_entry_t entries[32];
    int count = fat32_list_dir(fat32_root_cluster(), entries, 32);

    if (count <= 0) {
        print_line("ls: корневой каталог пуст", COL_WARN);
        return;
    }

    char line[TERM_LINE_LEN];
    char num[12];
    for (int i = 0; i < count; i++) {
        int p = 0;
        if (entries[i].is_dir) {
            tstr_copy(line, "[DIR]  ", 9);
            p = 7;
        } else {
            tstr_copy(line, "       ", 9);
            p = 7;
        }
        tstr_copy(line + p, entries[i].name, TERM_LINE_LEN - p - 14);
        while (line[p])
            p++;
        if (!entries[i].is_dir) {
            line[p++] = ' ';
            uint_to_str(entries[i].size, num);
            tstr_copy(line + p, num, 10);
            while (line[p])
                p++;
            tstr_copy(line + p, " B", 3);
        }
        print_line(line, COL_TEXT);
    }
}

static void cmd_cat(const char* name)
{
    if (!name[0]) {
        print_line("cat: укажите имя файла, напр.: cat readme.txt", COL_ERR);
        return;
    }

    ensure_disk();
    if (!disk_state) {
        print_line("cat: FAT32-раздел не найден", COL_ERR);
        return;
    }

    static fat32_entry_t entries[32];
    int count = fat32_list_dir(fat32_root_cluster(), entries, 32);

    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir || !tstr_eq(entries[i].name, name))
            continue;

        static uint8_t cat_buf[4096];
        uint32_t got = fat32_read_file(entries[i].first_cluster,
                                       entries[i].size,
                                       cat_buf, sizeof(cat_buf));

        /* Печатаем содержимое построчно, непечатные символы -> '.' */
        char line[TERM_LINE_LEN];
        int lp = 0;
        for (uint32_t k = 0; k < got; k++) {
            char ch = (char)cat_buf[k];
            if (ch == '\n') {
                line[lp] = 0;
                print_line(line, COL_TEXT);
                lp = 0;
            } else if (ch == '\r') {
                continue;
            } else if (ch >= 32 && ch < 127) {
                if (lp < TERM_LINE_LEN - 1)
                    line[lp++] = ch;
            } else {
                if (lp < TERM_LINE_LEN - 1)
                    line[lp++] = '.';
            }
        }
        if (lp > 0) {
            line[lp] = 0;
            print_line(line, COL_TEXT);
        }
        return;
    }

    print_line("cat: файл не найден", COL_ERR);
}

/* ---------- прочие команды ---------- */

static void cmd_help(void)
{
    print_line("Доступные команды:", COL_WARN);
    print_line("  help          - этот список", COL_TEXT);
    print_line("  clear         - очистить экран", COL_TEXT);
    print_line("  echo <текст>  - вывести текст", COL_TEXT);
    print_line("  date          - дата и время (RTC)", COL_TEXT);
    print_line("  sysinfo       - сведения о системе", COL_TEXT);
    print_line("  ls            - список файлов на FAT32-диске", COL_TEXT);
    print_line("  cat <файл>    - показать файл с диска", COL_TEXT);
}

static void cmd_sysinfo(void)
{
    char line[TERM_LINE_LEN];
    char num[12];

    print_line("igorOS 0.5 (Aurora), x86_64", COL_WARN);
    print_line("Загрузчик: Limine (BIOS/UEFI)", COL_TEXT);

    uint_to_str((uint32_t)scr_w_cached, num);
    tstr_copy(line, "Экран: ", 9);
    int p = 7;
    tstr_copy(line + p, num, 12);
    while (line[p]) p++;
    line[p++] = 'x';
    uint_to_str((uint32_t)scr_h_cached, num);
    tstr_copy(line + p, num, 12);
    print_line(line, COL_TEXT);

    audio_dev_t dev = sound_active_device();
    if (dev == AUDIO_DEV_AC97)
        print_line("Звук: AC'97", COL_TEXT);
    else if (dev == AUDIO_DEV_HDA)
        print_line("Звук: Intel HDA (без вывода звука)", COL_TEXT);
    else
        print_line("Звук: устройство не найдено", COL_TEXT);

    print_line("Клавиатура: раскладка US", COL_TEXT);
}

/* ---------- исполнение введенной строки ---------- */

static void exec_command(void)
{
    char full[TERM_LINE_LEN + 32];
    tstr_copy(full, TERM_PROMPT, sizeof(full) - 1);
    tstr_copy(full + tstr_len(full), input, sizeof(full) - tstr_len(full) - 1);
    print_line(full, COL_PROMPT);

    char cmd[TERM_LINE_LEN];
    char arg[TERM_LINE_LEN];
    int i = 0;

    while (input[i] && input[i] != ' ')
        i++;
    tstr_copy(cmd, input, (i < TERM_LINE_LEN - 1) ? i : TERM_LINE_LEN - 1);

    while (input[i] == ' ')
        i++;
    tstr_copy(arg, input + i, TERM_LINE_LEN - 1);

    if (!cmd[0]) {
        return;
    } else if (tstr_eq(cmd, "help")) {
        cmd_help();
    } else if (tstr_eq(cmd, "clear")) {
        line_count = 0;
    } else if (tstr_eq(cmd, "echo")) {
        print_line(arg[0] ? arg : "", COL_TEXT);
    } else if (tstr_eq(cmd, "date")) {
        cmd_date();
    } else if (tstr_eq(cmd, "sysinfo")) {
        cmd_sysinfo();
    } else if (tstr_eq(cmd, "ls")) {
        cmd_ls();
    } else if (tstr_eq(cmd, "cat")) {
        cmd_cat(arg);
    } else {
        print_line("Неизвестная команда. Введите 'help'.", COL_ERR);
    }

    input_len = 0;
    input[0] = 0;
}

/* ---------- публичный API ---------- */

void toggle_terminal(void)
{
    is_open = !is_open;

    if (is_open && !welcomed) {
        welcomed = 1;
        print_line("igorOS Terminal (введите 'help')", COL_WARN);
    }
}

int terminal_is_open(void)
{
    return is_open;
}

void terminal_feed_key(char c)
{
    if (!is_open)
        return;

    if (c == '\n') {
        exec_command();
    } else if (c == '\b') {
        if (input_len > 0) {
            input_len--;
            input[input_len] = 0;
        }
    } else if (c >= 32 && c < 127 && input_len < TERM_INPUT_LEN) {
        input[input_len++] = c;
        input[input_len] = 0;
    }
}

/* ---------- отрисовка ---------- */

void render_terminal_window(uint32_t* buf, int scr_w, int scr_h,
                            int mx, int my, int btn, int click)
{
    (void)btn;

    if (!is_open)
        return;

    scr_w_cached = scr_w;
    scr_h_cached = scr_h;

    int win_x = (scr_w - TERM_WIN_W) / 2;
    int win_y = 60;

    if (win_x < 4)
        win_x = 4;
    if (win_y + TERM_WIN_H > scr_h - 4)
        win_y = scr_h - TERM_WIN_H - 4;
    if (win_y < 4)
        win_y = 4;

    /* Тень и корпус */
    draw_rounded_rect_alpha(win_x + 4, win_y + 4, TERM_WIN_W, TERM_WIN_H, 12,
                            0x00000000, 90);
    draw_rounded_rect_buf(win_x, win_y, TERM_WIN_W, TERM_WIN_H, 12, COL_BG);

    /* Титульная панель */
    draw_rounded_rect_buf(win_x, win_y, TERM_WIN_W, TERM_TITLE_H, 12, COL_TITLE);
    draw_rect_buf(win_x, win_y + TERM_TITLE_H - 12, TERM_WIN_W, 12, COL_TITLE);

    draw_string("Terminal", win_x + TERM_WIN_W / 2 - 30, win_y + 8,
                0x00FFFFFF, buf, (uint32_t)scr_w);

    /* Кнопка закрытия */
    draw_rounded_rect_buf(win_x + 12, win_y + 10, 12, 12, 6, 0x00FF5F56);
    if (click && mx >= win_x + 12 && mx <= win_x + 24 &&
        my >= win_y + 10 && my <= win_y + 22) {
        is_open = 0;
        return;
    }

    /* Текст */
    int visible = (TERM_WIN_H - TERM_TEXT_TOP - 10) / TERM_LINE_H;
    if (visible < 1)
        visible = 1;
    if (visible > TERM_MAX_LINES)
        visible = TERM_MAX_LINES;

    int start = line_count - (visible - 1);
    if (start < 0)
        start = 0;

    for (int i = start; i < line_count; i++) {
        int row = i - start;
        draw_string(lines[i], win_x + TERM_TEXT_X,
                    win_y + TERM_TEXT_TOP + row * TERM_LINE_H,
                    line_colors[i], buf, (uint32_t)scr_w);
    }

    /* Строка ввода + мигающий курсор */
    int in_row = line_count - start;
    if (in_row >= visible)
        in_row = visible - 1;

    int y = win_y + TERM_TEXT_TOP + in_row * TERM_LINE_H;
    int x = win_x + TERM_TEXT_X;

    draw_string(TERM_PROMPT, x, y, COL_PROMPT, buf, (uint32_t)scr_w);
    x += font_text_width(TERM_PROMPT);

    draw_string(input, x, y, COL_TEXT, buf, (uint32_t)scr_w);
    x += font_text_width(input);

    blink_counter++;
    if ((blink_counter / 25) % 2 == 0)
        draw_rect_buf(x + 1, y + 2, 7, 13, COL_TEXT);
}
