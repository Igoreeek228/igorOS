#include "drivers/system/sound_manager.h"
#include "drivers/system/ac97.h"
#include "drivers/system/hda.h"

static audio_dev_t active_dev = AUDIO_DEV_NONE;

void sound_init(void) {
    /*
     * AC'97 пробуется первым: для него в проекте реализовано реальное
     * воспроизведение (в т.ч. в QEMU). HDA-ветка пока лишь определяет
     * контроллер, поэтому выбирается только при отсутствии AC'97.
     */
    if (ac97_init()) {
        active_dev = AUDIO_DEV_AC97;
        return;
    }

    if (hda_init()) {
        active_dev = AUDIO_DEV_HDA;
        return;
    }

    active_dev = AUDIO_DEV_NONE;
}

audio_dev_t sound_active_device(void) {
    return active_dev;
}

void sound_set_volume(uint8_t vol) {
    if (active_dev == AUDIO_DEV_AC97) ac97_set_volume(vol);
    else if (active_dev == AUDIO_DEV_HDA) hda_set_volume(vol);
}

void sound_play_pcm(const uint8_t* pcm_data, uint32_t length) {
    if (!pcm_data || length == 0) return;

    if (active_dev == AUDIO_DEV_AC97) ac97_play_pcm(pcm_data, length);
    else if (active_dev == AUDIO_DEV_HDA) hda_play_pcm(pcm_data, length);
}
