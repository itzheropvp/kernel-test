/*
 * irq.h - Hardware Interrupt Request (IRQ) Management
 *
 * WHAT IS THE PIC?
 *
 * The 8259A PIC (Programmable Interrupt Controller) is a chip that manages
 * hardware interrupt signals from devices (keyboard, timer, disk, etc.).
 *
 * The original IBM PC has TWO PICs:
 *
 *   Master PIC: handles IRQ 0-7
 *     IRQ0 = PIT Timer
 *     IRQ1 = Keyboard
 *     IRQ2 = Cascade (connected to slave PIC)
 *     IRQ3 = COM2
 *     IRQ4 = COM1
 *     IRQ5 = LPT2
 *     IRQ6 = Floppy
 *     IRQ7 = LPT1
 *
 *   Slave PIC: handles IRQ 8-15 (connected to master via IRQ2)
 *     IRQ8  = RTC
 *     IRQ9  = Available
 *     IRQ10 = Available
 *     IRQ11 = Available
 *     IRQ12 = PS/2 Mouse
 *     IRQ13 = FPU
 *     IRQ14 = Primary ATA
 *     IRQ15 = Secondary ATA
 *
 * WHY DO WE REMAP THE PIC?
 *
 * By default, the PIC maps IRQ0-7 to INT vectors 0x08-0x0F.
 * But the CPU uses vectors 0x00-0x1F for exceptions!
 * So IRQ0 (timer) would fire vector 8, which is also #DF (Double Fault).
 *
 * We REMAP the PIC so:
 *   Master PIC: IRQ0-7  → vectors 0x20-0x27 (32-39)
 *   Slave  PIC: IRQ8-15 → vectors 0x28-0x2F (40-47)
 *
 * WHAT IS EOI?
 *
 * After handling a hardware interrupt, we MUST send an "End of Interrupt"
 * signal to the PIC. Without EOI, the PIC won't send any more interrupts
 * of the same priority or lower — the system effectively freezes.
 */

#ifndef IRQ_H
#define IRQ_H

#include "kernel.h"
#include "isr.h"

/* IRQ handler function type: takes the register state */
typedef void (*irq_handler_t)(registers_t* regs);

/*
 * irq_init - Initialize the PIC and set up IRQ handler dispatch table.
 *
 * Must be called AFTER idt_init() since we need the IDT entries for
 * vectors 32-47 to already be set up.
 */
void irq_init(void);

/*
 * irq_install_handler - Register a C function to handle a specific IRQ.
 *
 * @irq:     IRQ number (0-15)
 * @handler: function to call when that IRQ fires
 *
 * Example:
 *   irq_install_handler(1, keyboard_handler);  // IRQ1 = keyboard
 */
void irq_install_handler(int irq, irq_handler_t handler);

/*
 * irq_uninstall_handler - Remove the handler for an IRQ.
 *
 * @irq: IRQ number (0-15)
 */
void irq_uninstall_handler(int irq);

/*
 * irq_handler - Called by the assembly stub for hardware interrupts.
 *
 * Dispatches to the registered C handler, then sends EOI to the PIC.
 * Defined in irq.c.
 */
void irq_handler(registers_t* regs);

/* Assembly stubs — one per IRQ line. Defined in interrupts.asm. */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#endif /* IRQ_H */
