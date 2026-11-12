#include "../../cpymo/cpymo_prelude.h"
#include "../include/cpymo_backend_startup_music.h"
#include "cpymo_import_sdl2.h"
#include <stdio.h>

/* 《Song of PyMO》启动音乐（SDL2 后端，PSP 除外）。
 *
 * 进入游戏选择器时由引擎调用 play，选中游戏或退出选择器时调用 stop。
 * 用一个独立的 SDL 音频设备循环播放 startup.wav，与无障碍音效设备、
 * 引擎 BGM 设备互不干扰（SDL2 允许多个播放设备并存）。
 *
 * PSP 的启动音乐走 EBOOT.PBP 的 SND0 区段（pack-pbp 打包），
 * 由系统在 XMB 中播放，因此 __PSP__ 下这里不编译播放逻辑，
 * 避免与系统启动音乐重复。
 */

#if defined(__PSP__) || defined(DISABLE_AUDIO)

void cpymo_backend_startup_music_play(void) {}
void cpymo_backend_startup_music_stop(void) {}

#else

/* 与 Android/桌面无障碍音效对齐后的观感音量：满量程的 3/4。 */
#define CPYMO_STARTUP_MUSIC_VOLUME (SDL_MIX_MAXVOLUME * 3 / 4)

static SDL_AudioDeviceID startup_music_dev = 0;
static Uint8 *startup_music_pcm = NULL;
static Uint32 startup_music_len = 0;
static Uint32 startup_music_pos = 0;
static SDL_AudioFormat startup_music_format = 0;
static int startup_music_looping = 0;

static void cpymo_backend_startup_music_callback(void *userdata, Uint8 *stream, int len)
{
	(void)userdata;

	SDL_memset(stream, 0, (size_t)len);

	if (startup_music_pcm == NULL || startup_music_len == 0) return;

	Uint32 remain = (Uint32)len;
	Uint32 written = 0;

	while (remain > 0) {
		if (startup_music_pos >= startup_music_len) {
			if (!startup_music_looping) return;
			startup_music_pos = 0;
		}

		Uint32 chunk = startup_music_len - startup_music_pos;
		if (chunk > remain) chunk = remain;

		SDL_MixAudioFormat(
			stream + written,
			startup_music_pcm + startup_music_pos,
			startup_music_format,
			chunk,
			CPYMO_STARTUP_MUSIC_VOLUME);

		startup_music_pos += chunk;
		written += chunk;
		remain -= chunk;
	}
}

static int cpymo_backend_startup_music_load(SDL_AudioSpec *out_spec)
{
	char *base_path = SDL_GetBasePath();

	/* 优先用可执行文件所在目录，与无障碍音效的查找顺序一致；
	 * 找不到再退回当前工作目录（开发时从仓库目录直接运行的情况）。 */
	if (base_path) {
		char full_path[1024];
		snprintf(full_path, sizeof(full_path), "%sstartup.wav", base_path);
		SDL_free(base_path);

		if (SDL_LoadWAV(full_path, out_spec,
				&startup_music_pcm, &startup_music_len) != NULL)
			return 1;
	}

	startup_music_pcm = NULL;
	startup_music_len = 0;

	if (SDL_LoadWAV("startup.wav", out_spec,
			&startup_music_pcm, &startup_music_len) != NULL)
		return 1;

	startup_music_pcm = NULL;
	startup_music_len = 0;
	return 0;
}

void cpymo_backend_startup_music_play(void)
{
	if (startup_music_dev) return;

	SDL_AudioSpec wav_spec, have_spec;
	if (!cpymo_backend_startup_music_load(&wav_spec)) {
		/* 没有随包分发音频时静默跳过，不打扰玩家。 */
		SDL_Log("[startup] startup.wav not found, no startup music.");
		return;
	}

	startup_music_format = wav_spec.format;
	startup_music_pos = 0;
	startup_music_looping = 1;

	wav_spec.callback = &cpymo_backend_startup_music_callback;
	wav_spec.userdata = NULL;

	/* 不允许 SDL 改格式：回调里按 WAV 原始格式混音。 */
	startup_music_dev = SDL_OpenAudioDevice(NULL, 0, &wav_spec, &have_spec, 0);
	if (startup_music_dev == 0) {
		SDL_Log("[startup] can not open audio device: %s", SDL_GetError());
		SDL_FreeWAV(startup_music_pcm);
		startup_music_pcm = NULL;
		startup_music_len = 0;
		startup_music_looping = 0;
		return;
	}

	SDL_PauseAudioDevice(startup_music_dev, 0);
}

void cpymo_backend_startup_music_stop(void)
{
	/* SDL_CloseAudioDevice 会等回调线程退出，之后才能释放 PCM。 */
	if (startup_music_dev) {
		SDL_CloseAudioDevice(startup_music_dev);
		startup_music_dev = 0;
	}

	startup_music_looping = 0;
	startup_music_pos = 0;

	if (startup_music_pcm) {
		SDL_FreeWAV(startup_music_pcm);
		startup_music_pcm = NULL;
	}

	startup_music_len = 0;
}

#endif
