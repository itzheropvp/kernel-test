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

#endif /* SHELL_H */
