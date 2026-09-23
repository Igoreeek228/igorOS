#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "limine.h"
#include "include/idt.h"
#include "timer.h"
#include "../boot/loading/load_logo.h"
#include "../src/gui/font.h"

__attribute__((used, section(".requests")))
static volatile uint64_t limine_base_revision[3] = {
    0xf9562b2d5c95a6c8,
    0x6a7b384944536bdc,
    0
};

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

uint8_t* g_fb_vram = 0;
uint32_t g_screen_w = 0;
uint32_t g_screen_h = 0;
uint32_t g_screen_pitch = 0;
uint32_t g_screen_bpp = 32;

extern void init_mouse(void);
extern void mouse_set_bounds(uint32_t width, uint32_t height);
extern void desktop_init(uint8_t* vram, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp);
extern void desktop_run(void);

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
}

static void serial_print(const char *str) {
    while (*str) outb(0x3F8, *str++);
}

static void disable_pic(void) {
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

static void enable_sse(void) {
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2);
    cr0 |= (1 << 1);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));
}

// Задержка на CMOS RTC (в секундах). Больше не используется в самом сплэше
// (там теперь spin_delay для плавной анимации), но оставил — может
// пригодиться где-то ещё (например, для паузы на экране выключения).
__attribute__((unused))
static void sleep_rtc(uint32_t seconds) {
    for (uint32_t s = 0; s < seconds; s++) {
        outb(0x70, 0x00);
        uint8_t last_sec = inb(0x71);
        while (1) {
            outb(0x70, 0x00);
            uint8_t curr_sec = inb(0x71);
            if (curr_sec != last_sec) break;
            __asm__ volatile ("pause");
        }
    }
}

__attribute__((noreturn))
static void halt(void) {
    serial_print("[HALT] System halted.\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void kernel_clear_screen(uint32_t color) {
    if (!g_fb_vram) return;
    uint32_t bytes_per_pixel = g_screen_bpp / 8;
    if (bytes_per_pixel == 0) bytes_per_pixel = 4;

    for (uint32_t y = 0; y < g_screen_h; y++) {
        uint8_t* row = g_fb_vram + (y * g_screen_pitch);
        for (uint32_t x = 0; x < g_screen_w; x++) {
            if (bytes_per_pixel == 4) {
                ((uint32_t*)row)[x] = color;
            } else if (bytes_per_pixel == 3) {
                row[x * 3 + 0] = (color) & 0xFF;
                row[x * 3 + 1] = (color >> 8) & 0xFF;
                row[x * 3 + 2] = (color >> 16) & 0xFF;
            }
        }
    }
}

// Заливка прямоугольника сплошным цветом — нужен для рисования полосы
// прогресса загрузки (рамка + заполнение).
static void fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!g_fb_vram) return;
    uint32_t bytes_per_pixel = g_screen_bpp / 8;
    if (bytes_per_pixel == 0) bytes_per_pixel = 4;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > (int)g_screen_w) x1 = (int)g_screen_w;
    int y1 = y + h; if (y1 > (int)g_screen_h) y1 = (int)g_screen_h;

    for (int py = y0; py < y1; py++) {
        uint8_t* row = g_fb_vram + ((uint32_t)py * g_screen_pitch);
        for (int px = x0; px < x1; px++) {
            if (bytes_per_pixel == 4) {
                ((uint32_t*)row)[px] = color;
            } else if (bytes_per_pixel == 3) {
                row[px * 3 + 0] = color & 0xFF;
                row[px * 3 + 1] = (color >> 8) & 0xFF;
                row[px * 3 + 2] = (color >> 16) & 0xFF;
            }
        }
    }
}

// Грубая пауза на CPU-тактах (не привязана к RTC, у которой шаг — целая
// секунда — для анимации прогресс-бара это слишком крупно). Абсолютная
// длительность "плывёт" в зависимости от скорости хоста/эмулятора, но
// для сплэш-экрана точность не критична — важно, чтобы полоса заметно
// и плавно заполнялась, а не дёргалась скачками раз в секунду.
static void spin_delay(uint32_t iterations) {
    for (volatile uint32_t i = 0; i < iterations; i++) {
        __asm__ volatile ("nop");
    }
}

// Общий расчёт адаптивного размера — используется и логотипом, и
// прогресс-баром под ним, чтобы бар всегда попадал точно под лого
// независимо от разрешения экрана.
static int compute_splash_logo_size(void) {
    int min_side = (int)((g_screen_w < g_screen_h) ? g_screen_w : g_screen_h);
    int target_size = (min_side * 22) / 100;
    if (target_size < 64) target_size = 64;
    if (target_size > 800) target_size = 800;
    return target_size;
}

static void draw_logo_to_screen_alpha(const unsigned char *logo, int src_w, int src_h, int alpha) {
    if (!g_fb_vram || !logo) return;
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    uint32_t bytes_per_pixel = g_screen_bpp / 8;
    if (bytes_per_pixel == 0) bytes_per_pixel = 4;

    int width = compute_splash_logo_size();
    int height = width;

    int start_x = ((int)g_screen_w - width) / 2;
    int start_y = ((int)g_screen_h - height) / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int target_x = start_x + x;
            int target_y = start_y + y;

            if (target_x >= 0 && target_x < (int)g_screen_w &&
                target_y >= 0 && target_y < (int)g_screen_h) {

                // Билинейная выборка из исходных src_w x src_h пикселей логотипа
                // (256 = масштаб фиксированной точки для дробной части координаты).
                int gx = (width  > 1) ? (x * (src_w - 1) * 256) / (width  - 1) : 0;
                int gy = (height > 1) ? (y * (src_h - 1) * 256) / (height - 1) : 0;
                int x0 = gx >> 8, y0 = gy >> 8;
                int x1 = (x0 < src_w - 1) ? x0 + 1 : x0;
                int y1 = (y0 < src_h - 1) ? y0 + 1 : y0;
                int fx = gx & 0xFF, fy = gy & 0xFF;

                int idx00 = (y0 * src_w + x0) * 3;
                int idx10 = (y0 * src_w + x1) * 3;
                int idx01 = (y1 * src_w + x0) * 3;
                int idx11 = (y1 * src_w + x1) * 3;

                uint8_t r = (uint8_t)((logo[idx00]     * (256 - fx) * (256 - fy) +
                                       logo[idx10]     * fx         * (256 - fy) +
                                       logo[idx01]     * (256 - fx) * fy +
                                       logo[idx11]     * fx         * fy) >> 16);
                uint8_t g = (uint8_t)((logo[idx00 + 1] * (256 - fx) * (256 - fy) +
                                       logo[idx10 + 1] * fx         * (256 - fy) +
                                       logo[idx01 + 1] * (256 - fx) * fy +
                                       logo[idx11 + 1] * fx         * fy) >> 16);
                uint8_t b = (uint8_t)((logo[idx00 + 2] * (256 - fx) * (256 - fy) +
                                       logo[idx10 + 2] * fx         * (256 - fy) +
                                       logo[idx01 + 2] * (256 - fx) * fy +
                                       logo[idx11 + 2] * fx         * fy) >> 16);

                // Big Sur-style fade-in: логотип "проявляется" из чёрного
                // фона, а не появляется мгновенно. Смешиваем цвет логотипа
                // с чёрным (0,0,0) по alpha/255 -- при alpha=0 пиксель
                // остаётся чёрным (фон), при alpha=255 это чистый цвет лого.
                if (alpha < 255) {
                    r = (uint8_t)((r * alpha) >> 8);
                    g = (uint8_t)((g * alpha) >> 8);
                    b = (uint8_t)((b * alpha) >> 8);
                }

                uint8_t* pixel_addr = g_fb_vram + (target_y * g_screen_pitch) + (target_x * bytes_per_pixel);

                if (bytes_per_pixel == 4) {
                    *(uint32_t*)pixel_addr = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                } else if (bytes_per_pixel == 3) {
                    pixel_addr[0] = b;
                    pixel_addr[1] = g;
                    pixel_addr[2] = r;
                }
            }
        }
    }
}

// Совместимость: старое имя для полностью непрозрачного (alpha=255)
// рисования логотипа, вдруг где-то ещё используется вызов без альфы.
__attribute__((unused))
static void draw_logo_to_screen(const unsigned char *logo, int src_w, int src_h) {
    draw_logo_to_screen_alpha(logo, src_w, src_h, 255);
}

// Рисует маленький закрашенный кружок (для лучиков спиннера) радиусом r
// вокруг (cx, cy). Дёшево -- спиннер маленький, лучиков всего 12.
static void fill_dot(int cx, int cy, int r, uint32_t color) {
    fill_rect(cx - r, cy - r, r * 2 + 1, r * 2 + 1, color);
}

// Big Sur-style индикатор загрузки: вместо линейной полосы -- 12 лучиков
// по кругу (как системный activity indicator в macOS), один "ведущий"
// лучик самый яркий, остальные плавно гаснут по кругу против него --
// создаёт эффект вращения при перерисовке с новым frame_index каждый раз.
//
// Реализовано без libm (freestanding kernel) через маленькую таблицу
// значений sin/cos, умноженных на 1000 и уже повёрнутых на 30° друг от
// друга (12 лучиков =360/12 = 30° шаг).
static void draw_boot_spinner(int frame_index, const char* label) {
    // sin(k*30°)*1000, cos(k*30°)*1000 для k = 0..11
    static const int sin1000[12] = {
        0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500
    };
    static const int cos1000[12] = {
        1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866
    };

    int logo_size = compute_splash_logo_size();
    int cx = (int)g_screen_w / 2;
    int cy = ((int)g_screen_h + logo_size) / 2 + 46; // чуть ниже логотипа

    int outer_r = logo_size / 10;
    if (outer_r < 14) outer_r = 14;
    int dot_r = outer_r / 8;
    if (dot_r < 2) dot_r = 2;

    // Стираем предыдущий кадр спиннера (квадратная область с запасом).
    fill_rect(cx - outer_r - dot_r - 2, cy - outer_r - dot_r - 2,
               (outer_r + dot_r + 2) * 2, (outer_r + dot_r + 2) * 2, 0x00000000);

    for (int k = 0; k < 12; k++) {
        // Позиция лучика k относительно текущего "ведущего" (frame_index):
        // 0 = самый яркий (ведущий), дальше по кругу против направления
        // вращения -- гаснет линейно до почти невидимого.
        int rel = (k - frame_index + 12 * 100) % 12; // всегда >= 0
        int brightness = 255 - (rel * 255) / 12;      // 255 .. ~21
        if (brightness < 40) brightness = 40;          // не гасить совсем в 0

        int dx = (outer_r * cos1000[k]) / 1000;
        int dy = (outer_r * sin1000[k]) / 1000;

        uint32_t gray = (uint32_t)brightness;
        uint32_t color = (gray << 16) | (gray << 8) | gray;

        fill_dot(cx + dx, cy + dy, dot_r, color);
    }

    // Подпись стадии под спиннером.
    if (label && g_screen_bpp == 32) {
        int text_area_w = logo_size + 80;
        int text_x0 = cx - text_area_w / 2;
        int text_y = cy + outer_r + dot_r + 14;
        fill_rect(text_x0, text_y - 2, text_area_w, 20, 0x00000000);

        int text_w = font_text_width(label);
        int text_x = cx - text_w / 2;
        uint32_t* vram32 = (uint32_t*)g_fb_vram;
        uint32_t stride = g_screen_pitch / 4;
        draw_string(label, text_x, text_y, 0x008E8E93, vram32, stride);
    }
}

// Рисует полосу загрузки: рамка + заполнение на percent% + подпись стадии
// под ней. Вызывается многократно по мере продвижения загрузки — именно
// это создаёт анимацию, а не статичную картинку.
//
// Оставлена как есть для обратной совместимости (пока не используется в
// kernel_main -- там теперь draw_boot_spinner), вдруг понадобится где-то
// как fallback.
__attribute__((unused))
static void draw_boot_progress(int percent, const char* label) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int logo_size = compute_splash_logo_size();
    int bar_w = logo_size;
    int bar_h = 6;
    int bar_x = ((int)g_screen_w - bar_w) / 2;
    int bar_y = ((int)g_screen_h + logo_size) / 2 + 28; // чуть ниже логотипа

    // Рамка (пустая полоса) и заполненная часть
    fill_rect(bar_x, bar_y, bar_w, bar_h, 0x002A2A30);
    int fill_w = (bar_w * percent) / 100;
    if (fill_w > 0) {
        fill_rect(bar_x, bar_y, fill_w, bar_h, 0x00FFFFFF);
    }

    // Подпись стадии под полосой — используем тот же шрифт, что и весь
    // остальной интерфейс (font.c уже линкуется в это же ядро).
    // Работает только для 32bpp-фреймбуфера: draw_string пишет как в
    // массив uint32_t, а на редких 24bpp-экранах это было бы некорректно.
    if (label && g_screen_bpp == 32) {
        // Сначала стираем предыдущую подпись (следующая может быть короче) —
        // берём чуть более широкую полосу, чем сам бар, с запасом.
        fill_rect(bar_x - 40, bar_y + 14, bar_w + 80, 20, 0x00000000);

        int text_w = font_text_width(label);
        int text_x = ((int)g_screen_w - text_w) / 2;
        int text_y = bar_y + 16;
        uint32_t* vram32 = (uint32_t*)g_fb_vram;
        uint32_t stride = g_screen_pitch / 4;
        draw_string(label, text_x, text_y, 0x008E8E93, vram32, stride);
    }
}

void kernel_main(void) {
    enable_sse();

    /*
     * НАСТОЯЩИЙ БАГ, из-за которого курсор не двигался вообще:
     * idt_init() (kernel/include/idt.c) нигде не вызывался. IDT никогда
     * не загружалась (lidt не выполнялся), и инструкция sti (разрешение
     * аппаратных прерываний) внутри idt_init() соответственно тоже
     * никогда не выполнялась. PS/2-мышь (drivers/system/mouse.c) корректно
     * настраивалась и слала пакеты по IRQ12, но CPU в принципе не мог их
     * обработать -- прерывания были глобально выключены с самого старта
     * ядра. Двойной вызов init_mouse(), который чинили раньше, был
     * реальной, но вторичной проблемой: даже без него курсор не мог
     * двигаться, пока не появился этот вызов idt_init().
     * Вызывается максимально рано, до любой другой инициализации.
     */
    idt_init();
    timer_init(250);
    
    serial_print("[1/6] Checking Limine framebuffer...\n");
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        serial_print("[ERROR] Limine framebuffer fail!\n");
        halt();
    }

    struct limine_framebuffer *fb = framebuffer_request.response->framebuffers[0];
    g_fb_vram = (uint8_t*)fb->address;
    g_screen_w = (uint32_t)fb->width;
    g_screen_h = (uint32_t)fb->height;
    g_screen_pitch = (uint32_t)fb->pitch;
    g_screen_bpp = (uint32_t)fb->bpp;

    serial_print("[2/6] Framebuffer initialized successfully.\n");
    
    // --- ЭКРАН ЗАГРУЗКИ (в стиле macOS Big Sur) ---
    serial_print("[3/6] Clearing screen...\n");
    kernel_clear_screen(0x00000000); // чёрный фон

    // Big Sur: логотип не появляется мгновенно, а плавно проявляется
    // (fade-in) из чёрного экрана. Делаем это через несколько кадров с
    // нарастающей альфой -- держится в пределах ~0.3с, в общий 5-секундный
    // бюджет загрузки не считаем отдельно (это часть той же паузы).
    serial_print("[4/6] Fading in logo...\n");
    for (int a = 0; a <= 255; a += 15) {
        draw_logo_to_screen_alpha(load_logo, LOGO_WIDTH, LOGO_HEIGHT, a);
        timer_wait_ms(20);
    }
    draw_logo_to_screen_alpha(load_logo, LOGO_WIDTH, LOGO_HEIGHT, 255);

    // Вместо линейной полосы прогресса -- вращающийся индикатор из 12
    // лучиков, как системный activity spinner в macOS.
    //
    // ТОЧНАЯ ДЛИТЕЛЬНОСТЬ: раньше весь сплэш был на spin_delay() --
    // busy-wait на CPU-тактах без привязки к реальному времени, поэтому
    // "сколько секунд идёт загрузка" плавало в зависимости от скорости
    // хоста/эмулятора. Теперь общая длительность спиннера — РОВНО 5 секунд
    // по CMOS RTC (тот же источник времени, что использует sleep_rtc() и
    // часы на панели), а не по числу nop-итераций. Отсчитываем 5 полных
    // секундных тиков RTC; внутри каждого тика крутим несколько кадров
    // спиннера через spin_delay (только чтобы кадры не сливались друг с
    // другом), но момент завершения задаёт именно RTC-секунда, а не
    // spin_delay -- поэтому итог не "плывёт" от скорости эмуляции.
    serial_print("[BOOT] Starting boot sequence with spinner (5s by PIT timer)...\n");

    static const char* boot_labels[5] = {
        "Checking hardware...",
        "Checking hardware...",
        "Initializing input devices...",
        "Preparing interface...",
        "Starting igorOS..."
    };

    int spin_frame = 0;
    int mouse_initialized = 0;
    uint64_t boot_start = timer_millis();
    uint64_t boot_end = boot_start + 5000ULL;

    while (timer_millis() < boot_end) {
        uint64_t elapsed = timer_millis() - boot_start;
        uint32_t second = (uint32_t)(elapsed / 1000ULL);
        if (second > 4) second = 4;

        if (second >= 2 && !mouse_initialized) {
            serial_print("[5/6] Initializing mouse...\n");
            mouse_set_bounds(g_screen_w, g_screen_h);
            init_mouse();
            mouse_initialized = 1;
        }

        draw_boot_spinner(spin_frame, boot_labels[second]);
        spin_frame = (spin_frame + 1) % 12;
        timer_wait_ms(40);
    }

    if (!mouse_initialized) {
        serial_print("[5/6] Initializing mouse...\n");
        mouse_set_bounds(g_screen_w, g_screen_h);
        init_mouse();
    }

    // --- ПЕРЕХОД К ОСНОВНОЙ ЗАГРУЗКЕ ---
    serial_print("[6/6] Starting desktop manager...\n");
    desktop_init(g_fb_vram, g_screen_w, g_screen_h, g_screen_pitch, g_screen_bpp);
    desktop_run();

    halt();
}

__attribute__((noreturn))
void _start(void) {
    __asm__ volatile ("cli");
    disable_pic();
    serial_init();

    serial_print("\n===============================\n");
    serial_print("       KERNEL BOOT START       \n");
    serial_print("===============================\n");

    kernel_main();
    halt();
}