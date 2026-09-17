// src/drivers/system/ata.c
//
// Минимальный ATA PIO-драйвер. Поддерживает только primary master, LBA28
// и 512-байтные сектора.
//
// Изначально был намеренно read-only (только IDENTIFY + READ SECTORS) —
// это было сделано как безопасная первая ступень, чтобы не рисковать
// содержимым диска, пока не проверена сама логика чтения. Теперь, когда
// чтение проверено (в т.ч. отдельным тестом на реальном FAT32-образе),
// добавлена WRITE SECTORS (0x30) + CACHE FLUSH (0xE7) — минимально
// необходимый набор для записи.

#include "ata.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALT_STATUS  0x3F6

#define ATA_SR_ERR  0x01
#define ATA_SR_DRQ  0x08
#define ATA_SR_DRDY 0x40
#define ATA_SR_BSY  0x80

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7

#define ATA_LBA28_MAX 0x0FFFFFFFu
#define ATA_TIMEOUT   1000000u

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static void ata_delay_400ns(void) {
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
    (void)inb(ATA_ALT_STATUS);
}

static int ata_wait_not_busy(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t st = inb(ATA_STATUS);
        if (!(st & ATA_SR_BSY)) return 1;
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; ++i) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR) return 0;
        if (st & ATA_SR_DRQ) return 1;
    }
    return 0;
}

int ata_init(void) {
    /* Select primary-master. This changes controller state, NOT disk data. */
    outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay_400ns();

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);

    /* IDENTIFY is read-only. */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF) return 0;

    if (!ata_wait_not_busy()) return 0;

    /* ATAPI devices normally return non-zero signature bytes here. */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) return 0;

    if (!ata_wait_drq()) return 0;

    /* Drain IDENTIFY's 256 words. We don't need model/capacity yet. */
    for (int i = 0; i < 256; ++i) {
        (void)inw(ATA_DATA);
    }

    return 1;
}

int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buf) {
    if (!buf || count == 0) return 0;

    /* This implementation is deliberately limited to LBA28. */
    uint32_t last_lba = lba + (uint32_t)count - 1u;
    if (last_lba < lba || last_lba > ATA_LBA28_MAX) return 0;

    if (!ata_wait_not_busy()) return 0;

    /* 0xE0 = LBA mode + primary master. */
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));

    /* The ONLY data-transfer command exposed by this driver. */
    outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);

    for (uint8_t s = 0; s < count; ++s) {
        if (!ata_wait_not_busy()) return 0;
        if (!ata_wait_drq()) return 0;

        uint16_t* dst = (uint16_t*)(buf + (uint32_t)s * 512u);
        for (int i = 0; i < 256; ++i) {
            dst[i] = inw(ATA_DATA);
        }
    }

    return 1;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buf) {
    if (!buf || count == 0) return 0;

    /* Тот же лимит LBA28, что и у чтения — драйвер не поддерживает LBA48. */
    uint32_t last_lba = lba + (uint32_t)count - 1u;
    if (last_lba < lba || last_lba > ATA_LBA28_MAX) return 0;

    if (!ata_wait_not_busy()) return 0;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay_400ns();

    outb(ATA_SECCOUNT, count);
    outb(ATA_LBA_LO,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI,  (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

    for (uint8_t s = 0; s < count; ++s) {
        if (!ata_wait_not_busy()) return 0;
        if (!ata_wait_drq()) return 0;

        const uint16_t* src = (const uint16_t*)(buf + (uint32_t)s * 512u);
        for (int i = 0; i < 256; ++i) {
            outw(ATA_DATA, src[i]);
        }
        // Пауза между секторами — контроллеру нужно немного времени, чтобы
        // подготовиться к приёму следующего блока.
        ata_delay_400ns();
    }

    if (!ata_wait_not_busy()) return 0;

    // CACHE FLUSH — просим диск гарантированно сбросить данные из
    // внутреннего кэша на физический носитель (иначе при внезапном
    // выключении можно потерять только что "записанные" данные).
    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    if (!ata_wait_not_busy()) return 0;

    return 1;
}
