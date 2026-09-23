#ifndef DOOM_FS_STDDEF_H
#define DOOM_FS_STDDEF_H
typedef unsigned long size_t;
typedef long          ptrdiff_t;
#define NULL ((void*)0)
#define offsetof(t,m) ((size_t)&(((t*)0)->m))
#endif
