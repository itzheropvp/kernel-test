/*
 * ata.c - ATA/IDE PIO Driver Implementation
 */

#include "ata.h"
#include "../libc/string.h"

/* ============================================================
 * PRIMARY ATA BUS REGISTERS
 * ============================================================ */
#define ATA_DATA       0x1F0  /* 16-bit data port                          */
#define ATA_ERROR      0x1F1  /* Error register (read)                     */
#define ATA_FEATURES   0x1F1  /* Features register (write)                 */
#define ATA_SECCOUNT   0x1F2  /* Sector count                              */
#define ATA_LBA_LO     0x1F3  /* LBA bits 0-7                              */
#define ATA_LBA_MID    0x1F4  /* LBA bits 8-15                             */
#define ATA_LBA_HI     0x1F5  /* LBA bits 16-23                            */
#define ATA_DRIVE_HEAD 0x1F6  /* Drive select + LBA bits 24-27             */
#define ATA_STATUS     0x1F7  /* Status (read)                             */
#define ATA_COMMAND    0x1F7  /* Command (write)                           */
#define ATA_ALT_STATUS 0x3F6  /* Alternate status (doesn't clear interrupt)*/

/* ATA Status register bits */
#define ATA_SR_ERR  0x01  /* Error occurred                                */
#define ATA_SR_DRQ  0x08  /* Data Request: drive is ready to transfer data */
#define ATA_SR_SRV  0x10  /* Overlapped mode service request               */
#define ATA_SR_DF   0x20  /* Drive Fault (does not set ERR)                */
#define ATA_SR_RDY  0x40  /* Drive is ready                                */
#define ATA_SR_BSY  0x80  /* Drive is busy (do not send commands)          */

/* ATA Commands */
#define ATA_CMD_READ     0x20  /* Read sectors (PIO, with retry)            */
#define ATA_CMD_WRITE    0x30  /* Write sectors (PIO, with retry)           */
#define ATA_CMD_FLUSH    0xE7  /* Flush write cache (wait for data on disk) */
#define ATA_CMD_IDENTIFY 0xEC  /* Identify drive (returns 512 bytes of info)*/

/* Drive select: master = 0xE0, slave = 0xF0 (with LBA mode bit set) */
#define ATA_MASTER 0xE0

/* ============================================================
 * LOW-LEVEL HELPERS
 * ============================================================ */

/*
 * ata_wait_bsy - Poll until the BSY (busy) bit clears.
 *
 * The drive sets BSY while it's processing a command.
 * We must not send new commands or read data while BSY is set.
 *
 * We limit iterations to avoid an infinite loop if the drive hangs.
 * Returns false if timed out.
 */
static bool ata_wait_bsy(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY)) return true;
    }
    return false;  /* Timeout */
}

/*
 * ata_wait_drq - Poll until DRQ (Data Request) bit is set.
 *
 * After sending a read/write command, we wait for DRQ=1 before
 * transferring data. DRQ means "I'm ready, you can read/write now."
 *
 * Returns false on timeout or error.
 */
static bool ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR) return false;  /* Drive reported error */
        if (status & ATA_SR_DF)  return false;  /* Drive fault          */
        if (status & ATA_SR_DRQ) return true;   /* Ready to transfer!   */
    }
    return false;  /* Timeout */
}

/*
 * ata_400ns_delay - Wait ~400 nanoseconds for the status to settle.
 *
 * After selecting a drive or sending a command, the ATA spec requires
 * at least 400 ns before reading the status register. We read the
 * alternate status port 4 times (each read takes ~100 ns on old hardware).
 */
static void ata_400ns_delay(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

/*
 * ata_setup_lba - Send LBA address and sector count to the drive.
 *
 * This is shared between read and write — only the command byte differs.
 */
static void ata_setup_lba(uint32_t lba, uint8_t sector_count) {
    /*
     * Drive/Head register format for LBA28:
     *   Bit 7: 1 (always)
     *   Bit 6: 1 (LBA mode)
     *   Bit 5: 1 (always)
     *   Bit 4: 0 (master drive)
     *   Bits 3-0: LBA bits 24-27
     *
     *   ATA_MASTER = 0xE0 = 1110 0000
     *   | ((lba >> 24) & 0x0F) adds bits 24-27 into the low nibble
     */
    outb(ATA_DRIVE_HEAD, ATA_MASTER | ((lba >> 24) & 0x0F));
    ata_400ns_delay();

    outb(ATA_SECCOUNT, sector_count);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8)  & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

bool ata_detect(void) {
    /*
     * Detection method: select master drive and send IDENTIFY command.
     * If no drive is present, the data bus "floats" and we read 0xFF
     * from the status port.
     *
     * IDENTIFY returns 256 words (512 bytes) of drive metadata:
     *   words 27-46: model string
     *   word  60:    28-bit LBA sector count
     * We don't use this data — just check if the command succeeds.
     */

    /* Select master drive */
    outb(ATA_DRIVE_HEAD, ATA_MASTER);
    ata_400ns_delay();

    /* Floating bus? No drive present. */
    if (inb(ATA_STATUS) == 0xFF) return false;

    /* Send IDENTIFY */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    /* Status = 0 after IDENTIFY → no drive */
    if (inb(ATA_STATUS) == 0) return false;

    /* Wait for BSY to clear */
    if (!ata_wait_bsy()) return false;

    /* Wait for DRQ (data ready) — IDENTIFY returns 256 words */
    if (!ata_wait_drq()) return false;

    /* Read and discard the 256 identification words */
    for (int i = 0; i < 256; i++) {
        inw(ATA_DATA);
    }

    return true;
}

bool ata_read_sector(uint32_t lba, uint8_t* buf) {
    if (!ata_wait_bsy()) return false;

    ata_setup_lba(lba, 1);

    /* Send READ command */
    outb(ATA_COMMAND, ATA_CMD_READ);
    ata_400ns_delay();

    /* Wait for drive to prepare the data */
    if (!ata_wait_bsy()) return false;
    if (!ata_wait_drq()) return false;

    /*
     * Read 256 words (= 512 bytes = 1 sector) from the data port.
     *
     * IMPORTANT: The ATA data port is 16-bit wide (0x1F0).
     * We must use 16-bit reads (inw), not 8-bit reads (inb).
     * Reading 256 words = reading 512 bytes.
     *
     * We cast buf to uint16_t* so each read fills two bytes at once.
     */
    uint16_t* words = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) {
        words[i] = inw(ATA_DATA);
    }

    return true;
}

bool ata_write_sector(uint32_t lba, const uint8_t* buf) {
    if (!ata_wait_bsy()) return false;

    ata_setup_lba(lba, 1);

    /* Send WRITE command */
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    ata_400ns_delay();

    /* Wait for drive to be ready to receive data */
    if (!ata_wait_bsy()) return false;
    if (!ata_wait_drq()) return false;

    /* Write 256 words to the data port */
    const uint16_t* words = (const uint16_t*)buf;
    for (int i = 0; i < 256; i++) {
        outw(ATA_DATA, words[i]);
    }

    /*
     * Flush the write cache.
     *
     * Modern drives have a write cache — data might be in the drive's
     * internal buffer, not yet written to the magnetic platters.
     * FLUSH CACHE (0xE7) forces the drive to write all cached data to disk.
     * Without this, a power loss could corrupt the last written sectors.
     */
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    if (!ata_wait_bsy()) return false;

    return true;
}
