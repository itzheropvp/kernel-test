/*
 * kernel.h - Core type definitions and low-level I/O utilities
 *
 * WHY THIS FILE EXISTS:
 *   A kernel runs in a "freestanding" environment — no OS, no libc, no
 *   standard headers (except the compiler-provided ones like stdarg.h).
 *   We must define our own integer types and basic utilities.
 *
 * FREESTANDING HEADERS (safe to include even in a kernel):
 *   <stdarg.h>  — va_list, va_start, va_arg, va_end  (variadic functions)
 *   <stddef.h>  — size_t, NULL, offsetof
 *   <stdint.h>  — uint8_t, uint16_t, etc.  (we define our own below)
 *   <stdbool.h> — bool, true, false
 *   <limits.h>  — INT_MAX, etc.
 */

#ifndef KERNEL_H
#define KERNEL_H

/* ============================================================
 * FIXED-WIDTH INTEGER TYPES
 *
 * The C standard integer types (int, long, etc.) have
 * implementation-defined sizes. For hardware programming we
 * always need EXACT widths, so we define them ourselves.
 *
 * The sizes below assume i686 (32-bit x86):
 *   char  = 8 bits
 *   short = 16 bits
 *   int   = 32 bits
 *   long long = 64 bits
 * ============================================================ */
typedef unsigned char       uint8_t;   /* 8-bit unsigned:  0 to 255            */
typedef unsigned short      uint16_t;  /* 16-bit unsigned: 0 to 65535          */
typedef unsigned int        uint32_t;  /* 32-bit unsigned: 0 to 4294967295     */
typedef unsigned long long  uint64_t;  /* 64-bit unsigned                      */

typedef signed char         int8_t;    /* 8-bit signed:  -128 to 127           */
typedef signed short        int16_t;   /* 16-bit signed: -32768 to 32767       */
typedef signed int          int32_t;   /* 32-bit signed: -2147483648 to ...    */
typedef signed long long    int64_t;   /* 64-bit signed                        */

typedef uint32_t            size_t;    /* Unsigned type for sizes/counts        */
typedef int32_t             ssize_t;   /* Signed version of size_t              */
typedef uint32_t            uintptr_t; /* Integer type large enough for pointer */

/* ============================================================
 * COMMON MACROS
 * ============================================================ */
#ifndef NULL
#define NULL ((void*)0)   /* Null pointer: address 0 */
#endif

#ifndef true
#define true  1
#define false 0
typedef int bool;
#endif

/* ============================================================
 * PORT I/O FUNCTIONS
 *
 * In x86, hardware devices are accessed via I/O ports (separate
 * address space from memory). Special instructions:
 *
 *   OUT port, value  — write value to hardware port
 *   IN  value, port  — read value from hardware port
 *
 * Examples of I/O ports we use:
 *   0x60  — PS/2 keyboard data
 *   0x64  — PS/2 keyboard status/command
 *   0x20  — Master PIC command
 *   0x21  — Master PIC data
 *   0xA0  — Slave PIC command
 *   0xA1  — Slave PIC data
 *   0x40  — PIT channel 0
 *   0x43  — PIT mode/command
 *
 * We use GCC inline assembly with "volatile" to prevent the
 * compiler from optimizing away these side-effectful operations.
 *
 * "=a"(ret)  — output: result goes into EAX, then into 'ret'
 * "d"(port)  — input: port number goes into EDX
 * "a"(val)   — input: value goes into EAX (AL for 8-bit)
 * ============================================================ */

/* Read 1 byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

/* Read 2 bytes (word) from an I/O port */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

/* Write 1 byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "dN"(port));
}

/* Write 2 bytes to an I/O port */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "dN"(port));
}

/*
 * io_wait() - Small delay for I/O operations
 *
 * Some old hardware needs a tiny delay between I/O operations.
 * Writing to port 0x80 (a "dead" diagnostic port) burns ~1 microsecond.
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

/* Enable hardware interrupts (STI = Set Interrupt Flag) */
static inline void sti(void) {
    __asm__ volatile ("sti");
}

/* Disable hardware interrupts (CLI = Clear Interrupt Flag) */
static inline void cli(void) {
    __asm__ volatile ("cli");
}

/* Halt the CPU until the next interrupt */
static inline void hlt(void) {
    __asm__ volatile ("hlt");
}

/* ============================================================
 * STRUCTURE PACKING
 *
 * __attribute__((packed)) tells GCC NOT to add padding bytes
 * between struct fields. This is critical when a struct must
 * exactly match a hardware-defined memory layout (like GDT
 * entries or IDT entries).
 * ============================================================ */
#define PACKED __attribute__((packed))

#endif /* KERNEL_H */
