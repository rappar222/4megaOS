#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_char(void);

#endif
