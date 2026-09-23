#ifndef DOOM_FS_UNISTD_H
#define DOOM_FS_UNISTD_H
typedef long ssize_t;
int close(int fd);
ssize_t read(int fd, void *buf, unsigned long n);
ssize_t write(int fd, const void *buf, unsigned long n);
unsigned int sleep(unsigned int s);
#endif
