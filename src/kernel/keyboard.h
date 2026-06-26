/*
 * keyboard.h - PS/2 Keyboard Driver
 *
 * WHAT IS PS/2?
 *
 * PS/2 is a serial protocol for connecting keyboards and mice.
 * It was introduced by IBM in 1987 and is still emulated by modern
 * systems for compatibility (even USB keyboards can emulate PS/2).
 *
 * PS/2 I/O PORTS:
 *   0x60 = Data port:    read keyboard scan codes here
 *   0x64 = Status port:  check if data is available (read)
 *                        send commands to keyboard controller (write)
 *
 * HOW IT WORKS:
 *
 * 1. A key is pressed or released
 * 2. The keyboard controller generates a "scan code" (a byte)
 * 3. It places the scan code in port 0x60 and fires IRQ1
 * 4. Our IRQ1 handler reads the scan code
 * 5. We convert the scan code to an ASCII character
 * 6. We put the character in a circular buffer
 * 7. The shell reads from the buffer (blocking until a key is available)
 *
 * SCAN CODES (Set 1):
 *
 * PS/2 uses "scan code set 1" (the original IBM XT set) by default.
 *
 * MAKE CODE: fired when key is PRESSED   (e.g., 'A' pressed → 0x1E)
 * BREAK CODE: fired when key is RELEASED (make + 0x80, e.g., 'A' released → 0x9E)
 *
 * We ignore break codes (we only care about key presses for the shell).
 *
 * CIRCULAR BUFFER:
 *
 * The keyboard buffer uses a ring buffer (circular buffer) with two indices:
 *   write_idx: where the IRQ handler puts new characters
 *   read_idx:  where the shell reads characters from
 *
 * ┌─────────────────────────────┐
 * │  . . . A B C D . . . . . . │   Buffer full from 'A' to 'D'
 * │         ↑       ↑           │
 * │      read_idx  write_idx    │
 * └─────────────────────────────┘
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "kernel.h"

/*
 * SPECIAL KEY CODES (returned by keyboard_getkey, stored in buffer as uint8_t)
 *
 * PS/2 "extended" keys are prefixed with scan code 0xE0.
 * We intercept that prefix in the IRQ handler and convert the following
 * scan code into one of these values (all in 0x80-0x88, so no clash with
 * ASCII printable chars 0x20-0x7E).
 *
 * NOTE: 0x89-0xFF is reserved for extended Latin-1 / CP437 characters
 * typed via language-specific keyboard layouts (Italian accented vowels, etc.).
 * Those values pass through to the shell/editor as literal bytes and are
 * translated to CP437 for display in vga_putchar().
 */
#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86
#define KEY_PGUP  0x87
#define KEY_PGDN  0x88

/* Keyboard layout IDs passed to keyboard_set_layout() */
#define KEYMAP_US  0   /* US QWERTY (default) */
#define KEYMAP_IT  1   /* Italian QWERTY      */

/* Initialize the keyboard driver (registers IRQ1 handler) */
void keyboard_init(void);

/*
 * keyboard_set_layout - Switch the active keyboard layout.
 * @layout: KEYMAP_US or KEYMAP_IT
 */
void keyboard_set_layout(int layout);

/*
 * keyboard_getkey - Block until a key event, return it as uint8_t.
 *
 * Returns:
 *   0x01-0x7E  — ASCII character (printable or control like \n, \b, \t)
 *   KEY_UP/DOWN/LEFT/RIGHT/HOME/END/DEL  — special keys (>= 0x80)
 *
 * Use this for any new code — it handles special keys correctly.
 */
uint8_t keyboard_getkey(void);

/*
 * keyboard_getchar - Legacy: block and return ASCII char.
 * Returns 0 for keys with no ASCII meaning.
 */
char keyboard_getchar(void);

/*
 * keyboard_readline - Read a line (simple, no history/cursor movement).
 * Used in places where we just need raw text input (e.g. format confirm prompt).
 */
void keyboard_readline(char* buf, size_t max_len);

#endif /* KEYBOARD_H */
