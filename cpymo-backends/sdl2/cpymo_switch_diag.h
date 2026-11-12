#ifndef CPYMO_SWITCH_DIAG_H
#define CPYMO_SWITCH_DIAG_H

/* Switch 上 stdout 不进模拟器日志、svcOutputDebugString 也不被记录，
 * 所以关键事件同时落盘到 sdmc:/cpymo-a11y.log，取回文件即可定位。
 * 非 Switch 平台展开为空，不产生任何代码，别的平台零影响。 */

#if defined(__SWITCH__)

void cpymo_switch_diag(const char *fmt, ...);
#define CPYMO_DIAG(...) cpymo_switch_diag(__VA_ARGS__)

#else

#define CPYMO_DIAG(...) ((void)0)

#endif

#endif
