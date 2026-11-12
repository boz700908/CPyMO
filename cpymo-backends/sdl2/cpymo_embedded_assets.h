/**
 * CPyMO 内嵌资源
 *
 * 随包分发的 WAV（启动音乐与无障碍音效）在构建时被编进可执行文件，
 * 使 Windows / Linux / macOS 可以只发一个程序文件，不必外带资源目录。
 *
 * 运行时顺序不变：先按老路径找文件（可覆盖、便于调试），找不到再用内嵌数据。
 */
#pragma once

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cpymo_embedded_asset_entry {
    const char *name;
    const unsigned char *data;
    unsigned len;
};

#ifdef CPYMO_EMBEDDED_ASSETS
extern const struct cpymo_embedded_asset_entry cpymo_embedded_asset_table[];
extern const unsigned cpymo_embedded_asset_count;
#endif

/** 用内嵌资源加载一个 WAV。成功返回 spec，并通过 buf/len 交出 PCM 缓冲区
 *（调用方用 SDL_FreeWAV 释放）；没有同名内嵌资源时返回 NULL。 */
SDL_AudioSpec *cpymo_embedded_load_wav(
    const char *name, SDL_AudioSpec *spec, Uint8 **buf, Uint32 *len);

#ifdef __cplusplus
}
#endif
