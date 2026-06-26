/*
 * fs.h - In-Memory Filesystem (Virtual Filesystem)
 *
 * WHAT IS THIS?
 *
 * A simple tree-based filesystem stored entirely in RAM.
 * No disk access — everything is lost when the machine reboots.
 * This is similar to Linux's tmpfs or Plan 9's ramfs.
 *
 * STRUCTURE:
 *
 * The filesystem is a tree of "nodes". Each node is either:
 *   - A DIRECTORY: contains references to child nodes
 *   - A FILE:      contains text data
 *
 * Example tree:
 *   /                   (root, directory)
 *   ├── home/           (directory)
 *   │   └── user/       (directory)
 *   │       └── notes   (file, content = "hello world")
 *   └── etc/            (directory)
 *       └── config      (file)
 *
 * DESIGN DECISIONS:
 *
 * We use a STATIC POOL of nodes (pre-allocated array) instead of
 * dynamic allocation. This avoids needing a full malloc() and makes
 * the implementation simpler. The downside is a fixed maximum.
 *
 * LIMITATIONS (intentional — to keep the code simple):
 *   - Max nodes: FS_MAX_NODES
 *   - Max filename length: FS_MAX_NAME
 *   - Max file size: FS_MAX_FILE_SIZE
 *   - Max children per directory: FS_MAX_CHILDREN
 *   - No permissions, timestamps, or other metadata
 *
 * PATHS:
 *   Absolute paths start with '/' (e.g., "/home/user/file")
 *   Relative paths are relative to the current working directory
 *   ".." goes up to the parent directory
 *   "."  refers to the current directory
 */

#ifndef FS_H
#define FS_H

#include "../kernel/kernel.h"

/* Filesystem limits */
#define FS_MAX_NODES    64    /* Maximum total number of files + directories */
#define FS_MAX_CHILDREN 16    /* Maximum files/dirs inside one directory      */
#define FS_MAX_NAME     64    /* Maximum length of a filename                 */
#define FS_MAX_FILE_SIZE 4096 /* Maximum file content size in bytes (4 KB)   */

/* Node type codes */
#define FS_TYPE_DIR  0
#define FS_TYPE_FILE 1

/*
 * fs_node_t - Represents one file or directory in the filesystem.
 *
 * Both files and directories share this same struct to keep things simple.
 * The 'type' field tells us which fields are relevant.
 */
typedef struct fs_node {
    char   name[FS_MAX_NAME];              /* Filename (not the full path)    */
    uint8_t type;                          /* FS_TYPE_DIR or FS_TYPE_FILE     */
    uint32_t size;                         /* Files: content length (bytes)   */
                                           /* Dirs:  number of children       */
    char   data[FS_MAX_FILE_SIZE];         /* File content (files only)       */
    struct fs_node* children[FS_MAX_CHILDREN]; /* Child nodes (dirs only)    */
    struct fs_node* parent;                /* Parent directory (NULL for root)*/
    bool   in_use;                         /* Is this slot used?              */
} fs_node_t;

/* ---- Init ---- */

/* Initialize the filesystem: set up the root directory "/" */
void fs_init(void);

/* ---- Node allocation ---- */

/* Allocate a new node from the pool. Returns NULL if pool is full. */
fs_node_t* fs_alloc_node(void);

/* ---- Path resolution ---- */

/*
 * fs_resolve - Resolve a path string to a node.
 *
 * @path: path string (absolute or relative)
 * @cwd:  current working directory node
 * Returns: pointer to the node, or NULL if not found
 */
fs_node_t* fs_resolve(const char* path, fs_node_t* cwd);

/* Get a child of a directory by name. NULL if not found. */
fs_node_t* fs_find_child(fs_node_t* dir, const char* name);

/* ---- Operations ---- */

/*
 * fs_mkdir - Create a new directory inside parent.
 * Returns the new node, or NULL on failure.
 */
fs_node_t* fs_mkdir(fs_node_t* parent, const char* name);

/*
 * fs_touch - Create a new empty file inside parent.
 * Returns the new node, or NULL on failure.
 */
fs_node_t* fs_touch(fs_node_t* parent, const char* name);

/*
 * fs_write - Write content to a file (overwrites existing content).
 * Returns number of bytes written, or -1 on failure.
 */
int fs_write(fs_node_t* file, const char* content, uint32_t len);

/*
 * fs_read - Read file content into buf.
 * Returns number of bytes read.
 */
uint32_t fs_read(fs_node_t* file, char* buf, uint32_t buf_len);

/*
 * fs_remove - Remove a file or empty directory.
 * Returns true on success, false if directory is not empty.
 */
bool fs_remove(fs_node_t* node);

/*
 * fs_get_path - Build the absolute path string for a node.
 * Writes into buf (max buf_len bytes).
 */
void fs_get_path(fs_node_t* node, char* buf, size_t buf_len);

/* ---- Global state ---- */

/* The root directory node */
extern fs_node_t* fs_root;

/*
 * node_pool - The backing array for all filesystem nodes.
 *
 * Exposed (non-static) so that disk_fs.c can serialize it to disk.
 * Do not access this directly from outside fs.c and disk_fs.c —
 * use the fs_* API functions instead.
 */
extern fs_node_t node_pool[FS_MAX_NODES];

/* Statistics */
int fs_node_count(void);

#endif /* FS_H */
