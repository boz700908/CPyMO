#pragma once

#include "../../cpymo/cpymo_prelude.h"

#ifdef ENABLE_TEXT_EXTRACT

/* ================================================================
 * Shared Accessibility Implementation for Desktop Platforms
 *
 * Used by: SDL1, Software, ASCII-Art backends
 * (SDL2/UWP have their own implementation with SDL2 audio)
 *
 * Provides:
 *   - cpymo_backend_text_extract_init()   : init TTS & sound
 *   - cpymo_backend_text_extract_free()   : cleanup
 *   - cpymo_backend_text_extract(text)    : speak text
 *   - cpymo_sdl2_accessibility_play_sound(type): play sound (1=enter, 2=menu, 3=select)
 *   - cpymo_sdl2_accessibility_vibrate(ms)    : haptic feedback
 *
 * Sound scheme aligned with Android:
 *   1 = SOUND_ENTER  (enter/confirm)
 *   2 = SOUND_MENU   (menu/cancel)
 *   3 = SOUND_SELECT (select/switch)
 * ================================================================ */

/* --- Platform-specific includes --- */
#if defined(_WIN32)
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#pragma comment(lib, "sapi.lib")
#pragma comment(lib, "ole32.lib")
#elif defined(__APPLE__) && !defined(__IOS__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#ifdef __OBJC__
#import <AppKit/AppKit.h>
#endif
#include <AudioToolbox/AudioToolbox.h>
#endif
#elif defined(__linux__)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#endif

/* --- Sound effects (aligned with Android's SoundPool WAV playback) --- */

#if defined(_WIN32)
/* Windows: try MessageBeep as fallback (WAV loading via SDL not available in these backends) */
void cpymo_sdl2_accessibility_play_sound(int sound_type)
{
	/* Map to system sounds: 1=enter(OK), 2=menu(warning), 3=select(info) */
	UINT sound = sound_type == 1 ? MB_OK :
	             (sound_type == 2 ? MB_ICONEXCLAMATION : MB_ICONASTERISK);
	MessageBeep(sound);
}
#elif defined(__APPLE__) && !defined(__IOS__)
void cpymo_sdl2_accessibility_play_sound(int sound_type)
{
	/* macOS: use system alert sound for all types */
	AudioServicesPlaySystemSound(kSystemSoundID_UserPreferredAlert);
	(void)sound_type;
}
#elif defined(__linux__)
void cpymo_sdl2_accessibility_play_sound(int sound_type)
{
	/* Linux: try aplay with WAV files (same names as Android) */
	const char *sound_files[] = { NULL, "enter.wav", "menu.wav", "select.wav" };
	if (sound_type >= 1 && sound_type <= 3) {
		pid_t child = fork();
		if (child == 0) {
			execlp("aplay", "aplay", "-q", sound_files[sound_type], (char *)NULL);
			/* fallback to system beep */
			execlp("beep", "beep", (char *)NULL);
			_exit(127);
		}
	}
}
#else
void cpymo_sdl2_accessibility_play_sound(int sound_type) { (void)sound_type; }
#endif

/* --- Haptic / vibration --- */

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
/* Desktop platforms: vibration via gamepad is not available in SDL1/Software/ASCII-Art backends.
 * SDL2 and UWP backends have their own implementation in cpymo_backend_input.c */
void cpymo_sdl2_accessibility_vibrate(int milliseconds) { (void)milliseconds; }
#else
void cpymo_sdl2_accessibility_vibrate(int milliseconds) { (void)milliseconds; }
#endif

/* --- TTS backends --- */

#if defined(_WIN32)
/* Windows SAPI (Speech API) - no Tolk dependency, uses built-in voices */
static ISpVoice *cpymo_sapi_voice = NULL;

void cpymo_backend_text_extract_init(void)
{
	if (SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
		/* CoInitialize may have been called already; ignore error */
	}
	if (FAILED(CoCreateInstance(&CLSID_SpVoice, NULL, CLSCTX_ALL,
	                            &IID_ISpVoice, (void **)&cpymo_sapi_voice))) {
		cpymo_sapi_voice = NULL;
	}
}

void cpymo_backend_text_extract_free(void)
{
	if (cpymo_sapi_voice) {
		ISpVoice_Release(cpymo_sapi_voice);
		cpymo_sapi_voice = NULL;
	}
	CoUninitialize();
}

void cpymo_backend_text_extract(const char *text)
{
	if (text == NULL || text[0] == '\0') return;
	if (cpymo_sapi_voice == NULL) return;

	int wide_len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
	if (wide_len <= 0) return;

	wchar_t *wide_text = (wchar_t *)malloc((size_t)wide_len * sizeof(wchar_t));
	if (wide_text == NULL) return;

	if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide_text, wide_len) > 0) {
		ISpVoice_Speak(cpymo_sapi_voice, wide_text, SPF_ASYNC, NULL);
	}
	free(wide_text);
}

#elif defined(__APPLE__) && !defined(__IOS__)
/* macOS NSSpeechSynthesizer (Objective-C only) */
void cpymo_backend_text_extract_init(void) {}

void cpymo_backend_text_extract_free(void) {}

void cpymo_backend_text_extract(const char *text)
{
#ifdef __OBJC__
	if (text == NULL || text[0] == '\0') return;

	NSString *announcement = [NSString stringWithUTF8String:text];
	if (announcement.length == 0) return;

	dispatch_async(dispatch_get_main_queue(), ^{
		static NSSpeechSynthesizer *speaker;
		if (speaker == nil) speaker = [[NSSpeechSynthesizer alloc] init];
		[speaker stopSpeaking];
		[speaker startSpeakingString:announcement];
	});
#else
	(void)text;
#endif
}

#elif defined(__linux__)
/* Linux speech-dispatcher (spd-say) */
void cpymo_backend_text_extract_init(void)
{
	signal(SIGCHLD, SIG_IGN);
}

void cpymo_backend_text_extract_free(void) {}

void cpymo_backend_text_extract(const char *text)
{
	if (text == NULL || text[0] == '\0') return;

	pid_t child = fork();
	if (child == 0) {
		execlp("spd-say", "spd-say", "--", text, (char *)NULL);
		/* fallback to espeak if spd-say not found */
		execlp("espeak", "espeak", text, (char *)NULL);
		_exit(127);
	}
}

#else
/* Fallback: output to stdout */
void cpymo_backend_text_extract_init(void) {}

void cpymo_backend_text_extract_free(void) {}

void cpymo_backend_text_extract(const char *text)
{
	if (text == NULL || text[0] == '\0') return;
	fprintf(stderr, "[TTS] %s\n", text);
}
#endif

#endif /* ENABLE_TEXT_EXTRACT */