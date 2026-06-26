/*
 * vga.h - VGA Text Mode Driver
 *
 * WHAT IS VGA TEXT MODE?
 *
 * The PC's VGA (Video Graphics Array) hardware supports a text mode
 * where the screen is treated as a grid of character cells.
 * The default resolution is 80 columns × 25 rows = 2000 cells.
 *
 * Each cell is stored as 2 bytes in a special memory region at 0xB8000:
 *
 *   Byte 0: ASCII character code
 *   Byte 1: Attribute byte = (background << 4) | foreground
 *
 * Example: 'A' in white-on-black = 0x07 0x41
 *   0x07 = attribute: foreground=7 (light grey), background=0 (black)
 *   0x41 = 'A' in ASCII
 *
 * The VGA buffer is MEMORY-MAPPED — writing to it immediately affects the screen.
 * No GPU API, no framebuffer protocol, just direct memory writes!
 *
 * CURSOR:
 * The VGA hardware has a blinking cursor. We control it through
 * I/O ports 0x3D4 (index register) and 0x3D5 (data register).
 */

#ifndef VGA_H
#define VGA_H

#include "../kernel/kernel.h"

/* Screen dimensions */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* VGA text mode buffer base address */
#define VGA_BUFFER  ((volatile uint16_t*)0xB8000)

/*
 * VGA Colors (4-bit palette = 16 colors)
 *
 * Bits 0-2: color (RGB)
 * Bit  3:   bright/intensity modifier
 */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW        = 14,
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

/* ---- Public API ---- */

/* Initialize VGA: clear screen, set default colors, place cursor at (0,0) */
void vga_init(void);

/* Clear the entire screen */
void vga_clear(void);

/* Set the active foreground and background color */
void vga_set_color(vga_color_t fg, vga_color_t bg);

/* Print a single character (handles \n, \t, \b) */
void vga_putchar(char c);

/* Print a null-terminated string */
void vga_print(const char* str);

/* Print a formatted string (like printf) */
void vga_printf(const char* fmt, ...);

/* Move the VGA hardware cursor to column x, row y */
void vga_move_cursor(uint8_t x, uint8_t y);

/* Get current cursor position */
uint8_t vga_get_col(void);
uint8_t vga_get_row(void);

#endif /* VGA_H */
