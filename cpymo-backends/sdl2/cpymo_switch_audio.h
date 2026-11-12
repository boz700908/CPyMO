#pragma once

#if defined(__SWITCH__) && defined(ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY)
#include "cpymo_import_sdl2.h"

enum { CPYMO_SW_AUDIO_STARTUP, CPYMO_SW_AUDIO_FEEDBACK, CPYMO_SW_AUDIO_SLOTS };
int cpymo_sw_audio_init(void);
void cpymo_sw_audio_free(void);
int cpymo_sw_audio_play(int slot, const SDL_AudioSpec *spec,
                        const Uint8 *pcm, Uint32 len, int volume);
void cpymo_sw_audio_stop(int slot);
void cpymo_sw_audio_mix(Uint8 *stream, int len);
void cpymo_switch_tts_mix(Uint8 *stream, int len, SDL_AudioFormat format,
                          int freq, int channels);
#endif
