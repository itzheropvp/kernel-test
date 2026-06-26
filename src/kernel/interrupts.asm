; =============================================================================
; interrupts.asm - ISR and IRQ stub routines
;
; WHY DO WE NEED THESE ASM STUBS?
;
; When an interrupt fires, the CPU automatically pushes the return state to
; the stack and jumps to the handler. But C function calling convention
; expects a specific stack layout. We need these stubs to:
;
;   1. Normalize the stack (push error code if CPU didn't, push int number)
;   2. Save ALL registers (pusha = push all general-purpose registers)
;   3. Set up kernel data segments (DS, ES, FS, GS may still have old values)
;   4. Call the C handler with a pointer to the saved register state
;   5. Restore everything and return with IRET (special return from interrupt)
;
; STACK STATE WHEN INTERRUPT FIRES (before our stub runs):
;
;   [For exceptions WITHOUT error code] — CPU pushes:
;     EFLAGS
;     CS
;     EIP         ← ESP points here
;
;   [For exceptions WITH error code] — CPU pushes:
;     EFLAGS
;     CS
;     EIP
;     Error code  ← ESP points here
;
;   [For IRQs] — CPU pushes (no error code):
;     EFLAGS
;     CS
;     EIP         ← ESP points here
;
; AFTER OUR STUB AND pusha, THE C HANDLER SEES THIS STRUCT (registers_t):
;
;   Lower address (top of stack):
;   ┌──────────┐
;   │    DS    │  ← ESP
;   │   EDI    │
;   │   ESI    │
;   │   EBP    │
;   │   ESP*   │  (* value before pusha)
;   │   EBX    │
;   │   EDX    │
;   │   ECX    │
;   │   EAX    │
;   │  int_no  │  ← interrupt vector number
;   │ err_code │  ← error code (0 if none)
;   │   EIP    │  ← CPU saved
;   │    CS    │  ← CPU saved
;   │  EFLAGS  │  ← CPU saved
;   └──────────┘
;   Higher address (bottom of stack)
; =============================================================================

[BITS 32]

extern isr_handler    ; Defined in isr.c — handles CPU exceptions
extern irq_handler    ; Defined in irq.c — dispatches hardware interrupts

; =============================================================================
; MACROS for stub generation
;
; Using macros avoids writing the same code 48 times!
; NASM's %macro directive defines a macro with N parameters.
; %1, %2 refer to the first and second arguments.
; =============================================================================

; For exceptions that do NOT push an error code:
; We push a dummy 0 so the stack layout is always the same.
%macro ISR_NOERR 1
global isr%1
isr%1:
    cli               ; Disable interrupts (safety — some might arrive during handling)
    push dword 0      ; Dummy error code
    push dword %1     ; Interrupt vector number
    jmp isr_common    ; Jump to common handler
%endmacro

; For exceptions that DO push an error code:
; The CPU already pushed the error code, so we just push the vector number.
%macro ISR_ERR 1
global isr%1
isr%1:
    cli
    push dword %1     ; Interrupt vector number (error code already on stack)
    jmp isr_common
%endmacro

; For hardware IRQs:
; %1 = IRQ number (0-15), %2 = IDT vector (32-47)
%macro IRQ 2
global irq%1
irq%1:
    cli
    push dword 0      ; Dummy error code (IRQs have no error code)
    push dword %2     ; IDT vector number (32 + IRQ number)
    jmp irq_common
%endmacro

; =============================================================================
; CPU EXCEPTION STUBS (vectors 0–31)
;
; Which exceptions have error codes?
;   YES: #DF(8), #TS(10), #NP(11), #SS(12), #GP(13), #PF(14), #AC(17)
;   NO:  All others
; =============================================================================
ISR_NOERR 0   ; #DE  Division by Zero
ISR_NOERR 1   ; #DB  Debug Exception
ISR_NOERR 2   ;      Non-Maskable Interrupt (NMI)
ISR_NOERR 3   ; #BP  Breakpoint (INT3)
ISR_NOERR 4   ; #OF  Overflow
ISR_NOERR 5   ; #BR  Bound Range Exceeded
ISR_NOERR 6   ; #UD  Invalid Opcode
ISR_NOERR 7   ; #NM  Device Not Available (no FPU)
ISR_ERR   8   ; #DF  Double Fault                   [ERROR CODE]
ISR_NOERR 9   ;      Coprocessor Segment Overrun (obsolete)
ISR_ERR   10  ; #TS  Invalid TSS                    [ERROR CODE]
ISR_ERR   11  ; #NP  Segment Not Present            [ERROR CODE]
ISR_ERR   12  ; #SS  Stack Segment Fault            [ERROR CODE]
ISR_ERR   13  ; #GP  General Protection Fault       [ERROR CODE]
ISR_ERR   14  ; #PF  Page Fault                     [ERROR CODE]
ISR_NOERR 15  ;      Reserved
ISR_NOERR 16  ; #MF  x87 FPU Floating-Point Error
ISR_ERR   17  ; #AC  Alignment Check                [ERROR CODE]
ISR_NOERR 18  ; #MC  Machine Check
ISR_NOERR 19  ; #XM  SIMD Floating-Point Exception
ISR_NOERR 20  ; #VE  Virtualization Exception
ISR_NOERR 21  ;      Reserved
ISR_NOERR 22  ;      Reserved
ISR_NOERR 23  ;      Reserved
ISR_NOERR 24  ;      Reserved
ISR_NOERR 25  ;      Reserved
ISR_NOERR 26  ;      Reserved
ISR_NOERR 27  ;      Reserved
ISR_NOERR 28  ;      Reserved
ISR_NOERR 29  ; #HV  Hypervisor Injection Exception
ISR_ERR   30  ; #SX  Security Exception             [ERROR CODE]
ISR_NOERR 31  ;      Reserved

; =============================================================================
; HARDWARE IRQ STUBS (vectors 32–47)
;
; IRQ 0  = PIT Timer (fires ~100 times/sec at our configuration)
; IRQ 1  = PS/2 Keyboard
; IRQ 2  = Cascade (slave PIC connected here)
; IRQ 3  = COM2 serial
; IRQ 4  = COM1 serial
; IRQ 5  = LPT2 parallel port
; IRQ 6  = Floppy disk controller
; IRQ 7  = LPT1 / spurious
; IRQ 8  = CMOS Real-Time Clock
; IRQ 9  = Legacy ACPI / available
; IRQ 10 = Available / network card
; IRQ 11 = Available / USB
; IRQ 12 = PS/2 Mouse
; IRQ 13 = FPU/Coprocessor
; IRQ 14 = Primary ATA (hard disk 0)
; IRQ 15 = Secondary ATA (hard disk 1) / spurious
; =============================================================================
IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

; =============================================================================
; COMMON ISR HANDLER
; All exception stubs jump here.
; =============================================================================
isr_common:
    pusha             ; Push EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX

    ; Save and reload the data segment
    ; We need to switch DS to our kernel data segment in case it was
    ; pointing to a user segment (for future user-mode support).
    mov ax, ds
    push eax          ; Save original DS on stack (becomes our 'ds' field)

    mov ax, 0x10      ; Kernel data segment selector (GDT entry 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Push a pointer to the register struct we built on the stack.
    ; ESP now points to the 'ds' field, which is the start of registers_t.
    push esp

    call isr_handler  ; Call C handler: void isr_handler(registers_t* regs)
    add esp, 4        ; Clean up the pushed 'esp' argument

    ; Restore the original data segment
    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa              ; Restore all general-purpose registers (reverse of pusha)
    add esp, 8        ; Remove int_no and err_code from stack
    iret              ; Restore EIP, CS, EFLAGS (and SS/ESP if privilege change)

; =============================================================================
; COMMON IRQ HANDLER
; All hardware IRQ stubs jump here.
; =============================================================================
irq_common:
    pusha

    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp

    call irq_handler  ; Call C handler: void irq_handler(registers_t* regs)
    add esp, 4

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8
    iret
