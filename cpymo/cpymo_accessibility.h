#ifndef CPYMO_ACCESSIBILITY_H
#define CPYMO_ACCESSIBILITY_H

/* ================================================================
 * Unified Accessibility Sound & Vibration API
 *
 * Sound types (aligned with Android):
 *   SOUND_ENTER  = 1 (confirm)
 *   SOUND_MENU   = 2 (cancel/menu)
 *   SOUND_SELECT = 3 (select/switch)
 *
 * Vibration durations (aligned with Android):
 *   10ms = light (select/switch)
 *   20ms = medium (skip hold)
 *   50ms = heavy (cancel/long press)
 * ================================================================ */

#define SOUND_ENTER  1
#define SOUND_MENU   2
#define SOUND_SELECT 3

#ifdef ENABLE_TEXT_EXTRACT

/* --- Platform detection --- */
#if defined(ENABLE_TEXT_EXTRACT_ANDROID_ACCESSIBILITY)
/* Android: uses VisualHelper.java */
#include "../cpymo-backends/android/app/src/main/cpp/include/cpymo_android.h"

#define cpymo_accessibility_play_sound(X) cpymo_android_play_sound(X)
#define cpymo_accessibility_vibrate(X)    /* Android handles vibration in SDLActivity.java */

#elif defined(ENABLE_TEXT_EXTRACT_IOS_ACCESSIBILITY)
/* iOS: uses cpymo_ios.m */
extern void cpymo_ios_accessibility_play_sound(int sound_type);
extern void cpymo_ios_accessibility_vibrate(int milliseconds);
extern void cpymo_ios_accessibility_game_selector_entered(void);

#define cpymo_accessibility_play_sound(X) cpymo_ios_accessibility_play_sound(X)
#define cpymo_accessibility_vibrate(X)    cpymo_ios_accessibility_vibrate(X)

#else
/* Desktop (SDL2) / Emscripten / Console: uses SDL2 backend */
extern void cpymo_sdl2_accessibility_play_sound(int sound_type);
extern void cpymo_sdl2_accessibility_vibrate(int milliseconds);

#define cpymo_accessibility_play_sound(X) cpymo_sdl2_accessibility_play_sound(X)
#define cpymo_accessibility_vibrate(X)    cpymo_sdl2_accessibility_vibrate(X)

#endif

#else
/* No accessibility: no-op macros */
#define cpymo_accessibility_play_sound(X)
#define cpymo_accessibility_vibrate(X)
#endif

#endif /* CPYMO_ACCESSIBILITY_H */
