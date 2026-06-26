/*
 * ata.h - ATA/IDE Disk Driver (PIO Mode)
 *
 * WHAT IS ATA?
 *
 * ATA (Advanced Technology Attachment), also called IDE (Integrated Drive
 * Electronics), is the classic standard for connecting hard disks and
 * CD-ROMs to a PC. Even though modern PCs use SATA/NVMe, QEMU emulates
 * an ATA controller — making it perfect for learning.
 *
 * HOW ATA WORKS:
 *
 * The ATA controller exposes two "buses":
 *   Primary   bus: ports 0x1F0–0x1F7, IRQ 14
 *   Secondary bus: ports 0x170–0x177, IRQ 15
 *
 * Each bus has two drives:
 *   Master (drive 0)
 *   Slave  (drive 1)
 *
 * We use Primary Master (the first hard disk QEMU sees).
 *
 * ADDRESSING — LBA28:
 *
 * LBA (Logical Block Addressing) gives every 512-byte sector a unique
 * 28-bit number starting from 0. We just say "read sector 42" and the
 * controller figures out which cylinder/head/sector that maps to.
 * Much simpler than the old CHS (Cylinder-Head-Sector) scheme.
 *
 * PIO vs DMA:
 *
 * PIO (Programmed I/O) = the CPU reads/writes every byte.
 *   Pros: simple, no setup
 *   Cons: CPU is busy 100% of the time during disk I/O
 *
 * DMA (Direct Memory Access) = a DMA controller transfers data independently.
 *   Pros: CPU is free during transfer
 *   Cons: more complex to set up
 *
 * We use PIO because it's simpler to learn. Real OSes use DMA.
 *
 * PRIMARY ATA BUS PORT MAP:
 *   Base = 0x1F0
 *   0x1F0  Data register        (16-bit! read/write 256 words per sector)
 *   0x1F1  Error (read) / Features (write)
 *   0x1F2  Sector count         (how many sectors to transfer)
 *   0x1F3  LBA low  (bits 0-7)
 *   0x1F4  LBA mid  (bits 8-15)
 *   0x1F5  LBA high (bits 16-23)
 *   0x1F6  Drive/Head select:
 *            Bit 7: always 1
 *            Bit 6: 1 = LBA mode
 *            Bit 5: always 1
 *            Bit 4: 0 = master, 1 = slave
 *            Bits 3-0: LBA bits 24-27
 *   0x1F7  Status (read) / Command (write)
 *   0x3F6  Alternate status / Device control (write: bit 2 = SRST)
 */

#ifndef ATA_H
#define ATA_H

#include "../kernel/kernel.h"

/* Sector size in bytes (always 512 for classic ATA) */
#define ATA_SECTOR_SIZE 512

/*
 * ata_detect - Check if a disk is present on the primary master.
 *
 * Returns true if a disk was found and responded to IDENTIFY,
 * false if the bus is floating (no disk).
 *
 * Call this before using ata_read/ata_write.
 */
bool ata_detect(void);

/*
 * ata_read_sector - Read one 512-byte sector from disk into buf.
 *
 * @lba: Logical Block Address (sector number, 0-based)
 * @buf: destination buffer (must be at least 512 bytes)
 *
 * Returns true on success, false on error.
 * Blocks (polls) until the disk is ready.
 */
bool ata_read_sector(uint32_t lba, uint8_t* buf);

/*
 * ata_write_sector - Write one 512-byte sector from buf to disk.
 *
 * @lba: Logical Block Address
 * @buf: source buffer (512 bytes)
 *
 * Returns true on success, false on error.
 * Blocks until the disk confirms the write is done.
 */
bool ata_write_sector(uint32_t lba, const uint8_t* buf);

#endif /* ATA_H */
