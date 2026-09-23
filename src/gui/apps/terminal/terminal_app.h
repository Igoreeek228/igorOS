#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H

#include <stdint.h>

/*
 * Терминал igorOS — консольное приложение рабочего стола.
 *
 * Принимает символы клавиатуры (см. drivers/system/keyboard.h) и исполняет
 * небольшой набор команд (help). Реестр FAT32 доступен только для чтения —
 * в терминале это команды ls/cat.
 */

/* Открыть/закрыть окно терминала. */
void toggle_terminal(void);

/* Открыт ли терминал (для маршрутизации клавиатуры). */
int terminal_is_open(void);

/* Подать символ клавиатуры: печатный символ, '\b' (backspace), '\n' (Enter).
 * Игнорируется, если окно закрыто или строка заполнена. */
void terminal_feed_key(char c);

/* Отрисовка окна в бэкбуфер. Вызывается каждый кадр из desktop_run(). */
void render_terminal_window(uint32_t* buf, int scr_w, int scr_h,
                            int mx, int my, int btn, int click);

#endif
