#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char* str);
char serial_getchar(void);
bool serial_received(void);

#endif
