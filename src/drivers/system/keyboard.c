#include "keyboard.h"
#include "pic.h"      /* inb / outb / irq_install_handler / pic_clear_mask */
#include <stdint.h>

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64
#define KBD_QUEUE_SIZE  128

/* Биты состояния модификаторов (левая/правая клавиши отдельно). */
#define M_LSHIFT 0x01
#define M_RSHIFT 0x02
#define M_LCTRL  0x04
#define M_RCTRL  0x08
#define M_LALT   0x10
#define M_RALT   0x20

static kbd_event_t queue[KBD_QUEUE_SIZE];
static volatile uint32_t q_head = 0;   /* пишет обработчик IRQ  */
static volatile uint32_t q_tail = 0;   /* читает главный цикл   */

static int mod_keys = 0;
static int caps_lock = 0;
static int ext_prefix = 0;
static int skip_bytes = 0;
static volatile int alt_f4_pending = 0;

static const char kbd_layout_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
  '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0, ' ',
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char kbd_layout_us_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',   0, ' ',
     0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static uint8_t current_mods(void) {
    uint8_t m = 0;
    if (mod_keys & (M_LSHIFT | M_RSHIFT)) m |= KBD_MOD_SHIFT;
    if (mod_keys & (M_LCTRL | M_RCTRL))   m |= KBD_MOD_CTRL;
    if (mod_keys & (M_LALT | M_RALT))     m |= KBD_MOD_ALT;
    if (caps_lock)                        m |= KBD_MOD_CAPS;
    return m;
}

static void queue_push(uint8_t code, uint8_t ext, uint8_t pressed) {
    uint32_t next = (q_head + 1) % KBD_QUEUE_SIZE;
    if (next == q_tail) return;            /* очередь полна -- теряем */
    kbd_event_t *e = &queue[q_head];
    e->scancode = code;
    e->extended = ext;
    e->pressed  = pressed;
    e->mods     = current_mods();
    __asm__ volatile ("" ::: "memory");
    q_head = next;
}

static void set_mod(int bit, int pressed) {
    if (pressed) mod_keys |= bit; else mod_keys &= ~bit;
}

/* Разбор одного байта от контроллера клавиатуры. */
static void kbd_process_byte(uint8_t b) {
    if (skip_bytes) { skip_bytes--; return; }

    if (b == 0xE0) { ext_prefix = 1; return; }
    if (b == 0xE1) { skip_bytes = 5; return; }         /* Pause/Break */
    /* 0x00/0xFF -- переполнение буфера, 0xFA/0xFE -- ACK/resend */
    if (b == 0x00 || b == 0xFF || b == 0xFA || b == 0xFE) {
        ext_prefix = 0;
        return;
    }

    int ext = ext_prefix;
    ext_prefix = 0;

    int pressed = !(b & 0x80);
    uint8_t code = b & 0x7F;

    /* "Фальшивый" Shift, который контроллер вставляет вокруг Insert/Del/
     * стрелок при включённом NumLock -- в события не пускаем. */
    if (ext && (code == 0x2A || code == 0x36)) return;

    if (!ext && code == 0x2A)      set_mod(M_LSHIFT, pressed);
    else if (!ext && code == 0x36) set_mod(M_RSHIFT, pressed);
    else if (code == 0x1D)         set_mod(ext ? M_RCTRL : M_LCTRL, pressed);
    else if (code == 0x38)         set_mod(ext ? M_RALT : M_LALT, pressed);
    else if (!ext && code == 0x3A && pressed) caps_lock = !caps_lock;

    /* Alt+F4 обрабатывает рабочий стол, в очередь не кладём. */
    if (pressed && !ext && code == 0x3E && (mod_keys & (M_LALT | M_RALT))) {
        alt_f4_pending = 1;
        return;
    }

    queue_push(code, (uint8_t)ext, (uint8_t)pressed);
}

/* Забрать все байты клавиатуры, ждущие в контроллере. Байты мыши
 * (бит 0x20 в статусе) не трогаем -- их читает обработчик IRQ12. */
static void kbd_drain_hw(void) {
    for (int i = 0; i < 16; i++) {
        uint8_t st = inb(KBD_STATUS_PORT);
        if (!(st & 0x01) || (st & 0x20)) break;
        kbd_process_byte(inb(KBD_DATA_PORT));
    }
}

static void keyboard_irq_handler(void) {
    kbd_drain_hw();
}

void init_keyboard(void) {
    /* сбросить всё, что накопилось до старта драйвера */
    for (int i = 0; i < 32; i++) {
        uint8_t st = inb(KBD_STATUS_PORT);
        if (!(st & 0x01)) break;
        inb(KBD_DATA_PORT);
    }
    mod_keys = 0;
    caps_lock = 0;
    ext_prefix = 0;
    skip_bytes = 0;
    alt_f4_pending = 0;
    q_head = q_tail = 0;

    irq_install_handler(1, keyboard_irq_handler);
    pic_clear_mask(1);
}

int keyboard_poll_event(kbd_event_t *ev) {
    /*
     * Страховка: если IRQ1 по какой-то причине не пришёл (некоторые
     * гипервизоры/прошивки), забираем байты сами. Прерывания на это
     * время выключены, чтобы не столкнуться с обработчиком.
     */
#ifndef KBD_HOST_TEST
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
#endif
    kbd_drain_hw();
#ifndef KBD_HOST_TEST
    __asm__ volatile ("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#endif

    if (q_tail == q_head) return 0;
    *ev = queue[q_tail];
    __asm__ volatile ("" ::: "memory");
    q_tail = (q_tail + 1) % KBD_QUEUE_SIZE;
    return 1;
}

char keyboard_event_to_char(const kbd_event_t *ev) {
    if (!ev->pressed) return 0;
    if (ev->extended) {
        /* Enter и "/" на цифровом блоке */
        if (ev->scancode == 0x1C) return '\n';
        if (ev->scancode == 0x35) return '/';
        return 0;
    }
    if (ev->scancode >= 128) return 0;

    char ch = kbd_layout_us[ev->scancode];
    int shift = (ev->mods & KBD_MOD_SHIFT) != 0;
    int caps  = (ev->mods & KBD_MOD_CAPS) != 0;

    if (ch >= 'a' && ch <= 'z')          /* буквы: Shift и Caps Lock взаимно гасятся */
        return (shift ^ caps) ? kbd_layout_us_shift[ev->scancode] : ch;
    return shift ? kbd_layout_us_shift[ev->scancode] : ch;
}

char keyboard_getchar(void) {
    kbd_event_t ev;
    while (keyboard_poll_event(&ev)) {
        char c = keyboard_event_to_char(&ev);
        if (c) return c;
    }
    return 0;
}

int keyboard_consume_alt_f4(void) {
    if (!alt_f4_pending) return 0;
    alt_f4_pending = 0;
    return 1;
}
