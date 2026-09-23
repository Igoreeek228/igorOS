#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

#define FAT32_MAX_NAME 13 /* 8.3 + dot + NUL; LFN will be added later. */

typedef struct {
    char name[FAT32_MAX_NAME];
    int is_dir;
    uint32_t size;
    uint32_t first_cluster;
} fat32_entry_t;

/*
 * Initializes a READ-ONLY FAT32 view of the first FAT32 primary partition
 * visible through the ATA read-only block device.
 */
int fat32_init(void);

int fat32_list_dir(uint32_t dir_cluster, fat32_entry_t* out, int max_entries);
uint32_t fat32_root_cluster(void);

/* Read file bytes. Never writes to the filesystem. */
uint32_t fat32_read_file(uint32_t first_cluster,
                         uint32_t size,
                         uint8_t* out_buf,
                         uint32_t max_len);

// Создаёт новый файл в директории dir_cluster (человекочитаемое имя,
// например "test.txt" — сконвертируется в короткое 8.3 автоматически).
// НЕ перезаписывает существующий файл с тем же именем — вернёт 0.
// Возвращает 1 при успехе.
int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t size);

#endif
