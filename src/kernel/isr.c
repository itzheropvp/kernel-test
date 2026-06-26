/*
 * isr.c - CPU Exception Handler
 *
 * When the CPU encounters an unrecoverable error (division by zero,
 * invalid opcode, page fault, etc.), it fires an exception.
 * Our isr_handler() is called and displays a "kernel panic" message.
 *
 * In a real OS like Linux, exceptions are more nuanced:
 *   - Page faults (#PF) are often handled by loading memory from disk (swapping)
 *   - Breakpoints (#BP) are used by debuggers
 *   - Some signal the current process to terminate (SIGSEGV etc.)
 * For our kernel, any exception = halt and show info.
 */

#include "isr.h"
#include "../drivers/vga.h"
#include "../libc/string.h"

/*
 * Human-readable names for the 32 CPU exception vectors.
 * Index corresponds to the interrupt vector number.
 */
static const char* exception_names[32] = {
    "Division By Zero",           /* 0  #DE */
    "Debug",                      /* 1  #DB */
    "Non Maskable Interrupt",     /* 2       */
    "Breakpoint",                 /* 3  #BP */
    "Overflow",                   /* 4  #OF */
    "Bound Range Exceeded",       /* 5  #BR */
    "Invalid Opcode",             /* 6  #UD */
    "Device Not Available",       /* 7  #NM */
    "Double Fault",               /* 8  #DF */
    "Coprocessor Seg Overrun",    /* 9       */
    "Invalid TSS",                /* 10 #TS */
    "Segment Not Present",        /* 11 #NP */
    "Stack-Segment Fault",        /* 12 #SS */
    "General Protection Fault",   /* 13 #GP */
    "Page Fault",                 /* 14 #PF */
    "Reserved",                   /* 15      */
    "x87 FPU Error",              /* 16 #MF */
    "Alignment Check",            /* 17 #AC */
    "Machine Check",              /* 18 #MC */
    "SIMD Floating-Point",        /* 19 #XM */
    "Virtualization Exception",   /* 20 #VE */
    "Reserved",                   /* 21      */
    "Reserved",                   /* 22      */
    "Reserved",                   /* 23      */
    "Reserved",                   /* 24      */
    "Reserved",                   /* 25      */
    "Reserved",                   /* 26      */
    "Reserved",                   /* 27      */
    "Reserved",                   /* 28      */
    "Reserved",                   /* 29      */
    "Security Exception",         /* 30 #SX */
    "Reserved",                   /* 31      */
};

/*
 * isr_handler - Called by assembly stubs when a CPU exception fires.
 *
 * This is our "kernel panic" function. We print diagnostic info and
 * halt the CPU forever.
 *
 * The 'regs' pointer lets us see the CPU state AT THE TIME of the crash,
 * which is invaluable for debugging.
 */
void isr_handler(registers_t* regs) {
    /* Switch to red-on-black for the panic message */
    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);

    vga_print("\n\n");
    vga_print("  *** KERNEL PANIC ***\n");
    vga_print("  ==================\n\n");

    if (regs->int_no < 32) {
        vga_printf("  Exception: [%u] %s\n",
                   regs->int_no,
                   exception_names[regs->int_no]);
    } else {
        vga_printf("  Exception: [%u] Unknown\n", regs->int_no);
    }

    /* The error code is meaningful for some exceptions:
     *   #PF (14): bits indicate present/write/user/reserved-bit/instruction-fetch
     *   #GP (13): 0 means not segment-related, non-0 encodes segment selector
     *   #SS, #NP, #TS: encode the segment selector that caused the fault
     */
    vga_printf("  Error Code: 0x%x\n", regs->err_code);

    vga_print("\n  CPU Register Dump:\n");
    vga_printf("    EIP=0x%p  CS=0x%x  EFLAGS=0x%x\n",
               regs->eip, regs->cs, regs->eflags);
    vga_printf("    EAX=0x%p  EBX=0x%p  ECX=0x%p  EDX=0x%p\n",
               regs->eax, regs->ebx, regs->ecx, regs->edx);
    vga_printf("    ESI=0x%p  EDI=0x%p  EBP=0x%p  ESP=0x%p\n",
               regs->esi, regs->edi, regs->ebp, regs->esp);
    vga_printf("    DS=0x%x\n", regs->ds);

    vga_print("\n  System halted. Restart your machine.\n");

    /* Disable interrupts and halt forever */
    __asm__ volatile ("cli; hlt");
    while (1) {}  /* In case hlt somehow returns */
}
