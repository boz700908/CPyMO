#ifndef CPYMO_SWITCH_TTS_LANG_H
#define CPYMO_SWITCH_TTS_LANG_H

#include <stddef.h>

typedef enum {
    CPYMO_TTS_LANG_ZH,
    CPYMO_TTS_LANG_JA,
    CPYMO_TTS_LANG_KO,
    CPYMO_TTS_LANG_LATIN,
    CPYMO_TTS_LANG_COUNT
} cpymo_tts_language;

typedef struct {
    const char *text;
    size_t length;
    cpymo_tts_language language;
} cpymo_tts_segment;

size_t cpymo_tts_next_segment(const char *text, size_t length,
                              cpymo_tts_language fallback, cpymo_tts_segment *out);
#endif
