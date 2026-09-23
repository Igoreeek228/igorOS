#include "drivers/system/sound_manager.h"
#include "drivers/system/ac97.h"
#include "drivers/system/hda.h"

static audio_dev_t active_dev = AUDIO_DEV_NONE;

void sound_init(void) {
    if (hda_init()) {
        active_dev = AUDIO_DEV_HDA;
        return;
    }

    if (ac97_init()) {
        active_dev = AUDIO_DEV_AC97;
        return;
    }

    active_dev = AUDIO_DEV_NONE;
}

void sound_set_volume(uint8_t vol) {
    if (active_dev == AUDIO_DEV_HDA) hda_set_volume(vol);
    else if (active_dev == AUDIO_DEV_AC97) ac97_set_volume(vol);
}

void sound_play_pcm(const uint16_t* samples, uint32_t count) {
    if (active_dev == AUDIO_DEV_HDA) hda_play_pcm(samples, count);
    else if (active_dev == AUDIO_DEV_AC97) ac97_play_pcm(samples, count);
}