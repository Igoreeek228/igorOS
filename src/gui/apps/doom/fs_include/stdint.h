#ifndef DOOM_FS_STDINT_H
#define DOOM_FS_STDINT_H
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;
typedef unsigned long      uintptr_t;
typedef long               intptr_t;
typedef uint64_t           uintmax_t;
typedef int64_t            intmax_t;
#define INT64_MAX  0x7FFFFFFFFFFFFFFFLL
#define UINT32_MAX 0xFFFFFFFFU
#define INT32_MAX  0x7FFFFFFF
#define INT32_MIN  (-INT32_MAX-1)
#endif
