/*
 * gdt.c - Global Descriptor Table setup
 */

#include "gdt.h"
#include "../libc/string.h"

/* The actual GDT: an array of 3 entries */
static gdt_entry_t  gdt[GDT_ENTRIES];

/* The descriptor loaded into the GDTR register */
static gdt_descriptor_t gdt_desc;

/*
 * gdt_set_entry - Fill in one GDT entry.
 *
 * @index:       which slot in gdt[] to fill
 * @base:        segment base address (physical address where segment starts)
 * @limit:       segment size (in bytes if G=0, in 4KB pages if G=1)
 * @access:      access byte (privilege level, type, etc.)
 * @granularity: granularity/flags byte
 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t granularity) {
    gdt[index].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[index].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[index].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[index].limit_low   = (uint16_t)(limit & 0xFFFF);

    /*
     * Pack the upper 4 bits of limit (bits 16-19) into the lower nibble
     * of the granularity byte, and the flags into the upper nibble.
     */
    gdt[index].granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt[index].granularity |= (granularity & 0xF0);

    gdt[index].access      = access;
}

void gdt_init(void) {
    /*
     * Set up the descriptor that points to our GDT array.
     *
     * limit = total size in bytes - 1
     * base  = address of the gdt[] array
     */
    gdt_desc.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
    gdt_desc.base  = (uint32_t)&gdt;

    /* ---------------------------------------------------------------
     * Entry 0: NULL descriptor
     *
     * The x86 spec requires the first GDT entry to be all zeros.
     * Loading a null selector into CS causes a #GP fault.
     * This is a safety feature: segment register = 0 → crash.
     * --------------------------------------------------------------- */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* ---------------------------------------------------------------
     * Entry 1: Kernel Code Segment
     *
     * Access byte breakdown (0x9A):
     *   Bit 7: 1    = Present (valid)
     *   Bit 6-5: 00 = DPL=0 (ring 0 / kernel privilege)
     *   Bit 4: 1    = S=1 (code or data segment, not system)
     *   Bit 3: 1    = Executable (this is a CODE segment)
     *   Bit 2: 0    = Not conforming
     *   Bit 1: 1    = Readable (we can read code from it)
     *   Bit 0: 0    = Not accessed yet
     *   0x9A = 1001 1010
     *
     * Granularity byte (0xCF):
     *   Bit 7: 1    = G=1 (limit in 4 KB pages)
     *   Bit 6: 1    = D=1 (32-bit segment)
     *   Bit 5: 0    = L=0 (not 64-bit)
     *   Bit 4: 0    = AVL=0 (unused)
     *   Bits 3-0: 0xF = upper 4 bits of limit (combined with lower limit = 0xFFFFF)
     *   So full limit = 0xFFFFF pages × 4096 = 4 GB
     *   0xCF = 1100 1111
     * --------------------------------------------------------------- */
    gdt_set_entry(1,
                  0x00000000,  /* base:  start at 0 */
                  0xFFFFFFFF,  /* limit: 4 GB (with page granularity) */
                  0x9A,        /* access: ring 0, code, readable */
                  0xCF);       /* 32-bit, 4 KB granularity */

    /* ---------------------------------------------------------------
     * Entry 2: Kernel Data Segment
     *
     * Same as code but:
     *   Bit 3: 0    = Not executable (DATA segment)
     *   Bit 1: 1    = Writable
     *   0x92 = 1001 0010
     * --------------------------------------------------------------- */
    gdt_set_entry(2,
                  0x00000000,
                  0xFFFFFFFF,
                  0x92,        /* access: ring 0, data, writable */
                  0xCF);

    /*
     * Load the GDT and reload segment registers.
     * We call the assembly function gdt_flush() with a pointer to gdt_desc.
     *
     * gdt_flush() uses the LGDT instruction to load the GDT, then does
     * a far jump to reload CS (code segment), and reloads DS, ES, etc.
     */
    gdt_flush((uint32_t)&gdt_desc);
}
