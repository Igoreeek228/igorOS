#ifndef DOOM_FS_STDLIB_H
#define DOOM_FS_STDLIB_H
#include <stddef.h>
void *malloc(size_t n);
void free(void *p);
void *calloc(size_t a, size_t b);
void *realloc(void *p, size_t n);
void exit(int code);
void abort(void);
int abs(int x);
long labs(long x);
int atoi(const char *s);
double atof(const char *s);
char *getenv(const char *n);
int system(const char *c);
int abs(int);
#endif
