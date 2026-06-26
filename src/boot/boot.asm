; =============================================================================
; boot.asm - Kernel Entry Point (Multiboot 1 compliant)
;
; WHAT IS MULTIBOOT?
;
;   Multiboot is a specification that defines a standard interface between
;   a bootloader (like GRUB) and an operating system kernel.
;
;   When GRUB loads our kernel, it:
;     1. Reads our kernel ELF file from disk
;     2. Loads it into memory (at the address specified in the ELF header)
;     3. Looks for the "Multiboot header" (magic bytes in first 8 KB)
;     4. Sets up some CPU state:
;        - Protected Mode (32-bit) is ALREADY enabled
;        - A20 line is enabled (can access > 1 MB of memory)
;        - EAX = 0x2BADB002 (Multiboot magic number)
;        - EBX = pointer to Multiboot info structure (memory map, etc.)
;     5. Jumps to our _start label
;
; WHAT IS PROTECTED MODE?
;
;   x86 CPUs start in "Real Mode" (16-bit, limited to 1 MB RAM).
;   "Protected Mode" (32-bit) enables:
;     - Full 4 GB address space
;     - Memory protection via segmentation and paging
;     - Privilege levels (rings 0-3)
;   GRUB switches us to Protected Mode before calling us.
;
; =============================================================================

; Tell NASM we're writing 32-bit code
[BITS 32]

; =============================================================================
; MULTIBOOT HEADER
;
; This header MUST appear within the first 8 KB of the kernel image.
; GRUB scans for the magic number 0x1BADB002 to identify a Multiboot kernel.
; =============================================================================

; Alignment flag: tell GRUB to align loaded modules on 4 KB page boundaries
MBOOT_PAGE_ALIGN    equ (1 << 0)

; Memory flag: ask GRUB to provide a memory map in the Multiboot info struct
MBOOT_MEM_INFO      equ (1 << 1)

; Combined flags
MBOOT_FLAGS         equ (MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO)

; The magic number GRUB looks for (0x1BADB002)
MBOOT_MAGIC         equ 0x1BADB002

; Checksum: the sum of MAGIC + FLAGS + CHECKSUM must equal 0 (mod 2^32)
; This lets GRUB verify the header wasn't corrupted.
MBOOT_CHECKSUM      equ -(MBOOT_MAGIC + MBOOT_FLAGS)

; Place the header in a special section (.multiboot) so the linker script
; can guarantee it ends up at the very start of the output file.
section .multiboot
align 4
    dd MBOOT_MAGIC       ; 4 bytes: magic number
    dd MBOOT_FLAGS       ; 4 bytes: feature flags
    dd MBOOT_CHECKSUM    ; 4 bytes: checksum

; =============================================================================
; KERNEL STACK
;
; The CPU needs a stack from the very beginning (function calls use it).
; We reserve 16 KB in the BSS section (zero-initialized uninitialized data).
;
; The stack on x86 grows DOWNWARD (from high to low addresses), so:
;   - stack_bottom = lowest address (end of stack space)
;   - stack_top    = highest address (initial stack pointer)
;
; We set ESP = stack_top so the stack has room to grow down.
;
; WHY 16 KB?
;   For a basic kernel without deep recursion or large local arrays,
;   16 KB is plenty. Linux uses 4-8 KB per task.
; =============================================================================

section .bss
align 16                  ; Stack must be 16-byte aligned (ABI requirement)
stack_bottom:
    resb 16384            ; Reserve 16 KB (16 * 1024 bytes) of stack space
stack_top:                ; Label at the HIGH end (initial ESP value)

; =============================================================================
; ENTRY POINT
; =============================================================================

section .text
global _start             ; Export _start so the linker knows where to begin
extern kernel_main        ; kernel_main is defined in kernel.c

_start:
    ; -------------------------------------------------------------------------
    ; Set up the stack pointer
    ;
    ; ESP (Extended Stack Pointer) must point to valid memory before we can
    ; call any functions. We use our reserved BSS space.
    ; -------------------------------------------------------------------------
    mov esp, stack_top

    ; -------------------------------------------------------------------------
    ; Pass Multiboot information to kernel_main
    ;
    ; GRUB left us two important values:
    ;   EAX = 0x2BADB002 (Multiboot magic number — confirms GRUB loaded us)
    ;   EBX = physical address of Multiboot info structure
    ;
    ; In the x86 calling convention (cdecl), function arguments are pushed
    ; RIGHT-TO-LEFT onto the stack before the call.
    ;
    ; kernel_main(uint32_t magic, uint32_t mboot_addr) expects:
    ;   [ESP+4] = magic (first arg)
    ;   [ESP+8] = mboot_addr (second arg)
    ;   ... but since we PUSH right-to-left:
    ; -------------------------------------------------------------------------
    push ebx              ; Second argument: Multiboot info pointer
    push eax              ; First argument:  Multiboot magic number

    ; -------------------------------------------------------------------------
    ; Call the C kernel!
    ;
    ; This is where we transition from Assembly to C.
    ; The kernel should never return, but if it does:
    ; -------------------------------------------------------------------------
    call kernel_main

    ; -------------------------------------------------------------------------
    ; Halt loop (kernel returned — should never happen)
    ;
    ; CLI = Clear Interrupt Flag (disable hardware interrupts)
    ; HLT = Halt the CPU (waits for an interrupt — but we disabled them)
    ; The loop is a safety net in case HLT somehow returns.
    ; -------------------------------------------------------------------------
    cli
.halt_loop:
    hlt
    jmp .halt_loop
