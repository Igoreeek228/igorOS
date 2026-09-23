#include "config.h"
#include "deh_str.h"
#include "doomtype.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_timer.h"
#include "i_video.h"
#include "i_system.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DEFAULT_RAM 8
#define MIN_RAM 6

typedef struct atexit_listentry_s {
    atexit_func_t func;
    boolean run_on_error;
    struct atexit_listentry_s *next;
} atexit_listentry_t;

static atexit_listentry_t *exit_funcs = NULL;

void I_AtExit(atexit_func_t func, boolean run_on_error) {
    atexit_listentry_t *e = malloc(sizeof(*e));
    e->func = func; e->run_on_error = run_on_error; e->next = exit_funcs;
    exit_funcs = e;
}

void I_Quit(void) {
    atexit_listentry_t *e;
    for (e = exit_funcs; e; e = e->next)
        if (!e->run_on_error) e->func();
}

/* I_Error must not freeze the whole OS: if doom_app armed a recovery point,
 * jump back to it and let the window show the message. */
void *doom_err_jmp[5];
int doom_err_armed = 0;
char doom_err_msg[160] = "";

void I_Error(char *error, ...) {
    va_list ap;
    va_start(ap, error);
    vsnprintf(doom_err_msg, sizeof(doom_err_msg), error, ap);
    va_end(ap);
    if (doom_err_armed) {
        doom_err_armed = 0;
        __builtin_longjmp(doom_err_jmp, 1);
    }
    for (;;) { __asm__ volatile("hlt"); }
}

byte *I_ZoneBase(int *size) {
    static byte zone[DEFAULT_RAM * 1024 * 1024];
    *size = DEFAULT_RAM * 1024 * 1024;
    return zone;
}

void I_PrintBanner(char *msg) { (void)msg; }
void I_PrintDivider(void) {}
void I_PrintStartupBanner(char *gamedescription) { (void)gamedescription; }

void I_Tactile(int on, int off, int total) { (void)on;(void)off;(void)total; }
int I_GetMemorySize(void) { return DEFAULT_RAM * 1024 * 1024; }
char *I_GetConfigDir(void) { return "."; }
char *I_GetUserDir(void) { return "."; }
char *I_FindExecutable(char *name) { (void)name; return NULL; }

void I_Init(void) {}

boolean I_ConsoleStdout(void) { return 0; }
boolean I_GetMemoryValue(unsigned int offset, void *value, int size) {
    (void)offset; memset(value, 0, size); return true;
}
