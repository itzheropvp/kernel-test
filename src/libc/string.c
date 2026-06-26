/*
 * string.c - Kernel string and memory utilities
 *
 * All functions here are implemented from scratch — no libc.
 * This is how embedded systems and kernels work: you're your own library.
 */

#include "string.h"

/* =============================================================
 * MEMORY FUNCTIONS
 * ============================================================= */

/*
 * memset - Fill n bytes starting at dest with byte value val.
 *
 * Used everywhere: clearing the screen, zeroing BSS, initializing buffers.
 * Note: val is int but only the lowest 8 bits are used.
 */
void* memset(void* dest, int val, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    uint8_t  v = (uint8_t)val;
    while (n--) {
        *d++ = v;
    }
    return dest;
}

/*
 * memcpy - Copy n bytes from src to dest.
 *
 * IMPORTANT: src and dest must NOT overlap.
 * If they might overlap, use memmove() instead.
 */
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t*       d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

/*
 * memmove - Copy n bytes from src to dest, handles overlapping regions.
 *
 * If src < dest and they overlap, copying forward would corrupt data,
 * so we copy backward instead.
 */
void* memmove(void* dest, const void* src, size_t n) {
    uint8_t*       d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d < s) {
        /* No overlap risk: copy forward */
        while (n--) *d++ = *s++;
    } else {
        /* Potential overlap: copy backward */
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

/*
 * memcmp - Compare n bytes from two memory regions.
 * Returns 0 if equal, <0 if a < b, >0 if a > b.
 */
int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* p = (const uint8_t*)a;
    const uint8_t* q = (const uint8_t*)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

/* =============================================================
 * STRING FUNCTIONS
 * ============================================================= */

/* strlen - Count characters up to (but not including) the null terminator */
size_t strlen(const char* s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

/* strcpy - Copy string src to dest (including null terminator) */
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));  /* copies until null byte is copied */
    return dest;
}

/* strncpy - Copy at most n bytes of src to dest */
char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n--) *d++ = '\0';  /* pad with nulls if src was shorter */
    return dest;
}

/* strcmp - Compare two strings. Returns 0 if equal. */
int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) {
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* strncmp - Compare at most n characters */
int strncmp(const char* a, const char* b, size_t n) {
    while (n && *a && (*a == *b)) {
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

/* strcat - Append src to the end of dest */
char* strcat(char* dest, const char* src) {
    char* d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

/* strncat - Append at most n chars of src to dest */
char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest + strlen(dest);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dest;
}

/* strchr - Find first occurrence of character c in string s */
char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

/* strrchr - Find last occurrence of character c in string s */
char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (char*)last;
}

/* =============================================================
 * NUMBER → STRING CONVERSION
 * ============================================================= */

/*
 * itoa - Integer To ASCII
 *
 * Converts a signed integer to a string in the given base.
 * base=10 → decimal, base=16 → hex, base=2 → binary
 *
 * Algorithm:
 *   1. Extract digits by repeatedly dividing by base (gets reversed digits)
 *   2. Reverse the result
 */
void itoa(int32_t value, char* buf, int base) {
    static const char digits[] = "0123456789abcdef";
    char   tmp[32];
    int    i = 0;
    bool   negative = false;
    uint32_t uval;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    /* Handle negative numbers (only for base 10) */
    if (value < 0 && base == 10) {
        negative = true;
        uval = (uint32_t)(-(value + 1)) + 1;  /* avoid overflow for INT_MIN */
    } else {
        uval = (uint32_t)value;
    }

    /* Extract digits in reverse order */
    while (uval > 0) {
        tmp[i++] = digits[uval % base];
        uval /= base;
    }

    if (negative) tmp[i++] = '-';

    /* Reverse into output buffer */
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* utoa - Unsigned integer to ASCII */
void utoa(uint32_t value, char* buf, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int  i = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value > 0) {
        tmp[i++] = digits[value % base];
        value /= base;
    }

    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Same as utoa but uppercase hex digits */
static void utoa_upper(uint32_t value, char* buf, int base) {
    static const char digits[] = "0123456789ABCDEF";
    char tmp[32];
    int  i = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    while (value > 0) {
        tmp[i++] = digits[value % base];
        value /= base;
    }

    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* =============================================================
 * STRING → NUMBER CONVERSION
 * ============================================================= */

/*
 * atoi - ASCII to integer.
 *
 * Converts a decimal string to a signed 32-bit integer.
 * Skips leading whitespace. Handles optional leading '+' or '-'.
 * Stops at the first non-digit character.
 *
 * Examples: "42" → 42,  "-7" → -7,  "  100px" → 100
 */
int32_t atoi(const char* s) {
    int32_t result   = 0;
    bool    negative = false;

    /* Skip leading spaces */
    while (*s == ' ' || *s == '\t') s++;

    /* Optional sign */
    if      (*s == '-') { negative = true;  s++; }
    else if (*s == '+') {                   s++; }

    /* Convert digits */
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return negative ? -result : result;
}

/* =============================================================
 * FORMATTED STRING FUNCTIONS
 * ============================================================= */

/*
 * kvsprintf - Write formatted string to buf using a va_list.
 *
 * Supported format specifiers:
 *   %[flags][width]type
 *
 * Flags:
 *   -     left-align within the field width (default: right-align)
 *   0     pad with '0' instead of spaces (for numeric types)
 *
 * Width:
 *   A decimal integer specifying the minimum field width.
 *   e.g. "%-10s" = left-aligned string in a 10-character wide column.
 *
 * Types: d/i u x X p s c %
 *
 * HOW WIDTH/PADDING WORKS:
 *
 *   %-10s  with "hello" → "hello     " (left-align, 5 spaces after)
 *   %10s   with "hello" → "     hello" (right-align, 5 spaces before)
 *   %05d   with 42      → "00042"      (zero-padded to 5 digits)
 */
int kvsprintf(char* buf, const char* fmt, va_list args) {
    char* out = buf;

    while (*fmt) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }
        fmt++;  /* skip '%' */

        /* ---- Parse flags ---- */
        bool left_align = false;
        bool zero_pad   = false;
        for (;;) {
            if      (*fmt == '-') { left_align = true; fmt++; }
            else if (*fmt == '0') { zero_pad   = true; fmt++; }
            else break;
        }

        /* ---- Parse minimum field width ---- */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }

        /* ---- Convert argument to string in tmp[] ---- */
        char        tmp[32];
        const char* str = tmp;

        switch (*fmt++) {
            case 'd': case 'i': {
                int32_t val = va_arg(args, int32_t);
                itoa(val, tmp, 10);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                utoa(val, tmp, 10);
                break;
            }
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                utoa(val, tmp, 16);
                break;
            }
            case 'X': {
                uint32_t val = va_arg(args, uint32_t);
                utoa_upper(val, tmp, 16);
                break;
            }
            case 'p': {
                /* Pointer: always "0x" + 8 hex digits */
                uint32_t val = (uint32_t)va_arg(args, void*);
                tmp[0] = '0'; tmp[1] = 'x';
                for (int i = 0; i < 8; i++) {
                    uint8_t nibble = (val >> (28 - i * 4)) & 0xF;
                    tmp[2 + i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
                }
                tmp[10] = '\0';
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                str = s ? s : "(null)";
                break;
            }
            case 'c': {
                tmp[0] = (char)va_arg(args, int);
                tmp[1] = '\0';
                break;
            }
            case '%': {
                tmp[0] = '%'; tmp[1] = '\0';
                break;
            }
            default: {
                /* Unknown specifier: print literally */
                tmp[0] = '%'; tmp[1] = *(fmt - 1); tmp[2] = '\0';
                break;
            }
        }

        /* ---- Apply width and alignment ---- */
        int len = (int)strlen(str);

        /* Right-align: print padding BEFORE the value */
        if (!left_align && width > len) {
            char pad = zero_pad ? '0' : ' ';
            for (int i = 0; i < width - len; i++) *out++ = pad;
        }

        /* Print the value */
        while (*str) *out++ = *str++;

        /* Left-align: print padding AFTER the value */
        if (left_align && width > len) {
            for (int i = 0; i < width - len; i++) *out++ = ' ';
        }
    }

    *out = '\0';
    return (int)(out - buf);
}

/* ksprintf - Formatted print to string buffer */
int ksprintf(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = kvsprintf(buf, fmt, args);
    va_end(args);
    return ret;
}
