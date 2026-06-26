/*
 * keyboard.c - PS/2 Keyboard Driver
 */

#include "keyboard.h"
#include "irq.h"
#include "../drivers/vga.h"
#include "../libc/string.h"

/* PS/2 keyboard I/O ports */
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64

/* If bit 0 of status port is set, data is available to read */
#define KB_STATUS_OUTPUT_FULL 0x01

/* Highest scan code value in our table */
#define KB_SCANCODE_MAX 58

/* ============================================================
 * SCAN CODE → ASCII TABLES
 *
 * Index = scan code (key pressed)
 * Value = ASCII character (0 = no ASCII equivalent)
 *
 * Scan codes above 0x80 are BREAK codes (key released) — we ignore those.
 *
 * Two tables: one for normal (unshifted) and one for shifted characters.
 * ============================================================ */

static const char scancode_to_ascii[KB_SCANCODE_MAX] = {
    /*  0 */ 0,    /* No key      */
    /*  1 */ 0,    /* ESC         */
    /*  2 */ '1',
    /*  3 */ '2',
    /*  4 */ '3',
    /*  5 */ '4',
    /*  6 */ '5',
    /*  7 */ '6',
    /*  8 */ '7',
    /*  9 */ '8',
    /* 10 */ '9',
    /* 11 */ '0',
    /* 12 */ '-',
    /* 13 */ '=',
    /* 14 */ '\b', /* Backspace   */
    /* 15 */ '\t', /* Tab         */
    /* 16 */ 'q',
    /* 17 */ 'w',
    /* 18 */ 'e',
    /* 19 */ 'r',
    /* 20 */ 't',
    /* 21 */ 'y',
    /* 22 */ 'u',
    /* 23 */ 'i',
    /* 24 */ 'o',
    /* 25 */ 'p',
    /* 26 */ '[',
    /* 27 */ ']',
    /* 28 */ '\n', /* Enter       */
    /* 29 */ 0,    /* Left Ctrl   */
    /* 30 */ 'a',
    /* 31 */ 's',
    /* 32 */ 'd',
    /* 33 */ 'f',
    /* 34 */ 'g',
    /* 35 */ 'h',
    /* 36 */ 'j',
    /* 37 */ 'k',
    /* 38 */ 'l',
    /* 39 */ ';',
    /* 40 */ '\'',
    /* 41 */ '`',
    /* 42 */ 0,    /* Left Shift  */
    /* 43 */ '\\',
    /* 44 */ 'z',
    /* 45 */ 'x',
    /* 46 */ 'c',
    /* 47 */ 'v',
    /* 48 */ 'b',
    /* 49 */ 'n',
    /* 50 */ 'm',
    /* 51 */ ',',
    /* 52 */ '.',
    /* 53 */ '/',
    /* 54 */ 0,    /* Right Shift */
    /* 55 */ '*',  /* Numpad *    */
    /* 56 */ 0,    /* Left Alt    */
    /* 57 */ ' ',  /* Space       */
};

/* Shifted versions (when Shift key is held) */
static const char scancode_to_ascii_shift[KB_SCANCODE_MAX] = {
    0,    /* 0  */
    0,    /* 1  ESC */
    '!',  /* 2  */
    '@',  /* 3  */
    '#',  /* 4  */
    '$',  /* 5  */
    '%',  /* 6  */
    '^',  /* 7  */
    '&',  /* 8  */
    '*',  /* 9  */
    '(',  /* 10 */
    ')',  /* 11 */
    '_',  /* 12 */
    '+',  /* 13 */
    '\b', /* 14 Backspace */
    '\t', /* 15 Tab */
    'Q',  /* 16 */
    'W',  /* 17 */
    'E',  /* 18 */
    'R',  /* 19 */
    'T',  /* 20 */
    'Y',  /* 21 */
    'U',  /* 22 */
    'I',  /* 23 */
    'O',  /* 24 */
    'P',  /* 25 */
    '{',  /* 26 */
    '}',  /* 27 */
    '\n', /* 28 Enter */
    0,    /* 29 Ctrl */
    'A',  /* 30 */
    'S',  /* 31 */
    'D',  /* 32 */
    'F',  /* 33 */
    'G',  /* 34 */
    'H',  /* 35 */
    'J',  /* 36 */
    'K',  /* 37 */
    'L',  /* 38 */
    ':',  /* 39 */
    '"',  /* 40 */
    '~',  /* 41 */
    0,    /* 42 Left Shift */
    '|',  /* 43 */
    'Z',  /* 44 */
    'X',  /* 45 */
    'C',  /* 46 */
    'V',  /* 47 */
    'B',  /* 48 */
    'N',  /* 49 */
    'M',  /* 50 */
    '<',  /* 51 */
    '>',  /* 52 */
    '?',  /* 53 */
    0,    /* 54 Right Shift */
    '*',  /* 55 */
    0,    /* 56 Alt */
    ' ',  /* 57 Space */
};

/* ============================================================
 * KEYBOARD STATE
 * ============================================================ */

/* Modifier key states */
static bool shift_pressed = false;
static bool caps_lock     = false;

/* ============================================================
 * CIRCULAR (RING) BUFFER
 *
 * This buffer decouples the IRQ handler (producer) from the
 * shell/reader (consumer). The IRQ runs asynchronously and
 * shouldn't block — it just drops the character into the buffer.
 * ============================================================ */

#define KB_BUFFER_SIZE 256

static volatile char   kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_write = 0;  /* Next write position */
static volatile uint32_t kb_read  = 0;  /* Next read position  */

/* Returns number of characters available in the buffer */
static uint32_t kb_buffer_count(void) {
    return (kb_write - kb_read) % KB_BUFFER_SIZE;
}

/* Put a character into the buffer (called from IRQ handler) */
static void kb_buffer_put(char c) {
    uint32_t next_write = (kb_write + 1) % KB_BUFFER_SIZE;
    if (next_write != kb_read) {  /* Don't overflow (drop char if full) */
        kb_buffer[kb_write] = c;
        kb_write = next_write;
    }
}

/* Get a character from the buffer (called from shell) */
static char kb_buffer_get(void) {
    char c = kb_buffer[kb_read];
    kb_read = (kb_read + 1) % KB_BUFFER_SIZE;
    return c;
}

/* ============================================================
 * IRQ1 HANDLER
 * ============================================================ */

static void keyboard_irq_handler(registers_t* regs) {
    (void)regs;

    /* Read the scan code from the data port */
    uint8_t scancode = inb(KB_DATA_PORT);

    /* Scan codes >= 0x80 are BREAK codes (key released) */
    if (scancode >= 0x80) {
        uint8_t make = scancode - 0x80;
        /* Track shift key releases */
        if (make == 0x2A || make == 0x36) {
            shift_pressed = false;
        }
        return;  /* Ignore key releases */
    }

    /* Track modifier key presses */
    if (scancode == 0x2A || scancode == 0x36) {
        /* Left Shift or Right Shift pressed */
        shift_pressed = true;
        return;
    }
    if (scancode == 0x3A) {
        /* Caps Lock: toggle */
        caps_lock = !caps_lock;
        return;
    }

    /* Convert scan code to ASCII */
    if (scancode >= KB_SCANCODE_MAX) return;  /* Unknown key */

    char c;
    if (shift_pressed) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
        /* Apply Caps Lock to alphabetic characters */
        if (caps_lock && c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';
        }
    }

    if (c == 0) return;  /* No ASCII representation */

    /* Add to circular buffer */
    kb_buffer_put(c);
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq_handler);
}

char keyboard_getchar(void) {
    /* Spin-wait until a character is available.
     * HLT is used to avoid burning CPU cycles. */
    while (kb_buffer_count() == 0) {
        __asm__ volatile ("hlt");
    }
    return kb_buffer_get();
}

void keyboard_readline(char* buf, size_t max_len) {
    size_t i = 0;

    while (i < max_len - 1) {
        char c = keyboard_getchar();

        if (c == '\n') {
            /* Enter: end of line */
            vga_putchar('\n');
            break;
        } else if (c == '\b') {
            /* Backspace: delete last character */
            if (i > 0) {
                i--;
                vga_putchar('\b');  /* Move cursor back and erase */
            }
        } else {
            /* Regular character: echo and store */
            buf[i++] = c;
            vga_putchar(c);
        }
    }

    buf[i] = '\0';  /* Null-terminate */
}
