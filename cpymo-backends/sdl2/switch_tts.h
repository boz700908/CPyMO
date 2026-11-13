/*
 * CPyMO Nintendo Switch 原生 TTS（ReadSpeaker / Nuance vtdb2）
 *
 * 设计原则（对齐 cpymo-a11y-design.md）：
 *   - 手势/按键只产生语义输入，不直接说话（原则 1）
 *   - 平台层只负责「出声 / 振动」（原则 7），文本→语音的调度在这里
 *   - 朗读挂状态改变处，不挂在渲染处
 *
 * 本模块对外只暴露四件事：
 *   init / free / speak / params
 * 它自己拉一条 guest 线程跑厂商 sdk 镜像（NSO），PCM 通过独立的 SDL 音频设备输出。
 */
#ifndef CPYMO_SWITCH_TTS_H
#define CPYMO_SWITCH_TTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 朗读的三种入队语义，见 /memories/tts-interrupt-rules.md */
typedef enum {
    /* 排队朗读，不打断正在播的（页面加载、状态描述） */
    CPYMO_SWITCH_TTS_APPEND = 0,
    /* 替换「最后一条还没开始播的」，正在播的不动（数值/焦点快速变化） */
    CPYMO_SWITCH_TTS_REPLACE_LAST = 1,
    /* 立即打断：清空队列 + 停掉当前语音（按键反馈） */
    CPYMO_SWITCH_TTS_INTERRUPT = 2
} cpymo_switch_tts_mode;

/* 语音参数。 */
typedef struct {
    int voice;   /* 中文音库 id，默认 11 */
    int pitch;   /* 默认 150 */
    int rate;    /* 默认 135 */
    int volume;  /* 0..1000，默认 90 */
    char emotion_category[32]; /* 默认 "happiness" */
    int emotion_level;         /* 默认 1，0 表示不加 emotion 标签 */
    int han_fallback;          /* 0 中文，1 日文 */
} cpymo_switch_tts_params;

typedef enum {
    CPYMO_SWITCH_TTS_STATE_IDLE = 0,
    CPYMO_SWITCH_TTS_STATE_LOADING,
    CPYMO_SWITCH_TTS_STATE_READY,
    CPYMO_SWITCH_TTS_STATE_FAILED
} cpymo_switch_tts_state;

void cpymo_switch_tts_params_default(cpymo_switch_tts_params *params);

/*
 * 初始化。assets_dir 为包含以下文件的目录（sdmc:/ 路径）：
 *   rtld.bin  main.bin  sdk.bin  D32-YAFANG.vtdb2
 * 返回 false 表示失败；原因见 cpymo_switch_tts_last_error()。
 * 本函数内部会拉起一条驻留线程去跑 guest 镜像，初始化完成（含引擎 init 调用）后才返回。
 */
bool cpymo_switch_tts_init(const char *assets_dir);

void cpymo_switch_tts_free(void);

/* 引擎是否可用（init 成功且引擎 init 调用没报错） */
bool cpymo_switch_tts_ready(void);

cpymo_switch_tts_state cpymo_switch_tts_state_get(void);

/* 最后一条错误的可读描述（静态存储，永不返回 NULL） */
const char *cpymo_switch_tts_last_error(void);

/* 入队一段 UTF-8 文本（含中文/emoji 直接给，内部做 vtml 转义） */
void cpymo_switch_tts_speak(const char *utf8_text, cpymo_switch_tts_mode mode);

/* 停止：清空队列并立刻静音（不改参数、不退出引擎） */
void cpymo_switch_tts_stop(void);

/* 参数热更新，下一次朗读生效 */
void cpymo_switch_tts_set_params(const cpymo_switch_tts_params *params);
void cpymo_switch_tts_get_params(cpymo_switch_tts_params *out);

/* 宿主每帧调一次即可（内部只做很轻的事：推进队列 + 振动到期） */
void cpymo_switch_tts_pump(void);

#ifdef __cplusplus
}
#endif

#endif
