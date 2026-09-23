#include <stdint.h>
#include <stdbool.h>
#include "pic.h"

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_CMD_PORT     0x64

// Глобальные переменные, ожидаемые desktop.c
int mouse_x = 0;
int mouse_y = 0;
int mouse_left_clicked = 0;

static inline void io_wait(void) {
    outb(0x80, 0x00);
}

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t max_x;
    uint32_t max_y;
    bool left_button;
    bool right_button;
    bool middle_button;
} mouse_state_t;

static mouse_state_t g_mouse = {0};
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];

static void mouse_irq_handler(void); /* определена ниже, нужна в mouse_init() */

/*
 * Максимальный сдвиг за один пакет.
 *
 * PS/2 мышь может прислать большой delta, если компьютер некоторое
 * время не опрашивал порт. Без ограничения курсор может "телепортироваться".
 */
#define MOUSE_MAX_DELTA 32

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;

    if (type == 0) {
        // Ждём байт от контроллера/мыши.
        while (timeout--) {
            if (inb(PS2_STATUS_PORT) & 0x01)
                return;
            io_wait();
        }
    } else {
        // Ждём, пока входной буфер контроллера освободится.
        while (timeout--) {
            if (!(inb(PS2_STATUS_PORT) & 0x02))
                return;
            io_wait();
        }
    }
}

static void mouse_write(uint8_t write_byte) {
    mouse_wait(1);
    outb(PS2_CMD_PORT, 0xD4);

    mouse_wait(1);
    outb(PS2_DATA_PORT, write_byte);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(PS2_DATA_PORT);
}

void mouse_set_bounds(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return;

    g_mouse.max_x = width;
    g_mouse.max_y = height;

    if (g_mouse.x < 0)
        g_mouse.x = 0;
    if (g_mouse.y < 0)
        g_mouse.y = 0;

    if (g_mouse.x >= (int32_t)width)
        g_mouse.x = (int32_t)width - 1;

    if (g_mouse.y >= (int32_t)height)
        g_mouse.y = (int32_t)height - 1;

    mouse_x = g_mouse.x;
    mouse_y = g_mouse.y;
}

void mouse_get_state(int32_t *x, int32_t *y,
                     bool *btn_left, bool *btn_right) {
    if (x) *x = g_mouse.x;
    if (y) *y = g_mouse.y;
    if (btn_left) *btn_left = g_mouse.left_button;
    if (btn_right) *btn_right = g_mouse.right_button;
}

void mouse_handle_byte(uint8_t data) {
    switch (mouse_cycle) {
        case 0:
            /*
             * У первого байта стандартного PS/2 пакета всегда установлен
             * бит 3. Если его нет — это мусор/потерянная синхронизация.
             */
            if ((data & 0x08) != 0) {
                mouse_packet[0] = data;
                mouse_cycle = 1;
            }
            break;

        case 1:
            mouse_packet[1] = data;
            mouse_cycle = 2;
            break;

        case 2:
            mouse_packet[2] = data;
            mouse_cycle = 0;

            /*
             * Биты 6 и 7 означают overflow.
             * Такой пакет не используем, иначе курсор может резко улететь.
             */
            if (mouse_packet[0] & 0xC0)
                return;

            g_mouse.left_button =
                (mouse_packet[0] & 0x01) != 0;

            g_mouse.right_button =
                (mouse_packet[0] & 0x02) != 0;

            g_mouse.middle_button =
                (mouse_packet[0] & 0x04) != 0;

            /*
             * В PS/2 delta — знаковое 8-битное число.
             * Не используем деление на 2: оно делает скорость
             * неравномерной и особенно плохо работает на маленьких delta.
             */
            int32_t rel_x = (int32_t)(int8_t)mouse_packet[1];
            int32_t rel_y = (int32_t)(int8_t)mouse_packet[2];

            /*
             * Ограничиваем один пакет.
             * Это защищает от резких скачков, если несколько событий
             * накопились или железо прислало некорректный пакет.
             */
            if (rel_x > MOUSE_MAX_DELTA)
                rel_x = MOUSE_MAX_DELTA;
            else if (rel_x < -MOUSE_MAX_DELTA)
                rel_x = -MOUSE_MAX_DELTA;

            if (rel_y > MOUSE_MAX_DELTA)
                rel_y = MOUSE_MAX_DELTA;
            else if (rel_y < -MOUSE_MAX_DELTA)
                rel_y = -MOUSE_MAX_DELTA;

            /*
             * PS/2: положительный Y означает движение вниз/в сторону
             * пользователя. Экранные координаты у нас растут вниз,
             * поэтому инвертируем Y.
             */
            g_mouse.x += rel_x;
            g_mouse.y -= rel_y;

            // Ограничение курсора границами экрана.
            if (g_mouse.x < 0)
                g_mouse.x = 0;

            if (g_mouse.y < 0)
                g_mouse.y = 0;

            if (g_mouse.max_x > 0 &&
                g_mouse.x >= (int32_t)g_mouse.max_x) {
                g_mouse.x = (int32_t)g_mouse.max_x - 1;
            }

            if (g_mouse.max_y > 0 &&
                g_mouse.y >= (int32_t)g_mouse.max_y) {
                g_mouse.y = (int32_t)g_mouse.max_y - 1;
            }

            mouse_x = g_mouse.x;
            mouse_y = g_mouse.y;
            mouse_left_clicked = g_mouse.left_button ? 1 : 0;
            break;
    }
}

void mouse_init(void) {
    /*
     * Включаем второй PS/2 порт.
     */
    mouse_wait(1);
    outb(PS2_CMD_PORT, 0xA8);

    /*
     * Получаем Controller Configuration Byte.
     */
    mouse_wait(1);
    outb(PS2_CMD_PORT, 0x20);

    uint8_t status = mouse_read();

    /*
     * Разрешаем IRQ12 и включаем второй порт.
     * Бит 1 = IRQ12 enable.
     * Бит 5 = second PS/2 port clock disabled.
     */
    status |= 0x02;
    status &= (uint8_t)~0x20;

    mouse_wait(1);
    outb(PS2_CMD_PORT, 0x60);

    mouse_wait(1);
    outb(PS2_DATA_PORT, status);

    /*
     * Сброс/стандартные настройки мыши.
     */
    mouse_write(0xF6);
    mouse_read();

    /*
     * 100 Hz — более чем достаточно для обычного PS/2 курсора
     * и не создаёт огромную очередь пакетов на медленном kernel loop.
     */
    mouse_write(0xF3);
    mouse_read();

    mouse_write(100);
    mouse_read();

    /*
     * Разрешение 2 counts/mm.
     * 0x03 было максимальным и давало слишком резкое движение
     * на некоторых мышах/адаптерах.
     */
    mouse_write(0xE8);
    mouse_read();

    mouse_write(0x02);
    mouse_read();

    /*
     * Включаем передачу данных.
     */
    mouse_write(0xF4);
    mouse_read();

    if (g_mouse.max_x == 0)
        g_mouse.max_x = 1024;

    if (g_mouse.max_y == 0)
        g_mouse.max_y = 768;

    g_mouse.x = (int32_t)(g_mouse.max_x / 2);
    g_mouse.y = (int32_t)(g_mouse.max_y / 2);

    mouse_cycle = 0;
    mouse_left_clicked = 0;

    mouse_x = g_mouse.x;
    mouse_y = g_mouse.y;

    /*
     * Регистрируем настоящий обработчик IRQ12 и снимаем маску.
     *
     * IRQ12 (мышь) висит на SLAVE PIC, у которого своя каскадная линия
     * к MASTER PIC -- это IRQ2. Если не снять маску и с IRQ2 тоже,
     * прерывания со slave PIC вообще не дойдут до CPU независимо от
     * состояния маски самого IRQ12 (то же самое явление и в OriginOS,
     * см. комментарий в их kernel/mouse.c о pic_remap() и masked
     * legacy PIC в UEFI-прошивках).
     */
    irq_install_handler(12, mouse_irq_handler);
    pic_clear_mask(2);
    pic_clear_mask(12);
}

void init_mouse(void) {
    mouse_init();
}

/*
 * IgorOS Nord: настоящий interrupt-driven PS/2.
 *
 * Раньше poll_mouse() сам читал порт 0x60 из главного цикла desktop.c
 * -- то есть пакеты обрабатывались только тогда, когда доходила
 * очередь в render-цикле, а не в момент, когда контроллер реально
 * прислал прерывание. При тяжёлом кадре (много окон/эффектов) это
 * ощущалось как "дёрганый" курсор: за то время, что уходило на
 * рендер, могло накопиться несколько пакетов, которые потом
 * обрабатывались все разом.
 *
 * Портировано по духу из OriginOS (kernel/mouse.c): там мышь тоже
 * висит на IRQ12 и байты читаются прямо в обработчике прерывания, а
 * не поллингом. Сама логика разбора пакета (mouse_handle_byte) в
 * IgorOS уже была написана корректно -- переносился только механизм
 * ДОСТАВКИ байт, а не сама арифметика курсора.
 */
static void mouse_irq_handler(void) {
    uint8_t data = inb(PS2_DATA_PORT);
    mouse_handle_byte(data);
}

/*
 * poll_mouse() оставлена как no-op-совместимость: desktop.c продолжает
 * вызывать её каждый кадр (в т.ч. дважды за кадр после более раннего
 * фикса), но теперь реальная работа делается в mouse_irq_handler(),
 * вызываемом настоящим прерыванием IRQ12. Пустая функция сохранена,
 * а не удалена, чтобы не трогать desktop.c лишний раз и не плодить
 * riск регрессии в уже отлаженном коде главного цикла.
 */
void poll_mouse(void) {
}
