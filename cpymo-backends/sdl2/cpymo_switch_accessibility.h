#ifndef CPYMO_SWITCH_ACCESSIBILITY_H
#define CPYMO_SWITCH_ACCESSIBILITY_H

/*
 * Nintendo Switch 无障碍桥
 *
 * 职责边界（与 Android/iOS 桥一致）：
 *   - 把触摸手势翻译成 cpymo_accessibility_action 并交给共享输入队列，
 *     不直接说话、不伪造键盘事件、不保留可复制文本副本；
 *   - 负责把 ReadSpeaker 引擎（switch_tts.c）按资产目录拉起来；
 *   - 平台层只负责“出声 / 振动 / 送去哪说”，语义全部由引擎层决定。
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

union SDL_Event;

/* 启动 TTS 引擎：按候选目录找资产，失败时返回 false 并打印原因。 */
bool cpymo_switch_accessibility_tts_start(void);

/* 确保进程自带的 romfs 已挂载（单文件 nro / NSP 用）；返回是否可用。 */
bool cpymo_switch_accessibility_romfs_ready(void);

/* 关闭 TTS 引擎（进程退出前调用；不释放 guest 代码内存）。 */
void cpymo_switch_accessibility_tts_stop(void);

/* 每帧在 SDL_PollEvent 循环里调用，处理触摸手势。 */
void cpymo_switch_accessibility_handle_event(const union SDL_Event *event);

/* 每帧调用一次，处理需要计时的长按手势。 */
void cpymo_switch_accessibility_tick(void);

/*
 * 探索模式的指针位置（复刻 Android/iOS/Web 的「移动指针但不点击」）。
 *
 * 单指按下与移动时把手指位置记为指针位置，抬指后指针停留在原处，
 * 直到下一次触摸。返回 false 表示还没有触摸过（此时沿用后端原生鼠标）。
 * x / y 为归一化坐标（0..1，相对游戏画面）。
 */
bool cpymo_switch_accessibility_touch_point(float *x, float *y);

/* 当前实际使用的资产目录（未启动时为空串），供诊断与设置页显示。 */
const char *cpymo_switch_accessibility_assets_dir(void);

/* 引擎是否可用（就绪）与最近一次错误。 */
bool cpymo_switch_accessibility_ready(void);
const char *cpymo_switch_accessibility_last_error(void);

/* ---- TTS 参数项（设置页用；顺序必须与 cpymo_config_ui.c 的 ITEM_TTS_* 一致） ---- */
#define CPYMO_SW_TTS_ITEM_RATE    0   /* 语速 1..1000，默认 135 */
#define CPYMO_SW_TTS_ITEM_PITCH   1   /* 音高 1..1000，默认 150 */
#define CPYMO_SW_TTS_ITEM_VOLUME  2   /* 音量 0..1000，默认 90  */
#define CPYMO_SW_TTS_ITEM_VOICE   3   /* 中文音库：10 HONG，11 YAFANG */
#define CPYMO_SW_TTS_ITEM_EMOTION 4   /* 情绪等级 0..4，0 表示关闭 */
#define CPYMO_SW_TTS_ITEM_COUNT   5
#define CPYMO_SW_TTS_EMOTION_MAX  4

int cpymo_switch_accessibility_tts_item_get(int item);
void cpymo_switch_accessibility_tts_item_set(int item, int val);

#ifdef __cplusplus
}
#endif

#endif
