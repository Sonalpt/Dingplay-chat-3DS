#include "dbg.h"
#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>

static FILE *s_f;
static u64 s_t0;

void dbg_init(void) {
    s_f = fopen("sdmc:/3ds/dingplay-chat/debug.log", "a");
    s_t0 = svcGetSystemTick();
    dbg_log("---- boot " APP_VERSION " ----");
}

void dbg_log(const char *fmt, ...) {
    if (!s_f) return;
    u64 ms = (svcGetSystemTick() - s_t0) / (SYSCLOCK_ARM11 / 1000);
    fprintf(s_f, "%6llu.%03llu ", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_f, fmt, ap);
    va_end(ap);
    fputc('\n', s_f);
    fflush(s_f);
}
