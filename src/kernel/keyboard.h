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

/* Initialize the keyboard driver (registers IRQ1 handler) */
void keyboard_init(void);

/*
 * keyboard_getchar - Block until a key is pressed, then return its ASCII value.
 *
 * Uses HLT to avoid spinning the CPU.
 * Returns 0 for special keys with no ASCII representation.
 */
char keyboard_getchar(void);

/*
 * keyboard_readline - Read a full line of text into buf (up to max_len-1 chars).
 *
 * Echoes characters to the VGA display.
 * Handles backspace.
 * Stops at newline (\n) or max_len-1 characters.
 * Always null-terminates the buffer.
 */
void keyboard_readline(char* buf, size_t max_len);

#endif /* KEYBOARD_H */
