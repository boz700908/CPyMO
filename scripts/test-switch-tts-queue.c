#include "../cpymo-backends/sdl2/switch_tts_queue.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    uint32_t head = 0, tail = 0, slot, i;
    assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_REPLACE_LAST, &slot));
    assert(slot == 0 && head == 1 && tail == 0);
    for (i = 1; i < 8; ++i) {
        assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_APPEND, &slot));
        assert(slot == i);
    }
    assert(!cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_APPEND, &slot));
    assert(head == 8 && tail == 0);
    assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_REPLACE_LAST, &slot));
    assert(slot == 7 && head == 8 && tail == 0);
    assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_INTERRUPT, &slot));
    assert(slot == 0 && head == 9 && tail == 8);

    head = tail = UINT32_MAX - 1;
    for (i = 0; i < 8; ++i)
        assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_APPEND, &slot));
    assert(head - tail == 8);
    assert(!cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_APPEND, &slot));
    assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_REPLACE_LAST, &slot));
    assert(slot == 5 && head - tail == 8);
    assert(cpymo_tts_queue_reserve(&head, &tail, 8, CPYMO_SWITCH_TTS_INTERRUPT, &slot));
    assert(head - tail == 1 && slot == 6);
    assert(!cpymo_tts_queue_reserve(&head, &tail, 0, CPYMO_SWITCH_TTS_APPEND, &slot));
    puts("switch TTS queue tests passed");
    return 0;
}
