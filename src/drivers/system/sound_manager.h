#ifndef SOUND_MANAGER_H
#define SOUND_MANAGER_H

#include <stdint.h>

/*
 * Звуковая подсистема: выбирает доступное аудиоустройство
 * и предоставляет единый интерфейс для приложений.
 */

typedef enum {
    AUDIO_DEV_NONE = 0,
    AUDIO_DEV_AC97,
    AUDIO_DEV_HDA
} audio_dev_t;

/* Сканирует устройства. Сначала пробуется AC'97 — для него реализовано
 * фактическое воспроизведение; HDA пока только инициализируется. */
void sound_init(void);

/* Какое устройство активно (для диагностики/индикаторов). */
audio_dev_t sound_active_device(void);

/* Громкость 0..100. */
void sound_set_volume(uint8_t vol);

/* Проигрывает 16-битный стерео-PCM указанной длины в байтах. */
void sound_play_pcm(const uint8_t* pcm_data, uint32_t length);

#endif
