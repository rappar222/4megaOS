#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
void* memset(void* dest, int val, size_t count);
void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int memcmp(const void* s1, const void* s2, size_t count);
char* strchr(const char* s, int c);
int atoi(const char* str);
uint32_t strtoul_hex(const char* str);
void itoa(int32_t value, char* str, int base);
void utoa(uint32_t value, char* str, int base);

#endif
