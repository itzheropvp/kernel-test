/*
 * shell.c - Interactive Command Shell Implementation
 */

#include "shell.h"
#include "../drivers/vga.h"
#include "../drivers/speaker.h"
#include "../kernel/keyboard.h"
#include "../kernel/timer.h"
#include "../fs/fs.h"
#include "../fs/disk_fs.h"
#include "../mm/memory.h"
#include "../libc/string.h"

/* ============================================================
 * COMMAND TABLE
 *
 * Each command is a struct with:
 *   name:   the string the user types
 *   help:   short description shown by 'help'
 *   func:   function to call (argc, argv, cwd)
 *
 * This table-driven approach makes it easy to add new commands:
 * just add an entry here and implement the handler function below.
 * ============================================================ */

/* Current working directory — starts at fs_root */
static fs_node_t* cwd;

/* Forward declarations for command handlers */
static void cmd_help(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_pwd(int argc, char** argv);
static void cmd_ls(int argc, char** argv);
static void cmd_cd(int argc, char** argv);
static void cmd_mkdir(int argc, char** argv);
static void cmd_touch(int argc, char** argv);
static void cmd_cat(int argc, char** argv);
static void cmd_write(int argc, char** argv);
static void cmd_rm(int argc, char** argv);
static void cmd_uname(int argc, char** argv);
static void cmd_free(int argc, char** argv);
static void cmd_halt(int argc, char** argv);
static void cmd_tree(int argc, char** argv);
static void cmd_beep(int argc, char** argv);
static void cmd_mario(int argc, char** argv);
static void cmd_format(int argc, char** argv);
static void cmd_save(int argc, char** argv);
static void cmd_load(int argc, char** argv);

typedef struct {
    const char* name;
    const char* help;
    void (*func)(int argc, char** argv);
} command_t;

static const command_t commands[] = {
    { "help",  "Show available commands",                cmd_help  },
    { "clear", "Clear the screen",                       cmd_clear },
    { "echo",  "Print text: echo <text>",                cmd_echo  },
    { "pwd",   "Print working directory",                cmd_pwd   },
    { "ls",    "List directory: ls [path]",              cmd_ls    },
    { "cd",    "Change directory: cd <path>",            cmd_cd    },
    { "mkdir", "Create directory: mkdir <name>",         cmd_mkdir },
    { "touch", "Create file: touch <name>",              cmd_touch },
    { "cat",   "Show file contents: cat <file>",         cmd_cat   },
    { "write", "Write to file: write <file> <text>",     cmd_write },
    { "rm",    "Remove file/dir: rm <name>",             cmd_rm    },
    { "uname", "Show kernel info",                       cmd_uname },
    { "free",  "Show memory usage",                      cmd_free  },
    { "tree",  "Show filesystem tree",                   cmd_tree  },
    { "beep",  "Play a tone: beep [hz] [ms]",           cmd_beep  },
    { "mario",  "Play the Mario theme",                  cmd_mario  },
    { "format", "Format data disk (WARNING: wipes disk)",cmd_format },
    { "save",   "Save filesystem to disk",               cmd_save   },
    { "load",   "Load filesystem from disk",             cmd_load   },
    { "halt",   "Shutdown the system",                   cmd_halt   },
    { NULL, NULL, NULL }  /* Sentinel to mark end of table */
};

/* ============================================================
 * COMMAND PARSER
 *
 * Splits a command line string into tokens (argv array).
 * Tokens are separated by spaces/tabs.
 *
 * Example:
 *   Input:  "write myfile hello world"
 *   Output: argv[0]="write", argv[1]="myfile", argv[2]="hello", argv[3]="world"
 *           argc = 4
 *
 * NOTE: This modifies the input string by replacing spaces with '\0'.
 * ============================================================ */

static int parse_line(char* line, char* argv[], int max_args) {
    int argc = 0;

    while (*line && argc < max_args) {
        /* Skip leading whitespace */
        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0') break;

        /* This token starts here */
        argv[argc++] = line;

        /* Find end of token */
        while (*line && *line != ' ' && *line != '\t') line++;

        /* Null-terminate the token */
        if (*line) *line++ = '\0';
    }

    return argc;
}

/* ============================================================
 * PROMPT
 * ============================================================ */

static void print_prompt(void) {
    char path[256];
    fs_get_path(cwd, path, sizeof(path));

    vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    vga_print("root");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print("@");

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_print("kernelos");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print(":");

    vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
    vga_print(path);

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print("$ ");
}

/* ============================================================
 * MAIN SHELL LOOP
 * ============================================================ */

void shell_run(void) {
    char input[SHELL_INPUT_MAX];
    char* argv[SHELL_ARGS_MAX];

    cwd = fs_root;

    /* Print welcome banner
     *
     * WHY PLAIN ASCII?
     * VGA text mode uses Code Page 437 (CP437), an 8-bit character set.
     * Unicode characters (like box-drawing glyphs or the em-dash U+2014)
     * are stored in the source as multi-byte UTF-8 sequences.
     * When printed byte-by-byte to VGA, each UTF-8 byte is treated as
     * a separate CP437 character — producing garbage/white blocks.
     *
     * Safe range: 0x20–0x7E (standard ASCII printable characters).
     * Above 0x7E the characters are CP437-specific (smiley faces, etc.)
     * and won't match what you see in your editor.
     */
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print("\n");
    vga_print("  +---------------------------------------------------------+\n");
    vga_print("  |                                                         |\n");
    vga_print("  |   KernelOS v0.1  -  A learning kernel                  |\n");
    vga_print("  |   32-bit x86, Multiboot, VGA text mode                 |\n");
    vga_print("  |                                                         |\n");
    vga_print("  +---------------------------------------------------------+\n");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_print("\n  Type 'help' for available commands.\n\n");

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    while (1) {
        /* Show the prompt */
        print_prompt();

        /* Read a line of input */
        keyboard_readline(input, SHELL_INPUT_MAX);

        /* Skip empty lines */
        if (input[0] == '\0') continue;

        /* Parse the command line into argc/argv */
        int argc = parse_line(input, argv, SHELL_ARGS_MAX);
        if (argc == 0) continue;

        /* Look up the command in our table */
        bool found = false;
        for (int i = 0; commands[i].name != NULL; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].func(argc, argv);
                found = true;
                break;
            }
        }

        if (!found) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("  Error: unknown command '%s'. Type 'help' for help.\n", argv[0]);
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        }
    }
}

/* ============================================================
 * COMMAND IMPLEMENTATIONS
 * ============================================================ */

static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print("\n  Available commands:\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    for (int i = 0; commands[i].name != NULL; i++) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_printf("    %-10s", commands[i].name);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_printf(" - %s\n", commands[i].help);
    }

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print("\n");
}

static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_echo(int argc, char** argv) {
    /* Print all arguments (except argv[0] = "echo") separated by spaces */
    for (int i = 1; i < argc; i++) {
        vga_print(argv[i]);
        if (i < argc - 1) vga_print(" ");
    }
    vga_print("\n");
}

static void cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    char path[256];
    fs_get_path(cwd, path, sizeof(path));
    vga_printf("  %s\n", path);
}

static void cmd_ls(int argc, char** argv) {
    /* Determine which directory to list */
    fs_node_t* target = cwd;

    if (argc >= 2) {
        target = fs_resolve(argv[1], cwd);
        if (!target) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("  ls: no such directory: %s\n", argv[1]);
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return;
        }
    }

    if (target->type != FS_TYPE_DIR) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  ls: not a directory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (target->size == 0) {
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_print("  (empty directory)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    vga_print("\n");
    for (uint32_t i = 0; i < target->size; i++) {
        fs_node_t* child = target->children[i];
        if (!child) continue;

        if (child->type == FS_TYPE_DIR) {
            vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
            vga_printf("  %s/\n", child->name);
        } else {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            vga_printf("  %-20s  %u bytes\n", child->name, child->size);
        }
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print("\n");
}

static void cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        /* 'cd' with no args goes to root (like 'cd ~' in bash) */
        cwd = fs_root;
        return;
    }

    fs_node_t* target = fs_resolve(argv[1], cwd);

    if (!target) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  cd: no such directory: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (target->type != FS_TYPE_DIR) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  cd: not a directory: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    cwd = target;
}

static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) {
        vga_print("  Usage: mkdir <name>\n");
        return;
    }

    fs_node_t* node = fs_mkdir(cwd, argv[1]);
    if (!node) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  mkdir: failed (name exists, dir full, or pool full)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void cmd_touch(int argc, char** argv) {
    if (argc < 2) {
        vga_print("  Usage: touch <name>\n");
        return;
    }

    fs_node_t* node = fs_touch(cwd, argv[1]);
    if (!node) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  touch: failed (name exists, dir full, or pool full)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void cmd_cat(int argc, char** argv) {
    if (argc < 2) {
        vga_print("  Usage: cat <file>\n");
        return;
    }

    fs_node_t* file = fs_resolve(argv[1], cwd);
    if (!file) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  cat: no such file: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (file->type != FS_TYPE_FILE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  cat: %s is a directory\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /* Print each character of the file */
    for (uint32_t i = 0; i < file->size; i++) {
        vga_putchar(file->data[i]);
    }
    if (file->size > 0 && file->data[file->size - 1] != '\n') {
        vga_print("\n");  /* Ensure output ends with newline */
    }
}

static void cmd_write(int argc, char** argv) {
    if (argc < 3) {
        vga_print("  Usage: write <file> <text>\n");
        return;
    }

    fs_node_t* file = fs_resolve(argv[1], cwd);

    /* If file doesn't exist, create it */
    if (!file) {
        file = fs_touch(cwd, argv[1]);
        if (!file) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_print("  write: failed to create file\n");
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return;
        }
    }

    if (file->type != FS_TYPE_FILE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  write: target is a directory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /* Concatenate all remaining arguments as the content */
    char content[FS_MAX_FILE_SIZE];
    content[0] = '\0';
    for (int i = 2; i < argc; i++) {
        strcat(content, argv[i]);
        if (i < argc - 1) strcat(content, " ");
    }
    strcat(content, "\n");

    int written = fs_write(file, content, (uint32_t)strlen(content));
    vga_printf("  Wrote %d bytes to '%s'\n", written, argv[1]);
}

static void cmd_rm(int argc, char** argv) {
    if (argc < 2) {
        vga_print("  Usage: rm <name>\n");
        return;
    }

    fs_node_t* node = fs_resolve(argv[1], cwd);
    if (!node) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  rm: no such file or directory: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (node == fs_root) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  rm: cannot remove root directory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (node == cwd) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  rm: cannot remove current directory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (!fs_remove(node)) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  rm: directory not empty: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void cmd_uname(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_print("\n  KernelOS 0.1.0 i686 (32-bit x86)\n");
    vga_print("  Built with GCC + NASM + GRUB Multiboot\n");
    vga_print("  Architecture: i386 protected mode\n");
    vga_print("  VGA: text mode 80x25\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_free(int argc, char** argv) {
    (void)argc; (void)argv;
    size_t used = memory_used();
    vga_printf("\n  Memory used by kernel allocator: %u bytes (%u KB)\n",
               used, used / 1024);
    vga_printf("  Filesystem nodes in use: %d / %d\n\n",
               fs_node_count(), FS_MAX_NODES);
}

/* Helper: print the directory tree recursively */
static void print_tree(fs_node_t* node, int depth) {
    /* Indentation */
    for (int i = 0; i < depth; i++) vga_print("  ");

    if (node->type == FS_TYPE_DIR) {
        vga_set_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK);
        vga_printf("%s/\n", node->name);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        for (uint32_t i = 0; i < node->size; i++) {
            if (node->children[i]) {
                print_tree(node->children[i], depth + 1);
            }
        }
    } else {
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        vga_printf("%s (%u B)\n", node->name, node->size);
    }
}

static void cmd_tree(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_print("\n");
    print_tree(fs_root, 0);
    vga_print("\n");
}

/*
 * visual_note - Draw a one-line visual indicator for a playing note.
 *
 * WHY THIS EXISTS:
 *   VirtualBox does not emulate the PC speaker (port 0x61 + PIT channel 2)
 *   in a way that produces audio on the host. Sound only works reliably in
 *   QEMU with an explicit -audiodev flag.
 *
 *   This function makes beep/mario useful even when silent: it draws the
 *   note name and a pitch bar on screen so you can "see" the music.
 *
 * PITCH BAR:
 *   Maps the frequency logarithmically across a 24-character bar.
 *   Low notes (C3=130Hz) → bar filled on the left.
 *   High notes (A6=1760Hz) → bar filled on the right.
 *
 * NOTE:
 *   VGA CP437 character 0x0E = the ♫ musical note symbol.
 *   Our vga_putchar does NOT treat 0x0E as a control character,
 *   so it renders directly as the CP437 glyph.
 */
static void visual_note(uint32_t hz, bool highlight) {
    /* 0x0E = ♫ in CP437 — VGA renders it as a musical note */
    const char MUSIC_NOTE = '\x0E';

    if (highlight) {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    }

    if (hz == 0) {
        vga_print("  [ rest ]                         \r");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /*
     * Pitch bar: 24 characters wide.
     * We approximate a logarithmic scale by dividing the range
     * C3 (130Hz) to A6 (1760Hz) into 24 steps.
     *
     * Rough log mapping without floating-point:
     *   We use integer approximation: position ≈ 24 * (hz - 130) / (1760 - 130)
     *   This is linear, not log, but good enough for a visual indicator.
     */
    #define BAR_WIDTH 24
    #define FREQ_MIN  130
    #define FREQ_MAX  1760

    uint32_t clamped = hz < FREQ_MIN ? FREQ_MIN : (hz > FREQ_MAX ? FREQ_MAX : hz);
    int      pos     = (int)((clamped - FREQ_MIN) * BAR_WIDTH / (FREQ_MAX - FREQ_MIN));

    char bar[BAR_WIDTH + 1];
    for (int i = 0; i < BAR_WIDTH; i++) {
        bar[i] = (i < pos) ? '=' : ' ';
    }
    bar[BAR_WIDTH] = '\0';

    /* Print on same line using \r to overwrite — gives a "live" effect */
    vga_putchar(' '); vga_putchar(' ');
    vga_putchar(MUSIC_NOTE); vga_putchar(' ');

    /* Note name */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_printf("%-4s", speaker_note_name(hz));

    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_printf(" [%s] %u Hz   \r", bar, hz);

    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_beep(int argc, char** argv) {
    /*
     * Usage:
     *   beep            → 440 Hz for 300 ms  (A4, the standard tuning note)
     *   beep <hz>       → custom frequency, 300 ms
     *   beep <hz> <ms>  → custom frequency and duration
     *
     * The visual indicator shows even when audio is silent (e.g. VirtualBox).
     *
     * Musical note reference:
     *   C4=261  D4=293  E4=329  F4=349  G4=392  A4=440  B4=493
     *   C5=523  D5=587  E5=659  F5=698  G5=784  A5=880
     */
    uint32_t hz = 440;
    uint32_t ms = 300;

    if (argc >= 2) hz = (uint32_t)atoi(argv[1]);
    if (argc >= 3) ms = (uint32_t)atoi(argv[2]);

    if (hz < 20)    hz = 20;
    if (hz > 20000) hz = 20000;
    if (ms == 0)    ms = 10;

    visual_note(hz, true);
    speaker_beep(hz, ms);
    /* Erase the live line and print final result on a new line */
    vga_printf("  \x0E %-4s  %u Hz  %u ms\n",
               speaker_note_name(hz), hz, ms);
}

static void cmd_mario(int argc, char** argv) {
    (void)argc; (void)argv;

    /*
     * Super Mario Bros. - Main Theme (opening fanfare)
     *
     * This demonstrates speaker_play_melody() with an array of
     * note frequencies and durations.
     *
     * Frequencies (Hz) for each note:
     *   E5=659, C5=523, G5=784, G4=392, A4=440, B4=494, Bb4=466,
     *   A4=440, F5=698, G5=784, E5=659, C5=523, D5=587, B4=494
     *
     * Duration unit: 125ms = one "eighth note" at ~120 BPM
     *   quarter note = 250ms, half note = 500ms
     *
     * 0 in the frequency array = rest (silence for that duration)
     */
    static const uint32_t freqs[] = {
        659, 659, 0,  659, 0,  523, 659, 0,
        784, 0,   0,  0,   392, 0,   0,  0,
        523, 0,   0,  392, 0,   0,   329, 0,
        0,   440, 0,  494, 0,   466, 440, 0,
        392, 659, 784, 880, 0,  698, 784, 0,
        659, 0,  523, 587, 494, 0,   0,   0,
    };
    static const uint32_t durs[] = {
        125, 125, 125, 125, 125, 125, 125, 125,
        125, 125, 125, 250, 125, 125, 250, 250,
        125, 125, 250, 125, 125, 250, 125, 125,
        250, 125, 125, 125, 125, 125, 125, 125,
        83,  83,  83,  125, 125, 125, 125, 125,
        125, 125, 125, 125, 125, 125, 250, 250,
    };

    int count = (int)(sizeof(freqs) / sizeof(freqs[0]));

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print("  *** Super Mario Bros. Theme ***\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_print("  (watch below even if your speaker is silent)\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    /*
     * Play each note individually so we can show a visual indicator
     * in sync with the audio. This is exactly what a music visualizer does:
     * draw something on screen at the moment each note starts playing.
     *
     * On QEMU: you hear the beeps AND see the note display.
     * On VirtualBox (silent): you still see the note names scroll by.
     */
    for (int i = 0; i < count; i++) {
        visual_note(freqs[i], freqs[i] != 0);

        if (freqs[i] == 0) {
            /* Rest: silence */
            uint32_t ticks = (durs[i] + 9) / 10;
            timer_sleep(ticks ? ticks : 1);
        } else {
            speaker_beep(freqs[i], durs[i]);
        }

        /* 30 ms inter-note gap */
        if (i < count - 1) timer_sleep(3);
    }

    /* Clear the live line */
    vga_print("                                          \n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_print("  Done!\n");
}

static void cmd_format(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!disk_fs_available()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  format: no data disk detected.\n");
        vga_print("  Make sure QEMU is started with a disk image (run.sh does this).\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print("  WARNING: This will erase all data on the disk.\n");
    vga_print("  Are you sure? (yes/no): ");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    char answer[8];
    keyboard_readline(answer, sizeof(answer));

    if (strcmp(answer, "yes") != 0) {
        vga_print("  Cancelled.\n");
        return;
    }

    vga_print("  Formatting disk...\n");
    if (disk_fs_format()) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("  Done! Filesystem written to disk. Changes will persist on reboot.\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  Format failed! Disk write error.\n");
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_save(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!disk_fs_available()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  save: no data disk detected.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    vga_print("  Saving filesystem to disk...\n");
    if (disk_fs_save()) {
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("  Saved! All changes are now persistent.\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  Save failed! Disk write error.\n");
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_load(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!disk_fs_available()) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  load: no data disk detected.\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    vga_print("  Loading filesystem from disk...\n");
    if (disk_fs_load()) {
        cwd = fs_root;  /* Reset cwd since the tree was replaced */
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("  Loaded! Filesystem restored from disk.\n");
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  Load failed! Disk may not be formatted yet (use 'format').\n");
    }
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_halt(int argc, char** argv) {
    (void)argc; (void)argv;
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_print("\n  Shutting down...\n");

    /* Auto-save filesystem to disk before powering off */
    if (disk_fs_available()) {
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_print("  Saving filesystem to disk...\n");
        if (disk_fs_save()) {
            vga_print("  Filesystem saved. Goodbye!\n");
        } else {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_print("  Warning: disk save failed!\n");
        }
    } else {
        vga_print("  Goodbye!\n");
    }

    /*
     * ACPI shutdown would require detecting and parsing ACPI tables.
     * For QEMU, writing 0x2000 to port 0x604 triggers a shutdown
     * (this is a QEMU-specific extension, not real hardware).
     */
    outw(0x604, 0x2000);   /* QEMU power off */
    outw(0xB004, 0x2000);  /* Alternative QEMU/Bochs shutdown */

    /* If that didn't work, just halt the CPU */
    vga_print("  (Could not ACPI shutdown - halting CPU)\n");
    __asm__ volatile ("cli; hlt");
    while (1) {}
}
