/*
 * script.h - HeroScript Interpreter
 *
 * WHAT IS HEROSCRIPT?
 *
 * HeroScript (.hero) is a simple scripting language for KernelOS.
 * It works similarly to bash shell scripts, but designed for this kernel.
 *
 * LANGUAGE REFERENCE:
 *
 *   # This is a comment
 *
 *   # Variables: set <NAME> <value>
 *   set NAME KernelOS
 *   set COUNT 3
 *
 *   # Use variables anywhere with $NAME
 *   echo Hello from $NAME
 *
 *   # Conditionals
 *   if exists /home
 *       echo home directory found
 *   end
 *
 *   if eq $NAME KernelOS
 *       echo running on KernelOS
 *   end
 *
 *   if neq $COUNT 0
 *       echo count is not zero
 *   end
 *
 *   # Loops: repeat N times
 *   repeat 3
 *       echo looping
 *   end
 *
 *   # Loops with variable
 *   repeat $COUNT
 *       echo iteration
 *   end
 *
 *   # Run another HeroScript file
 *   run /bin/other.hero
 *
 *   # Run any shell command
 *   mkdir /home/scripts
 *   touch /home/scripts/notes
 *   write /home/scripts/notes hello world
 *   beep 440 200
 *
 *   # Stop execution early
 *   exit
 *
 * CONDITION TYPES (after 'if'):
 *   exists <path>     — true if the path exists in the filesystem
 *   eq <a> <b>        — true if a equals b (after variable expansion)
 *   neq <a> <b>       — true if a does NOT equal b
 *
 * EXTENSION: .hero
 *   We chose .hero because .hs is already used by Haskell, and .hero is
 *   unique to this project. KernelOS treats any .hero filename as a script.
 */

#ifndef SCRIPT_H
#define SCRIPT_H

/*
 * script_run - Execute a HeroScript program from a string buffer.
 *
 * @content:  null-terminated script text (file contents)
 * @size:     length of content (used for bounds checking)
 *
 * Returns 0 on success, -1 on fatal error (e.g. too deeply nested).
 *
 * HOW IT WORKS (big picture):
 *   1. Split content into lines
 *   2. For each line: expand $VARIABLES, then:
 *      - If it's a scripting keyword (set/if/repeat/end/exit): handle it
 *      - Otherwise: call shell_exec_line() — same as typing it interactively
 *   3. Control flow (if/repeat/end) is tracked on a small stack
 */
int script_run(const char* content, int size);

#endif /* SCRIPT_H */
