/**
 * Tolk 运行时（Windows）
 *
 * Tolk 的读屏桥必须以 DLL 形式加载（Tolk.dll + 各家读屏客户端 DLL），无法静态链接。
 * 为了让 Windows 版只发一个 exe，这些文件在构建时被打进 exe 的 RCDATA 资源，
 * 首次用到语音时释放到 %LOCALAPPDATA%\CPyMO\runtime\ 再加载。
 *
 * 缺文件、缺权限、没有读屏软件时一律静默降级：游戏照常运行，只是没有语音。
 */
#pragma once

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 释放并加载 Tolk（可重复调用，只在第一次真正做事）。 */
void cpymo_tolk_load(void);

/** 卸载 Tolk（Tolk_Unload）。 */
void cpymo_tolk_unload(void);

/** 朗读一段文本，打断当前朗读（Tolk_Output(text, true)）。 */
void cpymo_tolk_output(const wchar_t *text);

/** 停止朗读（Tolk_Silence）。 */
void cpymo_tolk_silence(void);

#ifdef __cplusplus
}
#endif
