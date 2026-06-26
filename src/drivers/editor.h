/*
 * editor.h - VIM-like Modal Text Editor
 *
 * MODES (just like real VIM):
 *
 *   NORMAL mode  — default on open; navigate and issue commands
 *   INSERT mode  — type text; press Esc to return to NORMAL
 *   COMMAND mode — after pressing ':'; type :w :q :wq then Enter
 *
 * KEY BINDINGS (NORMAL mode):
 *
 *   h / ←      move left
 *   l / →      move right
 *   j / ↓      move down
 *   k / ↑      move up
 *   0          start of line
 *   $          end of line
 *   gg         go to first line
 *   G          go to last line
 *   w          next word
 *   b          previous word
 *   x          delete char under cursor
 *   dd         delete (and yank) current line
 *   yy         yank (copy) current line
 *   p          paste yanked line after current line
 *   P          paste yanked line before current line
 *   i          enter INSERT before cursor
 *   a          enter INSERT after cursor
 *   A          enter INSERT at end of line
 *   o          open new line below and enter INSERT
 *   O          open new line above and enter INSERT
 *   :          enter COMMAND mode
 *
 * COMMAND mode (:):
 *   :w         save file
 *   :q         quit (error if unsaved changes)
 *   :q!        quit without saving (force)
 *   :wq  :x    save and quit
 *
 * INSERT mode:
 *   Any char   insert at cursor
 *   Backspace  delete char before cursor
 *   Arrow keys move cursor
 *   Esc        return to NORMAL mode
 */

#ifndef EDITOR_H
#define EDITOR_H

#include "../fs/fs.h"

/*
 * editor_open - Open a file in the VIM-like editor.
 *
 * @node:     filesystem node to edit (must be FS_TYPE_FILE)
 * @filename: display name shown in the status bar
 *
 * Blocks until the user quits (:q or :wq).
 * If the user saves (:w or :wq), writes back to node via fs_write().
 * Returns to the shell prompt when done.
 */
void editor_open(fs_node_t* node, const char* filename);

#endif /* EDITOR_H */
