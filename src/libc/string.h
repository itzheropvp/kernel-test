/*
 * string.h - Kernel string and memory utility declarations
 *
 * Since we have no libc, we implement our own versions of the
 * essential functions from <string.h> and <stdio.h>.
 *
 * These are declared here and implemented in string.c.
 */

#ifndef STRING_H
#define STRING_H

#include "../kernel/kernel.h"
#include <stdarg.h>  /* va_list — safe to use even in freestanding mode */

/* ---- Memory operations ---- */
void* memset(void* dest, int val, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);

/* ---- String operations ---- */
size_t strlen(const char* s);
char*  strcpy(char* dest, const char* src);
char*  strncpy(char* dest, const char* src, size_t n);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strcat(char* dest, const char* src);
char*  strncat(char* dest, const char* src, size_t n);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);

/* ---- Number → string conversion ---- */
/* Convert signed integer to string in given base (e.g., 10 for decimal) */
void itoa(int32_t value, char* buf, int base);
/* Convert unsigned integer to string in given base */
void utoa(uint32_t value, char* buf, int base);
/* Convert ASCII string to signed integer ("42" → 42, "-7" → -7) */
int32_t atoi(const char* s);

/* ---- Formatted output ---- */
/*
 * kprintf / ksprintf: kernel printf equivalents.
 *
 * Supported format specifiers:
 *   %d / %i  — signed decimal integer
 *   %u       — unsigned decimal integer
 *   %x       — unsigned hex (lowercase)
 *   %X       — unsigned hex (uppercase)
 *   %p       — pointer (0x prefixed hex)
 *   %s       — string
 *   %c       — character
 *   %%       — literal percent
 *
 * We don't support width/padding/precision yet — feel free to
 * add them as a learning exercise!
 */
int ksprintf(char* buf, const char* fmt, ...);
int kvsprintf(char* buf, const char* fmt, va_list args);

#endif /* STRING_H */
