#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/*
 * ATA PIO interface. Поддерживает только primary master, LBA28,
 * 512-байтные сектора. Без AHCI/DMA — простой программный ввод-вывод.
 */
int ata_init(void);

/* Read count 512-byte sectors starting at LBA into buf. */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buf);

/* Write count 512-byte sectors starting at LBA from buf. */
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buf);

#endif
