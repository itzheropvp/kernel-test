/*
 * isr.h - Interrupt Service Routines (CPU exceptions 0-31)
 *
 * This declares the assembly stubs (defined in interrupts.asm) and
 * the C handler called when a CPU exception occurs.
 */

#ifndef ISR_H
#define ISR_H

#include "kernel.h"

/*
 * registers_t - Snapshot of CPU state when an interrupt occurred.
 *
 * This struct is built on the stack by our assembly stubs.
 * The layout MUST exactly match what the stubs push (see interrupts.asm).
 *
 * Field order (from low address / top of stack to high address):
 *   1. ds          — data segment (we push it ourselves)
 *   2-9. edi..eax  — general registers (pushed by PUSHA)
 *   10. int_no     — interrupt vector number (pushed by our stub)
 *   11. err_code   — CPU error code, or 0 for no-error exceptions
 *   12. eip        — instruction pointer (pushed by CPU)
 *   13. cs         — code segment (pushed by CPU)
 *   14. eflags     — flags register (pushed by CPU)
 *   [15. useresp]  — user stack pointer (only on privilege level change)
 *   [16. ss]       — user stack segment (only on privilege level change)
 */
typedef struct {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

/*
 * isr_handler - Called by the assembly stub for CPU exceptions.
 *
 * Prints diagnostic information and halts the kernel.
 * In a real OS this would handle page faults, call the debugger, etc.
 */
void isr_handler(registers_t* regs);

/*
 * Assembly stubs — one per CPU exception vector.
 * Defined in interrupts.asm, registered in the IDT by idt_init().
 */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

#endif /* ISR_H */
