/*
 * editor.c - VIM-like Modal Text Editor
 *
 * ARCHITECTURE:
 *
 *   Text is stored as a flat byte buffer (ed.buf[]) with '\n' between lines.
 *   This is the simplest correct representation — no line-pointer arrays,
 *   no gap buffer. For files up to FS_MAX_FILE_SIZE (4096 bytes) it's fast enough.
 *
 *   The cursor is a byte offset into ed.buf (ed.cursor).
 *   To find the current line/column, we scan from the start — O(n) but fine here.
 *
 *   The viewport (ed.view_top) tracks which line is at the top of the screen.
 *   It auto-scrolls to keep the cursor visible.
 *
 * SCREEN LAYOUT (80×25 VGA):
 *
 *   Row  0 - 21 : file content (22 lines visible)
 *   Row 22       : separator / status line
 *   Row 23       : mode indicator + filename + position
 *   Row 24       : command line (for : commands) / messages
 *
 * VIM DESIGN PHILOSOPHY (educational note):
 *
 *   VIM uses a "modal" design — the same keys do different things depending
 *   on the current mode. This is unintuitive at first but very powerful:
 *   in NORMAL mode every key can be a command (no modifier needed), giving
 *   you fast navigation without leaving the home row.
 *
 *   INSERT mode makes the keyboard work like a normal editor.
 *   COMMAND mode (after ':') lets you type multi-character commands.
 */

#include "editor.h"
#include "vga.h"
#include "../kernel/keyboard.h"
#include "../fs/fs.h"
#include "../libc/string.h"
#include "../kernel/kernel.h"

/* ---- Screen layout constants ---- */
#define ED_CONTENT_ROWS  22   /* rows 0-21: file text              */
#define ED_SEP_ROW       22   /* row 22: separator line            */
#define ED_STATUS_ROW    23   /* row 23: mode / filename / pos     */
#define ED_CMD_ROW       24   /* row 24: command bar / messages    */

/* ---- Editor mode ---- */
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
} ed_mode_t;

/* ---- Editor state (global/static to avoid large stack allocation) ---- */
typedef struct {
    char        buf[FS_MAX_FILE_SIZE + 1]; /* file content (flat buffer)    */
    int         len;                        /* bytes in buf                  */
    int         cursor;                     /* byte offset of cursor         */
    int         view_top;                   /* first visible line number     */
    char        filename[FS_MAX_NAME];
    fs_node_t*  node;
    bool        modified;
    ed_mode_t   mode;
    char        cmd[80];                    /* :command line input           */
    int         cmd_len;
    char        yank[FS_MAX_FILE_SIZE];     /* yank (copy) buffer            */
    int         yank_len;
    char        msg[80];                    /* status message                */
    char        pending;                    /* for dd / yy / gg two-key cmds */
} editor_t;

static editor_t ed;

/* ================================================================
 * LINE NAVIGATION HELPERS
 *
 * All work with byte offsets into ed.buf.
 * A "line" ends at '\n' or at ed.len (end of file).
 * ================================================================ */

/* Byte offset of the start of the line containing `pos` */
static int line_start(int pos) {
    while (pos > 0 && ed.buf[pos - 1] != '\n') pos--;
    return pos;
}

/* Byte offset of the '\n' at end of line, or ed.len if last line */
static int line_end(int pos) {
    while (pos < ed.len && ed.buf[pos] != '\n') pos++;
    return pos;
}

/* 0-based line number of `pos` (count '\n' before it) */
static int line_num(int pos) {
    int n = 0;
    for (int i = 0; i < pos; i++)
        if (ed.buf[i] == '\n') n++;
    return n;
}

/* 0-based column of `pos` */
static int col_num(int pos) {
    return pos - line_start(pos);
}

/* Byte offset of the start of line number `n` */
static int line_n_start(int n) {
    int p = 0;
    while (n > 0 && p < ed.len) {
        if (ed.buf[p] == '\n') n--;
        p++;
    }
    return p;
}

/* Total number of lines in the file */
static int total_lines(void) {
    int n = 1;
    for (int i = 0; i < ed.len; i++)
        if (ed.buf[i] == '\n') n++;
    return n;
}

/* ================================================================
 * BUFFER MUTATION HELPERS
 * ================================================================ */

static void buf_insert(int pos, char c) {
    if (ed.len >= FS_MAX_FILE_SIZE) return;
    memmove(ed.buf + pos + 1, ed.buf + pos, ed.len - pos);
    ed.buf[pos] = c;
    ed.len++;
    ed.buf[ed.len] = '\0';
    ed.modified = true;
}

static void buf_delete(int pos) {
    if (pos < 0 || pos >= ed.len) return;
    memmove(ed.buf + pos, ed.buf + pos + 1, ed.len - pos - 1);
    ed.len--;
    ed.buf[ed.len] = '\0';
    ed.modified = true;
}

/* ================================================================
 * CURSOR MOVEMENT (NORMAL MODE)
 * ================================================================ */

static void move_left(void) {
    if (ed.cursor > 0 && ed.buf[ed.cursor - 1] != '\n')
        ed.cursor--;
}

static void move_right(void) {
    /* In NORMAL mode, don't move past the last char on the line */
    if (ed.cursor < ed.len && ed.buf[ed.cursor] != '\n')
        ed.cursor++;
}

static void move_up(void) {
    int ls  = line_start(ed.cursor);
    if (ls == 0) return;
    int col = ed.cursor - ls;
    /* Go to previous line */
    int prev_end   = ls - 1;                   /* '\n' of previous line */
    int prev_start = line_start(prev_end);
    int prev_len   = prev_end - prev_start;
    ed.cursor = prev_start + (col < prev_len ? col : prev_len);
}

static void move_down(void) {
    int le = line_end(ed.cursor);
    if (le >= ed.len) return;                  /* already on last line */
    int col      = ed.cursor - line_start(ed.cursor);
    int next_start = le + 1;
    int next_end   = line_end(next_start);
    int next_len   = next_end - next_start;
    ed.cursor = next_start + (col < next_len ? col : next_len);
}

static void move_word_forward(void) {
    /* Skip current word, then skip whitespace */
    while (ed.cursor < ed.len && ed.buf[ed.cursor] != ' '
           && ed.buf[ed.cursor] != '\n') ed.cursor++;
    while (ed.cursor < ed.len && (ed.buf[ed.cursor] == ' '
           || ed.buf[ed.cursor] == '\n')) ed.cursor++;
}

static void move_word_back(void) {
    if (ed.cursor == 0) return;
    ed.cursor--;
    while (ed.cursor > 0 && (ed.buf[ed.cursor] == ' '
           || ed.buf[ed.cursor] == '\n')) ed.cursor--;
    while (ed.cursor > 0 && ed.buf[ed.cursor - 1] != ' '
           && ed.buf[ed.cursor - 1] != '\n') ed.cursor--;
}

/* ================================================================
 * LINE OPERATIONS (dd, yy, p, P)
 * ================================================================ */

static void yank_line(void) {
    int ls = line_start(ed.cursor);
    int le = line_end(ed.cursor);
    int end = (le < ed.len) ? le + 1 : le;   /* include '\n' if possible */
    ed.yank_len = end - ls;
    memcpy(ed.yank, ed.buf + ls, ed.yank_len);
    ed.yank[ed.yank_len] = '\0';
    strncpy(ed.msg, "1 line yanked", sizeof(ed.msg) - 1);
}

static void delete_line(void) {
    yank_line();   /* yank first so 'p' can paste it */
    int ls  = line_start(ed.cursor);
    int le  = line_end(ed.cursor);
    int end = (le < ed.len) ? le + 1 : le;

    memmove(ed.buf + ls, ed.buf + end, ed.len - end);
    ed.len -= (end - ls);
    ed.buf[ed.len] = '\0';
    ed.modified = true;

    if (ed.cursor >= ed.len) ed.cursor = (ed.len > 0) ? ed.len - 1 : 0;
    else ed.cursor = ls;

    strncpy(ed.msg, "1 line deleted", sizeof(ed.msg) - 1);
}

static void paste_after(void) {
    if (ed.yank_len == 0) return;
    int le  = line_end(ed.cursor);
    int ins = (le < ed.len) ? le + 1 : le;  /* insert after '\n' */

    if (ed.len + ed.yank_len > FS_MAX_FILE_SIZE) return;
    memmove(ed.buf + ins + ed.yank_len, ed.buf + ins, ed.len - ins);
    memcpy(ed.buf + ins, ed.yank, ed.yank_len);
    ed.len += ed.yank_len;
    ed.buf[ed.len] = '\0';
    ed.cursor = ins;
    ed.modified = true;
}

static void paste_before(void) {
    if (ed.yank_len == 0) return;
    int ls = line_start(ed.cursor);

    if (ed.len + ed.yank_len > FS_MAX_FILE_SIZE) return;
    memmove(ed.buf + ls + ed.yank_len, ed.buf + ls, ed.len - ls);
    memcpy(ed.buf + ls, ed.yank, ed.yank_len);
    ed.len += ed.yank_len;
    ed.buf[ed.len] = '\0';
    ed.cursor = ls;
    ed.modified = true;
}

/* Open a blank line below/above and enter INSERT */
static void open_line_below(void) {
    int le = line_end(ed.cursor);
    buf_insert(le, '\n');
    ed.cursor = le + 1;
    ed.mode = MODE_INSERT;
}

static void open_line_above(void) {
    int ls = line_start(ed.cursor);
    buf_insert(ls, '\n');
    ed.cursor = ls;
    ed.mode = MODE_INSERT;
}

/* ================================================================
 * RENDERING
 *
 * Uses vga_write_at / vga_write_str_at / vga_clear_row so we
 * never accidentally trigger a VGA scroll while drawing.
 * ================================================================ */

static void draw_content(void) {
    int cur_ln = line_num(ed.cursor);
    int cur_cl = col_num(ed.cursor);

    /* Auto-scroll: keep cursor line visible */
    if (cur_ln < ed.view_top)
        ed.view_top = cur_ln;
    if (cur_ln >= ed.view_top + ED_CONTENT_ROWS)
        ed.view_top = cur_ln - ED_CONTENT_ROWS + 1;

    /* Walk to first visible line */
    int p = line_n_start(ed.view_top);

    for (int row = 0; row < ED_CONTENT_ROWS; row++) {
        vga_clear_row(row, VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        int line_idx = ed.view_top + row;
        int col      = 0;

        if (p <= ed.len) {
            while (p < ed.len && ed.buf[p] != '\n' && col < VGA_WIDTH) {
                /* Highlight the character under the cursor in NORMAL mode */
                bool is_cursor = (p == ed.cursor && ed.mode == MODE_NORMAL);
                vga_write_at(row, col,
                             ed.buf[p],
                             is_cursor ? VGA_COLOR_BLACK : VGA_COLOR_WHITE,
                             is_cursor ? VGA_COLOR_WHITE : VGA_COLOR_BLACK);
                col++;
                p++;
            }

            /* Cursor at end of line (empty line or past last char) */
            if (p == ed.cursor && ed.mode == MODE_NORMAL && col < VGA_WIDTH) {
                vga_write_at(row, col, ' ', VGA_COLOR_BLACK, VGA_COLOR_WHITE);
            }

            if (p < ed.len && ed.buf[p] == '\n') p++;
        } else {
            /* Past end of file: draw '~' like real VIM */
            vga_write_at(row, 0, '~', VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
        }

        (void)line_idx; /* line_idx available for line numbers if added later */
    }

    /* ---- Separator row ---- */
    vga_clear_row(ED_SEP_ROW, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);

    /* ---- Status bar ---- */
    vga_clear_row(ED_STATUS_ROW, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);

    /* Mode label */
    const char* mode_str;
    vga_color_t mode_fg, mode_bg;
    switch (ed.mode) {
        case MODE_INSERT:
            mode_str = "-- INSERT --"; mode_fg = VGA_COLOR_WHITE; mode_bg = VGA_COLOR_GREEN;
            break;
        case MODE_COMMAND:
            mode_str = "-- COMMAND --"; mode_fg = VGA_COLOR_WHITE; mode_bg = VGA_COLOR_BROWN;
            break;
        default:
            mode_str = "-- NORMAL --"; mode_fg = VGA_COLOR_BLACK; mode_bg = VGA_COLOR_LIGHT_GREY;
            break;
    }
    vga_write_str_at(ED_STATUS_ROW, 0, mode_str, mode_fg, mode_bg);

    /* Filename + modified flag */
    char status[80];
    ksprintf(status, "  %s%s", ed.filename, ed.modified ? " [+]" : "");
    vga_write_str_at(ED_STATUS_ROW, 13, status, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);

    /* Position: line:col */
    char pos[16];
    ksprintf(pos, "%d:%d", cur_ln + 1, cur_cl + 1);
    int pos_col = VGA_WIDTH - strlen(pos) - 1;
    if (pos_col > 0)
        vga_write_str_at(ED_STATUS_ROW, pos_col, pos, VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);

    /* ---- Command / message row ---- */
    vga_clear_row(ED_CMD_ROW, VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    if (ed.mode == MODE_COMMAND) {
        /* Show the ':' and what's been typed */
        char cmd_display[82];
        cmd_display[0] = ':';
        strncpy(cmd_display + 1, ed.cmd, sizeof(cmd_display) - 2);
        cmd_display[sizeof(cmd_display) - 1] = '\0';
        vga_write_str_at(ED_CMD_ROW, 0, cmd_display, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    } else if (ed.msg[0]) {
        vga_write_str_at(ED_CMD_ROW, 0, ed.msg, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    } else {
        /* Hint for beginners */
        vga_write_str_at(ED_CMD_ROW, 0,
            "i=INSERT  Esc=NORMAL  :w=save  :q=quit  :wq=save+quit",
            VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    }

    /* ---- Hardware cursor position ---- */
    if (ed.mode == MODE_COMMAND) {
        /* Put cursor in the command bar after the colon + typed chars */
        vga_move_cursor((uint8_t)(1 + ed.cmd_len), ED_CMD_ROW);
    } else {
        /* Put cursor at the character position in the content area */
        int screen_row = cur_ln - ed.view_top;
        int screen_col = cur_cl;
        if (screen_row >= 0 && screen_row < ED_CONTENT_ROWS && screen_col < VGA_WIDTH)
            vga_move_cursor((uint8_t)screen_col, (uint8_t)screen_row);
    }
}

/* ================================================================
 * COMMAND HANDLER (:w :q :wq :q! :x)
 *
 * Returns true if the editor should exit, false to keep editing.
 * ================================================================ */

static bool handle_command(void) {
    ed.msg[0] = '\0';

    if (strcmp(ed.cmd, "w") == 0 || strcmp(ed.cmd, "write") == 0) {
        ed.node->size = (uint32_t)ed.len;
        fs_write(ed.node, ed.buf, (uint32_t)ed.len);
        ed.modified = false;
        ksprintf(ed.msg, "\"%s\" %d bytes written", ed.filename, ed.len);
        return false;
    }

    if (strcmp(ed.cmd, "q") == 0 || strcmp(ed.cmd, "quit") == 0) {
        if (ed.modified) {
            strncpy(ed.msg,
                "No write since last change! Use :q! to force, or :wq to save+quit.",
                sizeof(ed.msg) - 1);
            return false;
        }
        return true;
    }

    if (strcmp(ed.cmd, "q!") == 0) {
        return true;   /* discard changes */
    }

    if (strcmp(ed.cmd, "wq") == 0 || strcmp(ed.cmd, "x") == 0
                                   || strcmp(ed.cmd, "wq!") == 0) {
        ed.node->size = (uint32_t)ed.len;
        fs_write(ed.node, ed.buf, (uint32_t)ed.len);
        return true;
    }

    ksprintf(ed.msg, "Not an editor command: %s", ed.cmd);
    return false;
}

/* ================================================================
 * NORMAL MODE KEY HANDLER
 * ================================================================ */

static bool handle_normal(uint8_t key) {
    ed.msg[0] = '\0';

    /* Two-key commands: dd, yy, gg */
    if (ed.pending == 'd' && key == 'd') { delete_line(); ed.pending = 0; return false; }
    if (ed.pending == 'y' && key == 'y') { yank_line();   ed.pending = 0; return false; }
    if (ed.pending == 'g' && key == 'g') { ed.cursor = 0; ed.pending = 0; return false; }

    /* Any unrecognised second key resets pending */
    if (ed.pending && key != ed.pending) ed.pending = 0;

    switch ((char)key) {
        /* --- Movement --- */
        case 'h': case '\x82': move_left();         break;  /* h or ← */
        case 'l': case '\x83': move_right();        break;  /* l or → */
        case 'k': case '\x80': move_up();           break;  /* k or ↑ */
        case 'j': case '\x81': move_down();         break;  /* j or ↓ */
        case '0':
            ed.cursor = line_start(ed.cursor);
            break;
        case '$':
            ed.cursor = line_end(ed.cursor);
            /* In NORMAL mode, stay on last char, not past it */
            if (ed.cursor > line_start(ed.cursor)) ed.cursor--;
            break;
        case 'G':
            ed.cursor = line_n_start(total_lines() - 1);
            break;
        case 'w': move_word_forward(); break;
        case 'b': move_word_back();    break;

        /* --- Enter INSERT mode --- */
        case 'i': ed.mode = MODE_INSERT;  break;
        case 'a':
            /* Insert AFTER cursor */
            if (ed.cursor < ed.len && ed.buf[ed.cursor] != '\n')
                ed.cursor++;
            ed.mode = MODE_INSERT;
            break;
        case 'A':
            ed.cursor = line_end(ed.cursor);
            ed.mode   = MODE_INSERT;
            break;
        case 'o': open_line_below(); break;
        case 'O': open_line_above(); break;

        /* --- Edit --- */
        case 'x':
            if (ed.cursor < ed.len && ed.buf[ed.cursor] != '\n')
                buf_delete(ed.cursor);
            break;
        case 'd': ed.pending = 'd'; return false;  /* wait for second 'd' */
        case 'y': ed.pending = 'y'; return false;
        case 'g': ed.pending = 'g'; return false;
        case 'p': paste_after();  break;
        case 'P': paste_before(); break;

        /* --- Enter COMMAND mode --- */
        case ':':
            ed.mode    = MODE_COMMAND;
            ed.cmd[0]  = '\0';
            ed.cmd_len = 0;
            break;

        default: break;
    }

    ed.pending = 0;
    return false;
}

/* ================================================================
 * PUBLIC API
 * ================================================================ */

void editor_open(fs_node_t* node, const char* filename) {
    /* ---- Initialise editor state ---- */
    memset(&ed, 0, sizeof(ed));
    ed.node = node;
    strncpy(ed.filename, filename, FS_MAX_NAME - 1);

    /* Load file content */
    if (node->size > 0) {
        uint32_t n = fs_read(node, ed.buf, FS_MAX_FILE_SIZE);
        ed.len = (int)n;
    }
    ed.buf[ed.len] = '\0';

    /* If file is empty, start with one blank line so cursor has somewhere to sit */
    if (ed.len == 0) {
        ed.buf[0] = '\0';
    }

    ed.mode   = MODE_NORMAL;
    ed.cursor = 0;

    /*
     * MAIN EDITOR LOOP
     *
     * 1. Draw the screen
     * 2. Wait for a key
     * 3. Dispatch to the right mode handler
     * 4. Repeat
     */
    bool quit = false;
    while (!quit) {
        draw_content();

        uint8_t key = keyboard_getkey();

        switch (ed.mode) {

            case MODE_NORMAL:
                quit = handle_normal(key);
                break;

            case MODE_INSERT:
                /*
                 * INSERT MODE:
                 * - Esc → back to NORMAL
                 * - Arrows → move cursor
                 * - Backspace → delete char before cursor
                 * - Enter → insert newline
                 * - Printable → insert char
                 */
                if (key == 0x1B) {
                    ed.mode = MODE_NORMAL;
                    /* Move cursor back one if we went past the line */
                    if (ed.cursor > 0 && (ed.cursor >= ed.len
                            || ed.buf[ed.cursor] == '\n'))
                        if (ed.buf[ed.cursor - 1] != '\n')
                            ed.cursor--;
                } else if (key == KEY_UP)    { move_up();    }
                else if (key == KEY_DOWN)    { move_down();  }
                else if (key == KEY_LEFT)    {
                    if (ed.cursor > 0) ed.cursor--;
                }
                else if (key == KEY_RIGHT)   {
                    if (ed.cursor < ed.len) ed.cursor++;
                }
                else if (key == KEY_HOME)    { ed.cursor = line_start(ed.cursor); }
                else if (key == KEY_END)     { ed.cursor = line_end(ed.cursor);   }
                else if (key == KEY_DEL)     { buf_delete(ed.cursor);             }
                else if (key == '\b') {
                    if (ed.cursor > 0) {
                        ed.cursor--;
                        buf_delete(ed.cursor);
                    }
                } else if (key == '\n' || key == '\r') {
                    buf_insert(ed.cursor, '\n');
                    ed.cursor++;  /* move past the newline we just inserted */
                } else if ((key >= 0x20 && key < 0x80) || key >= 0x89) {
                    /*
                     * Printable character (ASCII 0x20-0x7E or extended Latin-1 0x89+).
                     * We skip 0x80-0x88 because those are our KEY_UP/DOWN/etc. constants.
                     *
                     * BUG THAT WAS HERE: buf_insert shifts the buffer right but does NOT
                     * advance ed.cursor — so every new character inserted at position 0,
                     * making text appear in reverse order.
                     * FIX: ed.cursor++ after every insert.
                     */
                    buf_insert(ed.cursor, (char)key);
                    ed.cursor++;  /* ← THE FIX: advance past the inserted character */
                }
                break;

            case MODE_COMMAND:
                /*
                 * COMMAND MODE:
                 * - Enter → execute the command
                 * - Esc   → cancel, back to NORMAL
                 * - Backspace → delete last char of command
                 * - Printable → append to command buffer
                 */
                if (key == '\n' || key == '\r') {
                    ed.mode = MODE_NORMAL;
                    quit = handle_command();
                    ed.cmd[0]  = '\0';
                    ed.cmd_len = 0;
                } else if (key == 0x1B) {
                    ed.mode    = MODE_NORMAL;
                    ed.cmd[0]  = '\0';
                    ed.cmd_len = 0;
                    strncpy(ed.msg, "cancelled", sizeof(ed.msg) - 1);
                } else if (key == '\b') {
                    if (ed.cmd_len > 0) {
                        ed.cmd_len--;
                        ed.cmd[ed.cmd_len] = '\0';
                    } else {
                        /* Backspace on empty command: cancel */
                        ed.mode = MODE_NORMAL;
                    }
                } else if (key >= 0x20 && key < 0x80 && ed.cmd_len < 79) {
                    ed.cmd[ed.cmd_len++] = (char)key;
                    ed.cmd[ed.cmd_len]   = '\0';
                }
                break;
        }
    }

    /* Restore the normal terminal screen after exiting */
    vga_clear();
}
