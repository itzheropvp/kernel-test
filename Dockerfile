# =============================================================================
# Dockerfile - Build environment for KernelOS
#
# WHAT IS DIFFERENT FROM A NORMAL DOCKERFILE?
#
# We use --platform=linux/amd64 to force an x86-64 Linux environment,
# even on Apple Silicon Macs (M1/M2/M3). This is necessary because:
#
#   1. Our kernel targets 32-bit x86 (i686)
#   2. The cross-compiler tools (gcc-i686-linux-gnu) are only packaged
#      for x86 Debian/Ubuntu, not for ARM64
#   3. Docker on Apple Silicon can run x86_64 images via Rosetta 2 emulation
#
# WHY gcc-i686-linux-gnu INSTEAD OF BUILDING GCC FROM SOURCE?
#
# Ubuntu provides a pre-built i686 cross-compiler in its package manager.
# It's called "i686-linux-gnu-gcc" and it:
#   - Runs on: x86_64 Linux (inside Docker)
#   - Produces: 32-bit ELF code for i686 hardware
#   - Installs in: seconds (vs 10-15 minutes to compile from source)
#
# The "-linux-gnu" suffix means it targets Linux by default, but with
# the right flags (-ffreestanding -nostdlib -nostdinc) it produces
# perfectly valid bare-metal code — exactly what a kernel needs.
#
# HOW TO USE:
#   # Build the Docker image (first time: ~2-3 minutes):
#   docker build -t kernelos .
#
#   # Build the kernel ISO:
#   docker run --rm -v $(pwd):/kernel kernelos
#
#   # Open a shell to debug inside the container:
#   docker run --rm -it -v $(pwd):/kernel kernelos bash
# =============================================================================

# Force x86_64 Linux — works on both Intel and Apple Silicon Macs
FROM --platform=linux/amd64 ubuntu:22.04

# Prevent apt-get from asking interactive questions (e.g., timezone)
ENV DEBIAN_FRONTEND=noninteractive

# Install all build tools in a single layer (Docker best practice: fewer layers)
RUN apt-get update && apt-get install -y \
    # Build automation tool (reads Makefile and runs build commands)
    make \
    # Assembler: converts our .asm files to object files
    nasm \
    # Cross C compiler: compiles C for i686 (runs on x86_64)
    gcc-i686-linux-gnu \
    # Cross linker, objdump, etc. for i686
    binutils-i686-linux-gnu \
    # ISO creation: used by grub-mkrescue to create bootable ISO images
    xorriso \
    # GRUB bootloader binaries (x86 BIOS version)
    grub-pc-bin \
    # FAT filesystem tools: required by grub-mkrescue internally
    mtools \
    # For debugging: lets you inspect the ELF kernel binary
    file \
    && rm -rf /var/lib/apt/lists/*

# Verify the cross-compiler is working before we use it
RUN i686-linux-gnu-gcc --version && nasm --version

# The kernel source will be mounted here from your Mac:
#   docker run -v /path/to/kerneltest:/kernel ...
WORKDIR /kernel

# Default command: build the kernel and create the ISO
CMD ["make", "all"]
