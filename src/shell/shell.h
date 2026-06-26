/*
 * shell.h - Interactive Command Shell
 *
 * WHAT IS A SHELL?
 *
 * A shell is the user interface to the OS — it reads commands from
 * the user, parses them, and calls the appropriate kernel functions.
 *
 * Famous shells: bash, zsh, sh, fish, cmd.exe, PowerShell
 *
 * OUR SHELL FEATURES:
 *   - Prompt showing current directory
 *   - Command parsing (splits input by spaces)
 *   - Built-in commands (no external processes — we have no process management yet)
 *   - Up to FS_MAX_ARGS arguments per command
 *
 * IMPLEMENTED COMMANDS:
 *   help           — list available commands
 *   clear          — clear the terminal screen
 *   echo [text]    — print text to the terminal
 *   pwd            — print working directory
 *   ls [path]      — list directory contents
 *   cd [path]      — change directory
 *   mkdir <name>   — create directory
 *   touch <name>   — create empty file
 *   cat <file>     — display file contents
 *   write <f> <t>  — write text to file
 *   rm <name>      — remove file or empty directory
 *   uname          — show kernel information
 *   free           — show memory usage
 *   halt           — power off
 */

#ifndef SHELL_H
#define SHELL_H

#include "../kernel/kernel.h"
#include "../fs/fs.h"

/* Maximum command line length */
#define SHELL_INPUT_MAX  256

/* Maximum number of arguments (including command name) */
#define SHELL_ARGS_MAX   16

/*
 * shell_run - Start the interactive shell.
 *
 * This function never returns — it loops forever reading and
 * executing commands until the user types 'halt'.
 */
void shell_run(void);

/*
 * shell_exec_line - Execute a single command line string.
 *
 * WHY THIS IS PUBLIC:
 *   The HeroScript interpreter (script.c) needs to call shell commands.
 *   Rather than duplicating the dispatch logic, we expose it here so
 *   scripts can run any built-in command by name.
 *
 *   Example: script calls shell_exec_line("mkdir /home/test")
 *            → exactly the same as the user typing it.
 */
void shell_exec_line(const char* line);

/*
 * shell_get_cwd - Return the shell's current working directory node.
 *
 * WHY THIS IS PUBLIC:
 *   The script interpreter needs to resolve relative paths (like
 *   "exists config.hero") using the same cwd the shell tracks.
 */
fs_node_t* shell_get_cwd(void);

#endif /* SHELL_H */
