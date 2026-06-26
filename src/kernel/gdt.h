/*
 * gdt.h - Global Descriptor Table
 *
 * WHAT IS THE GDT?
 *
 * In x86 Protected Mode, memory is accessed through "segments".
 * Every memory access goes through a segment register (CS, DS, ES, etc.)
 * which points to an entry in the GDT.
 *
 * Each GDT entry ("descriptor") defines:
 *   - Base address: where the segment starts in physical memory
 *   - Limit:        size of the segment
 *   - Flags:        type (code/data), privilege level (ring 0-3), etc.
 *
 * WHY DO WE NEED IT?
 *
 * The CPU won't let us execute code or access data without valid segment
 * descriptors. GRUB sets up a minimal GDT to get us into 32-bit mode,
 * but we need to install our own proper GDT.
 *
 * OUR GDT LAYOUT:
 *   Entry 0: Null descriptor   (required by x86 spec — never use index 0)
 *   Entry 1: Kernel Code       (ring 0, executable, 0x00000000–0xFFFFFFFF)
 *   Entry 2: Kernel Data       (ring 0, writable,   0x00000000–0xFFFFFFFF)
 *
 * With a "flat" memory model (base=0, limit=4GB), segments effectively
 * do nothing and the GDT just acts as a formality. Most modern OSes use
 * paging for real memory protection, not segmentation.
 *
 * SEGMENT SELECTORS:
 *   The segment registers (CS, DS, SS...) don't hold the base address —
 *   they hold a "selector": an index into the GDT packed into 16 bits:
 *
 *   Bits 15-3: Index (which GDT entry, 0–8191)
 *   Bit  2:    TI = Table Indicator (0=GDT, 1=LDT)
 *   Bits 1-0:  RPL = Requested Privilege Level (0=kernel, 3=user)
 *
 *   So kernel code selector = index 1 << 3 | 0 = 0x08
 *      kernel data selector = index 2 << 3 | 0 = 0x10
 */

#ifndef GDT_H
#define GDT_H

#include "kernel.h"

/*
 * GDT Entry (Segment Descriptor) — 8 bytes, hardware-defined format
 *
 * This is one of the most confusing data structures in x86 because
 * the fields are split across non-contiguous bits for historical reasons
 * (Intel had to maintain backward compatibility with the 80286).
 *
 *  Bit layout of each 8-byte descriptor:
 *
 *  63       56 55   52 51  48 47      40 39        16 15       0
 *  ┌──────────┬───────┬──────┬──────────┬────────────┬──────────┐
 *  │ Base[31:24]│ Flags │Limit[19:16]│  Access │ Base[23:0] │Limit[15:0]│
 *  └──────────┴───────┴──────┴──────────┴────────────┴──────────┘
 *
 * We use a packed C struct to match this layout exactly.
 */
typedef struct {
    uint16_t limit_low;    /* Limit bits 0-15  */
    uint16_t base_low;     /* Base  bits 0-15  */
    uint8_t  base_mid;     /* Base  bits 16-23 */

    /*
     * Access byte (bits 39-32):
     *   Bit 7: Present      (1 = this is a valid segment)
     *   Bit 6-5: DPL        (Descriptor Privilege Level: 0=kernel, 3=user)
     *   Bit 4: S            (0=system, 1=code/data)
     *   Bit 3: Executable   (1=code segment, 0=data segment)
     *   Bit 2: Direction/Conforming
     *   Bit 1: Readable/Writable
     *   Bit 0: Accessed     (CPU sets this when segment is used)
     */
    uint8_t  access;

    /*
     * Granularity byte (bits 55-48):
     *   Bit 7: G    (0=limit in bytes, 1=limit in 4KB pages)
     *   Bit 6: D/B  (0=16-bit, 1=32-bit segment)
     *   Bit 5: L    (1=64-bit code segment — we set to 0 for 32-bit)
     *   Bit 4: AVL  (available for OS use)
     *   Bits 3-0: Limit bits 16-19
     */
    uint8_t  granularity;

    uint8_t  base_high;    /* Base bits 24-31 */
} PACKED gdt_entry_t;

/*
 * GDT Descriptor (GDTR register format)
 *
 * The LGDT instruction loads this 6-byte structure:
 *   limit: size of GDT in bytes minus 1
 *   base:  physical address of the GDT
 */
typedef struct {
    uint16_t limit;       /* GDT size in bytes - 1 */
    uint32_t base;        /* Physical address of GDT array */
} PACKED gdt_descriptor_t;

/* Number of GDT entries we'll use */
#define GDT_ENTRIES 3

/* Segment selector values (what we load into CS, DS, SS, etc.) */
#define GDT_KERNEL_CODE  0x08   /* Entry 1: ring 0 code */
#define GDT_KERNEL_DATA  0x10   /* Entry 2: ring 0 data */

/*
 * gdt_init - Set up and load our GDT.
 *
 * After this call, our segments are properly configured.
 * This must be called very early in kernel startup, before
 * we do anything that relies on valid segment descriptors.
 */
void gdt_init(void);

/*
 * gdt_flush - Assembly routine that loads the GDT and reloads segment registers.
 *
 * Defined in gdt_flush.asm.
 * Takes a pointer to the gdt_descriptor_t struct.
 */
extern void gdt_flush(uint32_t gdt_descriptor_ptr);

#endif /* GDT_H */
