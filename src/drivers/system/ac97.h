#ifndef AC97_H
#define AC97_H

#include <stdint.h>

#define AC97_VENDOR_ID 0x8086
#define AC97_DEVICE_ID 0x2415

typedef struct {
    uint32_t phys_addr;
    uint16_t sample_count;
    uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

int ac97_init(void);
void ac97_set_volume(uint8_t vol);
void ac97_play_pcm(const uint16_t* samples, uint32_t count);

#endif