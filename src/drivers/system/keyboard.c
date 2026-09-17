#include <stdint.h>
#include <stdbool.h>

#define KBD_DATA_PORT   0x60
#define KBD_STATUS_PORT 0x64

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static bool shift_pressed = false;
static bool caps_lock = false;

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

char keyboard_scancode_to_char(uint8_t scancode) {
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = true;
        return 0;
    }
    if (scancode == 0xAA || scancode == 0xB6) {
        shift_pressed = false;
        return 0;
    }

    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return 0;
    }

    if (scancode & 0x80) {
        return 0;
    }

    if (scancode >= 128) return 0;

    char ch = kbd_layout_us[scancode];
    bool use_uppercase = shift_pressed ^ caps_lock;

    if (use_uppercase && ch >= 'a' && ch <= 'z') {
        return kbd_layout_us_shift[scancode];
    } else if (shift_pressed) {
        return kbd_layout_us_shift[scancode];
    }

    return ch;
}

char keyboard_pollchar(void) {
    uint8_t scancode = inb(KBD_DATA_PORT);
    return keyboard_scancode_to_char(scancode);
}

void init_keyboard(void) {
    while (inb(KBD_STATUS_PORT) & 0x01) {
        inb(KBD_DATA_PORT);
    }
}

char keyboard_getchar(void) {
    if (inb(KBD_STATUS_PORT) & 0x01) {
        return keyboard_pollchar();
    }
    return 0;
}
