#!/usr/bin/env bash
# =============================================================================
# run.sh - Build and run KernelOS using Docker + QEMU
#
# USAGE:
#   ./run.sh          — Build inside Docker, then run QEMU on your Mac
#   ./run.sh build    — Build only (creates build/kernel.iso)
#   ./run.sh qemu     — Run existing ISO in QEMU (skip build)
#   ./run.sh shell    — Open bash inside the Docker build container
#   ./run.sh rebuild  — Force rebuild the Docker image, then build kernel
#   ./run.sh clean    — Delete all build artifacts
#
# PREREQUISITES:
#   1. Docker Desktop installed and RUNNING (check menu bar for whale icon)
#        https://www.docker.com/products/docker-desktop/
#
#   2. QEMU installed on your Mac:
#        brew install qemu
#
# HOW IT WORKS:
#   - Kernel compilation happens INSIDE Docker (Linux environment)
#     because we need a Linux cross-compiler and GRUB tools
#   - QEMU runs OUTSIDE Docker on your Mac (simpler, no GPU forwarding needed)
#   - Your source files are shared via a Docker volume mount (-v flag)
#     so changes on your Mac are immediately visible inside the container
# =============================================================================

set -e  # Exit script immediately if any command fails

# --- Configuration -----------------------------------------------------------
IMAGE_NAME="kernelos"
KERNEL_ISO="build/kernel.iso"
DISK_IMG="build/disk.img"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Disk image size: 2 MB = 4096 sectors × 512 bytes
# Holds superblock + 64 nodes × 9 sectors = 577 sectors needed (~295 KB)
DISK_SECTORS=4096

# --platform=linux/amd64 ensures we always use an x86_64 container.
# This is necessary on Apple Silicon Macs (M1/M2/M3) which run ARM64 natively.
# Docker uses Rosetta 2 to emulate x86_64 — transparent but slightly slower.
DOCKER_PLATFORM="--platform linux/amd64"

# --- Color output helpers ----------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

log()  { echo -e "${CYAN}[KernelOS]${NC} $1"; }
ok()   { echo -e "${GREEN}[  OK   ]${NC} $1"; }
warn() { echo -e "${YELLOW}[ WARN  ]${NC} $1"; }
err()  { echo -e "${RED}[ ERROR ]${NC} $1"; exit 1; }

# --- Pre-flight checks -------------------------------------------------------

check_docker() {
    if ! docker info &>/dev/null; then
        echo ""
        err "Docker is not running!

  Fix:
    1. Open 'Docker Desktop' from your Applications folder
    2. Wait for the whale icon in the menu bar to stop animating
    3. Then run this script again

  If Docker isn't installed:
    https://www.docker.com/products/docker-desktop/"
    fi
}

check_qemu() {
    if ! command -v qemu-system-i386 &>/dev/null; then
        echo ""
        warn "QEMU is not installed. Install it with:"
        echo "    brew install qemu"
        echo ""
        echo "  Then run: ./run.sh qemu"
        return 1
    fi
    return 0
}

# --- Build function ----------------------------------------------------------

build_kernel() {
    check_docker

    log "Checking Docker image '${IMAGE_NAME}'..."

    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        log "Building Docker image for the first time..."
        log "This installs the cross-compiler tools — takes ~2-3 minutes."
        echo ""
        docker build $DOCKER_PLATFORM -t "$IMAGE_NAME" "$SCRIPT_DIR"
        echo ""
        ok "Docker image '${IMAGE_NAME}' is ready!"
    else
        log "Using cached Docker image '${IMAGE_NAME}'"
        log "(run './run.sh rebuild' if you changed the Dockerfile)"
    fi

    echo ""
    log "Building KernelOS inside Docker..."
    echo ""

    # Run the build inside Docker.
    # -v mounts your source directory into /kernel inside the container.
    # --rm removes the container after it exits (keeps things tidy).
    docker run --rm $DOCKER_PLATFORM \
        -v "$SCRIPT_DIR:/kernel" \
        "$IMAGE_NAME" \
        make all

    echo ""
    if [ -f "$SCRIPT_DIR/$KERNEL_ISO" ]; then
        ok "Build complete! ISO: $SCRIPT_DIR/$KERNEL_ISO"
    else
        err "Build failed — $KERNEL_ISO was not created"
    fi

    # Create a blank disk image if it doesn't exist yet.
    # This is the "hard disk" that persists the filesystem between reboots.
    # We use dd to write zeros — a raw (unformatted) disk image.
    if [ ! -f "$SCRIPT_DIR/$DISK_IMG" ]; then
        log "Creating blank data disk image ($DISK_SECTORS sectors = $(( DISK_SECTORS / 2 )) KB)..."
        dd if=/dev/zero of="$SCRIPT_DIR/$DISK_IMG" bs=512 count=$DISK_SECTORS 2>/dev/null
        ok "Blank disk image created: $DISK_IMG"
        log "Boot the kernel and type 'format' to initialize the filesystem on it."
    fi
}

# --- QEMU function -----------------------------------------------------------

run_qemu() {
    if ! check_qemu; then
        return
    fi

    if [ ! -f "$SCRIPT_DIR/$KERNEL_ISO" ]; then
        err "No ISO found at $KERNEL_ISO. Run './run.sh build' first."
    fi

    log "Starting KernelOS in QEMU..."
    echo ""
    echo -e "  ${BOLD}QEMU Controls:${NC}"
    echo "    Type in the QEMU window to interact with the kernel shell"
    echo "    Press Ctrl+C in this terminal to force-quit QEMU"
    echo "    The kernel 'halt' command does a clean shutdown"
    echo ""

    # -cdrom                       boot from our ISO (CD-ROM drive)
    # -drive                       attach our raw disk image as the first hard disk (IDE)
    # -m 32M                       give the VM 32 MB RAM
    # -no-reboot                   exit QEMU instead of rebooting on triple fault
    # -audiodev coreaudio,id=snd   create a CoreAudio output device named "snd"
    # -machine pc,pcspk-audiodev=snd  route the PC speaker (port 0x61 + PIT ch2)
    #                              to the "snd" audio backend — WITHOUT this line
    #                              QEMU emulates the speaker hardware but discards
    #                              the sound. This is why beep was silent before.
    #
    # AUDIO FALLBACK CHAIN:
    #   1. coreaudio (macOS native — best quality, no extra deps)
    #   2. sdl       (cross-platform, needs SDL2)
    #   3. none      (no audio, but visual indicators still work)

    DISK_FLAGS=""
    if [ -f "$SCRIPT_DIR/$DISK_IMG" ]; then
        DISK_FLAGS="-drive file=$SCRIPT_DIR/$DISK_IMG,format=raw,index=0,media=disk"
    fi

    # Attempt 1: macOS CoreAudio + Cocoa window (best on Mac)
    qemu-system-i386 \
        $DISK_FLAGS \
        -cdrom "$SCRIPT_DIR/$KERNEL_ISO" \
        -m 32M \
        -no-reboot \
        -no-shutdown \
        -audiodev coreaudio,id=snd \
        -machine pc,pcspk-audiodev=snd \
        -display cocoa,zoom-to-fit=on \
        2>/dev/null \
    || \
    # Attempt 2: SDL audio + SDL window
    qemu-system-i386 \
        $DISK_FLAGS \
        -cdrom "$SCRIPT_DIR/$KERNEL_ISO" \
        -m 32M \
        -no-reboot \
        -no-shutdown \
        -audiodev sdl,id=snd \
        -machine pc,pcspk-audiodev=snd \
        -display sdl,zoom-to-fit=on \
        2>/dev/null \
    || \
    # Attempt 3: no audio (visual indicators still work)
    qemu-system-i386 \
        $DISK_FLAGS \
        -cdrom "$SCRIPT_DIR/$KERNEL_ISO" \
        -m 32M \
        -no-reboot \
        -no-shutdown \
        -nographic \
        -serial stdio
}

# --- Main entry point --------------------------------------------------------

case "${1:-all}" in
    build)
        build_kernel
        ;;
    qemu)
        run_qemu
        ;;
    shell)
        check_docker
        log "Opening bash shell in Docker build container..."
        log "(Your source files are at /kernel inside the container)"
        echo ""
        docker run --rm -it $DOCKER_PLATFORM \
            -v "$SCRIPT_DIR:/kernel" \
            "$IMAGE_NAME" \
            bash
        ;;
    clean)
        log "Cleaning build artifacts..."
        rm -rf "$SCRIPT_DIR/build"
        rm -f "$SCRIPT_DIR/iso/boot/kernel.elf"
        ok "Clean complete"
        ;;
    rebuild)
        check_docker
        log "Removing old Docker image and rebuilding from scratch..."
        docker rmi "$IMAGE_NAME" 2>/dev/null && ok "Old image removed" || true
        build_kernel
        run_qemu
        ;;
    all|*)
        build_kernel
        run_qemu
        ;;
esac
