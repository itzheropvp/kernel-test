/*
 * disk_fs.c - Filesystem Persistence Layer
 *
 * Converts between in-memory fs_node_t structures and on-disk records.
 */

#include "disk_fs.h"
#include "fs.h"
#include "../drivers/ata.h"
#include "../libc/string.h"

/* ============================================================
 * ON-DISK STRUCTURES
 * ============================================================ */

/*
 * disk_superblock_t - Sector 0 of our filesystem partition.
 *
 * Must fit in exactly 512 bytes (one sector).
 * We use explicit padding to guarantee the size.
 */
typedef struct {
    uint32_t magic;       /* DISK_FS_MAGIC = 0x4B4F5346 ('KOSF') */
    uint32_t version;     /* DISK_FS_VERSION = 1                  */
    uint32_t root_idx;    /* Which node_pool index is the root?   */
    uint32_t node_count;  /* Total node slots (= FS_MAX_NODES)    */
    uint8_t  _pad[496];   /* Pad to exactly 512 bytes             */
} PACKED disk_superblock_t;

/*
 * disk_node_t - On-disk representation of one filesystem node.
 *
 * Sized to exactly 9 sectors (4608 bytes) for aligned disk access.
 *
 * Key difference from fs_node_t:
 *   - No pointers! parent and children are stored as int32 indices.
 *   - -1 means NULL (no parent / empty child slot).
 */
typedef struct {
    uint8_t  in_use;                        /*    1 byte  */
    uint8_t  type;                          /*    1 byte  */
    uint8_t  _pad[2];                       /*    2 bytes — alignment */
    uint32_t size;                          /*    4 bytes */
    int32_t  parent_idx;                    /*    4 bytes */
    int32_t  children_idx[FS_MAX_CHILDREN]; /*   64 bytes (16 × int32) */
    char     name[FS_MAX_NAME];             /*   64 bytes */
    char     data[FS_MAX_FILE_SIZE];        /* 4096 bytes */
    uint8_t  _fill[372];                    /*  372 bytes — pad to 4608 */
} PACKED disk_node_t;

/* Verify our struct is exactly 9 sectors at compile time */
/* 1+1+2+4+4+64+64+4096+372 = 4608 = 9×512 */
#define SECTORS_PER_NODE  9
#define SUPERBLOCK_SECTOR DISK_FS_START_LBA
#define NODES_START_SECTOR (DISK_FS_START_LBA + 1)

/* Convenience: LBA of node i's first sector */
#define NODE_LBA(i)  (NODES_START_SECTOR + (i) * SECTORS_PER_NODE)

/* ============================================================
 * POINTER ↔ INDEX CONVERSION
 *
 * We access node_pool[] from fs.c via the extern declaration.
 * The array is no longer static in fs.c.
 * ============================================================ */
extern fs_node_t node_pool[FS_MAX_NODES];

/* Returns the index (0..63) of a node pointer, or -1 for NULL */
static int32_t ptr_to_idx(fs_node_t* node) {
    if (!node) return -1;
    return (int32_t)(node - node_pool);
}

/* Returns the node pointer for index, or NULL for -1 */
static fs_node_t* idx_to_ptr(int32_t idx) {
    if (idx < 0 || idx >= FS_MAX_NODES) return NULL;
    return &node_pool[idx];
}

/* ============================================================
 * MULTI-SECTOR READ / WRITE HELPERS
 * ============================================================ */

/*
 * write_sectors - Write 'count' consecutive sectors starting at 'lba'.
 *
 * @src:   source buffer (must be count * 512 bytes)
 * @lba:   starting logical block address
 * @count: number of sectors to write
 */
static bool write_sectors(const void* src, uint32_t lba, uint32_t count) {
    const uint8_t* p = (const uint8_t*)src;
    for (uint32_t i = 0; i < count; i++) {
        if (!ata_write_sector(lba + i, p)) return false;
        p += ATA_SECTOR_SIZE;
    }
    return true;
}

/*
 * read_sectors - Read 'count' consecutive sectors starting at 'lba'.
 *
 * @dst:   destination buffer
 * @lba:   starting logical block address
 * @count: number of sectors to read
 */
static bool read_sectors(void* dst, uint32_t lba, uint32_t count) {
    uint8_t* p = (uint8_t*)dst;
    for (uint32_t i = 0; i < count; i++) {
        if (!ata_read_sector(lba + i, p)) return false;
        p += ATA_SECTOR_SIZE;
    }
    return true;
}

/* ============================================================
 * DISK DETECTION
 * ============================================================ */

static bool disk_available = false;

bool disk_fs_available(void) {
    return disk_available;
}

bool disk_fs_detect(void) {
    /* First check if an ATA drive is even present */
    if (!ata_detect()) {
        disk_available = false;
        return false;
    }
    disk_available = true;

    /* Read the superblock and check the magic number */
    disk_superblock_t sb;
    if (!read_sectors(&sb, SUPERBLOCK_SECTOR, 1)) return false;

    return sb.magic == DISK_FS_MAGIC;
}

/* ============================================================
 * SAVE — serialize in-memory filesystem to disk
 * ============================================================ */

bool disk_fs_save(void) {
    if (!disk_available) return false;

    /* ---- Write superblock ---- */
    disk_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic      = DISK_FS_MAGIC;
    sb.version    = DISK_FS_VERSION;
    sb.root_idx   = (uint32_t)ptr_to_idx(fs_root);
    sb.node_count = FS_MAX_NODES;

    if (!write_sectors(&sb, SUPERBLOCK_SECTOR, 1)) return false;

    /* ---- Write each node ---- */
    disk_node_t dn;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        fs_node_t* n = &node_pool[i];

        memset(&dn, 0, sizeof(dn));
        dn.in_use     = n->in_use ? 1 : 0;
        dn.type       = n->type;
        dn.size       = n->size;
        dn.parent_idx = ptr_to_idx(n->parent);

        /* Convert child pointers to indices */
        for (int j = 0; j < FS_MAX_CHILDREN; j++) {
            dn.children_idx[j] = ptr_to_idx(n->children[j]);
        }

        memcpy(dn.name, n->name, FS_MAX_NAME);

        /* Only copy file data for actual files */
        if (n->type == FS_TYPE_FILE && n->size > 0) {
            memcpy(dn.data, n->data, n->size);
        }

        if (!write_sectors(&dn, NODE_LBA(i), SECTORS_PER_NODE)) return false;
    }

    return true;
}

/* ============================================================
 * LOAD — deserialize filesystem from disk into memory
 * ============================================================ */

bool disk_fs_load(void) {
    if (!disk_available) return false;

    /* ---- Read and verify superblock ---- */
    disk_superblock_t sb;
    if (!read_sectors(&sb, SUPERBLOCK_SECTOR, 1)) return false;

    if (sb.magic != DISK_FS_MAGIC) return false;
    if (sb.version != DISK_FS_VERSION) return false;

    /* ---- Clear in-memory node pool ---- */
    memset(node_pool, 0, sizeof(node_pool));

    /* ---- First pass: load all node data (no pointers yet) ---- */
    /* We use a temporary parallel array to hold the on-disk indices */
    static int32_t  saved_parent[FS_MAX_NODES];
    static int32_t  saved_children[FS_MAX_NODES][FS_MAX_CHILDREN];

    disk_node_t dn;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!read_sectors(&dn, NODE_LBA(i), SECTORS_PER_NODE)) return false;

        fs_node_t* n = &node_pool[i];
        n->in_use = dn.in_use;
        n->type   = dn.type;
        n->size   = dn.size;
        n->parent   = NULL;  /* will be resolved in second pass */

        memcpy(n->name, dn.name, FS_MAX_NAME);

        if (n->type == FS_TYPE_FILE && dn.size > 0) {
            memcpy(n->data, dn.data, dn.size);
        }

        /* Clear children pointers — resolved in second pass */
        for (int j = 0; j < FS_MAX_CHILDREN; j++) {
            n->children[j] = NULL;
        }

        /* Save the indices for the second pass */
        saved_parent[i] = dn.parent_idx;
        for (int j = 0; j < FS_MAX_CHILDREN; j++) {
            saved_children[i][j] = dn.children_idx[j];
        }
    }

    /* ---- Second pass: resolve indices back to pointers ---- */
    /*
     * WHY TWO PASSES?
     * On the first pass we read each node's data, but we can't resolve
     * child pointer X until node X has been loaded. The second pass
     * runs after ALL nodes are in memory, so every index is valid.
     */
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!node_pool[i].in_use) continue;

        node_pool[i].parent = idx_to_ptr(saved_parent[i]);

        for (int j = 0; j < FS_MAX_CHILDREN; j++) {
            node_pool[i].children[j] = idx_to_ptr(saved_children[i][j]);
        }
    }

    /* ---- Restore the global root pointer ---- */
    fs_root = idx_to_ptr((int32_t)sb.root_idx);
    if (!fs_root) return false;  /* Corrupt disk */

    return true;
}

/* ============================================================
 * FORMAT — write an empty filesystem to disk
 * ============================================================ */

bool disk_fs_format(void) {
    if (!disk_available) return false;

    /*
     * "Format" in our context means:
     *   1. Re-initialize the in-memory filesystem (fresh root + default dirs)
     *   2. Save it to disk
     *
     * This matches what happens when you format a USB drive:
     * the old data is still physically on disk but the directory
     * structure is reset to empty.
     */
    fs_init();              /* Re-create root, home, etc, tmp, bin */
    return disk_fs_save();  /* Write to disk */
}
