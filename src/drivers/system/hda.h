#ifndef HDA_H
#define HDA_H

#include <stdint.h>

int hda_init(void);
void hda_set_volume(uint8_t vol);
void hda_play_pcm(const uint16_t* samples, uint32_t count);

#endif