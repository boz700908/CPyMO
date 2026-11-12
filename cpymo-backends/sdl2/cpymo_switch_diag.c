#include "cpymo_switch_diag.h"

#if defined(__SWITCH__)

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/* 追加写 sdmc:/cpymo-a11y.log。真机上 sdmc 可写；模拟器里落到
 * files/sdmc/ 目录下，用户用文件管理器取回即可。 */
void cpymo_switch_diag(const char *fmt, ...)
{
    static FILE *logf;
    static bool tried;
    static unsigned lines;
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    printf("%s", buf);

    if (!tried) {
        tried = true;
        logf = fopen("sdmc:/cpymo-a11y.log", "a");
    }

    if (logf) {
        if (lines == 0) fputs("=== CPyMO Switch 诊断日志 ===\n", logf);
        fputs(buf, logf);
        fflush(logf);
    }

    lines++;
}

#endif
