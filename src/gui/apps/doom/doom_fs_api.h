#ifndef DOOM_FS_API_H
#define DOOM_FS_API_H
#include <stdint.h>
#include <stddef.h>
size_t doom_fs_wad_size(void);
int doom_fs_list_saves(char names[][64], uint32_t sizes[], int max);
#endif
