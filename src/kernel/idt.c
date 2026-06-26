/*
 * idt.c - Interrupt Descriptor Table setup
 */

#include "idt.h"
#include "gdt.h"
#include "isr.h"
#include "irq.h"
#include "../libc/string.h"

/* The IDT: 256 entries × 8 bytes each = 2048 bytes */
static idt_entry_t idt[IDT_ENTRIES];

/* The descriptor loaded into IDTR */
static idt_descriptor_t idt_desc;

/* Assembly routine to actually load the IDT (uses LIDT instruction) */
static void idt_load(void) {
    __asm__ volatile ("lidt %0" : : "m"(idt_desc));
}

void idt_set_gate(uint8_t vector, uint32_t handler,
                  uint16_t selector, uint8_t type_attr) {
    idt[vector].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[vector].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].selector    = selector;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = type_attr;
}

void idt_init(void) {
    /* Set up the IDTR descriptor */
    idt_desc.limit = (uint16_t)(sizeof(idt_entry_t) * IDT_ENTRIES - 1);
    idt_desc.base  = (uint32_t)&idt;

    /* Zero out all entries first (marks them as not-present) */
    memset(&idt, 0, sizeof(idt));

    /*
     * Install CPU exception handlers (ISR 0-31)
     *
     * These are all declared as extern in isr.h and defined
     * as assembly stubs in interrupts.asm.
     *
     * Type 0x8E = Present, Ring 0, Interrupt Gate (32-bit)
     *   1000 1110
     *   P=1, DPL=00, 0, Type=1110 (32-bit interrupt gate)
     */
    idt_set_gate(0,  (uint32_t)isr0,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, GDT_KERNEL_CODE, 0x8E);

    /*
     * Install hardware IRQ handlers (vectors 32-47)
     *
     * These are remapped from their default hardware vectors (0-15)
     * to avoid conflicts with CPU exceptions.
     * The remapping is done in pic_init() (called from irq_init()).
     */
    idt_set_gate(32, (uint32_t)irq0,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, GDT_KERNEL_CODE, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, GDT_KERNEL_CODE, 0x8E);

    /* Load the IDT into the CPU's IDTR register */
    idt_load();
}
