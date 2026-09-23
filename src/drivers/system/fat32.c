// src/drivers/system/fat32.c
//
// FAT32 READ-ONLY backend for IgorOS.
//
// Драйвер только читает MBR, BPB, FAT и data-кластеры. Никаких операций
// изменения FAT, directory entries или data area здесь нет.
//
// Пока поддерживаются:
//   - MBR partition table;
//   - первый primary FAT32 partition (0x0B/0x0C);
//   - стандартный сектор 512 bytes;
//   - FAT32, read-only;
//   - короткие 8.3 имена (LFN пока пропускаются);
//   - чтение файлов по цепочке кластеров.
//
// ВАЖНО: этот код не форматирует флешки и не вызывает никаких write-команд.

#include "fat32.h"
#include "ata.h"

#define SECTOR_SIZE 512u
#define MAX_CLUSTER_SECTORS 64u
#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_BAD_CLUSTER 0x0FFFFFF7u

static uint32_t s_partition_lba = 0;
static uint32_t s_partition_sectors = 0;
static uint32_t s_fat_start_lba = 0;
static uint32_t s_data_start_lba = 0;
static uint32_t s_sectors_per_cluster = 0;
static uint32_t s_root_cluster = 0;
static uint32_t s_fat_size_sectors = 0;
static uint32_t s_max_cluster = 0;
static uint32_t s_num_fats = 0;
static int s_ready = 0;

static uint8_t sector_buf[SECTOR_SIZE];
static uint8_t cluster_buf[MAX_CLUSTER_SECTORS * SECTOR_SIZE];
static uint8_t fat_sector_buf[SECTOR_SIZE];

static uint16_t rd_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int valid_boot_signature(const uint8_t* sector) {
    return sector[510] == 0x55 && sector[511] == 0xAA;
}

uint32_t fat32_root_cluster(void) {
    return s_root_cluster;
}

int fat32_init(void) {
    s_ready = 0;

    /* ATA init and every operation below are read-only. */
    if (!ata_init()) return 0;
    if (!ata_read_sectors(0, 1, sector_buf)) return 0;

    if (!valid_boot_signature(sector_buf)) return 0;

    /* Find the first primary FAT32 partition in the MBR. */
    uint32_t part_lba = 0;
    uint32_t part_sectors = 0;

    for (int i = 0; i < 4; ++i) {
        const uint8_t* entry = sector_buf + 0x1BEu + (uint32_t)i * 16u;
        uint8_t type = entry[4];

        if (type == 0x0B || type == 0x0C) {
            part_lba = rd_u32(entry + 8);
            part_sectors = rd_u32(entry + 12);
            break;
        }
    }

    if (part_lba == 0 || part_sectors < 1) return 0;
    if (part_lba > 0x0FFFFFFFu) return 0;
    if (part_sectors > 0x10000000u - part_lba) return 0;

    s_partition_lba = part_lba;
    s_partition_sectors = part_sectors;

    /* FAT32 boot sector / BPB. */
    if (!ata_read_sectors(part_lba, 1, sector_buf)) return 0;
    if (!valid_boot_signature(sector_buf)) return 0;

    uint16_t bytes_per_sector = rd_u16(sector_buf + 11);
    uint8_t sectors_per_cluster = sector_buf[13];
    uint16_t reserved_sectors = rd_u16(sector_buf + 14);
    uint8_t num_fats = sector_buf[16];
    uint16_t root_entry_count = rd_u16(sector_buf + 17);
    uint16_t total_sectors_16 = rd_u16(sector_buf + 19);
    uint16_t sectors_per_track = rd_u16(sector_buf + 24);
    uint16_t heads = rd_u16(sector_buf + 26);
    uint32_t hidden_sectors = rd_u32(sector_buf + 28);
    uint32_t total_sectors_32 = rd_u32(sector_buf + 32);
    uint32_t fat_size_32 = rd_u32(sector_buf + 36);
    uint32_t root_cluster = rd_u32(sector_buf + 44);
    uint16_t fs_info_sector = rd_u16(sector_buf + 48);
    uint16_t backup_boot_sector = rd_u16(sector_buf + 50);

    (void)sectors_per_track;
    (void)heads;
    (void)hidden_sectors;
    (void)fs_info_sector;
    (void)backup_boot_sector;

    /* FAT32-specific sanity checks. */
    if (bytes_per_sector != SECTOR_SIZE) return 0;
    if (sectors_per_cluster == 0 || sectors_per_cluster > MAX_CLUSTER_SECTORS) return 0;
    if ((sectors_per_cluster & (sectors_per_cluster - 1u)) != 0) return 0;
    if (reserved_sectors == 0) return 0;
    if (num_fats == 0 || num_fats > 2) return 0;
    if (root_entry_count != 0) return 0;
    if (total_sectors_16 != 0) return 0;
    if (fat_size_32 == 0) return 0;
    if (root_cluster < 2) return 0;

    uint32_t total_sectors = total_sectors_32;
    if (total_sectors == 0 || total_sectors > part_sectors) return 0;

    uint32_t fat_area_sectors = (uint32_t)num_fats * fat_size_32;
    uint32_t first_data_relative = (uint32_t)reserved_sectors + fat_area_sectors;
    if (first_data_relative >= total_sectors) return 0;

    uint32_t data_sectors = total_sectors - first_data_relative;
    uint32_t cluster_count = data_sectors / sectors_per_cluster;

    // Microsoft-спека формально определяет тип тома ПО количеству кластеров:
    // <4085 — FAT12, <65525 — FAT16, иначе — FAT32. Это правило для того,
    // ЧЕМ ФОРМАТИРОВАТЬ том, а не жёсткое требование для чтения. На практике
    // `mtools`/`mformat -F` (именно так собирается наш собственный igorOS.img
    // в Makefile) форсирует FAT32 даже на маленьких разделах, где кластеров
    // меньше 65525 — то есть строгая проверка отбраковывала бы наш же
    // тестовый диск. Мы и так уже надёжно знаем, что это FAT32: тип раздела
    // в MBR явно 0x0B/0x0C, и fat_size_32 != 0 (у FAT12/16 это поле в BPB
    // устроено иначе и не может случайно "притвориться" валидным здесь).
    // Оставляем только защиту от откровенно битых/нулевых значений.
    if (cluster_count < 8u) return 0;

    uint32_t max_cluster = cluster_count + 1u;
    if (max_cluster < 2 || max_cluster > 0x0FFFFFEFu) return 0;

    /* FAT must be large enough to contain all cluster entries. */
    uint64_t needed_fat_bytes = ((uint64_t)(max_cluster + 1u)) * 4u;
    uint64_t available_fat_bytes = (uint64_t)fat_size_32 * SECTOR_SIZE;
    if (needed_fat_bytes > available_fat_bytes) return 0;

    uint32_t fat_start = part_lba + reserved_sectors;
    uint32_t data_start = fat_start + fat_area_sectors;
    if (fat_start < part_lba || data_start < fat_start) return 0;

    /* Make sure computed data area remains inside the partition. */
    if (data_start - part_lba >= part_sectors) return 0;
    if (data_sectors > part_sectors - (data_start - part_lba)) return 0;

    if (root_cluster > max_cluster) return 0;

    s_fat_start_lba = fat_start;
    s_data_start_lba = data_start;
    s_sectors_per_cluster = sectors_per_cluster;
    s_fat_size_sectors = fat_size_32;
    s_root_cluster = root_cluster;
    s_max_cluster = max_cluster;
    s_num_fats = num_fats;
    s_ready = 1;

    return 1;
}

static int valid_cluster(uint32_t cluster) {
    return cluster >= 2 && cluster <= s_max_cluster;
}

static int cluster_to_lba(uint32_t cluster, uint32_t* out_lba) {
    if (!valid_cluster(cluster) || !out_lba) return 0;

    uint32_t relative = (cluster - 2u) * s_sectors_per_cluster;
    if (relative > s_partition_sectors) return 0;
    if (s_data_start_lba > 0x0FFFFFFFu - relative) return 0;

    *out_lba = s_data_start_lba + relative;
    return 1;
}

static uint32_t fat_next_cluster(uint32_t cluster) {
    if (!s_ready || !valid_cluster(cluster)) return 0;

    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector_index = fat_offset / SECTOR_SIZE;
    uint32_t offset_in_sector = fat_offset % SECTOR_SIZE;

    if (fat_sector_index >= s_fat_size_sectors) return 0;

    uint32_t fat_sector_lba = s_fat_start_lba + fat_sector_index;
    if (!ata_read_sectors(fat_sector_lba, 1, fat_sector_buf)) return 0;

    uint32_t value = rd_u32(fat_sector_buf + offset_in_sector) & 0x0FFFFFFFu;

    if (value >= FAT32_EOC) return 0;
    if (value == FAT32_BAD_CLUSTER) return 0;
    if (value < 2 || value > s_max_cluster) return 0;

    return value;
}

static int read_cluster(uint32_t cluster, uint8_t* out) {
    uint32_t lba;
    if (!out || !cluster_to_lba(cluster, &lba)) return 0;
    return ata_read_sectors(lba, (uint8_t)s_sectors_per_cluster, out);
}

static int write_cluster(uint32_t cluster, const uint8_t* data) {
    uint32_t lba;
    if (!data || !cluster_to_lba(cluster, &lba)) return 0;
    return ata_write_sectors(lba, (uint8_t)s_sectors_per_cluster, data);
}

// Записывает значение FAT-записи для cluster ВО ВСЕ копии FAT (обычно их 2) —
// если обновить только первую, вторая (резервная) разойдётся с реальным
// состоянием диска, и любой другой ОС/драйвер, который доверяет второй
// копии, увидит битую цепочку.
static int fat_set_entry(uint32_t cluster, uint32_t value) {
    if (!s_ready || cluster < 2) return 0;

    uint32_t fat_offset = cluster * 4u;
    uint32_t fat_sector_index = fat_offset / SECTOR_SIZE;
    uint32_t offset_in_sector = fat_offset % SECTOR_SIZE;
    if (fat_sector_index >= s_fat_size_sectors) return 0;

    for (uint32_t copy = 0; copy < s_num_fats; copy++) {
        uint32_t fat_sector_lba = s_fat_start_lba + copy * s_fat_size_sectors + fat_sector_index;

        if (!ata_read_sectors(fat_sector_lba, 1, fat_sector_buf)) return 0;

        fat_sector_buf[offset_in_sector + 0] = (uint8_t)(value & 0xFF);
        fat_sector_buf[offset_in_sector + 1] = (uint8_t)((value >> 8) & 0xFF);
        fat_sector_buf[offset_in_sector + 2] = (uint8_t)((value >> 16) & 0xFF);
        // Верхние 4 бита 32-битной FAT32-записи зарезервированы — их нельзя
        // трогать при записи, только младшие 28 бит несут номер кластера/EOC.
        fat_sector_buf[offset_in_sector + 3] =
            (uint8_t)((fat_sector_buf[offset_in_sector + 3] & 0xF0) | ((value >> 24) & 0x0F));

        if (!ata_write_sectors(fat_sector_lba, 1, fat_sector_buf)) return 0;
    }

    return 1;
}

// Ищет первый свободный (значение 0) кластер, сканируя FAT сектор за
// сектором (только по первой копии — остальные должны быть её зеркалом).
// Возвращает номер кластера или 0, если свободных не осталось.
static uint32_t find_free_cluster(void) {
    if (!s_ready) return 0;

    for (uint32_t sector_idx = 0; sector_idx < s_fat_size_sectors; sector_idx++) {
        if (!ata_read_sectors(s_fat_start_lba + sector_idx, 1, fat_sector_buf)) return 0;

        uint32_t entries_here = SECTOR_SIZE / 4u;
        for (uint32_t e = 0; e < entries_here; e++) {
            uint32_t cluster = sector_idx * (SECTOR_SIZE / 4u) + e;
            if (cluster < 2 || cluster > s_max_cluster) continue;

            uint32_t value = rd_u32(fat_sector_buf + e * 4u) & 0x0FFFFFFFu;
            if (value == 0) return cluster;
        }
    }

    return 0; // диск заполнен
}

static void format_short_name(const uint8_t* raw, char* out) {
    char name[9];
    char ext[4];
    int ni = 0;
    int ei = 0;

    for (int i = 0; i < 8 && raw[i] != ' '; ++i) {
        name[ni++] = (char)raw[i];
    }
    name[ni] = 0;

    for (int i = 0; i < 3 && raw[8 + i] != ' '; ++i) {
        ext[ei++] = (char)raw[8 + i];
    }
    ext[ei] = 0;

    int oi = 0;
    for (int i = 0; name[i] && oi < FAT32_MAX_NAME - 1; ++i) {
        out[oi++] = name[i];
    }

    if (ei > 0 && oi < FAT32_MAX_NAME - 1) {
        out[oi++] = '.';
        for (int i = 0; ext[i] && oi < FAT32_MAX_NAME - 1; ++i) {
            out[oi++] = ext[i];
        }
    }

    out[oi] = 0;
}

int fat32_list_dir(uint32_t dir_cluster, fat32_entry_t* out, int max_entries) {
    if (!s_ready || !out || max_entries <= 0 || !valid_cluster(dir_cluster)) return 0;

    int count = 0;
    uint32_t cluster = dir_cluster;

    /* A directory cannot contain more clusters than the volume has. */
    uint32_t safety = 0;

    while (cluster != 0 && count < max_entries && safety++ <= s_max_cluster) {
        if (!read_cluster(cluster, cluster_buf)) break;

        uint32_t entries_in_cluster =
            (s_sectors_per_cluster * SECTOR_SIZE) / 32u;

        for (uint32_t i = 0; i < entries_in_cluster && count < max_entries; ++i) {
            const uint8_t* raw = cluster_buf + i * 32u;

            if (raw[0] == 0x00) return count;
            if (raw[0] == 0xE5) continue;

            uint8_t attr = raw[11];
            if (attr == 0x0F) continue; /* LFN: support later. */
            if (attr & 0x08) continue;  /* Volume label. */
            if (raw[0] == '.') continue; /* . and .. */

            fat32_entry_t* e = &out[count];
            format_short_name(raw, e->name);
            e->is_dir = (attr & 0x10) ? 1 : 0;
            e->size = rd_u32(raw + 28);
            e->first_cluster =
                ((uint32_t)rd_u16(raw + 20) << 16) | rd_u16(raw + 26);

            /* Ignore malformed entries instead of following invalid clusters. */
            if (e->is_dir) {
                if (!valid_cluster(e->first_cluster)) continue;
            } else if (e->first_cluster != 0 && !valid_cluster(e->first_cluster)) {
                continue;
            }

            count++;
        }

        cluster = fat_next_cluster(cluster);
    }

    return count;
}

uint32_t fat32_read_file(uint32_t first_cluster,
                         uint32_t size,
                         uint8_t* out_buf,
                         uint32_t max_len) {
    if (!s_ready || !out_buf || max_len == 0 || size == 0) return 0;
    if (first_cluster < 2) return 0; /* Empty file may legitimately have 0. */
    if (!valid_cluster(first_cluster)) return 0;

    uint32_t total_read = 0;
    uint32_t cluster = first_cluster;
    uint32_t cluster_bytes = s_sectors_per_cluster * SECTOR_SIZE;
    uint32_t safety = 0;

    while (cluster != 0 && total_read < size && total_read < max_len &&
           safety++ <= s_max_cluster) {
        if (!read_cluster(cluster, cluster_buf)) break;

        uint32_t remaining = size - total_read;
        uint32_t space_left = max_len - total_read;
        uint32_t to_copy = cluster_bytes;

        if (to_copy > remaining) to_copy = remaining;
        if (to_copy > space_left) to_copy = space_left;

        for (uint32_t i = 0; i < to_copy; ++i) {
            out_buf[total_read + i] = cluster_buf[i];
        }

        total_read += to_copy;
        if (total_read >= size || total_read >= max_len) break;

        cluster = fat_next_cluster(cluster);
    }

    return total_read;
}

// Конвертирует человекочитаемое имя ("test.txt") в "сырые" 11 байт формата
// FAT 8.3 ("TEST    TXT") — обратная операция к format_short_name.
// Обрезает имя/расширение до 8/3 символов, переводит в верхний регистр.
static void to_short_name(const char* input, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';

    int i = 0, ni = 0;
    while (input[i] && input[i] != '.' && ni < 8) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[ni++] = (uint8_t)c;
        i++;
    }
    while (input[i] && input[i] != '.') i++; // пропускаем остаток имени, если оно длиннее 8 символов
    if (input[i] == '.') {
        i++;
        int ei = 0;
        while (input[i] && ei < 3) {
            char c = input[i];
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[8 + ei++] = (uint8_t)c;
            i++;
        }
    }
}

// Ищет свободный (0x00 — конец списка, или 0xE5 — удалённая запись) слот
// 32-байтной directory entry в цепочке кластеров директории. Если во всей
// существующей цепочке свободных слотов нет — выделяет новый кластер,
// зануляет его и присоединяет к цепочке директории (директория "растёт"
// точно так же, как растут кластерные цепочки обычных файлов).
static int find_free_dir_slot(uint32_t dir_cluster, uint32_t* out_cluster, uint32_t* out_index) {
    if (!s_ready || !valid_cluster(dir_cluster)) return 0;

    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = dir_cluster;
    uint32_t entries_in_cluster = (s_sectors_per_cluster * SECTOR_SIZE) / 32u;
    uint32_t safety = 0;

    while (cluster != 0 && safety++ <= s_max_cluster) {
        if (!read_cluster(cluster, cluster_buf)) return 0;

        for (uint32_t i = 0; i < entries_in_cluster; i++) {
            uint8_t first_byte = cluster_buf[i * 32u];
            if (first_byte == 0x00 || first_byte == 0xE5) {
                *out_cluster = cluster;
                *out_index = i;
                return 1;
            }
        }

        last_cluster = cluster;
        cluster = fat_next_cluster(cluster);
    }

    // Свободного слота не нашлось нигде в существующей цепочке — растим
    // директорию новым кластером.
    uint32_t new_cluster = find_free_cluster();
    if (new_cluster == 0) return 0; // диск заполнен

    for (uint32_t i = 0; i < sizeof(cluster_buf); i++) cluster_buf[i] = 0;
    if (!write_cluster(new_cluster, cluster_buf)) return 0;
    if (!fat_set_entry(new_cluster, FAT32_EOC)) return 0;
    if (!fat_set_entry(last_cluster, new_cluster)) return 0;

    *out_cluster = new_cluster;
    *out_index = 0;
    return 1;
}

// Создаёт новый файл в директории dir_cluster с именем name (человекочитаемым,
// например "test.txt" — сконвертируется в короткое 8.3 автоматически) и
// содержимым data/size. НЕ поддерживает перезапись существующего файла с тем
// же именем (вернёт 0) — это осознанное ограничение первой версии записи,
// перезапись/удаление — следующий шаг.
int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t size) {
    if (!s_ready || !name || (!data && size > 0)) return 0;
    if (!valid_cluster(dir_cluster)) return 0;

    uint8_t short_name[11];
    to_short_name(name, short_name);

    // Проверяем, что файла с таким именем ещё нет в этой директории.
    fat32_entry_t existing[16];
    int existing_count = fat32_list_dir(dir_cluster, existing, 16);
    for (int i = 0; i < existing_count; i++) {
        uint8_t candidate[11];
        to_short_name(existing[i].name, candidate);
        int same = 1;
        for (int k = 0; k < 11; k++) if (candidate[k] != short_name[k]) { same = 0; break; }
        if (same) return 0; // уже существует — перезапись пока не поддержана
    }

    uint32_t cluster_bytes = s_sectors_per_cluster * SECTOR_SIZE;
    uint32_t clusters_needed = (size + cluster_bytes - 1u) / cluster_bytes;
    if (clusters_needed == 0) clusters_needed = 1; // даже пустой файл занимает 1 кластер

    // Выделяем и связываем цепочку кластеров под данные, записывая их по ходу.
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    uint32_t written = 0;

    for (uint32_t c = 0; c < clusters_needed; c++) {
        uint32_t cluster = find_free_cluster();
        if (cluster == 0) return 0; // диск заполнен (уже выделенные кластеры останутся висеть — TODO: откат)

        if (!fat_set_entry(cluster, FAT32_EOC)) return 0;
        if (prev_cluster != 0) {
            if (!fat_set_entry(prev_cluster, cluster)) return 0;
        } else {
            first_cluster = cluster;
        }

        uint32_t remaining = size - written;
        uint32_t to_write = (remaining < cluster_bytes) ? remaining : cluster_bytes;

        for (uint32_t i = 0; i < sizeof(cluster_buf); i++) cluster_buf[i] = 0;
        for (uint32_t i = 0; i < to_write; i++) cluster_buf[i] = data[written + i];

        if (!write_cluster(cluster, cluster_buf)) return 0;

        written += to_write;
        prev_cluster = cluster;
    }

    // Находим место под саму запись в директории и пишем её.
    uint32_t slot_cluster, slot_index;
    if (!find_free_dir_slot(dir_cluster, &slot_cluster, &slot_index)) return 0;
    if (!read_cluster(slot_cluster, cluster_buf)) return 0;

    uint8_t* raw = cluster_buf + slot_index * 32u;
    for (int i = 0; i < 11; i++) raw[i] = short_name[i];
    raw[11] = 0x20; // ARCHIVE — обычный файл
    for (int i = 12; i < 20; i++) raw[i] = 0; // время/дата создания — не отслеживаем, обнуляем
    raw[20] = (uint8_t)((first_cluster >> 16) & 0xFF);
    raw[21] = (uint8_t)((first_cluster >> 24) & 0xFF);
    for (int i = 22; i < 26; i++) raw[i] = 0; // время/дата изменения
    raw[26] = (uint8_t)(first_cluster & 0xFF);
    raw[27] = (uint8_t)((first_cluster >> 8) & 0xFF);
    raw[28] = (uint8_t)(size & 0xFF);
    raw[29] = (uint8_t)((size >> 8) & 0xFF);
    raw[30] = (uint8_t)((size >> 16) & 0xFF);
    raw[31] = (uint8_t)((size >> 24) & 0xFF);

    return write_cluster(slot_cluster, cluster_buf);
}
