#include "../../cpymo/cpymo_prelude.h"
#include "../include/cpymo_backend_text.h"
#include "cpymo_import_sdl2.h"

#ifndef DISABLE_STB_TRUETYPE

#include "../../cpymo/cpymo_utils.h"
#include "../../cpymo/cpymo_parser.h"
#include "../../stb/stb_truetype.h"
#include "../include/cpymo_backend_image.h"
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <assert.h>

extern stbtt_fontinfo font;

typedef struct {
    float scale;
    int ascent;
    float baseline;
    int width, height;
    cpymo_backend_image img;
} cpymo_backend_text_internal;


void cpymo_backend_font_render(void *out_or_null, int *w, int *h, cpymo_str text, float scale, float baseline);

error_t cpymo_backend_text_create(
    cpymo_backend_text *out,
    float *out_width,
    cpymo_str utf8_string,
    float single_character_size_in_logical_screen)
{
    cpymo_str text = utf8_string;
    if (text.len == 0) 
        return CPYMO_ERR_INVALID_ARG;

    cpymo_backend_text_internal *t = 
        (cpymo_backend_text_internal *)malloc(sizeof(cpymo_backend_text_internal));
    if (t == NULL) return CPYMO_ERR_OUT_OF_MEM;

    t->scale = stbtt_ScaleForPixelHeight(&font, single_character_size_in_logical_screen);
    stbtt_GetFontVMetrics(&font, &t->ascent, NULL, NULL);
    t->baseline = t->scale * t->ascent;

    cpymo_backend_font_render(NULL, &t->width, &t->height, text, t->scale, t->baseline);
    
    assert(t->width > 0 && t->height > 0);

    t->height += 8; // MAGIC FOR MORE MEMORY
    t->width += 4;  // MAGIC FOR EDGE CLAMP
    
    void *screen = malloc(t->width * t->height);
    if (screen == NULL) {
        free(t);
        return CPYMO_ERR_OUT_OF_MEM;
    }

    memset(screen, 0, t->width * t->height);

    int w = t->width, h = t->height;
    cpymo_backend_font_render(screen, &w, &h, text, t->scale, t->baseline);

    void *screen2 = malloc(t->width * t->height * 4);
    if (screen2 == NULL) {
        free(screen);
        free(t);
        return CPYMO_ERR_OUT_OF_MEM;
    }

    memset(screen2, 255, t->width * t->height * 4);

    cpymo_utils_attach_mask_to_rgba(screen2, screen, t->width, t->height);
    free(screen);

    error_t err = cpymo_backend_image_load(&t->img, screen2, t->width, t->height, cpymo_backend_image_format_rgba);
    if (err != CPYMO_ERR_SUCC) {
        free(screen2);
        free(t);
        return CPYMO_ERR_UNKNOWN;
    }

    *out = t;
    *out_width = (float)w;

    return CPYMO_ERR_SUCC;
}

void cpymo_backend_text_free(cpymo_backend_text t)
{
    cpymo_backend_text_internal *tt = (cpymo_backend_text_internal *)t;
    cpymo_backend_image_free(tt->img);
    free(t);
}

void cpymo_backend_text_draw(
    cpymo_backend_text text,   
    float x, float y_baseline,
    cpymo_color col, float alpha,
    enum cpymo_backend_image_draw_type draw_type)
{
    cpymo_backend_text_internal *t = (cpymo_backend_text_internal *)text;

    SDL_SetTextureColorMod((SDL_Texture *)t->img, 255 - col.r, 255 - col.g, 255 - col.b);
    cpymo_backend_image_draw(
        x + 1,
        y_baseline - t->baseline + 1,
        (float)t->width,
        (float)t->height,
        t->img,
        0,
        0,
        t->width,
        t->height,
        alpha,
        draw_type);

    SDL_SetTextureColorMod((SDL_Texture *)t->img, col.r, col.g, col.b);
    cpymo_backend_image_draw(
        x,
        y_baseline - t->baseline,
        (float)t->width,
        (float)t->height,
        t->img,
        0,
        0,
        t->width,
        t->height,
        alpha,
        draw_type);
}

float cpymo_backend_text_width(cpymo_str t, float single_character_size_in_logical_screen)
{
    float scale = stbtt_ScaleForPixelHeight(&font, single_character_size_in_logical_screen);
    int ascent;
    stbtt_GetFontVMetrics(&font, &ascent, NULL, NULL);
    float baseline = scale * ascent;

    int w, h;
    cpymo_backend_font_render(NULL, &w, &h, t, scale, baseline);

    return (float)w;
}

#endif

#ifdef ENABLE_TEXT_EXTRACT

/* ================================================================
 * Shared last-spoken-text buffer (aligned with Android accessibility)
 * ================================================================ */
static char *last_spoken_text = NULL;
static int copy_feedback_in_progress = 0;

static void save_last_spoken_text(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    if (last_spoken_text) free(last_spoken_text);
    last_spoken_text = (char *)malloc(strlen(text) + 1);
    if (last_spoken_text) strcpy(last_spoken_text, text);
}

/* ================================================================
 * Platform-specific TTS includes
 * ================================================================ */
#if defined(_WIN32) && defined(ENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD)
#include <windows.h>
#include <Tolk.h>
#elif defined(__UWP__)
/* UWP TTS implemented in cpymo_backend_uwp.cpp */
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#elif defined(__APPLE__) && !defined(__IOS__)
extern void cpymo_macos_accessibility_announce(const char *text);
#elif defined(__linux__) && defined(ENABLE_TEXT_EXTRACT_LINUX_ACCESSIBILITY)
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__IOS__)
extern void cpymo_ios_accessibility_announce(const char *text);
extern void cpymo_ios_accessibility_play_sound(int sound_type);
#endif

/* === Sound type constants (aligned with Android) === */
#define SOUND_ENTER  1
#define SOUND_MENU   2
#define SOUND_SELECT 3

/* === Shared SDL2 Accessibility Sound System (Windows, macOS, Linux) === */
#if !defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY) && !defined(ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY)

extern void cpymo_sdl2_accessibility_vibrate(int milliseconds);

static SDL_AudioDeviceID accessibility_audio_dev = 0;
static SDL_AudioSpec accessibility_audio_spec = {0};
static Uint8 *accessibility_wav_bufs[4] = {NULL, NULL, NULL, NULL};
static Uint32 accessibility_wav_lens[4] = {0, 0, 0, 0};

/* Boost WAV sample amplitude to match Android SoundPool perceived volume.
 * Android SoundPool plays at full system volume through ASSISTANCE_SONIFICATION
 * audio path, while SDL2 outputs raw PCM without any system gain stage.
 * A gain of 2.0x (+6dB) compensates for this difference. */
static void amplify_wav_buffer(Uint8 *buf, Uint32 len, SDL_AudioFormat format, float gain)
{
    if (gain <= 1.0f || buf == NULL) return;

    if (SDL_AUDIO_ISFLOAT(format)) {
        float *samples = (float *)buf;
        Uint32 count = len / (Uint32)sizeof(float);
        for (Uint32 i = 0; i < count; i++) {
            float s = samples[i] * gain;
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            samples[i] = s;
        }
    } else if (SDL_AUDIO_ISSIGNED(format)) {
        int bytes_per_sample = (int)SDL_AUDIO_BITSIZE(format) / 8;
        Uint32 count = len / (Uint32)bytes_per_sample;
        if (bytes_per_sample == 2) {
            Sint16 *samples = (Sint16 *)buf;
            for (Uint32 i = 0; i < count; i++) {
                int s = (int)((float)samples[i] * gain);
                if (s > 32767) s = 32767;
                else if (s < -32768) s = -32768;
                samples[i] = (Sint16)s;
            }
        } else if (bytes_per_sample == 4) {
            Sint32 *samples = (Sint32 *)buf;
            for (Uint32 i = 0; i < count; i++) {
                float s = (float)samples[i] * gain;
                if (s > 2147483647.0f) s = 2147483647.0f;
                else if (s < -2147483648.0f) s = -2147483648.0f;
                samples[i] = (Sint32)s;
            }
        }
    } else {
        /* Unsigned (e.g. 8-bit): center at 128, scale, clamp, re-center */
        for (Uint32 i = 0; i < len; i++) {
            int s = (int)((float)((int)buf[i] - 128) * gain) + 128;
            if (s > 255) s = 255;
            else if (s < 0) s = 0;
            buf[i] = (Uint8)s;
        }
    }
}

void cpymo_sdl2_accessibility_sound_init(void)
{
    /* Load WAV files first to get their actual audio specs */
    char *base_path = SDL_GetBasePath();
    const char *sound_files[] = { NULL, "enter.wav", "menu.wav", "select.wav" };
    SDL_AudioSpec wav_spec = {0};
    int have_spec = 0;

    for (int i = 1; i <= 3; i++) {
        SDL_AudioSpec spec;
        char full_path[1024];
        int loaded = 0;

        /* Try executable directory first */
        if (base_path) {
            snprintf(full_path, sizeof(full_path), "%s%s", base_path, sound_files[i]);
            if (SDL_LoadWAV(full_path, &spec,
                    &accessibility_wav_bufs[i], &accessibility_wav_lens[i]) != NULL)
                loaded = 1;
        }
        /* Fallback to current working directory */
        if (!loaded) {
            if (SDL_LoadWAV(sound_files[i], &spec,
                    &accessibility_wav_bufs[i], &accessibility_wav_lens[i]) == NULL) {
                accessibility_wav_bufs[i] = NULL;
                accessibility_wav_lens[i] = 0;
            } else {
                loaded = 1;
            }
        }

        if (loaded) {
            /* Boost volume to match Android SoundPool perceived loudness */
            amplify_wav_buffer(accessibility_wav_bufs[i], accessibility_wav_lens[i],
                               spec.format, 2.0f);
            if (!have_spec) {
                wav_spec = spec;
                have_spec = 1;
            }
        }
    }
    if (base_path) SDL_free(base_path);

    /* Open audio device with WAV's actual spec (not hardcoded) */
    if (have_spec) {
        accessibility_audio_spec = wav_spec;
        accessibility_audio_dev = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, 0);
        if (accessibility_audio_dev) {
            /* Prime the audio pipeline: queue a tiny silence buffer and unpause
             * so Windows WASAPI starts its audio thread now, not on first play.
             * No blocking wait — SDL2 audio thread runs asynchronously. */
            int bytes_per_sample = (int)SDL_AUDIO_BITSIZE(wav_spec.format) / 8;
            int channels = (int)wav_spec.channels;
            int silence_samples = (wav_spec.freq * channels) / 200; /* 5ms */
            Uint32 silence_len = (Uint32)(silence_samples * bytes_per_sample);
            Uint8 *silence = (Uint8 *)SDL_calloc(1, silence_len);
            if (silence) {
                SDL_QueueAudio(accessibility_audio_dev, silence, silence_len);
                SDL_PauseAudioDevice(accessibility_audio_dev, 0);
                SDL_free(silence);
            }
        }
    }
}

void cpymo_sdl2_accessibility_sound_free(void)
{
    for (int i = 1; i <= 3; i++) {
        if (accessibility_wav_bufs[i]) {
            SDL_FreeWAV(accessibility_wav_bufs[i]);
            accessibility_wav_bufs[i] = NULL;
        }
    }
    if (accessibility_audio_dev) {
        SDL_CloseAudioDevice(accessibility_audio_dev);
        accessibility_audio_dev = 0;
    }
    if (last_spoken_text) {
        free(last_spoken_text);
        last_spoken_text = NULL;
    }
}

void cpymo_sdl2_accessibility_sound_reset(void)
{
    /* Close and reopen the audio device on the new default output.
     * Windows reassigns the default device when headphones are
     * plugged/unplugged; reopened devices pick up the new output. */
    if (accessibility_audio_dev) {
        SDL_CloseAudioDevice(accessibility_audio_dev);
        accessibility_audio_dev = 0;
    }
    if (accessibility_audio_spec.format == 0) return;

    accessibility_audio_dev = SDL_OpenAudioDevice(NULL, 0, &accessibility_audio_spec, NULL, 0);
    if (accessibility_audio_dev) {
        int bytes_per_sample = (int)SDL_AUDIO_BITSIZE(accessibility_audio_spec.format) / 8;
        int channels = (int)accessibility_audio_spec.channels;
        int silence_samples = (accessibility_audio_spec.freq * channels) / 200;
        Uint32 silence_len = (Uint32)(silence_samples * bytes_per_sample);
        Uint8 *silence = (Uint8 *)SDL_calloc(1, silence_len);
        if (silence) {
            SDL_QueueAudio(accessibility_audio_dev, silence, silence_len);
            SDL_PauseAudioDevice(accessibility_audio_dev, 0);
            SDL_free(silence);
        }
    }
}

void cpymo_sdl2_accessibility_play_sound(int sound_type)
{
    if (sound_type >= 1 && sound_type <= 3
        && accessibility_audio_dev
        && accessibility_wav_bufs[sound_type]) {
        SDL_ClearQueuedAudio(accessibility_audio_dev);
        if (SDL_QueueAudio(accessibility_audio_dev,
                accessibility_wav_bufs[sound_type],
                accessibility_wav_lens[sound_type]) == 0) {
            SDL_PauseAudioDevice(accessibility_audio_dev, 0);
        }
    }
    else {
        /* Fallback to system beep */
#if defined(_WIN32)
        UINT sound = sound_type == 1 ? MB_OK : (sound_type == 2 ? MB_ICONEXCLAMATION : MB_ICONASTERISK);
        MessageBeep(sound);
#endif
    }

    /* === Vibration (scaled for game controller rumble motor inertia) ===
     * Android uses linear vibration motors that respond instantly to short pulses
     * (10ms is perceptible). Game controller rumble motors have rotational inertia
     * and need ~50ms minimum to spin up and be felt. We scale durations up by 6x
     * so the perceived intensity matches Android. */
    switch (sound_type) {
        case SOUND_ENTER:  cpymo_sdl2_accessibility_vibrate(60); break;
        case SOUND_MENU:   cpymo_sdl2_accessibility_vibrate(150); break;
        case SOUND_SELECT: cpymo_sdl2_accessibility_vibrate(60); break;
        default: break;
    }
}

/* === Copy / append-copy last spoken text (aligned with Android) ===
 * Android: two-finger swipe left/right
 * Desktop: F1/F2 on keyboard, LB+DPad Left/Right on controller */

/* Speak feedback without overwriting last_spoken_text (matches Android's
 * textToSpeechWithoutCopy behavior, so repeated copies still get the
 * original game text, not "已复制") */
static void speak_feedback(const char *msg)
{
    if (copy_feedback_in_progress) return;
    copy_feedback_in_progress = 1;
    char *saved = last_spoken_text ? SDL_strdup(last_spoken_text) : NULL;
    cpymo_backend_text_extract(msg);
    if (last_spoken_text) free(last_spoken_text);
    last_spoken_text = saved;
    copy_feedback_in_progress = 0;
}

void cpymo_backend_text_copy_last(void)
{
    if (last_spoken_text == NULL) return;
    SDL_SetClipboardText(last_spoken_text);
    cpymo_sdl2_accessibility_vibrate(60);
    cpymo_sdl2_accessibility_play_sound(SOUND_SELECT);
    speak_feedback("已复制");
}

void cpymo_backend_text_append_copy_last(void)
{
    if (last_spoken_text == NULL) return;
    char *old = SDL_GetClipboardText();
    size_t old_len = old ? strlen(old) : 0;
    size_t new_len = strlen(last_spoken_text);
    if (old_len > SIZE_MAX - new_len - 2) {
        if (old) SDL_free(old);
        return;
    }
    char *combined = (char *)malloc(old_len + new_len + 2);
    if (combined) {
        if (old && old_len > 0) {
            memcpy(combined, old, old_len);
            combined[old_len] = '\n';
            memcpy(combined + old_len + 1, last_spoken_text, new_len + 1);
        } else {
            memcpy(combined, last_spoken_text, new_len + 1);
        }
        SDL_SetClipboardText(combined);
        free(combined);
    }
    if (old) SDL_free(old);
    cpymo_sdl2_accessibility_vibrate(60);
    cpymo_sdl2_accessibility_play_sound(SOUND_SELECT);
    speak_feedback("已追加复制");
}

#endif /* !Android && !iOS */

/* Android and iOS use their native accessibility feedback paths.  The SDL2
 * event loop still requests an audio-device reset, so provide a no-op rather
 * than making these builds depend on the desktop WAV implementation. */
#if defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY) || defined(ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY)
void cpymo_sdl2_accessibility_sound_reset(void) {}
#endif

/* ================================================================
 * Platform-specific TTS backends
 * ================================================================ */

/* --- Windows: Tolk (SAPI, NVDA, ZD, etc.) --- */
#if defined(_WIN32) && defined(ENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD)
void cpymo_backend_text_extract_init(void)
{
    Tolk_Load();
    cpymo_sdl2_accessibility_sound_init();
}

void cpymo_backend_text_extract_free(void)
{
    Tolk_Unload();
    cpymo_sdl2_accessibility_sound_free();
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);

    int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (wide_len == 0) return;

    wchar_t *wide_text = (wchar_t *)malloc((size_t)wide_len * sizeof(*wide_text));
    if (wide_text == NULL) return;

    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide_text, wide_len) != 0)
        Tolk_Output(wide_text, true);

    free(wide_text);
}

/* --- UWP: Windows::Media::SpeechSynthesis --- */
#elif defined(__UWP__)
void cpymo_backend_text_extract_init(void);
void cpymo_backend_text_extract_free(void);
void cpymo_backend_text_extract(const char *text);

/* --- iOS: AVSpeechSynthesizer / VoiceOver --- */
#elif defined(__IOS__)
void cpymo_backend_text_extract_init(void) {}
void cpymo_backend_text_extract_free(void) {
    if (last_spoken_text) { free(last_spoken_text); last_spoken_text = NULL; }
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);
    cpymo_ios_accessibility_announce(text);
}

void cpymo_backend_text_copy_last(void)
{
    extern void cpymo_ios_accessibility_copy_speech_text(const char *text, bool append);
    cpymo_ios_accessibility_copy_speech_text(last_spoken_text, false);
}

void cpymo_backend_text_append_copy_last(void)
{
    extern void cpymo_ios_accessibility_copy_speech_text(const char *text, bool append);
    cpymo_ios_accessibility_copy_speech_text(last_spoken_text, true);
}

/* --- Emscripten: ARIA live region --- */
#elif defined(__EMSCRIPTEN__)
void cpymo_backend_text_extract_init(void)
{
    cpymo_sdl2_accessibility_sound_init();

    emscripten_run_script(
        "var bar = document.getElementById('cpymo-accessibility-bar');"
        "if (!bar) {"
        "bar = document.createElement('div');"
        "bar.id = 'cpymo-accessibility-bar';"
        "bar.setAttribute('role', 'status');"
        "bar.setAttribute('aria-live', 'polite');"
        "bar.setAttribute('aria-atomic', 'true');"
        "bar.style.cssText = 'position:fixed;left:-9999px;top:0;width:1px;height:1px;overflow:hidden;opacity:0;pointer-events:none';"
        "document.body.appendChild(bar);"
        "}"
        "if (!window.cpymoAccessibilityGestures && Module.canvas) {"
        "var c=Module.canvas,s=null,lastSingleTap=0,lastTwoTap=0,hold=0;"
        "var action=function(a){if(Module._cpymo_accessibility_enqueue_action)Module._cpymo_accessibility_enqueue_action(a);};"
        "var direction=function(dx,dy){return Math.abs(dx)>Math.abs(dy)?(dx>0?'ArrowRight':'ArrowLeft'):(dy>0?'ArrowDown':'ArrowUp');};"
        "c.addEventListener('touchstart',function(e){var now=Date.now();if(e.touches.length===1){var t=e.touches[0];s={x:t.clientX,y:t.clientY,n:1,doubleTap:now-lastSingleTap<300};lastSingleTap=now;hold=setTimeout(function(){action(6);s=null},500)}else if(e.touches.length===2){clearTimeout(hold);var a=e.touches[0],b=e.touches[1];s={x:(a.clientX+b.clientX)/2,y:(a.clientY+b.clientY)/2,n:2,skipHold:now-lastTwoTap<300};if(s.skipHold)hold=setTimeout(function(){action(8)},200);lastTwoTap=now}},{passive:true});"
        "c.addEventListener('touchend',function(e){clearTimeout(hold);if(!s)return;var t=e.changedTouches[0],dx=t.clientX-s.x,dy=t.clientY-s.y,d=direction(dx,dy);if(s.n===2&&e.touches.length===0){if(s.skipHold)action(9);if(Math.max(Math.abs(dx),Math.abs(dy))>32){if(d==='ArrowLeft')action(10);else if(d==='ArrowRight')action(11);else if(d==='ArrowDown')action(6)}}else if(s.n===1){if(Math.max(Math.abs(dx),Math.abs(dy))>32){action(d==='ArrowUp'?1:d==='ArrowDown'?2:d==='ArrowLeft'?3:4)}else if(s.doubleTap)action(5)}s=null},{passive:true});"
        "window.cpymoAccessibilityGestures=true;"
        "}"
    );
}

void cpymo_backend_text_extract_free(void)
{
    cpymo_sdl2_accessibility_sound_free();

    emscripten_run_script(
        "var bar = document.getElementById('cpymo-accessibility-bar');"
        "if (bar) bar.parentNode.removeChild(bar);"
    );
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);

    EM_ASM_INT({var b=document.getElementById('cpymo-accessibility-bar');if(b)b.textContent=UTF8ToString($0);return 0}, text);
}

/* --- macOS: NSSpeechSynthesizer --- */
#elif defined(__APPLE__)
void cpymo_backend_text_extract_init(void)
{
    cpymo_sdl2_accessibility_sound_init();
}

void cpymo_backend_text_extract_free(void)
{
    cpymo_sdl2_accessibility_sound_free();
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);
    cpymo_macos_accessibility_announce(text);
}

/* --- Linux: speech-dispatcher (spd-say) --- */
#elif defined(__linux__) && defined(ENABLE_TEXT_EXTRACT_LINUX_ACCESSIBILITY)
void cpymo_backend_text_extract_init(void)
{
    signal(SIGCHLD, SIG_IGN);
    cpymo_sdl2_accessibility_sound_init();
}

void cpymo_backend_text_extract_free(void)
{
    cpymo_sdl2_accessibility_sound_free();
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);

    pid_t child = fork();
    if (child == 0) {
        execlp("spd-say", "spd-say", "--", text, (char *)NULL);
        _exit(127);
    }
}

/* --- Android / fallback --- */
#else
void cpymo_backend_text_extract_init(void) {}
void cpymo_backend_text_extract_free(void) {
    if (last_spoken_text) { free(last_spoken_text); last_spoken_text = NULL; }
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    save_last_spoken_text(text);

#ifdef ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY
    extern void cpymo_android_text_to_speech(const char *text);
    cpymo_android_text_to_speech(text);
#endif

#ifdef ENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD
    SDL_SetClipboardText(text);
#endif

#if !defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY) && !defined(ENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD)
    /* Console / embedded platform: output to stderr for accessibility debugging */
    fprintf(stderr, "[Accessibility] %s\n", text);
#endif
}

/* Android keeps the system clipboard bridge in Java, while the shared backend
 * remains the sole owner of the last game text and input de-duplication. */
#if defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY)
void cpymo_backend_text_copy_last(void) {
    extern void cpymo_android_copy_speech_text(const char *text, bool append);
    cpymo_android_copy_speech_text(last_spoken_text, false);
}
void cpymo_backend_text_append_copy_last(void) {
    extern void cpymo_android_copy_speech_text(const char *text, bool append);
    cpymo_android_copy_speech_text(last_spoken_text, true);
}
#endif

#endif

#endif /* ENABLE_TEXT_EXTRACT */
