#include "cpymo_switch_audio.h"

#if defined(__SWITCH__) && defined(ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY)
#include "../include/cpymo_backend_audio.h"
#include <limits.h>

typedef struct {
    Uint8 *pcm;
    Uint32 len, pos;
    int volume;
} sw_audio_slot;

static SDL_mutex *audio_mutex;
static sw_audio_slot slots[CPYMO_SW_AUDIO_SLOTS];
static SDL_AudioFormat output_format;
static int output_freq, output_channels;

int cpymo_sw_audio_init(void)
{
    const cpymo_backend_audio_info *info = cpymo_backend_audio_get_info();
    if (!info) return 0;
    switch (info->format) {
    case cpymo_backend_audio_s16: output_format = AUDIO_S16SYS; break;
    case cpymo_backend_audio_s32: output_format = AUDIO_S32SYS; break;
    case cpymo_backend_audio_f32: output_format = AUDIO_F32SYS; break;
    default: return 0;
    }
    output_freq = (int)info->freq;
    output_channels = (int)info->channels;
    audio_mutex = SDL_CreateMutex();
    return audio_mutex != NULL;
}

void cpymo_sw_audio_stop(int slot)
{
    if (!audio_mutex || slot < 0 || slot >= CPYMO_SW_AUDIO_SLOTS) return;
    SDL_LockMutex(audio_mutex);
    SDL_free(slots[slot].pcm);
    SDL_memset(&slots[slot], 0, sizeof(slots[slot]));
    SDL_UnlockMutex(audio_mutex);
}

void cpymo_sw_audio_free(void)
{
    int i;
    if (!audio_mutex) return;
    for (i = 0; i < CPYMO_SW_AUDIO_SLOTS; ++i) cpymo_sw_audio_stop(i);
    SDL_DestroyMutex(audio_mutex);
    audio_mutex = NULL;
}

int cpymo_sw_audio_play(int slot, const SDL_AudioSpec *spec,
                        const Uint8 *pcm, Uint32 len, int volume)
{
    SDL_AudioCVT cvt;
    Uint8 *copy;
    Uint32 converted_len;
    int result;
    if (!audio_mutex || !spec || !pcm || !len || len > INT_MAX ||
        slot < 0 || slot >= CPYMO_SW_AUDIO_SLOTS) return 0;
    result = SDL_BuildAudioCVT(&cvt, spec->format, spec->channels, spec->freq,
                               output_format, output_channels, output_freq);
    if (result < 0 || cvt.len_mult <= 0 || len > (Uint32)(INT_MAX / cvt.len_mult)) return 0;
    copy = (Uint8 *)SDL_malloc((size_t)len * (size_t)cvt.len_mult);
    if (!copy) return 0;
    SDL_memcpy(copy, pcm, len);
    cvt.buf = copy;
    cvt.len = (int)len;
    if (result && SDL_ConvertAudio(&cvt) < 0) {
        SDL_free(copy);
        return 0;
    }
    converted_len = result ? (Uint32)cvt.len_cvt : len;
    SDL_LockMutex(audio_mutex);
    SDL_free(slots[slot].pcm);
    slots[slot].pcm = copy;
    slots[slot].len = converted_len;
    slots[slot].pos = 0;
    slots[slot].volume = volume;
    SDL_UnlockMutex(audio_mutex);
    return 1;
}

void cpymo_sw_audio_mix(Uint8 *stream, int len)
{
    int i;
    if (!audio_mutex || len <= 0) return;
    SDL_LockMutex(audio_mutex);
    for (i = 0; i < CPYMO_SW_AUDIO_SLOTS; ++i) {
        sw_audio_slot *s = &slots[i];
        Uint32 n;
        if (!s->pcm || s->pos >= s->len) continue;
        n = s->len - s->pos;
        if (n > (Uint32)len) n = (Uint32)len;
        SDL_MixAudioFormat(stream, s->pcm + s->pos, output_format, n, s->volume);
        s->pos += n;
    }
    SDL_UnlockMutex(audio_mutex);
    cpymo_switch_tts_mix(stream, len, output_format, output_freq, output_channels);
}
#endif
