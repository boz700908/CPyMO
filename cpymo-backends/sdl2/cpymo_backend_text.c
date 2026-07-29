#include "../../cpymo/cpymo_prelude.h"
#include "../include/cpymo_backend_text.h"
#include "cpymo_import_sdl2.h"

#ifndef DISABLE_STB_TRUETYPE

#include "../../cpymo/cpymo_utils.h"
#include "../../cpymo/cpymo_parser.h"
#include "../../stb/stb_truetype.h"
#include "../include/cpymo_backend_image.h"
#include <stdlib.h>
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

#ifdef ENABLE_TEXT_EXTRACT

/* === Sound type constants (aligned with Android) === */
#define SOUND_ENTER  1
#define SOUND_MENU   2
#define SOUND_SELECT 3

/* === Shared SDL2 Accessibility Sound System (Windows, macOS, Linux) === */
#if !defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY) && !defined(ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY)

static SDL_AudioDeviceID accessibility_audio_dev = 0;
static Uint8 *accessibility_wav_bufs[4] = {NULL, NULL, NULL, NULL};
static Uint32 accessibility_wav_lens[4] = {0, 0, 0, 0};

void cpymo_sdl2_accessibility_sound_init(void)
{
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;

    accessibility_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE);

    const char *sound_files[] = { NULL, "enter.wav", "menu.wav", "select.wav" };
    for (int i = 1; i <= 3; i++) {
        SDL_AudioSpec wav_spec;
        if (SDL_LoadWAV(sound_files[i], &wav_spec,
                &accessibility_wav_bufs[i], &accessibility_wav_lens[i]) == NULL) {
            accessibility_wav_bufs[i] = NULL;
            accessibility_wav_lens[i] = 0;
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
            return;
        }
    }

    /* Fallback to system beep */
#if defined(_WIN32)
    UINT sound = sound_type == 1 ? MB_OK : (sound_type == 2 ? MB_ICONEXCLAMATION : MB_ICONASTERISK);
    MessageBeep(sound);
#endif
}

#endif /* !Android && !iOS */

/* === Vibration via gamepad (aligned with Android haptic) === */
void cpymo_sdl2_accessibility_vibrate(int milliseconds)
{
    extern SDL_GameController **gamecontrollers;
    extern size_t gamecontrollers_count;

    for (size_t i = 0; i < gamecontrollers_count; ++i) {
        if (gamecontrollers[i] != NULL)
            SDL_GameControllerRumble(gamecontrollers[i], SDL_MAX_UINT16, SDL_MAX_UINT16, (Uint32)milliseconds);
    }

#ifdef __EMSCRIPTEN__
    /* Browser Vibration API for mobile web */
    EM_ASM({
        if (navigator.vibrate) {
            navigator.vibrate($0);
        }
    }, milliseconds);
#endif
}

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
void cpymo_backend_text_extract_free(void) {}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    cpymo_ios_accessibility_announce(text);
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

    pid_t child = fork();
    if (child == 0) {
        execlp("spd-say", "spd-say", "--", text, (char *)NULL);
        _exit(127);
    }
}

/* --- Android / fallback --- */
#else
void cpymo_backend_text_extract_init(void) {}
void cpymo_backend_text_extract_free(void) {}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;

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
#endif

#endif /* ENABLE_TEXT_EXTRACT */