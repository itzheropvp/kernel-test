/*
 * disk_fs.h - Persistent Filesystem (save/load to ATA disk)
 *
 * WHAT THIS DOES:
 *
 * Our in-memory filesystem (fs.c) stores everything in RAM — it vanishes
 * on every reboot. This layer adds persistence: it serializes the node
 * tree to disk sectors and restores it on the next boot.
 *
 * DISK LAYOUT (on the raw data disk, disk.img):
 *
 *   Sector 0          — Superblock (magic + metadata)
 *   Sectors 1 – 576   — Node data: 64 nodes × 9 sectors each
 *
 *   Total needed: 577 sectors × 512 bytes = ~295 KB
 *   Our disk.img is 2 MB, so there's plenty of room.
 *
 * WHY INDICES INSTEAD OF POINTERS?
 *
 * On disk we can't store memory addresses (pointers) because the kernel
 * is loaded at a different address each time, and even if it weren't,
 * a pointer saved from one boot is meaningless on the next boot.
 *
 * Instead, we replace every pointer with the INDEX of the node in the
 * node_pool[] array (0..63). -1 means NULL.
 *
 *   ptr  → idx:  idx = node - node_pool     (pointer arithmetic)
 *   idx  → ptr:  ptr = &node_pool[idx]      (array indexing)
 *
 * ON-DISK NODE RECORD (disk_node_t, exactly 9 sectors = 4608 bytes):
 *
 *   in_use          1 byte   — is this slot occupied?
 *   type            1 byte   — FS_TYPE_DIR or FS_TYPE_FILE
 *   _padding        2 bytes  — align next field to 4 bytes
 *   size            4 bytes  — file: content length; dir: child count
 *   parent_idx      4 bytes  — index of parent node (-1 = root)
 *   children_idx   64 bytes  — 16 × int32 child indices (-1 = empty)
 *   name           64 bytes  — filename string
 *   data         4096 bytes  — file content (unused for dirs)
 *   _fill         372 bytes  — padding to reach 4608 = 9×512
 */

#ifndef DISK_FS_H
#define DISK_FS_H

#include "../kernel/kernel.h"

/* Magic number in the superblock: "KOSF" = KernelOS FileSystem */
#define DISK_FS_MAGIC    0x4B4F5346
#define DISK_FS_VERSION  1

/* LBA address where our filesystem data starts on disk */
#define DISK_FS_START_LBA  0

/*
 * disk_fs_detect - Check whether the disk is present and formatted.
 *
 * Returns true if:
 *   a) An ATA disk is detected
 *   b) Sector 0 contains our magic number (previously formatted)
 */
bool disk_fs_detect(void);

/*
 * disk_fs_format - Write an empty (freshly initialized) filesystem to disk.
 *
 * Use this when first setting up the disk, or when you want to wipe it.
 * Saves the current in-memory filesystem state.
 *
 * Returns true on success.
 */
bool disk_fs_format(void);

/*
 * disk_fs_save - Serialize the current in-memory filesystem to disk.
 *
 * Call this to persist changes (e.g., before halt, or on demand with 'save').
 * Returns true on success.
 */
bool disk_fs_save(void);

/*
 * disk_fs_load - Deserialize the filesystem from disk into memory.
 *
 * Overwrites the current in-memory filesystem!
 * Call this on boot if disk_fs_detect() returns true.
 * Returns true on success.
 */
bool disk_fs_load(void);

/* Returns true if a disk was detected at boot */
bool disk_fs_available(void);

#endif /* DISK_FS_H */
