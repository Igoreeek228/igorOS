#ifndef DOOM_FS_STDIO_H
#define DOOM_FS_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct MEMFILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EOF (-1)
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fflush(FILE *f);
int fgetc(FILE *f);
int ungetc(int c, FILE *f);
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int sscanf(const char *s, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int remove(const char *p);
int rename(const char *a, const char *b);
#endif
