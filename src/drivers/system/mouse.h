#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

extern int mouse_x;
extern int mouse_y;
extern int mouse_left_clicked;

void init_mouse(void);
void poll_mouse(void);

#endif