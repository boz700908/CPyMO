#include "../cpymo-backends/sdl2/switch_tts_lang.h"
#include <assert.h>
#include <string.h>

static void expect(const char *input, cpymo_tts_language fallback,
                   const char *piece, cpymo_tts_language lang)
{
    cpymo_tts_segment seg;
    size_t n = cpymo_tts_next_segment(input, strlen(input), fallback, &seg);
    assert(n == strlen(piece));
    assert(seg.text == input && seg.length == n && seg.language == lang);
    assert(!memcmp(input, piece, n));
}

int main(void)
{
    cpymo_tts_segment seg;
    const char bad[] = { (char)0xf0, (char)0x9f, 0 };
    expect("你好 hello!", CPYMO_TTS_LANG_ZH, "你好", CPYMO_TTS_LANG_ZH);
    expect(" hello!", CPYMO_TTS_LANG_ZH, " hello!", CPYMO_TTS_LANG_LATIN);
    expect("漢字です。", CPYMO_TTS_LANG_ZH, "漢字です。", CPYMO_TTS_LANG_JA);
    expect("你好 hello 漢字です。", CPYMO_TTS_LANG_ZH, "你好", CPYMO_TTS_LANG_ZH);
    expect(" 漢字です。", CPYMO_TTS_LANG_ZH, " 漢字です。", CPYMO_TTS_LANG_JA);
    expect("かな漢字。", CPYMO_TTS_LANG_ZH, "かな漢字。", CPYMO_TTS_LANG_JA);
    expect("漢字。", CPYMO_TTS_LANG_JA, "漢字。", CPYMO_TTS_LANG_JA);
    expect("漢字。", CPYMO_TTS_LANG_ZH, "漢字。", CPYMO_TTS_LANG_ZH);
    expect("𠀀你好", CPYMO_TTS_LANG_ZH, "𠀀你好", CPYMO_TTS_LANG_ZH);
    expect("안녕 hello", CPYMO_TTS_LANG_ZH, "안녕", CPYMO_TTS_LANG_KO);
    expect("!?", CPYMO_TTS_LANG_KO, "!", CPYMO_TTS_LANG_KO);
    assert(cpymo_tts_next_segment(bad, 2, CPYMO_TTS_LANG_ZH, &seg) == 2);
    assert(seg.language == CPYMO_TTS_LANG_ZH);
    assert(cpymo_tts_next_segment("", 0, CPYMO_TTS_LANG_ZH, &seg) == 0);
    return 0;
}
