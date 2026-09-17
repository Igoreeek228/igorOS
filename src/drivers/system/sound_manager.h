#ifndef SOUND_MANAGER_H
#define SOUND_MANAGER_H

#include <stdint.h>

typedef enum {
    AUDIO_DEV_NONE = 0,
    AUDIO_DEV_AC97,
    AUDIO_DEV_HDA
} audio_dev_t;

void sound_init(void);
void sound_set_volume(uint8_t vol);
void sound_play_pcm(const uint16_t* samples, uint32_t count);

#endif