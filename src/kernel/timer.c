/*
 * timer.c - PIT Timer Driver
 */

#include "timer.h"
#include "irq.h"

/* PIT I/O ports */
#define PIT_CHANNEL0  0x40   /* Channel 0 data port (for IRQ0)  */
#define PIT_CMD       0x43   /* Mode/Command register            */

/*
 * PIT command byte: 0x36
 *   Bits 7-6: 00 = Channel 0
 *   Bits 5-4: 11 = Access mode: low byte then high byte (lobyte/hibyte)
 *   Bits 3-1: 011 = Mode 3: Square Wave Generator
 *   Bit  0:   0   = Binary counting (not BCD)
 *   0x36 = 0011 0110
 */
#define PIT_CMD_CHANNEL0_RATE 0x36

/* Running tick counter — incremented by the IRQ0 handler */
static volatile uint32_t timer_ticks = 0;

/*
 * timer_irq_handler - Called every time IRQ0 fires.
 *
 * This is the "heartbeat" of the kernel.
 * At 100 Hz, it runs 100 times per second.
 */
static void timer_irq_handler(registers_t* regs) {
    (void)regs;  /* unused parameter — suppress compiler warning */
    timer_ticks++;
}

void timer_init(uint32_t frequency) {
    /*
     * Calculate the divisor.
     * The PIT counts down from this value and fires IRQ0 when it hits 0.
     *
     * Example: frequency = 100 Hz
     *   divisor = 1,193,182 / 100 = 11,931
     *   Each tick = 1/100 second = 10 ms
     */
    uint32_t divisor = PIT_FREQUENCY / frequency;

    /* Send the command byte to configure Channel 0 */
    outb(PIT_CMD, PIT_CMD_CHANNEL0_RATE);

    /*
     * Send the divisor as two bytes (low byte first, then high byte).
     * This is called "lobyte/hibyte" mode — set in the command byte above.
     */
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));         /* Low  byte */
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF)); /* High byte */

    /* Register our IRQ0 handler */
    irq_install_handler(0, timer_irq_handler);
}

uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

void timer_sleep(uint32_t ticks) {
    uint32_t end = timer_ticks + ticks;
    /*
     * Spin until the desired tick count is reached.
     * HLT halts the CPU until the next interrupt fires (energy-efficient busy-wait).
     * Without HLT here, we'd burn 100% CPU just spinning.
     */
    while (timer_ticks < end) {
        __asm__ volatile ("hlt");
    }
}
