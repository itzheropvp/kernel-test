/*
 * fs.c - In-Memory Filesystem Implementation
 */

#include "fs.h"
#include "../libc/string.h"

/* ============================================================
 * NODE POOL
 *
 * Instead of using dynamic allocation (malloc), we pre-allocate
 * a fixed array of nodes. This is a common technique in embedded
 * systems and kernels where simplicity is valued over flexibility.
 *
 * Each node is either "in use" (in_use=true) or "free" (in_use=false).
 * ============================================================ */
/* Not static — disk_fs.c needs direct access to serialize/deserialize nodes */
fs_node_t node_pool[FS_MAX_NODES];

/* The root directory "/" — accessible everywhere */
fs_node_t* fs_root = NULL;

/* ============================================================
 * INITIALIZATION
 * ============================================================ */

void fs_init(void) {
    /* Clear the entire node pool */
    memset(node_pool, 0, sizeof(node_pool));

    /* Create the root directory */
    fs_root = fs_alloc_node();
    strcpy(fs_root->name, "/");
    fs_root->type   = FS_TYPE_DIR;
    fs_root->size   = 0;
    fs_root->parent = NULL;  /* Root has no parent */

    /* Create a default directory structure (like a minimal Linux /):
     * /home
     * /etc
     * /tmp
     * /bin
     */
    fs_node_t* home = fs_mkdir(fs_root, "home");
    fs_mkdir(fs_root, "etc");
    fs_mkdir(fs_root, "tmp");
    fs_node_t* bin  = fs_mkdir(fs_root, "bin");

    /* Create a welcome file */
    fs_node_t* motd = fs_touch(home, "welcome.txt");
    const char* msg =
        "Welcome to KernelOS!\n"
        "Type 'help' to see available commands.\n"
        "Type 'run hello.hero' to run a HeroScript.\n"
        "This filesystem lives in RAM - type 'save' to persist to disk.\n";
    fs_write(motd, msg, (uint32_t)strlen(msg));

    /* Create a sample HeroScript to demonstrate the language */
    fs_node_t* demo = fs_touch(bin, "hello.hero");
    const char* script =
        "# hello.hero - Sample HeroScript\n"
        "# Run this with:  run /bin/hello.hero\n"
        "# Or just type:   /bin/hello.hero\n"
        "\n"
        "set NAME KernelOS\n"
        "set COUNT 3\n"
        "\n"
        "echo ----------------------------\n"
        "echo Hello from HeroScript!\n"
        "echo Running on $NAME\n"
        "echo ----------------------------\n"
        "\n"
        "# Create a directory and file\n"
        "mkdir /tmp/demo\n"
        "touch /tmp/demo/notes\n"
        "write /tmp/demo/notes This file was created by HeroScript!\n"
        "\n"
        "# Check it worked\n"
        "if exists /tmp/demo/notes\n"
        "    echo File created successfully.\n"
        "    cat /tmp/demo/notes\n"
        "end\n"
        "\n"
        "# Loop example\n"
        "echo Counting $COUNT times:\n"
        "repeat $COUNT\n"
        "    beep 440 100\n"
        "end\n"
        "\n"
        "echo Done!\n";
    fs_write(demo, script, (uint32_t)strlen(script));
}

/* ============================================================
 * NODE ALLOCATION
 * ============================================================ */

fs_node_t* fs_alloc_node(void) {
    /* Linear scan through pool to find a free slot */
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!node_pool[i].in_use) {
            memset(&node_pool[i], 0, sizeof(fs_node_t));
            node_pool[i].in_use = true;
            return &node_pool[i];
        }
    }
    return NULL;  /* Pool full */
}

/* ============================================================
 * CHILD LOOKUP
 * ============================================================ */

fs_node_t* fs_find_child(fs_node_t* dir, const char* name) {
    if (!dir || dir->type != FS_TYPE_DIR) return NULL;

    for (uint32_t i = 0; i < dir->size; i++) {
        if (dir->children[i] && strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

/* ============================================================
 * PATH RESOLUTION
 *
 * Walks the filesystem tree one component at a time.
 *
 * Example: fs_resolve("/home/user/file", root)
 *   1. path starts with '/' → start at root
 *   2. component "home" → find child "home" of root
 *   3. component "user" → find child "user" of home
 *   4. component "file" → find child "file" of user
 *   5. return the "file" node
 * ============================================================ */

fs_node_t* fs_resolve(const char* path, fs_node_t* cwd) {
    if (!path || path[0] == '\0') return cwd;

    /* Make a mutable copy of the path so we can tokenize it */
    char buf[256];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Absolute paths start at root, relative paths start at cwd */
    fs_node_t* current = (buf[0] == '/') ? fs_root : cwd;

    /* Walk through path components separated by '/' */
    char* component = buf;
    if (buf[0] == '/') component++;  /* Skip leading '/' */

    while (*component) {
        /* Find end of this component */
        char* slash = strchr(component, '/');
        if (slash) *slash = '\0';  /* Temporarily terminate at the '/' */

        /* Handle special components */
        if (strcmp(component, ".") == 0) {
            /* "." = current directory, do nothing */
        } else if (strcmp(component, "..") == 0) {
            /* ".." = parent directory */
            if (current->parent != NULL) {
                current = current->parent;
            }
            /* If at root, stay at root */
        } else if (*component != '\0') {
            /* Normal component: find it as a child */
            fs_node_t* child = fs_find_child(current, component);
            if (!child) return NULL;  /* Not found */
            current = child;
        }

        /* Move to next component */
        if (slash) {
            component = slash + 1;
        } else {
            break;
        }
    }

    return current;
}

/* ============================================================
 * DIRECTORY OPERATIONS
 * ============================================================ */

fs_node_t* fs_mkdir(fs_node_t* parent, const char* name) {
    if (!parent || parent->type != FS_TYPE_DIR) return NULL;
    if (parent->size >= FS_MAX_CHILDREN) return NULL;
    if (strlen(name) >= FS_MAX_NAME) return NULL;

    /* Check if name already exists */
    if (fs_find_child(parent, name)) return NULL;

    /* Allocate a new node */
    fs_node_t* node = fs_alloc_node();
    if (!node) return NULL;

    strcpy(node->name, name);
    node->type   = FS_TYPE_DIR;
    node->size   = 0;
    node->parent = parent;

    /* Add to parent's children list */
    parent->children[parent->size++] = node;

    return node;
}

/* ============================================================
 * FILE OPERATIONS
 * ============================================================ */

fs_node_t* fs_touch(fs_node_t* parent, const char* name) {
    if (!parent || parent->type != FS_TYPE_DIR) return NULL;
    if (parent->size >= FS_MAX_CHILDREN) return NULL;
    if (strlen(name) >= FS_MAX_NAME) return NULL;

    /* Check if name already exists */
    if (fs_find_child(parent, name)) return NULL;

    fs_node_t* node = fs_alloc_node();
    if (!node) return NULL;

    strcpy(node->name, name);
    node->type   = FS_TYPE_FILE;
    node->size   = 0;
    node->parent = parent;

    parent->children[parent->size++] = node;

    return node;
}

int fs_write(fs_node_t* file, const char* content, uint32_t len) {
    if (!file || file->type != FS_TYPE_FILE) return -1;
    if (len > FS_MAX_FILE_SIZE) len = FS_MAX_FILE_SIZE;

    memcpy(file->data, content, len);
    file->size = len;
    return (int)len;
}

uint32_t fs_read(fs_node_t* file, char* buf, uint32_t buf_len) {
    if (!file || file->type != FS_TYPE_FILE) return 0;

    uint32_t to_read = file->size < buf_len ? file->size : buf_len;
    memcpy(buf, file->data, to_read);
    return to_read;
}

bool fs_remove(fs_node_t* node) {
    if (!node || node == fs_root) return false;
    if (node->type == FS_TYPE_DIR && node->size > 0) return false;  /* Not empty */

    fs_node_t* parent = node->parent;
    if (!parent) return false;

    /* Remove from parent's children array (shift remaining entries left) */
    for (uint32_t i = 0; i < parent->size; i++) {
        if (parent->children[i] == node) {
            /* Shift everything after i left by one */
            for (uint32_t j = i; j < parent->size - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->children[parent->size - 1] = NULL;
            parent->size--;
            break;
        }
    }

    /* Return node to pool */
    memset(node, 0, sizeof(fs_node_t));
    return true;
}

/* ============================================================
 * PATH STRING BUILDER
 *
 * Walk up from a node to the root to build the full path.
 * Uses a recursive approach: get parent's path first, then append name.
 * ============================================================ */

void fs_get_path(fs_node_t* node, char* buf, size_t buf_len) {
    if (!node || buf_len == 0) return;

    if (node == fs_root) {
        /* Special case: root's path is "/" */
        strncpy(buf, "/", buf_len);
        return;
    }

    /* Build path recursively */
    char parent_path[256];
    fs_get_path(node->parent, parent_path, sizeof(parent_path));

    /* Avoid double slashes (parent_path might already end in "/") */
    if (strcmp(parent_path, "/") == 0) {
        ksprintf(buf, "/%s", node->name);
    } else {
        ksprintf(buf, "%s/%s", parent_path, node->name);
    }
}

int fs_node_count(void) {
    int count = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (node_pool[i].in_use) count++;
    }
    return count;
}
