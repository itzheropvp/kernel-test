; =============================================================================
; gdt_flush.asm - Load the GDT and reload segment registers
;
; WHY DO WE NEED ASSEMBLY HERE?
;
; After loading a new GDT with the LGDT instruction, the CPU's segment
; registers (CS, DS, SS, etc.) still contain the OLD selector values.
; We must reload them to point to our new GDT entries.
;
; The tricky part is reloading CS (Code Segment). You can't just do:
;   mov cs, 0x08   ; INVALID — CS is read-only via normal instructions!
;
; Instead, you use a FAR JMP, which atomically:
;   1. Sets CS to the new selector
;   2. Jumps to the new code location
;
; This is why we need assembly — there's no C equivalent.
; =============================================================================

[BITS 32]
global gdt_flush

; gdt_flush(uint32_t gdt_descriptor_ptr)
; The argument is passed on the stack (cdecl calling convention).
gdt_flush:
    ; Get argument: pointer to gdt_descriptor_t
    mov eax, [esp + 4]    ; eax = &gdt_desc

    ; LGDT — Load Global Descriptor Table Register
    ; Loads our 6-byte descriptor (base + limit) into the CPU's GDTR register
    lgdt [eax]

    ; Reload data segment registers
    ; We use selector 0x10 = GDT entry 2 (kernel data segment)
    mov ax, 0x10
    mov ds, ax            ; Data Segment
    mov es, ax            ; Extra Segment
    mov fs, ax            ; FS (general purpose, often used for thread-local storage later)
    mov gs, ax            ; GS (general purpose)
    mov ss, ax            ; Stack Segment

    ; Reload CS (Code Segment) via a far jump
    ; Selector 0x08 = GDT entry 1 (kernel code segment)
    ; After this jump, the CPU uses our new GDT entry 1 for all code fetches.
    jmp 0x08:.flush_cs

.flush_cs:
    ret                   ; Return to C code
