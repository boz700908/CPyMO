#ifndef INCLUDE_CPYMO_BACKEND_STARTUP_MUSIC
#define INCLUDE_CPYMO_BACKEND_STARTUP_MUSIC

#ifdef __cplusplus
extern "C" {
#endif

/* 《Song of PyMO》启动音乐。
 *
 * 应用启动时播放一次（从头到末尾，不循环），由引擎在
 * cpymo_engine_play_startup_music_once 里调用，全进程只触发一次。
 * 该乐曲随可执行文件一起分发（startup.wav），由各后端自行加载与混音。
 *
 * 后端可以完全不实现：下面用弱符号声明，链接后地址为 NULL 时引擎侧跳过。
 * 自带音频图标的平台就不实现——PSP 走 EBOOT.PBP 的 SND0，3DS 走 banner 音乐。
 * 不支持弱符号的编译器（MSVC）下是普通声明，因此这些后端必须给出实现。
 */
#if defined(__GNUC__) || defined(__clang__)
#define CPYMO_STARTUP_MUSIC_WEAK __attribute__((weak))
#else
#define CPYMO_STARTUP_MUSIC_WEAK
#endif

CPYMO_STARTUP_MUSIC_WEAK void cpymo_backend_startup_music_play(void);
CPYMO_STARTUP_MUSIC_WEAK void cpymo_backend_startup_music_stop(void);

#ifdef __cplusplus
}
#endif

#endif
