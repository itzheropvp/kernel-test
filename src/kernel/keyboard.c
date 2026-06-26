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
 * SCAN CODE → CHARACTER TABLES
 *
 * Index = scan code (key pressed)
 * Value = character byte (0 = no output for this key)
 *
 * US layout: standard ASCII.
 * Italian layout: uses Latin-1 byte values for accented chars.
 *   These reach vga_putchar() as-is; vga_putchar() translates
 *   Latin-1 0x80-0xFF to the correct CP437 glyph for display.
 *
 * ENCODING NOTE:
 *   0x20-0x7E  = plain ASCII
 *   0x80-0x88  = KEY_UP/DOWN/LEFT/RIGHT/HOME/END/DEL/PGUP/PGDN (never in tables)
 *   0x89-0xFF  = Latin-1 extended chars (accented vowels, £, etc.)
 * ============================================================ */

/* ---- US QWERTY ---- */

static const uint8_t scancode_to_ascii[KB_SCANCODE_MAX] = {
    /*  0 */ 0,    /* No key      */
    /*  1 */ 0,    /* ESC — handled specially */
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

static const uint8_t scancode_to_ascii_shift[KB_SCANCODE_MAX] = {
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

/* ---- Italian QWERTY ----
 *
 * Italian keyboard differences from US QWERTY:
 *   - Letters are in the same positions (both are QWERTY).
 *   - Special characters and symbols differ significantly.
 *   - Accented vowels have dedicated keys (è, à, ì, ò, ù).
 *   - The key to the right of '0' gives apostrophe (not -).
 *   - Shift+number row is different (e.g. Shift+2 = " not @).
 *   - @ and # require AltGr (see altgr table below).
 *
 * Latin-1 byte values for Italian accented characters:
 *   0xE8=è  0xE9=é  0xE0=à  0xEC=ì  0xF2=ò  0xF9=ù  0xE7=ç  0xA3=£
 * vga_putchar() translates these to CP437 for correct on-screen display.
 */
static const uint8_t scancode_to_ascii_it[KB_SCANCODE_MAX] = {
    /*  0 */ 0,
    /*  1 */ 0,       /* ESC */
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
    /* 12 */ '\'',    /* IT: apostrophe (US: -) */
    /* 13 */ 0xEC,    /* IT: ì (US: =) — Latin-1 0xEC */
    /* 14 */ '\b',
    /* 15 */ '\t',
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
    /* 26 */ 0xE8,    /* IT: è (US: [) — Latin-1 0xE8 */
    /* 27 */ '+',     /* IT: + (US: ]) */
    /* 28 */ '\n',
    /* 29 */ 0,       /* Left Ctrl */
    /* 30 */ 'a',
    /* 31 */ 's',
    /* 32 */ 'd',
    /* 33 */ 'f',
    /* 34 */ 'g',
    /* 35 */ 'h',
    /* 36 */ 'j',
    /* 37 */ 'k',
    /* 38 */ 'l',
    /* 39 */ 0xF2,    /* IT: ò (US: ;) — Latin-1 0xF2 */
    /* 40 */ 0xE0,    /* IT: à (US: ') — Latin-1 0xE0 */
    /* 41 */ '\\',    /* IT: \ (US: `) */
    /* 42 */ 0,       /* Left Shift */
    /* 43 */ 0xF9,    /* IT: ù (US: \) — Latin-1 0xF9 */
    /* 44 */ 'z',
    /* 45 */ 'x',
    /* 46 */ 'c',
    /* 47 */ 'v',
    /* 48 */ 'b',
    /* 49 */ 'n',
    /* 50 */ 'm',
    /* 51 */ ',',
    /* 52 */ '.',
    /* 53 */ '-',     /* IT: - (US: /) */
    /* 54 */ 0,       /* Right Shift */
    /* 55 */ '*',
    /* 56 */ 0,       /* Left Alt */
    /* 57 */ ' ',
};

static const uint8_t scancode_to_ascii_shift_it[KB_SCANCODE_MAX] = {
    /*  0 */ 0,
    /*  1 */ 0,       /* ESC */
    /*  2 */ '!',
    /*  3 */ '"',     /* IT: " (US: @) */
    /*  4 */ 0xA3,    /* IT: £ (US: #) — Latin-1 0xA3 */
    /*  5 */ '$',
    /*  6 */ '%',
    /*  7 */ '&',     /* IT: & (US: ^) */
    /*  8 */ '/',     /* IT: / (US: &) */
    /*  9 */ '(',     /* IT: ( (US: *) */
    /* 10 */ ')',     /* IT: ) (US: () */
    /* 11 */ '=',     /* IT: = (US: )) */
    /* 12 */ '?',     /* IT: ? (US: _) */
    /* 13 */ '^',     /* IT: ^ (US: +) */
    /* 14 */ '\b',
    /* 15 */ '\t',
    /* 16 */ 'Q',
    /* 17 */ 'W',
    /* 18 */ 'E',
    /* 19 */ 'R',
    /* 20 */ 'T',
    /* 21 */ 'Y',
    /* 22 */ 'U',
    /* 23 */ 'I',
    /* 24 */ 'O',
    /* 25 */ 'P',
    /* 26 */ 0xE9,    /* IT: é (US: {) — Latin-1 0xE9 */
    /* 27 */ '*',     /* IT: * (US: }) */
    /* 28 */ '\n',
    /* 29 */ 0,
    /* 30 */ 'A',
    /* 31 */ 'S',
    /* 32 */ 'D',
    /* 33 */ 'F',
    /* 34 */ 'G',
    /* 35 */ 'H',
    /* 36 */ 'J',
    /* 37 */ 'K',
    /* 38 */ 'L',
    /* 39 */ 0xE7,    /* IT: ç (US: :) — Latin-1 0xE7 */
    /* 40 */ 0xB0,    /* IT: ° (US: ") — Latin-1 0xB0 */
    /* 41 */ '|',     /* IT: | (US: ~) */
    /* 42 */ 0,       /* Left Shift */
    /* 43 */ 0xA7,    /* IT: § (US: |) — Latin-1 0xA7 */
    /* 44 */ 'Z',
    /* 45 */ 'X',
    /* 46 */ 'C',
    /* 47 */ 'V',
    /* 48 */ 'B',
    /* 49 */ 'N',
    /* 50 */ 'M',
    /* 51 */ ';',     /* IT: ; (US: <) */
    /* 52 */ ':',     /* IT: : (US: >) */
    /* 53 */ '_',     /* IT: _ (US: ?) */
    /* 54 */ 0,       /* Right Shift */
    /* 55 */ '*',
    /* 56 */ 0,
    /* 57 */ ' ',
};

/*
 * AltGr table for Italian keyboard.
 *
 * AltGr (Right Alt) is how Italian keyboards access programming characters
 * that aren't on the normal or shifted layers.  On physical keyboards, AltGr
 * sends E0 38 (make) / E0 B8 (break) — distinguished from plain Left Alt by
 * the 0xE0 prefix.
 *
 * Most important Italian AltGr combos:
 *   AltGr+2  → @     AltGr+3  → #
 *   AltGr+7  → {     AltGr+8  → [
 *   AltGr+9  → ]     AltGr+0  → }
 *   AltGr+ì  → `     (backtick — not otherwise reachable on Italian kbd)
 */
static const uint8_t scancode_altgr_it[KB_SCANCODE_MAX] = {
    0, 0,      /* 0, 1 */
    0,         /* 2: 1 — no AltGr */
    '@',       /* 3: 2 → @ */
    '#',       /* 4: 3 → # */
    0, 0, 0,   /* 5-7: 4-6 */
    '{',       /* 8: 7 → { */
    '[',       /* 9: 8 → [ */
    ']',       /* 10: 9 → ] */
    '}',       /* 11: 0 → } */
    0,         /* 12: ' */
    '`',       /* 13: ì → ` (backtick) */
    0, 0,      /* 14-15: Backspace, Tab */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 16-25: Q-P */
    '[',       /* 26: è → [ (alternate) */
    ']',       /* 27: + → ] (alternate) */
    0, 0,      /* 28-29: Enter, Ctrl */
    0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 30-38: A-L */
    '@',       /* 39: ò → @ (alternate position on some keyboards) */
    '#',       /* 40: à → # (alternate position on some keyboards) */
    0, 0,      /* 41-42 */
    0,         /* 43: ù */
    0, 0, 0, 0, 0, 0, 0,  /* 44-50: Z-M */
    0, 0, 0,   /* 51-53: , . - */
    0, 0, 0, 0,  /* 54-57 */
};

/* ============================================================
 * KEYBOARD STATE
 * ============================================================ */

/* Modifier key states */
static bool shift_pressed = false;
static bool caps_lock     = false;
static bool altgr_pressed = false;  /* Right Alt / AltGr (E0+38) */

/*
 * Extended key flag: set when the IRQ handler receives 0xE0.
 * The NEXT scan code received after 0xE0 is an "extended" key.
 * We also use this to distinguish Right Alt (AltGr) from Left Alt:
 *   Left Alt  → 0x38  (plain make code)
 *   Right Alt → E0 38 (extended make code) = AltGr
 */
static bool extended_key = false;

/* Active keyboard layout (KEYMAP_US or KEYMAP_IT) */
static int  current_keymap = KEYMAP_US;

/* ============================================================
 * CIRCULAR (RING) BUFFER
 *
 * Changed from char to uint8_t so we can store values >= 0x80
 * for special keys (arrows, Home, End, etc.) without signed-ness issues.
 * ============================================================ */

#define KB_BUFFER_SIZE 256

static volatile uint8_t  kb_buffer[KB_BUFFER_SIZE];
static volatile uint32_t kb_write = 0;
static volatile uint32_t kb_read  = 0;

static uint32_t kb_buffer_count(void) {
    return (kb_write - kb_read) % KB_BUFFER_SIZE;
}

static void kb_buffer_put(uint8_t c) {
    uint32_t next_write = (kb_write + 1) % KB_BUFFER_SIZE;
    if (next_write != kb_read) {
        kb_buffer[kb_write] = c;
        kb_write = next_write;
    }
}

static uint8_t kb_buffer_get(void) {
    uint8_t c = kb_buffer[kb_read];
    kb_read = (kb_read + 1) % KB_BUFFER_SIZE;
    return c;
}

/* ============================================================
 * IRQ1 HANDLER
 * ============================================================ */

static void keyboard_irq_handler(registers_t* regs) {
    (void)regs;

    uint8_t scancode = inb(KB_DATA_PORT);

    /*
     * 0xE0 = Extended key prefix.
     * The NEXT scan code will be an extended key (arrow, Home, End, etc.).
     * Just set the flag and return; the next IRQ will do the real work.
     */
    if (scancode == 0xE0) {
        extended_key = true;
        return;
    }

    /*
     * Handle extended key scan codes (preceded by 0xE0).
     * This covers arrow keys, Home, End, Del, PgUp, PgDn, AND Right Alt (AltGr).
     */
    if (extended_key) {
        extended_key = false;

        if (scancode & 0x80) {
            /* Extended break code (key release) */
            uint8_t make = scancode & 0x7F;
            if (make == 0x38) altgr_pressed = false;  /* Right Alt released */
            return;
        }

        /* Extended make codes */
        if (scancode == 0x38) { altgr_pressed = true; return; }  /* Right Alt / AltGr */

        uint8_t special = 0;
        switch (scancode) {
            case 0x48: special = KEY_UP;    break;  /* Up Arrow    */
            case 0x50: special = KEY_DOWN;  break;  /* Down Arrow  */
            case 0x4B: special = KEY_LEFT;  break;  /* Left Arrow  */
            case 0x4D: special = KEY_RIGHT; break;  /* Right Arrow */
            case 0x47: special = KEY_HOME;  break;  /* Home        */
            case 0x4F: special = KEY_END;   break;  /* End         */
            case 0x53: special = KEY_DEL;   break;  /* Delete      */
            case 0x49: special = KEY_PGUP;  break;  /* Page Up     */
            case 0x51: special = KEY_PGDN;  break;  /* Page Down   */
        }
        if (special) kb_buffer_put(special);
        return;
    }

    /* Regular key: break codes (>= 0x80) are key releases */
    if (scancode >= 0x80) {
        uint8_t make = scancode - 0x80;
        if (make == 0x2A || make == 0x36) shift_pressed = false;
        return;
    }

    /* Track modifier key presses */
    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = true; return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock; return; }

    /*
     * ESC key (scan code 0x01): put 0x1B (ASCII ESC) in the buffer.
     * VIM uses ESC to leave INSERT mode.
     */
    if (scancode == 0x01) {
        kb_buffer_put(0x1B);
        return;
    }

    /*
     * ISO extra key (scan code 0x56): the key between Left Shift and Z
     * on European 102-key keyboards.  Not present on US 101-key keyboards.
     * Italian: '<' unshifted, '>' shifted.
     */
    if (scancode == 0x56 && current_keymap == KEYMAP_IT) {
        kb_buffer_put((uint8_t)(shift_pressed ? '>' : '<'));
        return;
    }

    if (scancode >= KB_SCANCODE_MAX) return;

    uint8_t c;

    /* AltGr layer (Italian only) */
    if (altgr_pressed && current_keymap == KEYMAP_IT) {
        c = scancode_altgr_it[scancode];
        if (c) { kb_buffer_put(c); return; }
        /* Fall through to normal layer if no AltGr mapping */
    }

    /* Normal / shifted layer — select tables based on active layout */
    if (current_keymap == KEYMAP_IT) {
        c = shift_pressed ? scancode_to_ascii_shift_it[scancode]
                          : scancode_to_ascii_it[scancode];
    } else {
        c = shift_pressed ? scancode_to_ascii_shift[scancode]
                          : scancode_to_ascii[scancode];
    }

    /* Apply caps lock for plain letters (only works on ASCII a-z range) */
    if (!shift_pressed && caps_lock && c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 'a' + 'A');

    if (c == 0) return;
    kb_buffer_put(c);
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq_handler);
}

void keyboard_set_layout(int layout) {
    current_keymap = (layout == KEYMAP_IT) ? KEYMAP_IT : KEYMAP_US;
    altgr_pressed  = false;  /* release any stuck AltGr state on layout change */
}

uint8_t keyboard_getkey(void) {
    while (kb_buffer_count() == 0)
        __asm__ volatile ("hlt");
    return kb_buffer_get();
}

char keyboard_getchar(void) {
    /* Legacy wrapper: returns ASCII char, 0 for special keys */
    uint8_t k = keyboard_getkey();
    return (k < 0x80) ? (char)k : 0;
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
