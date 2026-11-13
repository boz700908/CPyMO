#ifndef CPYMO_SWITCH_TTS_QUEUE_H
#define CPYMO_SWITCH_TTS_QUEUE_H

#include "switch_tts.h"
#include <stdint.h>

/* The caller owns the queue lock while reserving and writing the slot. */
static inline bool cpymo_tts_queue_reserve(uint32_t *head, uint32_t *tail,
                                           uint32_t capacity,
                                           cpymo_switch_tts_mode mode,
                                           uint32_t *slot)
{
    if (!head || !tail || !slot || !capacity) return false;
    if (mode == CPYMO_SWITCH_TTS_INTERRUPT) {
        *tail = *head;
    } else if (mode == CPYMO_SWITCH_TTS_REPLACE_LAST && *head != *tail) {
        --*head;
    } else if (*head - *tail >= capacity) {
        return false;
    }
    *slot = *head % capacity;
    ++*head;
    return true;
}
#endif
