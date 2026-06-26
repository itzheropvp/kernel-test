/*
 * kernel.c - Kernel Entry Point
 *
 * This is where C execution begins. Our boot.asm calls kernel_main()
 * after setting up the stack and saving the Multiboot information.
 *
 * INITIALIZATION ORDER MATTERS:
 *
 * 1. VGA first — so we can print debug output immediately
 * 2. GDT   — must be set up before we can properly use segments
 * 3. IDT   — must be set up before we can enable interrupts
 * 4. IRQs  — initializes the PIC (interrupt controller)
 * 5. Timer — uses IRQ0; must install handler before enabling interrupts
 * 6. Keyboard — uses IRQ1
 * 7. Memory — initialize the bump allocator
 * 8. Filesystem — creates the in-memory filesystem tree
 * 9. STI (enable interrupts!) — now IRQ handlers are ready
 * 10. Shell — interactive loop, never returns
 *
 * WHY DOES ORDER MATTER?
 * If we enable interrupts (STI) before the IDT is set up, and a timer
 * interrupt fires, the CPU won't know where to dispatch it → triple fault.
 */

#include "kernel.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "timer.h"
#include "keyboard.h"
#include "../drivers/vga.h"
#include "../drivers/speaker.h"
#include "../mm/memory.h"
#include "../fs/fs.h"
#include "../fs/disk_fs.h"
#include "../shell/shell.h"

/*
 * Multiboot info structure (simplified)
 *
 * GRUB passes a pointer to a much larger struct with memory maps,
 * boot device info, command line args, etc.
 * We only use the basics for now.
 */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;  /* KB of memory below 1 MB */
    uint32_t mem_upper;  /* KB of memory above 1 MB */
    /* ... more fields we don't use ... */
} multiboot_info_t;

/* Multiboot magic number GRUB should have put in EAX */
#define MULTIBOOT_MAGIC 0x2BADB002

/*
 * kernel_main - Called by boot.asm after setting up the stack.
 *
 * @magic:      should equal MULTIBOOT_MAGIC (0x2BADB002)
 * @mboot_addr: physical address of the Multiboot info struct
 */
void kernel_main(uint32_t magic, uint32_t mboot_addr) {
    /* ---- Step 1: VGA driver ---------------------------------------- */
    vga_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] VGA initialized\n");

    /* ---- Verify Multiboot ------------------------------------------ */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    if (magic != MULTIBOOT_MAGIC) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("[BOOT] ERROR: Not loaded by a Multiboot-compliant bootloader!\n");
        /* Halt — something is very wrong */
        __asm__ volatile ("cli; hlt");
    }

    /* Print some boot info from GRUB */
    multiboot_info_t* mbi = (multiboot_info_t*)mboot_addr;
    vga_printf("[BOOT] Memory: %u KB low, %u KB high (~%u MB total)\n",
               mbi->mem_lower,
               mbi->mem_upper,
               (mbi->mem_upper + 1024) / 1024);

    /* ---- Step 2: GDT ----------------------------------------------- */
    gdt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] GDT loaded\n");

    /* ---- Step 3: IDT ----------------------------------------------- */
    idt_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] IDT loaded\n");

    /* ---- Step 4: IRQ / PIC ----------------------------------------- */
    irq_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] PIC initialized (IRQs remapped to 0x20-0x2F)\n");

    /* ---- Step 5: Timer --------------------------------------------- */
    timer_init(100);  /* 100 Hz = 10 ms per tick */
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] PIT timer initialized at 100 Hz\n");

    /* ---- Step 6: Keyboard ------------------------------------------ */
    keyboard_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] PS/2 keyboard driver ready\n");

    /* ---- Step 7: Memory allocator ---------------------------------- */
    memory_init();
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] Memory allocator initialized\n");

    /* ---- Step 8: Filesystem ---------------------------------------- */
    /*
     * Try to load the filesystem from the data disk first.
     * If the disk has our magic number → restore persisted data.
     * Otherwise → fall back to the default in-memory filesystem.
     *
     * This is exactly how a real OS mounts a partition on boot:
     *   1. Detect the disk
     *   2. Read the superblock / partition table
     *   3. Mount (load) the filesystem structures into memory
     */
    if (disk_fs_detect()) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("[BOOT] Data disk found — loading filesystem...\n");
        if (disk_fs_load()) {
            vga_print("[BOOT] Filesystem loaded from disk\n");
        } else {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            vga_print("[BOOT] Disk read failed — using default filesystem\n");
            fs_init();
        }
    } else {
        /* No formatted disk: initialize defaults in RAM */
        fs_init();
        if (disk_fs_available()) {
            vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
            vga_print("[BOOT] Disk not formatted — type 'format' to enable persistence\n");
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
            vga_print("[BOOT] No data disk — using in-memory filesystem\n");
        }
    }

    /* ---- Step 9: Enable interrupts --------------------------------- */
    /*
     * STI = Set Interrupt Flag
     * From this point on, hardware interrupts can fire.
     * The timer will start ticking and keyboard presses will be registered.
     */
    __asm__ volatile ("sti");
    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("[BOOT] Interrupts enabled\n");

    /* Boot chime: three rising notes (C-E-G = C major chord) */
    speaker_beep(523, 80);   /* C5 */
    speaker_beep(659, 80);   /* E5 */
    speaker_beep(784, 150);  /* G5 */

    /* Short delay to show boot messages */
    timer_sleep(50);

    /* ---- Step 10: Start the shell ---------------------------------- */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    shell_run();

    /* Should NEVER reach here — shell loops forever */
    __asm__ volatile ("cli; hlt");
    while (1) {}
}
