/*
 * script.c - HeroScript Interpreter Implementation
 *
 * ARCHITECTURE OVERVIEW:
 *
 *   shell_exec_line()          ← user types a command
 *         │
 *         ▼  (when user types "run foo.hero" or just "foo.hero")
 *   cmd_run()                  ← reads the file from the filesystem
 *         │
 *         ▼
 *   script_run()               ← THIS FILE: interprets .hero scripts
 *         │
 *         ▼  (for any non-keyword line, e.g. "echo hello", "mkdir /tmp")
 *   shell_exec_line()          ← reuses the shell's command dispatcher
 *
 * This circular call (shell → script → shell) is intentional and safe:
 *   - shell calls script for .hero files
 *   - script calls shell for individual commands
 *   - "run other.hero" inside a script causes shell to call script again
 *   - We limit nesting with a depth counter (SCRIPT_MAX_CALL_DEPTH)
 */

#include "script.h"
#include "../shell/shell.h"     /* shell_exec_line, shell_get_cwd */
#include "../fs/fs.h"           /* fs_resolve, fs_root            */
#include "../mm/memory.h"       /* kmalloc                        */
#include "../libc/string.h"     /* strlen, strcmp, strncpy, atoi  */
#include "../drivers/vga.h"     /* vga_print, vga_printf          */
#include "../kernel/kernel.h"   /* bool, NULL                     */

/* ============================================================
 * LIMITS
 *
 * These constants are conservatively sized.
 * WHY CONSERVATIVE?
 * Our kernel stack is only 16 KB (set in boot.asm).
 * Each function call level uses stack space for local variables.
 * If we allocate too much per level, we get a stack overflow —
 * which in a kernel means a triple fault and immediate reset.
 * ============================================================ */

#define SCRIPT_MAX_LINES      128   /* max lines per script file    */
#define SCRIPT_MAX_VARS       16    /* max variables per script     */
#define SCRIPT_VAR_NAME_LEN   16    /* max variable name length     */
#define SCRIPT_VAR_VAL_LEN    128   /* max variable value length    */
#define SCRIPT_MAX_DEPTH      6     /* max nested if/repeat blocks  */
#define SCRIPT_MAX_CALL_DEPTH 4     /* max script-calls-script nesting */
#define SCRIPT_MAX_LINE_LEN   256   /* max length of one line       */
#define SCRIPT_MAX_TOKS       16    /* max tokens per line          */

/* ============================================================
 * DATA STRUCTURES
 * ============================================================ */

/*
 * A single script variable.
 * All variables are strings — like in bash, there's no int or bool type.
 * When a number is needed (e.g. repeat $N), we convert with atoi().
 */
typedef struct {
    char name[SCRIPT_VAR_NAME_LEN];
    char val[SCRIPT_VAR_VAL_LEN];
} hs_var_t;

/*
 * Control flow stack frame.
 *
 * When the interpreter hits 'if' or 'repeat', it pushes one of these.
 * When it hits 'end', it pops (and for repeat, maybe jumps back).
 *
 * This is exactly how a simple interpreter manages control flow:
 * a stack of "what block am I in?" contexts.
 *
 * FRAME_IF:
 *   skip = true  → we're inside a false branch, skip until 'end'
 *   skip = false → condition was true, execute normally
 *
 * FRAME_REPEAT:
 *   top_line  = first line of loop body (line AFTER 'repeat')
 *   remaining = iterations left (counts down to 0)
 *   skip      = true if N <= 0 (skip the whole block)
 */
#define FRAME_IF     0
#define FRAME_REPEAT 1

typedef struct {
    int  type;       /* FRAME_IF or FRAME_REPEAT                      */
    int  top_line;   /* repeat: line index to jump back to            */
    int  remaining;  /* repeat: iterations still to execute           */
    bool skip;       /* true = skip until matching 'end'              */
} hs_frame_t;

/* ============================================================
 * CALL DEPTH GUARD
 *
 * Prevents infinite recursion when a script calls 'run' on itself
 * or when two scripts call each other in a cycle.
 * ============================================================ */
static int call_depth = 0;

/* ============================================================
 * VARIABLE TABLE HELPERS
 * ============================================================ */

static hs_var_t* var_find(hs_var_t* vars, int n, const char* name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(vars[i].name, name) == 0)
            return &vars[i];
    }
    return NULL;
}

static void var_set(hs_var_t* vars, int* n, const char* name, const char* val) {
    /* Update existing variable if it already exists */
    hs_var_t* v = var_find(vars, *n, name);
    if (v) {
        strncpy(v->val, val, SCRIPT_VAR_VAL_LEN - 1);
        v->val[SCRIPT_VAR_VAL_LEN - 1] = '\0';
        return;
    }
    /* No room: silently drop */
    if (*n >= SCRIPT_MAX_VARS) return;
    strncpy(vars[*n].name, name, SCRIPT_VAR_NAME_LEN - 1);
    vars[*n].name[SCRIPT_VAR_NAME_LEN - 1] = '\0';
    strncpy(vars[*n].val, val, SCRIPT_VAR_VAL_LEN - 1);
    vars[*n].val[SCRIPT_VAR_VAL_LEN - 1] = '\0';
    (*n)++;
}

static const char* var_get(hs_var_t* vars, int n, const char* name) {
    hs_var_t* v = var_find(vars, n, name);
    return v ? v->val : "";
}

/* ============================================================
 * VARIABLE EXPANSION
 *
 * Scans `src` character by character.
 * When it sees '$', it reads the following identifier (letters,
 * digits, underscores), looks it up in the variable table, and
 * inserts its value into `dst`.
 *
 * Example:
 *   vars: NAME="KernelOS"
 *   src:  "Hello from $NAME!"
 *   dst:  "Hello from KernelOS!"
 *
 * This is how bash and all shells implement $VARIABLE substitution.
 * ============================================================ */
static void expand_vars(const char* src, char* dst, int dst_size,
                        hs_var_t* vars, int n_vars) {
    int di = 0;
    while (*src && di < dst_size - 1) {
        if (*src == '$') {
            src++;  /* skip the '$' */

            /* Collect the variable name (identifier characters) */
            char vname[SCRIPT_VAR_NAME_LEN] = {0};
            int  vi = 0;
            while (*src && vi < SCRIPT_VAR_NAME_LEN - 1 &&
                   ((*src >= 'a' && *src <= 'z') ||
                    (*src >= 'A' && *src <= 'Z') ||
                    (*src >= '0' && *src <= '9') ||
                     *src == '_')) {
                vname[vi++] = *src++;
            }
            vname[vi] = '\0';

            /* Look up and copy the value */
            const char* val = var_get(vars, n_vars, vname);
            while (*val && di < dst_size - 1)
                dst[di++] = *val++;
        } else {
            dst[di++] = *src++;
        }
    }
    dst[di] = '\0';
}

/* ============================================================
 * TOKENIZER
 *
 * Splits a string by spaces/tabs, returning an array of pointers.
 * Modifies the string in-place (replaces delimiters with '\0').
 *
 * Like parse_line() in shell.c — we duplicate it here so script.c
 * doesn't depend on a private shell function.
 * ============================================================ */
static int tokenize(char* s, char* toks[], int max_toks) {
    int n = 0;
    while (*s && n < max_toks) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        toks[n++] = s;
        while (*s && *s != ' ' && *s != '\t') s++;
        if (*s) *s++ = '\0';
    }
    return n;
}

/* ============================================================
 * CONDITION EVALUATOR
 *
 * Evaluates the condition that follows 'if'.
 * `parts` is the tokenized remainder of the line after "if".
 *
 * Supported forms:
 *   exists <path>     → check if filesystem node exists
 *   eq     <a> <b>   → string equality (after var expansion)
 *   neq    <a> <b>   → string inequality
 *
 * Note: paths are resolved relative to the shell's current directory,
 * using shell_get_cwd(). Absolute paths (starting with '/') always work.
 * ============================================================ */
static bool eval_condition(char** parts, int n,
                           hs_var_t* vars, int n_vars) {
    if (n < 1) return false;

    char a[SCRIPT_VAR_VAL_LEN];
    char b[SCRIPT_VAR_VAL_LEN];

    if (strcmp(parts[0], "exists") == 0 && n >= 2) {
        expand_vars(parts[1], a, sizeof(a), vars, n_vars);
        /* Try resolving from cwd first, then from root */
        fs_node_t* base = shell_get_cwd();
        if (!base) base = fs_root;
        return fs_resolve(a, base) != NULL;
    }

    if (strcmp(parts[0], "eq") == 0 && n >= 3) {
        expand_vars(parts[1], a, sizeof(a), vars, n_vars);
        expand_vars(parts[2], b, sizeof(b), vars, n_vars);
        return strcmp(a, b) == 0;
    }

    if (strcmp(parts[0], "neq") == 0 && n >= 3) {
        expand_vars(parts[1], a, sizeof(a), vars, n_vars);
        expand_vars(parts[2], b, sizeof(b), vars, n_vars);
        return strcmp(a, b) != 0;
    }

    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_printf("  script: unknown condition '%s'\n", parts[0]);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    return false;
}

/* ============================================================
 * MAIN INTERPRETER
 * ============================================================ */

int script_run(const char* content, int size) {
    (void)size;  /* used for safety but content is null-terminated */

    /* ---- Depth guard ---- */
    if (call_depth >= SCRIPT_MAX_CALL_DEPTH) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  script: max nesting depth reached (circular run?)\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        return -1;
    }
    call_depth++;

    /*
     * STEP 1: Copy content into a mutable heap buffer.
     *
     * We need a mutable copy because:
     *   - The original `content` is in a filesystem node (we shouldn't mutate it)
     *   - We split lines by replacing '\n' with '\0' (in-place modification)
     *
     * WHY HEAP (kmalloc) AND NOT STACK?
     *   A script file can be up to 4096 bytes (FS_MAX_FILE_SIZE).
     *   Allocating 4096 bytes on the kernel stack per nesting level
     *   would quickly overflow our 16 KB stack. Heap is safer.
     *
     * LIMITATION: our bump allocator never frees memory. For a learning
     * kernel this is acceptable — in production you'd use a real allocator.
     */
    int len = strlen(content);
    if (len == 0) { call_depth--; return 0; }

    char* buf = (char*)kmalloc(len + 1);
    if (!buf) {
        vga_set_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        vga_print("  script: out of memory\n");
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
        call_depth--;
        return -1;
    }
    memcpy(buf, content, len + 1);

    /*
     * STEP 2: Split the buffer into line pointers.
     *
     * We replace '\n' with '\0' and store a pointer to the start
     * of each line. This is O(n) and avoids any extra allocations.
     *
     * Visual example:
     *   Before: "echo hi\nmkdir /tmp\n"
     *   After:  "echo hi\0mkdir /tmp\0"
     *   line_ptrs[0] → "echo hi"
     *   line_ptrs[1] → "mkdir /tmp"
     */
    char* line_ptrs[SCRIPT_MAX_LINES];
    int   n_lines = 0;

    char* p = buf;
    while (*p && n_lines < SCRIPT_MAX_LINES) {
        line_ptrs[n_lines++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = '\0';
    }

    /* ---- Per-script state (all on the stack) ---- */
    hs_var_t  vars[SCRIPT_MAX_VARS];
    int       n_vars = 0;
    hs_frame_t stack[SCRIPT_MAX_DEPTH];
    int       stack_top = 0;
    int       skip_depth = 0;  /* nested blocks inside a skipped region */

    memset(vars,  0, sizeof(vars));
    memset(stack, 0, sizeof(stack));

    /*
     * STEP 3: Execute line by line.
     *
     * `i` is the current line index.
     * For 'repeat', we set i = frame.top_line to loop back.
     */
    int i = 0;
    while (i < n_lines) {
        char* raw = line_ptrs[i];

        /* Strip leading whitespace (indented lines are fine) */
        while (*raw == ' ' || *raw == '\t') raw++;

        /* Skip blank lines and comments */
        if (*raw == '\0' || *raw == '#') { i++; continue; }

        /*
         * ARE WE CURRENTLY SKIPPING?
         *
         * When an 'if' condition is false, we set skip=true on the frame.
         * We still need to parse the lines to find the matching 'end',
         * but we DON'T execute them.
         *
         * We track 'skip_depth' for nested if/repeat inside the skipped
         * region, so we know which 'end' belongs to OUR block.
         *
         * Example of why skip_depth is needed:
         *   if eq 1 0         ← false, skip=true
         *     repeat 3        ← skip_depth = 1 (nested block)
         *       echo no       ← skipped
         *     end             ← skip_depth = 0 (closes repeat, not if)
         *   end               ← skip_depth = 0 → this is OUR end, pop IF
         */
        bool skipping = (stack_top > 0 && stack[stack_top - 1].skip);

        if (skipping) {
            if (strncmp(raw, "if ", 3) == 0 || strcmp(raw, "if") == 0 ||
                strncmp(raw, "repeat ", 7) == 0 || strcmp(raw, "repeat") == 0) {
                /* Nested block inside the skip region */
                skip_depth++;
            } else if (strcmp(raw, "end") == 0) {
                if (skip_depth > 0) {
                    skip_depth--;
                } else {
                    /* This 'end' closes OUR skipped block */
                    stack_top--;
                }
            }
            i++;
            continue;
        }

        /*
         * VARIABLE EXPANSION
         *
         * Before processing any line, replace all $NAME references
         * with their current values. The result goes into `expanded`.
         *
         * We expand into a local buffer so the original line_ptrs[i]
         * is unchanged (needed when repeat jumps back to re-execute).
         */
        char expanded[SCRIPT_MAX_LINE_LEN];
        expand_vars(raw, expanded, sizeof(expanded), vars, n_vars);

        /* Tokenize the expanded line for keyword detection */
        char  tok_buf[SCRIPT_MAX_LINE_LEN];
        char* toks[SCRIPT_MAX_TOKS];
        strncpy(tok_buf, expanded, sizeof(tok_buf) - 1);
        tok_buf[sizeof(tok_buf) - 1] = '\0';
        int n_toks = tokenize(tok_buf, toks, SCRIPT_MAX_TOKS);

        if (n_toks == 0) { i++; continue; }

        const char* kw = toks[0];

        /* ============================================================
         * KEYWORD: set NAME value...
         *
         * Sets a variable. The value is everything after the name,
         * taken from the EXPANDED line (so $VAR inside the value works).
         *
         * Example:
         *   set NAME KernelOS
         *   set GREETING Hello $NAME   → GREETING = "Hello KernelOS"
         * ============================================================ */
        if (strcmp(kw, "set") == 0 && n_toks >= 2) {
            const char* vname = toks[1];

            /* Extract value from expanded string: skip "set NAME " */
            const char* val_ptr = expanded;
            /* skip "set" */
            while (*val_ptr && *val_ptr != ' ') val_ptr++;
            while (*val_ptr == ' ') val_ptr++;
            /* skip NAME */
            while (*val_ptr && *val_ptr != ' ') val_ptr++;
            while (*val_ptr == ' ') val_ptr++;

            var_set(vars, &n_vars, vname, val_ptr);
            i++;
            continue;
        }

        /* ============================================================
         * KEYWORD: if <condition>
         *
         * Evaluates the condition. If true, execute the block normally.
         * If false, push a frame with skip=true — lines until 'end'
         * will be scanned but not executed.
         * ============================================================ */
        if (strcmp(kw, "if") == 0) {
            if (stack_top >= SCRIPT_MAX_DEPTH) {
                vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                vga_print("  script: too deeply nested\n");
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                i++;
                continue;
            }
            bool cond = eval_condition(toks + 1, n_toks - 1, vars, n_vars);
            stack[stack_top].type      = FRAME_IF;
            stack[stack_top].skip      = !cond;
            stack[stack_top].top_line  = 0;
            stack[stack_top].remaining = 0;
            stack_top++;
            i++;
            continue;
        }

        /* ============================================================
         * KEYWORD: repeat N
         *
         * Push a REPEAT frame.
         * `top_line` = the line AFTER this 'repeat' line (body start).
         * `remaining` = N (count down to 0).
         *
         * At the matching 'end': decrement remaining. If > 0, jump back
         * to top_line. If 0, pop the frame and continue.
         *
         * HOW REPEAT WORKS STEP BY STEP (N=2):
         *   line 5: repeat 2  → push {top=6, remaining=2}
         *   line 6: echo hi   → executed
         *   line 7: end       → remaining=2-1=1 > 0, jump to line 6
         *   line 6: echo hi   → executed again
         *   line 7: end       → remaining=1-1=0, pop frame, go to line 8
         * ============================================================ */
        if (strcmp(kw, "repeat") == 0) {
            if (stack_top >= SCRIPT_MAX_DEPTH) {
                vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                vga_print("  script: too deeply nested\n");
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                i++;
                continue;
            }
            int n = (n_toks >= 2) ? atoi(toks[1]) : 0;
            stack[stack_top].type      = FRAME_REPEAT;
            stack[stack_top].top_line  = i + 1;
            stack[stack_top].remaining = n;
            stack[stack_top].skip      = (n <= 0);  /* skip if 0 or negative */
            stack_top++;
            i++;
            continue;
        }

        /* ============================================================
         * KEYWORD: end
         *
         * Closes the innermost if or repeat block.
         * ============================================================ */
        if (strcmp(kw, "end") == 0) {
            if (stack_top == 0) {
                vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                vga_print("  script: 'end' without matching 'if' or 'repeat'\n");
                vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
                i++;
                continue;
            }

            hs_frame_t* f = &stack[stack_top - 1];

            if (f->type == FRAME_REPEAT) {
                f->remaining--;
                if (f->remaining > 0) {
                    /* Jump back to start of loop body */
                    i = f->top_line;
                    continue;  /* Note: no i++ here! */
                }
            }

            /* Done with this block: pop it */
            stack_top--;
            i++;
            continue;
        }

        /* ============================================================
         * KEYWORD: exit
         *
         * Stop this script early. Like 'exit' in bash.
         * ============================================================ */
        if (strcmp(kw, "exit") == 0) {
            break;
        }

        /* ============================================================
         * EVERYTHING ELSE: delegate to the shell
         *
         * Any line that isn't a scripting keyword is treated as a
         * shell command. We pass the already-expanded string.
         *
         * This means ALL shell commands work in scripts:
         *   echo, mkdir, touch, cat, write, beep, save, load, etc.
         * ============================================================ */
        shell_exec_line(expanded);
        i++;
    }

    call_depth--;
    return 0;
}
