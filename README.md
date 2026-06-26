# KernelOS

A simple 32-bit x86 kernel built from scratch as a learning project — written entirely with the help of AI (Claude) to understand how operating systems work at the lowest level.

---

## About This Project

This kernel was built as an educational experiment: starting from zero knowledge of OS development, using AI as a teacher and pair-programmer to build every component from scratch and understand the *why* behind each one.

Every line of code was discussed, explained, and understood before being written. The goal was not to copy a working kernel but to learn what a kernel actually does — how it boots, how it talks to hardware, how it manages memory and files, how it reads your keyboard.

**What was built with AI:**
- The x86 boot process (Multiboot, GDT, IDT, ISR/IRQ)
- VGA text mode driver with CP437 character rendering
- PS/2 keyboard driver with extended scan codes and multi-layout support
- PC speaker driver with frequency-to-note lookup
- ATA PIO disk driver (LBA28 mode)
- In-memory hierarchical filesystem
- Disk persistence (filesystem serialization to/from raw disk sectors)
- Interactive shell with readline (history, tab-complete, cursor movement)
- HeroScript — a custom scripting language with `.hero` files
- VIM-like modal text editor
- Italian keyboard layout with AltGr support

---

## Prerequisites

You need two tools installed on your machine:

**Docker Desktop**
The kernel compiles inside a Linux container (because we need a cross-compiler targeting 32-bit x86). Docker provides that environment without touching your host system.
- Download: https://www.docker.com/products/docker-desktop/
- Make sure the Docker daemon is running (whale icon in your menu bar) before building.

**QEMU**
QEMU is the emulator that runs the kernel ISO on your machine.
- macOS: `brew install qemu`
- Linux: `sudo apt install qemu-system-x86`

---

## Quick Start

```bash
# Clone the repository
git clone <repo-url>
cd kerneltest

# Build and run (Docker handles the cross-compilation)
./run.sh
```

That's it. QEMU opens a window and you're dropped into the shell.

---

## Building

```bash
./run.sh           # Build inside Docker, then launch QEMU
./run.sh build     # Build only — produces build/kernel.iso
./run.sh qemu      # Launch an already-built ISO in QEMU (skip build)
./run.sh shell     # Open an interactive bash shell inside the Docker container
./run.sh rebuild   # Force a full Docker image rebuild (use if Dockerfile changed)
./run.sh clean     # Delete all build artifacts (build/ directory)
```

---

## Running in VirtualBox

If you want to run the kernel on a different PC using VirtualBox:

1. Build the ISO: `./run.sh build`
2. Copy `build/kernel.iso` to the target machine
3. In VirtualBox, create a new VM:
   - Type: **Other**, Version: **Other/Unknown (32-bit)**
   - RAM: 64 MB or more
   - No hard disk needed (the kernel boots from the ISO)
4. In VM Settings → Storage: attach `kernel.iso` as an IDE optical drive
5. In VM Settings → Audio: enable audio with **ICH AC97** (for better compatibility)
6. Start the VM — it boots directly into the shell

> **Note on audio in VirtualBox:** The PC speaker (port 0x61) is not forwarded to the host audio system in VirtualBox. The `beep` and `mario` commands will show a visual pitch indicator on screen even without audio output. Sound works fully in QEMU with the flags in `run.sh`.

---

## Disk Persistence

The kernel supports saving and loading the filesystem to a raw disk image (`build/disk.img`). The disk image is created automatically the first time you run `./run.sh`.

Any files you create survive a reboot only if you save them:
```
save         ← write filesystem to disk
load         ← restore filesystem from disk
format       ← erase the disk and write a fresh filesystem (asks for confirmation)
```

The filesystem is also auto-saved when you `halt` the system, if a disk is available.

---

## Shell

The shell starts in the root directory `/` and supports:

| Feature | How to use |
|---------|-----------|
| Command history | Up / Down arrow keys |
| Cursor movement | Left / Right / Home / End |
| Mid-line editing | Type anywhere in the line; Delete removes char under cursor |
| Tab autocomplete | Tab completes command names (first word) or file/directory names (path after a space) |
| Clear line | Escape |
| Comments | Lines starting with `#` are ignored |

### All Commands

#### Filesystem

| Command | Description |
|---------|-------------|
| `ls [path]` | List the current directory, or a specific path |
| `ls /bin` | List a specific directory |
| `cd <path>` | Change directory (supports `.` `..` and absolute paths starting with `/`) |
| `cd` | Go to root directory |
| `pwd` | Print the current working directory path |
| `mkdir <name>` | Create a new directory |
| `touch <name>` | Create a new empty file |
| `cat <file>` | Print the contents of a file to the screen |
| `write <file> <text>` | Write (overwrite) text into a file |
| `rm <name>` | Remove a file or an empty directory |
| `tree` | Show the full filesystem tree from root |

#### Text Editor (VIM)

| Command | Description |
|---------|-------------|
| `vim <file>` | Open a file in the built-in VIM-like editor. Creates the file if it doesn't exist. |

#### Scripting

| Command | Description |
|---------|-------------|
| `run <file.hero>` | Execute a HeroScript file |
| `<file>.hero` | Typing a `.hero` filename directly also runs it |

#### Audio

| Command | Description |
|---------|-------------|
| `beep` | Play a 440 Hz tone (A4) for 300 ms |
| `beep <hz>` | Play a tone at a specific frequency |
| `beep <hz> <ms>` | Play a tone at a specific frequency for a specific duration |
| `mario` | Play the Super Mario Bros. theme using the PC speaker |

#### Disk

| Command | Description |
|---------|-------------|
| `save` | Save the entire in-memory filesystem to the disk image |
| `load` | Load the filesystem from the disk image into memory |
| `format` | Wipe the disk and write a fresh empty filesystem (asks for confirmation) |

#### System

| Command | Description |
|---------|-------------|
| `uname` | Show kernel version and architecture info |
| `free` | Show memory allocator usage and filesystem node count |
| `clear` | Clear the screen |
| `echo <text>` | Print text to the screen |
| `keymap [us\|it]` | Show or change the keyboard layout (see below) |
| `help` | List all available commands with short descriptions |
| `halt` | Auto-save filesystem and shut down the system |

---

## VIM Editor

Open any file with `vim <filename>`. The editor has three modes, exactly like real VIM.

### Modes

| Mode | How to enter | What it does |
|------|-------------|--------------|
| **NORMAL** | Default on open; press `Esc` from any mode | Navigate and issue commands |
| **INSERT** | Press `i`, `a`, `A`, `o`, or `O` in NORMAL | Type text |
| **COMMAND** | Press `:` in NORMAL | Type `:w`, `:q`, etc. then Enter |

### NORMAL Mode Keys

| Key | Action |
|-----|--------|
| `h` / `←` | Move left |
| `l` / `→` | Move right |
| `j` / `↓` | Move down |
| `k` / `↑` | Move up |
| `0` | Jump to start of line |
| `$` | Jump to end of line |
| `gg` | Jump to first line |
| `G` | Jump to last line |
| `w` | Jump forward one word |
| `b` | Jump backward one word |
| `x` | Delete character under cursor |
| `dd` | Delete (and yank) the current line |
| `yy` | Yank (copy) the current line |
| `p` | Paste yanked line after current line |
| `P` | Paste yanked line before current line |
| `i` | Enter INSERT mode before cursor |
| `a` | Enter INSERT mode after cursor |
| `A` | Enter INSERT mode at end of line |
| `o` | Open new line below and enter INSERT mode |
| `O` | Open new line above and enter INSERT mode |
| `:` | Enter COMMAND mode |

### COMMAND Mode

| Command | Action |
|---------|--------|
| `:w` | Save the file |
| `:q` | Quit (shows error if there are unsaved changes) |
| `:q!` | Quit without saving (force) |
| `:wq` or `:x` | Save and quit |

### INSERT Mode

Type normally. Backspace deletes the character before the cursor. Arrow keys move the cursor. `Esc` returns to NORMAL mode.

---

## HeroScript (.hero)

HeroScript is a simple scripting language built into the kernel. Script files use the `.hero` extension.

A sample script at `/bin/hello.hero` is included in the default filesystem.

### Syntax

```hero
# This is a comment

# Variables
set NAME World
echo Hello, $NAME!

# Conditionals
if exists /bin/hello.hero
    echo The file exists!
end

if eq $NAME World
    echo Name is World
end

if neq $NAME Linux
    echo Name is not Linux
end

# Loops
set COUNT 3
repeat $COUNT
    echo Looping...
end

# Any shell command works inside a script
beep 440 200
mkdir /home/test
touch /home/test/file.txt

# Exit early
exit
```

### Keywords

| Keyword | Syntax | Description |
|---------|--------|-------------|
| `set` | `set NAME value` | Create or update a variable |
| `echo` | `echo text $VAR` | Print text (variables expanded with `$`) |
| `if` | `if exists <path>` | Conditional: true if file/dir exists |
| `if` | `if eq $VAR value` | Conditional: true if variable equals value |
| `if` | `if neq $VAR value` | Conditional: true if variable does not equal value |
| `end` | `end` | End of `if` or `repeat` block |
| `repeat` | `repeat N` | Repeat the block N times (N can be a `$variable`) |
| `exit` | `exit` | Stop script execution immediately |

Any line that is not a HeroScript keyword is passed directly to the shell, so all shell commands work inside scripts.

Scripts can call other scripts (up to 4 levels deep).

---

## Keyboard Layouts

By default the kernel uses US QWERTY. To switch layouts:

```
keymap it    # Switch to Italian QWERTY
keymap us    # Switch back to US QWERTY
keymap       # Show available layouts and AltGr reference
```

### Italian Layout Notes

Letters are in the same QWERTY positions. The differences are in punctuation and the number row:

| Key | US | Italian |
|-----|-----|---------|
| Shift+2 | `@` | `"` |
| Shift+7 | `&` | `/` |
| Shift+8 | `*` | `(` |
| Shift+9 | `(` | `)` |
| Shift+0 | `)` | `=` |
| Key next to 0 | `-` | `'` (apostrophe) |
| Key where US has `/` | `/` | `-` |
| Key to the right of P | `[` | `è` |
| Key next to `è` | `]` | `+` |
| Shift+`.` | `>` | `:` (for VIM!) |
| Key to the right of L | `;` | `ò` |
| Key next to `ò` | `'` | `à` |
| Key where US has `\` | `\` | `ù` |
| Key between Left Shift and Z | — | `<` / `>` |

**AltGr combinations** (hold Right Alt):

| AltGr + | Character |
|---------|-----------|
| `2` | `@` |
| `3` | `#` |
| `7` | `{` |
| `8` | `[` |
| `9` | `]` |
| `0` | `}` |
| `ì` key | `` ` `` (backtick) |

---

## Project Structure

```
kerneltest/
├── src/
│   ├── boot/           # Multiboot entry point (assembly)
│   ├── kernel/
│   │   ├── kernel.c    # kmain() — kernel entry point
│   │   ├── gdt.c/.h    # Global Descriptor Table (memory segments)
│   │   ├── idt.c/.h    # Interrupt Descriptor Table
│   │   ├── isr.c/.h    # Interrupt Service Routines (exceptions)
│   │   ├── irq.c/.h    # Hardware Interrupt Requests (IRQ0-15)
│   │   ├── keyboard.c/.h  # PS/2 keyboard driver + layout tables
│   │   └── timer.c/.h  # PIT timer driver (used for beep timing)
│   ├── drivers/
│   │   ├── vga.c/.h    # VGA text mode driver + Latin-1→CP437 translation
│   │   ├── speaker.c/.h   # PC speaker driver
│   │   ├── ata.c/.h    # ATA PIO disk driver (LBA28)
│   │   └── editor.c/.h    # VIM-like modal text editor
│   ├── fs/
│   │   ├── fs.c/.h     # In-memory hierarchical filesystem
│   │   └── disk_fs.c/.h   # Filesystem serialization to/from disk
│   ├── mm/
│   │   └── memory.c/.h    # Simple bump allocator (kmalloc)
│   ├── libc/
│   │   └── string.c/.h    # Kernel string functions (no stdlib)
│   ├── shell/
│   │   └── shell.c/.h     # Interactive shell + all command implementations
│   └── script/
│       └── script.c/.h    # HeroScript interpreter
├── iso/
│   └── boot/grub/      # GRUB bootloader configuration
├── Dockerfile          # Build environment (cross-compiler + GRUB tools)
├── Makefile            # Build rules
├── linker.ld           # Linker script for the kernel ELF
└── run.sh              # Build + run script
```

---

## Technical Details

| Component | Implementation |
|-----------|---------------|
| Architecture | x86 32-bit protected mode |
| Boot protocol | Multiboot (loaded by GRUB 2) |
| Display | VGA text mode, 80×25 characters, Code Page 437 |
| Keyboard | PS/2, Scan Code Set 1, IRQ1, ring buffer |
| Disk | ATA PIO, LBA28, primary IDE bus (0x1F0) |
| Memory | Bump allocator (no free), 1 MB heap |
| Filesystem | In-memory tree, 64 nodes max, 4 KB per file |
| Disk format | Superblock (magic 0x4B4F5346) + 64 node slots, 9 sectors each |
| Audio | PC speaker via PIT channel 2 + port 0x61 |
| Toolchain | GCC i686-linux-gnu cross-compiler, NASM, GNU ld, GRUB mkrescue |

---

## Learning Resources

If you want to understand how this kernel works, these are the concepts to look up:

- **Multiboot specification** — how GRUB loads a kernel
- **x86 protected mode** — memory segmentation, GDT
- **Interrupt handling** — IDT, ISR, IRQ, PIC (8259A)
- **VGA text mode** — the 0xB8000 memory-mapped buffer
- **PS/2 keyboard protocol** — scan code set 1, make/break codes
- **ATA PIO** — talking to hard drives directly through I/O ports
- **PC speaker** — PIT channel 2, port 0x61
- **Ring buffers** — how the keyboard and other I/O drivers buffer data

---

*Built entirely with AI assistance (Claude by Anthropic) as a learning exercise in OS development.*
