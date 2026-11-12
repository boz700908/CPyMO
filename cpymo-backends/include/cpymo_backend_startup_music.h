#ifndef INCLUDE_CPYMO_BACKEND_STARTUP_MUSIC
#define INCLUDE_CPYMO_BACKEND_STARTUP_MUSIC

#ifdef __cplusplus
extern "C" {
#endif

/* 《Song of PyMO》启动音乐。
 *
 * 进入游戏选择器时开始播放，选中一款游戏或退出选择器时停止。
 * 该乐曲随可执行文件一起分发（startup.wav），由各后端自行加载与混音。
 *
 * 后端可以选择不实现：下面用弱符号声明，链接后地址为 NULL 时
 * 引擎侧会跳过调用（见 cpymo_game_selector.c 的包装函数）。
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
