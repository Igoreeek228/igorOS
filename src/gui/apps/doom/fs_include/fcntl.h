#ifndef DOOM_FS_FCNTL_H
#define DOOM_FS_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
int open(const char *p, int flags, ...);
#endif
