#include "switch_tts_lang.h"
#include <stdint.h>

enum { NEUTRAL = -1, HAN = -2 };

static size_t decode(const unsigned char *s, size_t n, uint32_t *out)
{
    uint32_t c, min;
    size_t i, len;
    if (!n) return 0;
    c = s[0];
    if (c < 0x80) { *out = c; return 1; }
    if (c >= 0xc2 && c <= 0xdf) { len = 2; c &= 0x1f; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { len = 3; c &= 0x0f; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { len = 4; c &= 7; min = 0x10000; }
    else { *out = 0xfffd; return 1; }
    if (n < len) { *out = 0xfffd; return 1; }
    for (i = 1; i < len; ++i) {
        if ((s[i] & 0xc0) != 0x80) { *out = 0xfffd; return 1; }
        c = (c << 6) | (s[i] & 0x3f);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
        *out = 0xfffd; return 1;
    }
    *out = c;
    return len;
}

static int classify(uint32_t c)
{
    if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x31f0 && c <= 0x31ff) ||
        (c >= 0xff66 && c <= 0xff9d) || (c >= 0x1b000 && c <= 0x1b16f))
        return CPYMO_TTS_LANG_JA;
    if ((c >= 0xac00 && c <= 0xd7af) || (c >= 0x1100 && c <= 0x11ff) ||
        (c >= 0x3130 && c <= 0x318f)) return CPYMO_TTS_LANG_KO;
    if ((c >= 0x3400 && c <= 0x4dbf) || (c >= 0x4e00 && c <= 0x9fff) ||
        (c >= 0xf900 && c <= 0xfaff) || (c >= 0x20000 && c <= 0x323af)) return HAN;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= 0xc0 && c <= 0x2af)) return CPYMO_TTS_LANG_LATIN;
    return NEUTRAL;
}

static int sentence_break(uint32_t c)
{
    return c == '.' || c == '!' || c == '?' || c == '\n' ||
           c == 0x3002 || c == 0xff01 || c == 0xff1f;
}

static int han_language(const char *text, size_t len, int current,
                        cpymo_tts_language fallback)
{
    size_t pos = 0;
    uint32_t c;
    if (current == CPYMO_TTS_LANG_JA) return CPYMO_TTS_LANG_JA;
    while (pos < len) {
        int type;
        pos += decode((const unsigned char *)text + pos, len - pos, &c);
        type = classify(c);
        if (type == CPYMO_TTS_LANG_JA) return CPYMO_TTS_LANG_JA;
        if (type == CPYMO_TTS_LANG_KO || type == CPYMO_TTS_LANG_LATIN ||
            sentence_break(c)) break;
    }
    return fallback == CPYMO_TTS_LANG_JA ? CPYMO_TTS_LANG_JA : CPYMO_TTS_LANG_ZH;
}

size_t cpymo_tts_next_segment(const char *text, size_t length,
                              cpymo_tts_language fallback, cpymo_tts_segment *out)
{
    size_t pos = 0, end = 0, step, neutral = 0;
    int current = NEUTRAL;
    uint32_t c;
    if (!text || !out || !length) return 0;
    if (fallback < 0 || fallback >= CPYMO_TTS_LANG_COUNT) fallback = CPYMO_TTS_LANG_ZH;
    while (end < length) {
        step = decode((const unsigned char *)text + end, length - end, &c);
        end += step;
        if (sentence_break(c)) break;
    }
    while (pos < end) {
        int type;
        step = decode((const unsigned char *)text + pos, end - pos, &c);
        type = classify(c);
        if (type == HAN) type = han_language(text + pos + step, end - pos - step, current, fallback);
        if (type != NEUTRAL) {
            if (current != NEUTRAL && type != current) {
                out->text = text;
                out->length = neutral ? neutral : pos;
                out->language = (cpymo_tts_language)current;
                return out->length;
            }
            current = type;
            neutral = 0;
        } else if (current != NEUTRAL && !neutral && !sentence_break(c)) {
            neutral = pos;
        }
        pos += step;
    }
    out->text = text;
    out->length = end;
    out->language = current == NEUTRAL ? fallback : (cpymo_tts_language)current;
    return end;
}
