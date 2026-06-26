/*
 * irq.c - PIC initialization and IRQ dispatch
 */

#include "irq.h"
#include "../drivers/vga.h"
#include "../libc/string.h"

/* ============================================================
 * PIC I/O PORTS
 *
 * Each PIC has two ports:
 *   Command port: used to send initialization and control commands
 *   Data port:    used to configure masks and read status
 * ============================================================ */
#define PIC_MASTER_CMD  0x20   /* Master PIC: command port */
#define PIC_MASTER_DATA 0x21   /* Master PIC: data port    */
#define PIC_SLAVE_CMD   0xA0   /* Slave  PIC: command port */
#define PIC_SLAVE_DATA  0xA1   /* Slave  PIC: data port    */

/*
 * End of Interrupt command.
 * We write this to the PIC command port after handling an IRQ.
 * Without it, the PIC won't generate new interrupts of the same priority.
 */
#define PIC_EOI         0x20

/*
 * PIC initialization words (ICW = Initialization Command Words)
 *
 * The 8259A is programmed with a sequence of "words" (bytes).
 * We send ICW1 through ICW4 in order.
 *
 * ICW1: 0x11 = 0001 0001
 *   Bit 4: 1 = initialization (required)
 *   Bit 3: 0 = edge triggered
 *   Bit 1: 0 = cascade mode (two PICs)
 *   Bit 0: 1 = ICW4 needed
 *
 * ICW2: vector offset (where IRQ0 maps in the IDT)
 *   Master: 0x20 = 32 → IRQ0 maps to vector 32
 *   Slave:  0x28 = 40 → IRQ8 maps to vector 40
 *
 * ICW3: cascade configuration
 *   Master: 0x04 = bit 2 set → IRQ2 is connected to slave
 *   Slave:  0x02 = slave ID = 2 (matches master's IRQ2)
 *
 * ICW4: 0x01 = 8086 mode (not 8080 mode)
 */
#define ICW1_INIT       0x11
#define ICW4_8086       0x01

/* Interrupt vector base for remapped IRQs */
#define IRQ_BASE        0x20   /* IRQ0 → vector 32 (0x20) */

/* ============================================================
 * IRQ HANDLER TABLE
 *
 * We store one function pointer per IRQ (0-15).
 * NULL means "no handler installed" for that IRQ.
 * ============================================================ */
static irq_handler_t irq_handlers[16];

/*
 * pic_init - Remap the 8259A PIC and disable all IRQs initially.
 *
 * After this function, the PIC will send IRQ0→vector32, IRQ1→vector33, etc.
 */
static void pic_init(void) {
    /*
     * ICW1: Start initialization sequence.
     * We send 0x11 to both PICs' command ports simultaneously.
     */
    outb(PIC_MASTER_CMD,  ICW1_INIT);
    io_wait();
    outb(PIC_SLAVE_CMD,   ICW1_INIT);
    io_wait();

    /*
     * ICW2: Set the interrupt vector base addresses.
     * Master: IRQ0-7  will trigger vectors 0x20-0x27
     * Slave:  IRQ8-15 will trigger vectors 0x28-0x2F
     */
    outb(PIC_MASTER_DATA, IRQ_BASE);        /* Master IRQ0 → vector 32 */
    io_wait();
    outb(PIC_SLAVE_DATA,  IRQ_BASE + 8);   /* Slave  IRQ8 → vector 40 */
    io_wait();

    /*
     * ICW3: Configure cascade.
     * Master: bit 2 = IRQ line 2 is connected to the slave
     * Slave:  value 2 = this slave is connected to master's IRQ2
     */
    outb(PIC_MASTER_DATA, 0x04);   /* Master: slave at IRQ2 */
    io_wait();
    outb(PIC_SLAVE_DATA,  0x02);   /* Slave:  slave ID = 2  */
    io_wait();

    /* ICW4: Set 8086 mode */
    outb(PIC_MASTER_DATA, ICW4_8086);
    io_wait();
    outb(PIC_SLAVE_DATA,  ICW4_8086);
    io_wait();

    /*
     * Set interrupt mask registers.
     * Writing 1 to a bit MASKS (disables) that IRQ.
     * Writing 0 UNMASKS (enables) it.
     *
     * We start with ALL IRQs masked, then individual drivers
     * unmask their IRQ when they initialize.
     *
     * Actually, let's enable all — our stubs will handle any
     * unexpected IRQs gracefully by ignoring them.
     */
    outb(PIC_MASTER_DATA, 0x00);   /* Enable all master IRQs */
    outb(PIC_SLAVE_DATA,  0x00);   /* Enable all slave IRQs  */
}

void irq_init(void) {
    /* Zero out the handler table (all NULL = no handlers yet) */
    memset(irq_handlers, 0, sizeof(irq_handlers));

    /* Initialize and remap the PIC */
    pic_init();
}

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_uninstall_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

/*
 * irq_handler - Called by IRQ assembly stubs.
 *
 * We receive regs->int_no = 32 + IRQ_number.
 * So the actual IRQ number = int_no - 32.
 */
void irq_handler(registers_t* regs) {
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    /* Call the registered handler (if any) */
    if (irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq](regs);
    }

    /*
     * Send End of Interrupt (EOI) signal to the PIC(s).
     *
     * For IRQ 8-15 (slave PIC): we must send EOI to BOTH
     * the slave AND the master (because the slave is cascaded
     * through the master's IRQ2).
     *
     * For IRQ 0-7 (master only): just send to master.
     */
    if (irq >= 8) {
        outb(PIC_SLAVE_CMD, PIC_EOI);   /* EOI to slave */
    }
    outb(PIC_MASTER_CMD, PIC_EOI);      /* EOI to master (always) */
}
