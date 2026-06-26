/*
 * vga.c - VGA Text Mode Driver Implementation
 *
 * This is often the first thing to implement in a kernel — you need
 * to see output before you can debug anything else!
 */

#include "vga.h"
#include "../libc/string.h"

/*
 * VGA CRT Controller registers
 *
 * These I/O ports control the VGA cursor position.
 * We write the register INDEX to 0x3D4, then the VALUE to 0x3D5.
 *
 * Register 14 (0x0E) = cursor position HIGH byte
 * Register 15 (0x0F) = cursor position LOW byte
 *
 * Position is a LINEAR index: pos = row * VGA_WIDTH + col
 */
#define VGA_CTRL_PORT 0x3D4  /* CRT Controller Index */
#define VGA_DATA_PORT 0x3D5  /* CRT Controller Data  */

/* ---- Driver state (private to this file) ---- */
static volatile uint16_t* vga_buf = VGA_BUFFER;
static uint8_t  cursor_row = 0;
static uint8_t  cursor_col = 0;
static uint8_t  current_color;  /* packed attribute byte */

/*
 * make_entry - Pack a character + color into one 16-bit VGA entry.
 *
 * Memory layout of the 16-bit entry:
 *   Bits 15-12: background color (4 bits)
 *   Bits 11-8:  foreground color (4 bits)
 *   Bits 7-0:   ASCII character  (8 bits)
 *
 * We store it as:
 *   high byte (bits 15-8) = attribute
 *   low  byte (bits 7-0)  = character
 */
static inline uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)c | ((uint16_t)attr << 8);
}

/* Build an attribute byte from foreground and background colors */
static inline uint8_t make_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)fg | ((uint8_t)bg << 4);
}

/* ---- Hardware cursor control ---- */

static void update_hw_cursor(void) {
    uint16_t pos = (uint16_t)(cursor_row * VGA_WIDTH + cursor_col);

    /* Write high byte of cursor position to register 14 */
    outb(VGA_CTRL_PORT, 14);
    outb(VGA_DATA_PORT, (uint8_t)(pos >> 8));

    /* Write low byte of cursor position to register 15 */
    outb(VGA_CTRL_PORT, 15);
    outb(VGA_DATA_PORT, (uint8_t)(pos & 0xFF));
}

/* ---- Scrolling ---- */

/*
 * scroll - Scroll the screen up by one line when we reach the bottom.
 *
 * We do this by:
 *   1. Moving all lines up by one (line 1→0, 2→1, ..., 24→23)
 *   2. Clearing the last line (row 24)
 */
static void scroll(void) {
    uint8_t blank = make_entry(' ', current_color);

    /* If we're past the last row, scroll */
    if (cursor_row >= VGA_HEIGHT) {
        /* Move each row up by one */
        for (int row = 1; row < VGA_HEIGHT; row++) {
            for (int col = 0; col < VGA_WIDTH; col++) {
                vga_buf[(row - 1) * VGA_WIDTH + col] =
                    vga_buf[row * VGA_WIDTH + col];
            }
        }

        /* Clear the last row */
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;
        }

        cursor_row = VGA_HEIGHT - 1;
    }
}

/* ---- Public API ---- */

void vga_init(void) {
    current_color = make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    cursor_row = 0;
    cursor_col = 0;
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = make_entry(' ', current_color);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buf[i] = blank;
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hw_cursor();
}

void vga_set_color(vga_color_t fg, vga_color_t bg) {
    current_color = make_color(fg, bg);
}

/*
 * latin1_to_cp437 - Translate a Latin-1 (ISO 8859-1) byte to CP437.
 *
 * VGA text mode uses Code Page 437 (CP437) for character rendering, but our
 * keyboard driver stores typed characters as Latin-1 byte values for extended
 * chars (è, à, ì, ò, ù, ç, £, °, ...).  This function maps those Latin-1
 * bytes to the matching CP437 glyph so they display correctly on screen.
 *
 * Only the characters relevant for Italian keyboard (and a few others) are
 * listed.  Anything not in the table returns '?' as a visible placeholder.
 */
static uint8_t latin1_to_cp437(uint8_t c) {
    switch (c) {
        /* Italian keyboard characters (Latin-1 → CP437) */
        case 0xA3: return 0x9C;  /* £ */
        case 0xA7: return 0x15;  /* § */
        case 0xB0: return 0xF8;  /* ° (degree) */
        case 0xC0: return 0x41;  /* À → A (fallback) */
        case 0xC7: return 0x80;  /* Ç */
        case 0xC8: return 0x45;  /* È → E (fallback) */
        case 0xC9: return 0x90;  /* É */
        case 0xCC: return 0x49;  /* Ì → I (fallback) */
        case 0xD2: return 0x4F;  /* Ò → O (fallback) */
        case 0xD9: return 0x55;  /* Ù → U (fallback) */
        case 0xE0: return 0x85;  /* à */
        case 0xE1: return 0xA0;  /* á */
        case 0xE2: return 0x83;  /* â */
        case 0xE4: return 0x84;  /* ä */
        case 0xE5: return 0x86;  /* å */
        case 0xE6: return 0x91;  /* æ */
        case 0xE7: return 0x87;  /* ç */
        case 0xE8: return 0x8A;  /* è */
        case 0xE9: return 0x82;  /* é */
        case 0xEA: return 0x88;  /* ê */
        case 0xEB: return 0x89;  /* ë */
        case 0xEC: return 0x8D;  /* ì */
        case 0xED: return 0xA1;  /* í */
        case 0xEE: return 0x8C;  /* î */
        case 0xEF: return 0x8B;  /* ï */
        case 0xF1: return 0xA4;  /* ñ */
        case 0xF2: return 0x95;  /* ò */
        case 0xF3: return 0xA2;  /* ó */
        case 0xF4: return 0x93;  /* ô */
        case 0xF6: return 0x94;  /* ö */
        case 0xF9: return 0x97;  /* ù */
        case 0xFA: return 0xA3;  /* ú */
        case 0xFB: return 0x96;  /* û */
        case 0xFC: return 0x81;  /* ü */
        default:   return '?';   /* unsupported extended char */
    }
}

void vga_putchar(char c) {
    switch (c) {
        case '\n':
            /* Newline: go to beginning of next row */
            cursor_col = 0;
            cursor_row++;
            break;

        case '\r':
            /* Carriage return: go to beginning of current row */
            cursor_col = 0;
            break;

        case '\t':
            /* Tab: advance to next multiple of 4 */
            cursor_col = (uint8_t)((cursor_col + 4) & ~3);
            if (cursor_col >= VGA_WIDTH) {
                cursor_col = 0;
                cursor_row++;
            }
            break;

        case '\b':
            /* Backspace: move one column left and erase */
            if (cursor_col > 0) {
                cursor_col--;
                vga_buf[cursor_row * VGA_WIDTH + cursor_col] =
                    make_entry(' ', current_color);
            }
            break;

        default: {
            /*
             * Normal character.  Latin-1 bytes (>= 0x80) are translated to
             * the matching CP437 glyph so accented characters (è, à, ì, ù…)
             * display correctly in VGA text mode.
             */
            uint8_t glyph = (uint8_t)c;
            if (glyph >= 0x80) glyph = latin1_to_cp437(glyph);

            vga_buf[cursor_row * VGA_WIDTH + cursor_col] =
                (uint16_t)glyph | ((uint16_t)current_color << 8);
            cursor_col++;

            /* Wrap to next line if we hit the right edge */
            if (cursor_col >= VGA_WIDTH) {
                cursor_col = 0;
                cursor_row++;
            }
            break;
        }
    }

    /* Scroll if we went past the bottom */
    scroll();

    /* Update the hardware cursor blinking position */
    update_hw_cursor();
}

void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_printf(const char* fmt, ...) {
    char buf[1024];  /* 1 KB buffer for formatted output */
    va_list args;
    va_start(args, fmt);
    kvsprintf(buf, fmt, args);
    va_end(args);
    vga_print(buf);
}

void vga_move_cursor(uint8_t x, uint8_t y) {
    cursor_col = x;
    cursor_row = y;
    update_hw_cursor();
}

uint8_t vga_get_col(void) { return cursor_col; }
uint8_t vga_get_row(void) { return cursor_row; }

void vga_write_at(int row, int col, char c, vga_color_t fg, vga_color_t bg) {
    if (row < 0 || row >= VGA_HEIGHT || col < 0 || col >= VGA_WIDTH) return;
    uint8_t attr  = (uint8_t)fg | ((uint8_t)bg << 4);
    uint8_t glyph = (uint8_t)c;
    if (glyph >= 0x80) glyph = latin1_to_cp437(glyph);
    vga_buf[row * VGA_WIDTH + col] = (uint16_t)glyph | ((uint16_t)attr << 8);
}

void vga_write_str_at(int row, int col, const char* s, vga_color_t fg, vga_color_t bg) {
    while (*s && col < VGA_WIDTH)
        vga_write_at(row, col++, *s++, fg, bg);
}

void vga_clear_row(int row, vga_color_t fg, vga_color_t bg) {
    uint8_t attr = (uint8_t)fg | ((uint8_t)bg << 4);
    uint16_t blank = ' ' | ((uint16_t)attr << 8);
    for (int c = 0; c < VGA_WIDTH; c++)
        vga_buf[row * VGA_WIDTH + c] = blank;
}
