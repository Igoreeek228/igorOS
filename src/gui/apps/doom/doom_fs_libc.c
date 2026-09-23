/* Minimal freestanding libc for doomgeneric on IgorOS kernel */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* ---- heap ---- */
#define HEAP_SIZE (12 * 1024 * 1024)
static uint8_t heap[HEAP_SIZE];
static size_t heap_ptr = 0;

void *malloc(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (heap_ptr + n > HEAP_SIZE) return 0;
    void *p = &heap[heap_ptr];
    heap_ptr += n;
    return p;
}
void free(void *p) { (void)p; }
void *calloc(size_t a, size_t b) {
    size_t n = a * b;
    void *p = malloc(n);
    if (p) {
        uint8_t *q = p;
        for (size_t i = 0; i < n; i++) q[i] = 0;
    }
    return p;
}
void *realloc(void *p, size_t n) {
    void *q = malloc(n);
    if (q && p) {
        /* best-effort copy unknown size — callers rarely need exact */
        uint8_t *d = q, *s = p;
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    }
    return q;
}

/* ---- string / mem ---- */
void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
void *memset(void *d, int c, size_t n) {
    uint8_t *dd = d; while (n--) *dd++ = (uint8_t)c; return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *aa = a, *bb = b;
    while (n--) { if (*aa != *bb) return *aa - *bb; aa++; bb++; }
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
char *strcpy(char *d, const char *s) { char *o = d; while ((*d++ = *s++)); return o; }
char *strncpy(char *d, const char *s, size_t n) {
    size_t i; for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0; return d;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}
char *strcat(char *d, const char *s) { strcpy(d + strlen(d), s); return d; }
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == 0) ? (char *)s : 0;
}
char *strrchr(const char *s, int c) {
    const char *l = 0;
    while (*s) { if (*s == (char)c) l = s; s++; }
    return (char *)l;
}
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
int atoi(const char *s) {
    int v = 0, sig = 1;
    if (*s == '-') { sig = -1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return v * sig;
}

/* ---- stdio stubs (memory FILE for DOOM.WAD) ---- */
typedef struct {
    uint8_t *data;
    size_t size;      /* logical size */
    size_t capacity;  /* allocated */
    size_t pos;
    int eof;
    int writable;
    int slot;         /* -1 = WAD readonly; >=0 = save slot index */
} MEMFILE;

extern const uint8_t doom_wad_start[];
extern const uint8_t doom_wad_end[];

/* In-RAM save slots (doomsav0.dsg … temp.dsg) — no disk in freestanding */
#define SAVE_SLOTS 8
#define SAVE_CAP   (512 * 1024)
static uint8_t save_store[SAVE_SLOTS][SAVE_CAP];
static size_t  save_len[SAVE_SLOTS];
static char    save_name[SAVE_SLOTS][64];

#define MAX_FILES 8
static MEMFILE files[MAX_FILES];
static int file_used[MAX_FILES];

typedef MEMFILE FILE;

FILE *stdin; FILE *stdout; FILE *stderr;

static int path_is_wad(const char *path) {
    if (!path) return 0;
    for (const char *p = path; *p; p++) {
        if ((p[0]=='D'||p[0]=='d') && (p[1]=='O'||p[1]=='o') &&
            (p[2]=='O'||p[2]=='o') && (p[3]=='M'||p[3]=='m') &&
            p[4]=='.') return 1;
        if (p[0]=='.' && (p[1]=='W'||p[1]=='w') && (p[2]=='A'||p[2]=='a') &&
            (p[3]=='D'||p[3]=='d')) return 1;
    }
    return 0;
}

static int path_is_save(const char *path) {
    if (!path) return 0;
    for (const char *p = path; *p; p++) {
        if (p[0]=='.' && (p[1]=='d'||p[1]=='D') && (p[2]=='s'||p[2]=='S') &&
            (p[3]=='g'||p[3]=='G')) return 1;
        /* also allow bare temp / doomsav */
        if ((p[0]=='t'||p[0]=='T') && (p[1]=='e'||p[1]=='E') &&
            (p[2]=='m'||p[2]=='M') && (p[3]=='p'||p[3]=='P')) return 1;
    }
    return 0;
}

static void path_basename(const char *path, char *out, size_t outn) {
    const char *s = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') s = p + 1;
    size_t i = 0;
    while (s[i] && i + 1 < outn) { out[i] = s[i]; i++; }
    out[i] = 0;
}

static int find_save_slot(const char *basename, int create) {
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (save_name[i][0] && strcmp(save_name[i], basename) == 0)
            return i;
    }
    if (!create) return -1;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        if (!save_name[i][0]) {
            size_t j = 0;
            while (basename[j] && j + 1 < 64) {
                save_name[i][j] = basename[j]; j++;
            }
            save_name[i][j] = 0;
            save_len[i] = 0;
            return i;
        }
    }
    return -1;
}

FILE *fopen(const char *path, const char *mode) {
    int write = 0, read = 1;
    if (mode) {
        for (const char *m = mode; *m; m++) {
            if (*m == 'w' || *m == 'a') write = 1;
            if (*m == 'r') read = 1;
            if (*m == '+') write = 1;
        }
    }

    int fi = -1;
    for (int i = 0; i < MAX_FILES; i++)
        if (!file_used[i]) { fi = i; break; }
    if (fi < 0) return 0;

    /* IWAD — read-only from embedded blob */
    if (path_is_wad(path) && !write) {
        size_t sz = (size_t)(doom_wad_end - doom_wad_start);
        if (sz < 16) return 0;
        file_used[fi] = 1;
        files[fi].data = (uint8_t *)doom_wad_start;
        files[fi].size = sz;
        files[fi].capacity = sz;
        files[fi].pos = 0;
        files[fi].eof = 0;
        files[fi].writable = 0;
        files[fi].slot = -1;
        return &files[fi];
    }

    /* Savegames / temp.dsg — RAM slots */
    if (path_is_save(path) || write) {
        char base[64];
        path_basename(path ? path : "temp.dsg", base, sizeof(base));
        if (!base[0]) {
            base[0]='t'; base[1]='e'; base[2]='m'; base[3]='p';
            base[4]='.'; base[5]='d'; base[6]='s'; base[7]='g'; base[8]=0;
        }
        int slot = find_save_slot(base, write ? 1 : 0);
        if (slot < 0 && read && !write) return 0; /* load missing */
        if (slot < 0) return 0;

        if (write && mode && mode[0] == 'w')
            save_len[slot] = 0; /* truncate */

        file_used[fi] = 1;
        files[fi].data = save_store[slot];
        files[fi].size = save_len[slot];
        files[fi].capacity = SAVE_CAP;
        files[fi].pos = (write && mode && mode[0] == 'a') ? save_len[slot] : 0;
        files[fi].eof = 0;
        files[fi].writable = write;
        files[fi].slot = slot;
        return &files[fi];
    }

    return 0;
}

int fclose(FILE *f) {
    if (!f) return -1;
    if (f->writable && f->slot >= 0) {
        save_len[f->slot] = f->size;
    }
    int i = (int)(f - files);
    if (i >= 0 && i < MAX_FILES) file_used[i] = 0;
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !size) return 0;
    size_t want = size * nmemb;
    size_t left = (f->pos < f->size) ? (f->size - f->pos) : 0;
    if (want > left) { want = left; f->eof = 1; }
    memcpy(ptr, f->data + f->pos, want);
    f->pos += want;
    return size ? (want / size) : 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    if (!f || !f->writable || !size) return 0;
    size_t want = size * nmemb;
    size_t need = f->pos + want;
    if (need > f->capacity) {
        /* clamp */
        if (f->pos >= f->capacity) return 0;
        want = f->capacity - f->pos;
        need = f->capacity;
    }
    memcpy(f->data + f->pos, ptr, want);
    f->pos += want;
    if (f->pos > f->size) f->size = f->pos;
    if (f->slot >= 0) save_len[f->slot] = f->size;
    return size ? (want / size) : 0;
}

int fseek(FILE *f, long off, int whence) {
    if (!f) return -1;
    long np = (long)f->pos;
    if (whence == 0) np = off;
    else if (whence == 1) np = (long)f->pos + off;
    else if (whence == 2) np = (long)f->size + off;
    if (np < 0) np = 0;
    if (f->writable) {
        if ((size_t)np > f->capacity) np = (long)f->capacity;
        if ((size_t)np > f->size) {
            /* zero-fill gap */
            for (size_t i = f->size; i < (size_t)np; i++) f->data[i] = 0;
            f->size = (size_t)np;
            if (f->slot >= 0) save_len[f->slot] = f->size;
        }
    } else {
        if ((size_t)np > f->size) np = (long)f->size;
    }
    f->pos = (size_t)np;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) { return f ? (long)f->pos : -1; }
int feof(FILE *f) { return f ? f->eof : 1; }
int ferror(FILE *f) { (void)f; return 0; }
int fflush(FILE *f) { (void)f; return 0; }

int fgetc(FILE *f) {
    if (!f || f->pos >= f->size) { if (f) f->eof = 1; return -1; }
    return f->data[f->pos++];
}
int ungetc(int c, FILE *f) {
    if (!f || f->pos == 0) return -1;
    f->pos--; return c;
}

int printf(const char *fmt, ...) { (void)fmt; return 0; }
int fprintf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
int sprintf(char *buf, const char *fmt, ...) {
    /* very tiny: only support %s %d %c copies poorly — use vsprintf-like minimal */
    (void)fmt;
    if (buf) buf[0] = 0;
    return 0;
}
int snprintf(char *buf, size_t n, const char *fmt, ...) {
    (void)fmt; if (buf && n) buf[0] = 0; return 0;
}
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
    (void)fmt; (void)ap; if (buf && n) buf[0] = 0; return 0;
}
int sscanf(const char *s, const char *fmt, ...) { (void)s; (void)fmt; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { (void)c; return c; }

void exit(int code) { (void)code; for(;;){} }
void abort(void) { for(;;){} }
char *getenv(const char *n) { (void)n; return 0; }
int system(const char *c) { (void)c; return -1; }
int remove(const char *p) {
    if (!p) return -1;
    char base[64];
    /* basename */
    const char *s = p;
    for (const char *q = p; *q; q++) if (*q=='/'||*q=='\\') s = q+1;
    size_t i=0; while (s[i] && i+1<64) { base[i]=s[i]; i++; } base[i]=0;
    for (int si = 0; si < SAVE_SLOTS; si++) {
        if (save_name[si][0] && strcmp(save_name[si], base) == 0) {
            save_name[si][0] = 0;
            save_len[si] = 0;
            return 0;
        }
    }
    return -1;
}
int rename(const char *a, const char *b) {
    if (!a || !b) return -1;
    char ba[64], bb[64];
    const char *s = a;
    for (const char *q = a; *q; q++) if (*q=='/'||*q=='\\') s = q+1;
    size_t i=0; while (s[i] && i+1<64) { ba[i]=s[i]; i++; } ba[i]=0;
    s = b;
    for (const char *q = b; *q; q++) if (*q=='/'||*q=='\\') s = q+1;
    i=0; while (s[i] && i+1<64) { bb[i]=s[i]; i++; } bb[i]=0;

    int src = -1;
    for (int si = 0; si < SAVE_SLOTS; si++)
        if (save_name[si][0] && strcmp(save_name[si], ba) == 0) { src = si; break; }
    if (src < 0) return -1;

    /* overwrite existing dest */
    for (int si = 0; si < SAVE_SLOTS; si++)
        if (si != src && save_name[si][0] && strcmp(save_name[si], bb) == 0) {
            save_name[si][0] = 0; save_len[si] = 0;
        }
    i=0; while (bb[i] && i+1<64) { save_name[src][i]=bb[i]; i++; }
    save_name[src][i]=0;
    return 0;
}

/* math used by engine occasionally */
double fabs(double x) { return x < 0 ? -x : x; }


int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncasecmp(const char *a, const char *b, size_t n) {
    while (n && *a && *b) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return ca - cb;
        a++; b++; n--;
    }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strdup(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    char *d = (char *)malloc(n + 1);
    if (!d) return 0;
    for (size_t i = 0; i <= n; i++) d[i] = s[i];
    return d;
}


double atof(const char *s) {
    double v = 0.0, frac = 0.0, base = 0.1;
    int sign = 1, after = 0;
    if (!s) return 0.0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (!after) v = v * 10.0 + (*s - '0');
            else { frac += (*s - '0') * base; base *= 0.1; }
        } else if (*s == '.' && !after) after = 1;
        else break;
    }
    return sign * (v + frac);
}


/* ---- API for File Manager / other apps ---- */
size_t doom_fs_wad_size(void) {
    return (size_t)(doom_wad_end - doom_wad_start);
}

int doom_fs_list_saves(char names[][64], uint32_t sizes[], int max) {
    int n = 0;
    for (int i = 0; i < SAVE_SLOTS && n < max; i++) {
        if (!save_name[i][0]) continue;
        size_t j = 0;
        while (save_name[i][j] && j + 1 < 64) {
            names[n][j] = save_name[i][j];
            j++;
        }
        names[n][j] = 0;
        if (sizes) sizes[n] = (uint32_t)save_len[i];
        n++;
    }
    return n;
}
