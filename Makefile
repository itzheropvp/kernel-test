# =============================================================================
# Makefile - Build system for KernelOS
#
# HOW MAKE WORKS:
#
#   make reads this file and builds "targets". Each target has:
#     1. Dependencies (files it needs before it can run)
#     2. Recipe (shell commands, MUST be indented with a real TAB character)
#
#   Make only rebuilds a file if its source is NEWER than the output.
#   This makes incremental builds fast.
#
# TARGETS:
#   make all    — build the bootable ISO (default)
#   make kernel — build just kernel.elf
#   make iso    — create the bootable kernel.iso
#   make clean  — delete all build output
# =============================================================================

# --- Toolchain ----------------------------------------------------------------
#
# CC  = C compiler (i686-linux-gnu-gcc cross-compiles C for 32-bit x86)
# AS  = assembler  (NASM assembles our .asm files)
# LD  = linker     (i686-linux-gnu-ld links object files into kernel.elf)
#
# These are available inside the Docker container after apt-get install.

CC := i686-linux-gnu-gcc
AS := nasm
LD := i686-linux-gnu-ld

# --- Include path for compiler built-in headers ------------------------------
#
# -nostdinc removes ALL include search paths, which is what we want for a
# kernel (we don't want <stdio.h> or <stdlib.h> sneaking in).
#
# BUT we still need <stdarg.h> for va_list (variadic functions like printf).
# That header is part of the COMPILER, not libc — it's stored alongside GCC.
#
# This command asks GCC where its built-in headers live:
#   i686-linux-gnu-gcc -print-file-name=include
# Returns something like: /usr/lib/gcc-cross/i686-linux-gnu/11/include
#
# We pass it with -isystem so the compiler finds stdarg.h even with -nostdinc.
# (-isystem is like -I but suppresses warnings from headers in that path)

GCC_INCLUDES := $(shell $(CC) -print-file-name=include 2>/dev/null)

# --- Compiler flags ----------------------------------------------------------
#
# -std=gnu99          C99 standard + GNU extensions (lets us use // comments, etc.)
# -ffreestanding      Tell GCC: no OS, no libc, no startup code
#                     Also: don't assume any standard functions exist
# -O2                 Optimize (level 2). Safe for kernel code.
# -Wall -Wextra       Enable most warnings — helps catch bugs early
# -Werror             Treat warnings as errors — enforce clean code
# -fno-stack-protector No stack canaries (requires libc __stack_chk_fail)
# -fno-pie            Don't generate position-independent executable
#                     (our kernel loads at a fixed address: 0x100000)
# -fno-pic            Don't generate position-independent code
# -fno-builtin        Don't replace memset/memcpy/etc with compiler builtins
#                     (we define our own versions in string.c)
# -nostdlib           Don't link any standard libraries (libc, libgcc, etc.)
# -nostdinc           Don't search any standard include directories
# -isystem $(GCC_INCLUDES)  BUT add the compiler's own headers (for stdarg.h)
# -I src              Find our own header files in the src/ directory

CFLAGS := -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
           -fno-stack-protector -fno-pie -fno-pic -fno-builtin \
           -nostdlib -nostdinc \
           -isystem $(GCC_INCLUDES) \
           -I src

# --- Assembler flags ---------------------------------------------------------
#
# -f elf32   Output format: 32-bit ELF object file
#            (ELF = Executable and Linkable Format — standard on Linux/x86)

ASFLAGS := -f elf32

# --- Linker flags ------------------------------------------------------------
#
# -T linker.ld   Use our custom linker script (controls memory layout)
# -nostdlib      Don't link standard startup files (crt0.o, libc.a, etc.)
# -no-pie        Don't create a position-independent executable

LDFLAGS := -T linker.ld -nostdlib -no-pie

# --- Source file discovery ---------------------------------------------------
#
# $(shell find ...) runs a shell command during Makefile parsing.
# We automatically find all .c and .asm files so we don't have to
# list them manually when adding new source files.

C_SOURCES   := $(shell find src -name '*.c')
ASM_SOURCES := $(shell find src -name '*.asm')

# Convert source paths to build output paths:
#   src/kernel/kernel.c   → build/kernel/kernel.c.o
#   src/boot/boot.asm     → build/boot/boot.asm.o
#
# $(patsubst pattern,replacement,list) does pattern substitution.

C_OBJECTS   := $(patsubst src/%.c,   build/%.c.o,   $(C_SOURCES))
ASM_OBJECTS := $(patsubst src/%.asm, build/%.asm.o, $(ASM_SOURCES))
ALL_OBJECTS := $(C_OBJECTS) $(ASM_OBJECTS)

# --- Phony targets -----------------------------------------------------------
# .PHONY means these names are commands, not actual files.
# Without this, make would be confused if a file named 'clean' existed.

.PHONY: all kernel iso clean run info

# Default target (what runs when you just type 'make')
all: iso

# --- Kernel ELF binary -------------------------------------------------------
#
# This links all the object files into a single ELF binary.
# The linker script (linker.ld) controls:
#   - Which address to load at (0x100000 = 1 MB)
#   - The order and alignment of sections (.text, .data, .bss, etc.)

kernel: build/kernel.elf

build/kernel.elf: $(ALL_OBJECTS)
	@echo ""
	@echo "[LD]   Linking kernel ELF..."
	@mkdir -p build
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]   Kernel:  $@"
	@file $@

# --- C source file compilation -----------------------------------------------
#
# Pattern rule: any build/%.c.o target is built from the matching src/%.c
#
# $<    = first prerequisite (the .c source file)
# $@    = the target (the .o output file)
# $(@D) = directory part of $@ (used with mkdir -p to create the directory)
#
# -c = compile only (don't link); produces a .o object file

build/%.c.o: src/%.c
	@mkdir -p $(@D)
	@echo "[CC]   $<"
	$(CC) $(CFLAGS) -c $< -o $@

# --- Assembly file compilation -----------------------------------------------

build/%.asm.o: src/%.asm
	@mkdir -p $(@D)
	@echo "[AS]   $<"
	$(AS) $(ASFLAGS) $< -o $@

# --- Bootable ISO creation ---------------------------------------------------
#
# grub-mkrescue takes a directory and creates a bootable ISO from it.
# Our 'iso/' directory structure:
#   iso/
#   └── boot/
#       ├── grub/
#       │   └── grub.cfg   ← GRUB menu configuration
#       └── kernel.elf     ← our kernel binary (copied here by this rule)
#
# The resulting ISO can be booted in QEMU with -cdrom kernel.iso,
# or written to a USB drive with 'dd' to boot on real hardware!

iso: build/kernel.iso

build/kernel.iso: build/kernel.elf
	@echo "[ISO]  Creating bootable ISO..."
	@mkdir -p iso/boot
	@cp build/kernel.elf iso/boot/kernel.elf
	grub-mkrescue -o build/kernel.iso iso 2>/dev/null || \
	grub-mkrescue -o build/kernel.iso iso
	@echo "[OK]   ISO:     build/kernel.iso"

# --- Run in QEMU (use this inside Docker shell or Linux) ---------------------

run: iso
	@echo "[QEMU] Booting KernelOS..."
	qemu-system-i386 \
		-cdrom build/kernel.iso \
		-m 32M \
		-no-reboot \
		-no-shutdown \
		-nographic \
		-serial stdio

# --- Show build info ---------------------------------------------------------

info:
	@echo "Compiler: $(CC)"
	@echo "GCC includes: $(GCC_INCLUDES)"
	@echo "C sources:   $(C_SOURCES)"
	@echo "ASM sources: $(ASM_SOURCES)"

# --- Clean build artifacts ---------------------------------------------------

clean:
	@echo "[CLEAN] Removing build/ directory..."
	@rm -rf build
	@rm -f iso/boot/kernel.elf
	@echo "[DONE]"

# Remove the disk image too (WARNING: this wipes persisted data!)
clean-disk:
	@echo "[CLEAN] Removing disk image (all persisted data will be lost)..."
	@rm -f build/disk.img
	@echo "[DONE]"
