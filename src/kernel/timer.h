/*
 * timer.h - PIT (Programmable Interval Timer) Driver
 *
 * WHAT IS THE PIT?
 *
 * The Intel 8253/8254 PIT is a hardware chip that generates periodic
 * electrical pulses at programmable frequencies.
 *
 * It has 3 channels:
 *   Channel 0: Connected to IRQ0 — generates timer interrupts (WE USE THIS)
 *   Channel 1: Was used for DRAM refresh (obsolete)
 *   Channel 2: Connected to the PC speaker — beeps!
 *
 * HOW TO PROGRAM IT:
 *
 * The PIT runs on a base clock of 1,193,182 Hz (~1.19 MHz).
 * By setting a "divisor", we control how often IRQ0 fires:
 *
 *   IRQ frequency = 1,193,182 / divisor
 *
 * We want 100 Hz (100 interrupts per second):
 *   divisor = 1,193,182 / 100 = 11931
 *
 * I/O PORTS:
 *   0x40 = Channel 0 data port (read/write)
 *   0x43 = Mode/Command register (write-only)
 */

#ifndef TIMER_H
#define TIMER_H

#include "kernel.h"

/* PIT base clock frequency in Hz */
#define PIT_FREQUENCY 1193182

/*
 * timer_init - Configure the PIT to fire at the given frequency (Hz).
 *
 * @frequency: desired interrupts per second (e.g., 100 for 100 Hz)
 *
 * Call this before enabling interrupts with STI.
 */
void timer_init(uint32_t frequency);

/*
 * timer_get_ticks - Return the number of timer interrupts since boot.
 *
 * Increases by 1 every (1/frequency) seconds.
 * At 100 Hz: 1 second = 100 ticks.
 */
uint32_t timer_get_ticks(void);

/*
 * timer_sleep - Busy-wait for approximately the given number of ticks.
 *
 * At 100 Hz, 1 tick ≈ 10 milliseconds.
 *
 * WARNING: This is a busy-wait — it burns CPU cycles.
 * A real OS would put the thread to sleep and schedule other tasks.
 */
void timer_sleep(uint32_t ticks);

#endif /* TIMER_H */
