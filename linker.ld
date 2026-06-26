/*
 * linker.ld - Kernel Linker Script
 *
 * WHAT IS A LINKER SCRIPT?
 *
 * The linker combines multiple compiled object files (.o) into a single
 * executable (our kernel.elf). A linker script controls:
 *
 *   1. WHERE in memory each section is placed
 *   2. WHAT ORDER the sections appear
 *   3. Which SYMBOL is the entry point
 *
 * WHY 0x100000 (1 MB)?
 *
 * The x86 memory map at boot (legacy PC layout):
 *   0x00000–0x004FF: BIOS/IVT/BDA (Interrupt Vector Table, BIOS Data Area)
 *   0x00500–0x07BFF: Available
 *   0x07C00–0x07DFF: Boot sector (where BIOS loads the MBR)
 *   0x07E00–0x9FFFF: Available (extended by real mode programs)
 *   0xA0000–0xBFFFF: VGA video memory (0xB8000 = text mode buffer)
 *   0xC0000–0xFFFFF: BIOS ROM
 *   0x100000+:       Extended memory (RAM above 1 MB — what we use!)
 *
 * GRUB loads our kernel at 0x100000 (the "1 MB mark") to avoid all
 * the legacy reserved regions below it.
 *
 * ELF SECTIONS:
 *   .multiboot — Multiboot header (must be first, so GRUB finds it)
 *   .text      — Executable code (read + execute)
 *   .rodata    — Read-only data: string literals, const arrays
 *   .data      — Initialized read-write data: global variables with initial values
 *   .bss       — Uninitialized data: global variables (zeroed at startup)
 *
 * The 'kernel_end' symbol is created by this script — it marks the
 * end of the kernel binary in memory. Our bump allocator starts there.
 */

ENTRY(_start)          /* Tell the linker that _start (in boot.asm) is main entry */

SECTIONS {
    /*
     * Start placing output at 1 MB (physical address).
     * '.' is the "location counter" — the current output address.
     */
    . = 0x100000;

    /* ---- .text: executable code ---- */
    .text ALIGN(4096) : {
        *(.multiboot)  /* Multiboot header MUST be at the start */
        *(.text)       /* All .text sections from all object files */
    }

    /* ---- .rodata: read-only data (string literals, etc.) ---- */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* ---- .data: initialized global/static variables ---- */
    .data ALIGN(4096) : {
        *(.data)
    }

    /*
     * .bss: Uninitialized (or zero-initialized) global/static variables
     *
     * The BIOS and bootloader don't zero this memory for us.
     * Normally the C runtime startup code (crt0) zeroes BSS.
     * Since we have no crt0 (freestanding), we rely on our variables
     * being in static arrays (already zero since they're in .bss and
     * QEMU/GRUB does zero-initialize .bss for us in practice).
     *
     * COMMON: uninitialized external (non-static) variables go here.
     */
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /*
     * kernel_end: marks the end of the kernel binary.
     * Our memory allocator (memory.c) uses this as the start of the heap.
     */
    kernel_end = .;
}
