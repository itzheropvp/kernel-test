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
#include "../script/script.h"
#include "../drivers/editor.h"

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

/*
 * Kernel-internal clipboard.
 *
 * Shared between the shell readline (Ctrl+K/U cuts, Ctrl+V pastes)
 * and the VIM editor (yy writes here so you can paste in the shell).
 *
 * This is NOT the host OS clipboard — QEMU has no mechanism to bridge
 * the host clipboard to PS/2 keyboard input without SPICE or VirtIO.
 */
#define CLIPBOARD_SIZE 1024
static char g_clipboard[CLIPBOARD_SIZE];

void shell_set_clipboard(const char* text, int len) {
    if (len < 0) len = 0;
    if (len >= CLIPBOARD_SIZE) len = CLIPBOARD_SIZE - 1;
    memcpy(g_clipboard, text, len);
    g_clipboard[len] = '\0';
}

const char* shell_get_clipboard(void) {
    return g_clipboard;
}

/* Forward declarations */
static void print_prompt(void);
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
static void cmd_run(int argc, char** argv);
static void cmd_vim(int argc, char** argv);
static void cmd_keymap(int argc, char** argv);
static void cmd_sleep(int argc, char** argv);

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
    { "vim",    "Edit a file: vim <file>",                cmd_vim    },
    { "run",    "Run a HeroScript file: run <file.hero>", cmd_run   },
    { "keymap", "Set keyboard layout: keymap [us|it]",   cmd_keymap },
    { "halt",   "Shutdown the system",                   cmd_halt   },
    { "sleep", "Pause for milliseconds: sleep [ms]  (default 1000)", cmd_sleep },
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
 * COMMAND HISTORY
 *
 * Stores the last HISTORY_SIZE commands in a ring buffer.
 * Pressing Up/Down in shell_readline navigates through them.
 *
 * RING BUFFER LAYOUT:
 *   hist_head points to the NEXT slot to write.
 *   The most recent command is at hist_head-1 (wrapping).
 *   hist_get(0) = most recent, hist_get(1) = second most recent.
 * ============================================================ */

#define HISTORY_SIZE 16

static char hist_buf[HISTORY_SIZE][SHELL_INPUT_MAX];
static int  hist_count = 0;   /* total entries stored (capped at HISTORY_SIZE) */
static int  hist_head  = 0;   /* ring buffer write position                    */

static void hist_add(const char* line) {
    if (!line || line[0] == '\0') return;
    strncpy(hist_buf[hist_head], line, SHELL_INPUT_MAX - 1);
    hist_buf[hist_head][SHELL_INPUT_MAX - 1] = '\0';
    hist_head = (hist_head + 1) % HISTORY_SIZE;
    if (hist_count < HISTORY_SIZE) hist_count++;
}

/* idx=0 → most recent, idx=1 → second most recent, etc. */
static const char* hist_get(int idx) {
    if (idx < 0 || idx >= hist_count) return NULL;
    int pos = ((hist_head - 1 - idx) % HISTORY_SIZE + HISTORY_SIZE) % HISTORY_SIZE;
    return hist_buf[pos];
}

/* ============================================================
 * SHELL READLINE WITH HISTORY, TAB COMPLETE, CURSOR MOVEMENT
 *
 * This replaces the simple keyboard_readline() call in shell_run().
 * Features:
 *   ↑ / ↓         — browse command history
 *   ← / →         — move cursor left/right (allows mid-line editing)
 *   Home / End     — jump to start/end of input
 *   Delete         — delete char UNDER cursor (vs Backspace = before cursor)
 *   Tab            — autocomplete command name or filesystem path
 *   Ctrl not supported yet (no Ctrl key tracking)
 *
 * HOW REDRAW WORKS:
 *   We record prompt_col/prompt_row at the start.
 *   On every change, we use vga_write_at() to overwrite the input area
 *   directly in the VGA buffer (no cursor movement, no scroll).
 *   Then vga_move_cursor() positions the blinking hardware cursor.
 *   This avoids the scroll-while-redrawing problem.
 * ============================================================ */

static void shell_readline(char* buf, int max_len) {
    int len         = 0;   /* chars currently in buf              */
    int cursor      = 0;   /* cursor position within buf (0..len) */
    int hist_idx    = -1;  /* -1 = not browsing history           */
    int disp_len    = 0;   /* max chars we've drawn (for erase)   */
    char saved[SHELL_INPUT_MAX];  /* input saved before history browse */
    saved[0] = '\0';
    buf[0]   = '\0';

    /* Record where input starts: right after the "$ " prompt */
    uint8_t prompt_col = vga_get_col();
    uint8_t prompt_row = vga_get_row();

    /* --- Inner helper: redraw the input line in-place --- */
    #define REDRAW() do { \
        int _end = disp_len > len ? disp_len : len; \
        for (int _i = 0; _i < len && prompt_col + _i < VGA_WIDTH; _i++) \
            vga_write_at(prompt_row, prompt_col + _i, buf[_i], \
                         VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
        for (int _i = len; _i < _end && prompt_col + _i < VGA_WIDTH; _i++) \
            vga_write_at(prompt_row, prompt_col + _i, ' ', \
                         VGA_COLOR_WHITE, VGA_COLOR_BLACK); \
        disp_len = len; \
        vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row); \
    } while(0)

    while (1) {
        uint8_t key = keyboard_getkey();

        /* --- ENTER --- */
        if (key == '\n' || key == '\r') {
            vga_move_cursor(0, prompt_row);
            vga_putchar('\n');
            if (len > 0) hist_add(buf);
            break;
        }

        /* --- BACKSPACE --- */
        if (key == '\b') {
            if (cursor > 0) {
                memmove(buf + cursor - 1, buf + cursor, len - cursor);
                cursor--;
                len--;
                buf[len] = '\0';
                REDRAW();
            }
            continue;
        }

        /* --- TAB: autocomplete --- */
        if (key == '\t') {
            /* Find start of the word being typed */
            int word_start = cursor;
            while (word_start > 0 && buf[word_start - 1] != ' ') word_start--;

            char prefix[SHELL_INPUT_MAX];
            int prefix_len = cursor - word_start;
            strncpy(prefix, buf + word_start, prefix_len);
            prefix[prefix_len] = '\0';

            bool first_word = (word_start == 0);

            /* Collect matches */
            const char* matches[32];
            int n = 0;

            if (first_word) {
                /* Match command names */
                for (int i = 0; commands[i].name && n < 32; i++) {
                    if (strncmp(commands[i].name, prefix, prefix_len) == 0)
                        matches[n++] = commands[i].name;
                }
            } else {
                /* Match filesystem entries */
                const char* slash = strrchr(prefix, '/');
                fs_node_t*  dir;
                const char* name_part;
                char dir_path[FS_MAX_NAME * 2];

                if (slash) {
                    int dlen = (int)(slash - prefix);
                    if (dlen == 0) { dir_path[0] = '/'; dir_path[1] = '\0'; }
                    else { strncpy(dir_path, prefix, dlen); dir_path[dlen] = '\0'; }
                    dir       = fs_resolve(dir_path, cwd);
                    name_part = slash + 1;
                } else {
                    dir       = cwd;
                    name_part = prefix;
                }

                int nlen = strlen(name_part);
                if (dir && dir->type == FS_TYPE_DIR) {
                    for (int i = 0; i < FS_MAX_CHILDREN && n < 32; i++) {
                        fs_node_t* ch = dir->children[i];
                        if (ch && ch->in_use &&
                            strncmp(ch->name, name_part, nlen) == 0)
                            matches[n++] = ch->name;
                    }
                }
            }

            if (n == 1) {
                /* Single match: insert the rest of the word */
                const char* m    = matches[0];
                /* How many chars of this match already typed? */
                const char* base = strrchr(prefix, '/');
                base = base ? base + 1 : prefix;
                int typed = strlen(base);
                const char* tail = m + typed;
                int tlen = strlen(tail);

                if (len + tlen < max_len - 1) {
                    memmove(buf + cursor + tlen, buf + cursor, len - cursor);
                    memcpy(buf + cursor, tail, tlen);
                    cursor += tlen;
                    len    += tlen;
                    buf[len] = '\0';

                    /* Add trailing space for commands */
                    if (first_word && len < max_len - 1) {
                        memmove(buf + cursor + 1, buf + cursor, len - cursor);
                        buf[cursor++] = ' ';
                        len++;
                        buf[len] = '\0';
                    }
                    REDRAW();
                }
            } else if (n > 1) {
                /* Multiple matches: show them below and reprint prompt */
                vga_move_cursor(0, prompt_row);
                vga_putchar('\n');
                vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
                for (int i = 0; i < n; i++) {
                    vga_print(matches[i]);
                    vga_print("  ");
                }
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                vga_putchar('\n');
                /* Reprint the prompt so user can continue typing */
                print_prompt();
                prompt_col = vga_get_col();
                prompt_row = vga_get_row();
                disp_len   = 0;
                REDRAW();
            }
            /* n == 0: no match — do nothing */
            continue;
        }

        /* --- ESCAPE: clear line --- */
        if (key == 0x1B) {
            cursor = 0; len = 0; buf[0] = '\0';
            hist_idx = -1;
            REDRAW();
            continue;
        }

        /* --- CTRL KEY BINDINGS ---
         *
         * Standard readline-style bindings.  Ctrl+letter arrives as
         * the ASCII control code 0x01-0x1A (produced by keyboard.c).
         *
         * Internal clipboard note: these cut/paste keys use the
         * kernel-internal clipboard (g_clipboard), NOT the host OS
         * clipboard.  QEMU has no way to bridge the host clipboard to
         * PS/2 keyboard input without SPICE or VirtIO infrastructure.
         *
         * Cross-app clipboard within the kernel:
         *   • VIM  yy  → writes to the same clipboard
         *   • Shell Ctrl+K / Ctrl+U → cut to clipboard
         *   • Shell Ctrl+V / Ctrl+Y → paste from clipboard
         */

        /* Ctrl+C (0x03): cancel current line, like pressing Ctrl+C in bash */
        if (key == 0x03) {
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            /* print ^C at the current cursor position then newline */
            vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row);
            vga_print("^C\n");
            len = 0; cursor = 0; buf[0] = '\0';
            hist_idx = -1;
            break;  /* return empty buf → shell_exec_line ignores it */
        }

        /* Ctrl+L (0x0C): clear screen, reprint prompt, keep current input */
        if (key == 0x0C) {
            vga_clear();
            print_prompt();
            prompt_col = vga_get_col();
            prompt_row = vga_get_row();
            disp_len   = 0;
            REDRAW();
            continue;
        }

        /* Ctrl+A (0x01): jump to start of line */
        if (key == 0x01) {
            cursor = 0;
            vga_move_cursor(prompt_col, prompt_row);
            continue;
        }

        /* Ctrl+E (0x05): jump to end of line */
        if (key == 0x05) {
            cursor = len;
            vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row);
            continue;
        }

        /* Ctrl+K (0x0B): kill (cut) from cursor to end of line → clipboard */
        if (key == 0x0B) {
            if (cursor < len) {
                shell_set_clipboard(buf + cursor, len - cursor);
                len = cursor;
                buf[len] = '\0';
                REDRAW();
            }
            continue;
        }

        /* Ctrl+U (0x15): kill (cut) from start of line to cursor → clipboard */
        if (key == 0x15) {
            if (cursor > 0) {
                shell_set_clipboard(buf, cursor);
                memmove(buf, buf + cursor, len - cursor);
                len -= cursor;
                cursor = 0;
                buf[len] = '\0';
                REDRAW();
            }
            continue;
        }

        /* Ctrl+W (0x17): delete the word immediately before the cursor */
        if (key == 0x17) {
            int old = cursor;
            while (cursor > 0 && buf[cursor - 1] == ' ') cursor--;
            while (cursor > 0 && buf[cursor - 1] != ' ') cursor--;
            if (cursor < old) {
                shell_set_clipboard(buf + cursor, old - cursor);
                memmove(buf + cursor, buf + old, len - old);
                len -= (old - cursor);
                buf[len] = '\0';
                REDRAW();
            }
            continue;
        }

        /* Ctrl+V (0x16) or Ctrl+Y (0x19): paste from internal clipboard */
        if (key == 0x16 || key == 0x19) {
            const char* clip = shell_get_clipboard();
            int clip_len = strlen(clip);
            if (clip_len > 0 && len + clip_len < max_len - 1) {
                memmove(buf + cursor + clip_len, buf + cursor, len - cursor);
                memcpy(buf + cursor, clip, clip_len);
                cursor += clip_len;
                len    += clip_len;
                buf[len] = '\0';
                hist_idx = -1;
                REDRAW();
            }
            continue;
        }

        /* --- ARROW KEYS AND NAVIGATION --- */
        if (key == KEY_UP) {
            if (hist_idx == -1) {
                strncpy(saved, buf, SHELL_INPUT_MAX - 1);
                saved[SHELL_INPUT_MAX - 1] = '\0';
            }
            const char* entry = hist_get(hist_idx + 1);
            if (entry) {
                hist_idx++;
                strncpy(buf, entry, max_len - 1);
                buf[max_len - 1] = '\0';
                len = cursor = strlen(buf);
                REDRAW();
            }
            continue;
        }
        if (key == KEY_DOWN) {
            if (hist_idx > 0) {
                hist_idx--;
                const char* entry = hist_get(hist_idx);
                strncpy(buf, entry, max_len - 1);
                buf[max_len - 1] = '\0';
            } else {
                hist_idx = -1;
                strncpy(buf, saved, max_len - 1);
                buf[max_len - 1] = '\0';
            }
            len = cursor = strlen(buf);
            REDRAW();
            continue;
        }
        if (key == KEY_LEFT)  { if (cursor > 0)   { cursor--; vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row); } continue; }
        if (key == KEY_RIGHT) { if (cursor < len)  { cursor++; vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row); } continue; }
        if (key == KEY_HOME)  { cursor = 0;   vga_move_cursor(prompt_col, prompt_row); continue; }
        if (key == KEY_END)   { cursor = len; vga_move_cursor((uint8_t)(prompt_col + cursor), prompt_row); continue; }
        if (key == KEY_DEL)   {
            if (cursor < len) {
                memmove(buf + cursor, buf + cursor + 1, len - cursor - 1);
                len--;
                buf[len] = '\0';
                REDRAW();
            }
            continue;
        }

        /* --- PRINTABLE CHARACTER: insert at cursor ---
         * Accept standard ASCII (0x20-0x7E) and extended Latin-1 (0x89+).
         * The gap 0x80-0x88 is reserved for KEY_UP/DOWN/LEFT/RIGHT/etc.
         */
        if ((key >= 0x20 && key < 0x80) || key >= 0x89) {
            if (len < max_len - 1) {
                memmove(buf + cursor + 1, buf + cursor, len - cursor);
                buf[cursor] = (char)key;
                cursor++;
                len++;
                buf[len] = '\0';
                hist_idx = -1;
                REDRAW();
            }
        }
    }

    #undef REDRAW
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
 * PUBLIC SHELL API
 * ============================================================ */

struct fs_node* shell_get_cwd(void) {
    return cwd;
}

/*
 * shell_exec_line - Parse and execute one command string.
 *
 * This is the heart of the shell — it's called both by the interactive
 * loop (when the user presses Enter) and by the HeroScript interpreter
 * (when a script line is not a scripting keyword).
 *
 * This separation is the key design pattern:
 *   interactive prompt  →  shell_exec_line()
 *   HeroScript line     →  shell_exec_line()   (same code path!)
 *
 * SPECIAL BEHAVIOR:
 *   - Lines starting with '#' are treated as comments (ignored).
 *   - A filename ending in ".hero" is treated as "run <filename>".
 *     This lets you type  hello.hero  directly, like typing  ./hello.sh
 *     in bash.
 */
void shell_exec_line(const char* input) {
    char   line[SHELL_INPUT_MAX];
    char*  argv[SHELL_ARGS_MAX];

    /* Work on a copy — parse_line modifies the string in place */
    strncpy(line, input, SHELL_INPUT_MAX - 1);
    line[SHELL_INPUT_MAX - 1] = '\0';

    /* Strip leading whitespace */
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;

    /* Ignore empty lines and comment lines */
    if (*p == '\0' || *p == '#') return;

    int argc = parse_line(p, argv, SHELL_ARGS_MAX);
    if (argc == 0) return;

    /*
     * AUTO-RUN .hero FILES
     *
     * If the first token ends with ".hero", treat it as "run <filename>".
     * Lets you type  hello.hero  directly instead of  run hello.hero,
     * similar to how bash lets you run  ./script.sh  without typing 'bash'.
     */
    int name_len = strlen(argv[0]);
    if (name_len > 5 && strcmp(argv[0] + name_len - 5, ".hero") == 0) {
        char* run_argv[2] = { (char*)"run", argv[0] };
        cmd_run(2, run_argv);
        return;
    }

    /* Normal command table dispatch */
    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    vga_printf("  Error: unknown command '%s'. Type 'help'.\n", argv[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

/* ============================================================
 * MAIN SHELL LOOP
 * ============================================================ */

void shell_run(void) {
    char input[SHELL_INPUT_MAX];

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
    vga_print("  |    KernelOS v0.1  -  A learning kernel                  |\n");
    vga_print("  |    32-bit x86, Multiboot, VGA text mode                 |\n");
    vga_print("  |                                                         |\n");
    vga_print("  +---------------------------------------------------------+\n");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_print("\n  Type 'help' for available commands.\n\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    while (1) {
        print_prompt();
        shell_readline(input, SHELL_INPUT_MAX);
        shell_exec_line(input);
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

static void cmd_vim(int argc, char** argv) {
    /*
     * vim <filename>
     *
     * Opens the file in the built-in VIM-like editor.
     * If the file doesn't exist, creates it first with touch.
     * On :wq or :w the content is saved back to the filesystem node.
     */
    if (argc < 2) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  Usage: vim <filename>\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    fs_node_t* node = fs_resolve(argv[1], cwd);

    /* Create the file if it doesn't exist */
    if (!node) {
        node = fs_touch(cwd, argv[1]);
        if (!node) {
            vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
            vga_printf("  vim: cannot create '%s'\n", argv[1]);
            vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
            return;
        }
    }

    if (node->type != FS_TYPE_FILE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  vim: '%s' is a directory\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    editor_open(node, argv[1]);

    /* After the editor exits, vga_clear() was called — reprint the shell state */
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
}

static void cmd_run(int argc, char** argv) {
    /*
     * run <file.hero>
     *
     * Finds the named file in the filesystem, reads its content,
     * and passes it to the HeroScript interpreter (script_run).
     *
     * If you omit the .hero extension, we try to add it automatically.
     * So both of these work:
     *   run hello
     *   run hello.hero
     */
    if (argc < 2) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  Usage: run <file.hero>\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /* Try to find the file as given */
    fs_node_t* node = fs_resolve(argv[1], cwd);

    /* If not found and doesn't already end in .hero, try adding the extension */
    if (!node) {
        int n = strlen(argv[1]);
        if (n < 5 || strcmp(argv[1] + n - 5, ".hero") != 0) {
            char with_ext[FS_MAX_NAME];
            ksprintf(with_ext, "%s.hero", argv[1]);
            node = fs_resolve(with_ext, cwd);
        }
    }

    if (!node) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  run: file not found: %s\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (node->type != FS_TYPE_FILE) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  run: '%s' is not a file\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    /* Hand off to the HeroScript interpreter */
    script_run(node->data, node->size);
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

static void cmd_keymap(int argc, char** argv) {
    /*
     * keymap         — show current layout
     * keymap us      — switch to US QWERTY
     * keymap it      — switch to Italian QWERTY
     *
     * Italian keyboard differences from US:
     *   - Accented vowels on dedicated keys: è à ì ò ù
     *   - Apostrophe key where US has '-'
     *   - Slash moved: the '-' key is where US '/' is
     *   - Shift layer differs: Shift+2=" Shift+7=/ Shift+8=( etc.
     *   - AltGr gives access to: @ # { [ ] }
     *     AltGr+2=@  AltGr+3=#  AltGr+7={  AltGr+8=[  AltGr+9=]  AltGr+0=}
     *     AltGr+ì=`  (backtick, not otherwise reachable on Italian layout)
     */
    if (argc < 2) {
        /* Show current layout */
        vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
        /* We can't easily query current_keymap from here — just show the hint */
        vga_print("\n  Usage: keymap <us|it>\n");
        vga_print("  Layouts available:\n");
        vga_print("    us   US QWERTY (default)\n");
        vga_print("    it   Italian QWERTY (with AltGr for @ # { [ ] })\n\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return;
    }

    if (strcmp(argv[1], "it") == 0) {
        keyboard_set_layout(KEYMAP_IT);
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("  Keyboard layout: Italian (it)\n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_print("  AltGr+2=@  AltGr+3=#  AltGr+7={  AltGr+8=[  AltGr+9=]  AltGr+0=}\n");
        vga_print("  AltGr+ì=`   ISO key (</>)  accented vowels: e'=");
        vga_putchar((char)0xE8); /* è */
        vga_print(" a'=");
        vga_putchar((char)0xE0); /* à */
        vga_print(" i'=");
        vga_putchar((char)0xEC); /* ì */
        vga_print(" o'=");
        vga_putchar((char)0xF2); /* ò */
        vga_print(" u'=");
        vga_putchar((char)0xF9); /* ù */
        vga_print("\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else if (strcmp(argv[1], "us") == 0) {
        keyboard_set_layout(KEYMAP_US);
        vga_set_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        vga_print("  Keyboard layout: US QWERTY (us)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_printf("  keymap: unknown layout '%s' (use 'us' or 'it')\n", argv[1]);
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}

static void cmd_sleep(int argc, char** argv) {
    /*
     * sleep <ms>
     *
     * Pause execution for the given number of milliseconds.
     * Defaults to 1000 ms (1 second) if no argument is given.
     *
     * The timer runs at 100 Hz (1 tick = 10 ms), so the minimum
     * resolution is 10 ms. Values below 10 ms sleep for one tick.
     *
     * Works in the shell AND in HeroScript:
     *   sleep 500      # pause 500 ms
     *   sleep 2000     # pause 2 seconds
     */
    uint32_t ms = 1000;
    if (argc >= 2) ms = (uint32_t)atoi(argv[1]);

    if (ms == 0) return;

    /* Convert ms → 100 Hz ticks (ceiling division: 1 tick = 10 ms) */
    uint32_t ticks = (ms + 9) / 10;
    timer_sleep(ticks);
}