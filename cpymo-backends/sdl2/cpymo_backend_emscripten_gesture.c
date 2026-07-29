/* ================================================================
 * CPyMO Emscripten Accessibility Gesture System
 *
 * Provides touch gesture support for the web platform's mobile page,
 * aligned with Android's accessibility gesture scheme.
 *
 * Gesture mapping (matches Android's VisualHelper + GestureDetector):
 *   Single tap          → scan (move mouse to tap position)
 *   Double tap          → OK (Enter key)
 *   Single swipe        → direction keys (Up/Down/Left/Right)
 *   Long press (0.5s)   → Cancel (ESC key)
 *   Two-finger double tap → Skip (Ctrl key)
 *   Two-finger swipe down  → Cancel (ESC key)
 *   Two-finger double press hold → Skip hold (Ctrl down/up)
 *
 * Also provides:
 *   - Vibration via navigator.vibrate()
 *   - Sound via the existing SDL2 accessibility audio system
 *
 * This file is compiled only for Emscripten (__EMSCRIPTEN__).
 * ================================================================ */

#include "../../cpymo/cpymo_prelude.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#ifdef ENABLE_TEXT_EXTRACT

/* Forward declarations */
extern void cpymo_sdl2_accessibility_play_sound(int sound_type);
extern void cpymo_sdl2_accessibility_vibrate(int milliseconds);

/* Deferred key-up callback: used by knock to delay KEYUP so the engine
 * has time to detect the key as pressed (SDL_GetKeyboardState snapshot).
 * Without this delay, KEYDOWN+KEYUP in the same frame would be invisible. */
static void emscripten_inject_key_up_deferred(void *scancode_ptr)
{
    SDL_Scancode sc = (SDL_Scancode)(uintptr_t)scancode_ptr;
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYUP;
    ev.key.keysym.scancode = sc;
    ev.key.state = SDL_RELEASED;
    SDL_PushEvent(&ev);
}

/* Inject a key knock: push KEYDOWN now, schedule KEYUP after 50ms */
static void emscripten_inject_key_knock(SDL_Scancode scancode)
{
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));

    ev.type = SDL_KEYDOWN;
    ev.key.keysym.scancode = scancode;
    ev.key.state = SDL_PRESSED;
    SDL_PushEvent(&ev);

    emscripten_async_call(emscripten_inject_key_up_deferred,
        (void *)(uintptr_t)scancode, 50);
}

/* Inject a key down event */
static void emscripten_inject_key_down(SDL_Scancode scancode)
{
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.scancode = scancode;
    ev.key.state = SDL_PRESSED;
    SDL_PushEvent(&ev);
}

/* Inject a key up event */
static void emscripten_inject_key_up(SDL_Scancode scancode)
{
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYUP;
    ev.key.keysym.scancode = scancode;
    ev.key.state = SDL_RELEASED;
    SDL_PushEvent(&ev);
}

/* Inject a mouse move event */
static void emscripten_inject_mouse_move(int x, int y)
{
    SDL_Event ev;
    SDL_memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEMOTION;
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = 0;
    ev.motion.yrel = 0;
    SDL_PushEvent(&ev);
}

/* Exported to JavaScript: called from gesture handler */
void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_swipe(int direction)
{
    /* direction: 0=up, 1=down, 2=left, 3=right */
    cpymo_sdl2_accessibility_play_sound(3); /* SOUND_SELECT */
    cpymo_sdl2_accessibility_vibrate(10);

    SDL_Scancode sc;
    switch (direction) {
        case 0: sc = SDL_SCANCODE_UP;    break;
        case 1: sc = SDL_SCANCODE_DOWN;  break;
        case 2: sc = SDL_SCANCODE_LEFT;  break;
        case 3: sc = SDL_SCANCODE_RIGHT; break;
        default: return;
    }
    emscripten_inject_key_knock(sc);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_double_tap(void)
{
    /* OK: Enter key */
    cpymo_sdl2_accessibility_play_sound(3); /* SOUND_SELECT */
    cpymo_sdl2_accessibility_vibrate(10);
    emscripten_inject_key_knock(SDL_SCANCODE_RETURN);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_tap(int x, int y)
{
    /* Scan: move mouse to tap position */
    emscripten_inject_mouse_move(x, y);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_long_press(void)
{
    /* Cancel: ESC key */
    cpymo_sdl2_accessibility_play_sound(2); /* SOUND_MENU */
    cpymo_sdl2_accessibility_vibrate(50);
    emscripten_inject_key_knock(SDL_SCANCODE_ESCAPE);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_two_double_tap(void)
{
    /* Skip: Ctrl key */
    cpymo_sdl2_accessibility_play_sound(3); /* SOUND_SELECT */
    cpymo_sdl2_accessibility_vibrate(10);
    emscripten_inject_key_knock(SDL_SCANCODE_LCTRL);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_two_swipe_down(void)
{
    /* Cancel: ESC key */
    cpymo_sdl2_accessibility_play_sound(2); /* SOUND_MENU */
    cpymo_sdl2_accessibility_vibrate(50);
    emscripten_inject_key_knock(SDL_SCANCODE_ESCAPE);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_two_double_press_start(void)
{
    /* Skip hold start: Ctrl down */
    cpymo_sdl2_accessibility_vibrate(20);
    emscripten_inject_key_down(SDL_SCANCODE_LCTRL);
}

void EMSCRIPTEN_KEEPALIVE cpymo_emscripten_gesture_on_two_double_press_end(void)
{
    /* Skip hold end: Ctrl up */
    cpymo_sdl2_accessibility_vibrate(20);
    emscripten_inject_key_up(SDL_SCANCODE_LCTRL);
}

/* ================================================================
 * Initialize gesture system (called from main.c after SDL init)
 * ================================================================ */
void cpymo_emscripten_gesture_init(void)
{
    EM_ASM({
        /* ================================================================
         * Emscripten Accessibility Gesture Detector
         * Aligned with Android's gesture scheme
         * ================================================================ */

        var canvas = document.querySelector("canvas");
        if (!canvas) {
            console.warn("[CPyMO] No canvas found, gesture support disabled");
            return;
        }

        /* Prevent default touch behaviors on canvas */
        canvas.style.touchAction = "none";

        /* Store handlers for cleanup */
        var handlers = new Object();

        var gesture = new Object();
        gesture.startTime = 0;
        gesture.startX = 0;
        gesture.startY = 0;
        gesture.fingerCount = 0;
        gesture.lastTapTime = 0;
        gesture.lastTapX = 0;
        gesture.lastTapY = 0;
        gesture.tapCount = 0;
        gesture.longPressTimer = null;
        gesture.isLongPress = false;
        gesture.moved = false;
        gesture.twoFingerStartTime = 0;
        gesture.twoFingerStartX = 0;
        gesture.twoFingerStartY = 0;
        gesture.twoFingerTapCount = 0;
        gesture.twoFingerLastTapTime = 0;
        gesture.twoFingerDoublePressTimer = null;
        gesture.twoFingerDoublePressHeld = false;

        var SWIPE_THRESHOLD = 10;
        var LONG_PRESS_TIME = 500;
        var DOUBLE_TAP_TIMEOUT = 200;
        var TWO_FINGER_DOUBLE_TAP_TIMEOUT = 400;

        function resetGesture() {
            if (gesture.longPressTimer) {
                clearTimeout(gesture.longPressTimer);
                gesture.longPressTimer = null;
            }
            if (gesture.twoFingerDoublePressTimer) {
                clearTimeout(gesture.twoFingerDoublePressTimer);
                gesture.twoFingerDoublePressTimer = null;
            }
            gesture.startTime = 0;
            gesture.startX = 0;
            gesture.startY = 0;
            gesture.fingerCount = 0;
            gesture.isLongPress = false;
            gesture.moved = false;
            gesture.tapCount = 0;
            gesture.lastTapTime = 0;
            gesture.lastTapX = 0;
            gesture.lastTapY = 0;
            gesture.twoFingerStartTime = 0;
            gesture.twoFingerStartX = 0;
            gesture.twoFingerStartY = 0;
            gesture.twoFingerTapCount = 0;
            gesture.twoFingerLastTapTime = 0;
            gesture.twoFingerDoublePressHeld = false;
        }

        var opts = new Object();
        opts.passive = false;

        canvas.addEventListener("touchstart", handlers.touchstart = function(e) {
            var touches = e.touches;
            gesture.fingerCount = touches.length;
            gesture.startTime = Date.now();
            gesture.moved = false;
            gesture.isLongPress = false;

            if (gesture.fingerCount === 1) {
                gesture.startX = touches[0].clientX;
                gesture.startY = touches[0].clientY;

                /* Start long press timer */
                gesture.longPressTimer = setTimeout(function() {
                    gesture.isLongPress = true;
                    /* C call: long press = cancel */
                    Module.ccall("cpymo_emscripten_gesture_on_long_press",
                        null, [], []);
                    resetGesture();
                }, LONG_PRESS_TIME);

            } else if (gesture.fingerCount === 2) {
                /* Two-finger gesture */
                gesture.twoFingerStartTime = Date.now();
                gesture.twoFingerStartX = (touches[0].clientX + touches[1].clientX) / 2;
                gesture.twoFingerStartY = (touches[0].clientY + touches[1].clientY) / 2;

                /* Two-finger double press timer */
                gesture.twoFingerDoublePressTimer = setTimeout(function() {
                    if (!gesture.moved) {
                        gesture.twoFingerDoublePressHeld = true;
                        Module.ccall("cpymo_emscripten_gesture_on_two_double_press_start",
                            null, [], []);
                    }
                }, DOUBLE_TAP_TIMEOUT);
            }

            e.preventDefault();
        }, opts);

        canvas.addEventListener("touchmove", handlers.touchmove = function(e) {
            if (gesture.fingerCount === 0) return;

            var touches = e.touches;
            if (gesture.fingerCount === 1) {
                var dx = touches[0].clientX - gesture.startX;
                var dy = touches[0].clientY - gesture.startY;
                if (Math.abs(dx) > SWIPE_THRESHOLD || Math.abs(dy) > SWIPE_THRESHOLD) {
                    gesture.moved = true;
                    if (gesture.longPressTimer) {
                        clearTimeout(gesture.longPressTimer);
                        gesture.longPressTimer = null;
                    }
                }
            } else if (gesture.fingerCount === 2) {
                var cx = (touches[0].clientX + touches[1].clientX) / 2;
                var cy = (touches[0].clientY + touches[1].clientY) / 2;
                var dx = cx - gesture.twoFingerStartX;
                var dy = cy - gesture.twoFingerStartY;
                if (Math.abs(dx) > SWIPE_THRESHOLD || Math.abs(dy) > SWIPE_THRESHOLD) {
                    gesture.moved = true;
                    if (gesture.twoFingerDoublePressTimer) {
                        clearTimeout(gesture.twoFingerDoublePressTimer);
                        gesture.twoFingerDoublePressTimer = null;
                    }
                }
            }

            e.preventDefault();
        }, opts);

        canvas.addEventListener("touchend", handlers.touchend = function(e) {
            if (gesture.fingerCount === 0) return;

            if (gesture.longPressTimer) {
                clearTimeout(gesture.longPressTimer);
                gesture.longPressTimer = null;
            }
            if (gesture.twoFingerDoublePressTimer) {
                clearTimeout(gesture.twoFingerDoublePressTimer);
                gesture.twoFingerDoublePressTimer = null;
            }

            /* Handle two-finger double press end */
            if (gesture.twoFingerDoublePressHeld) {
                gesture.twoFingerDoublePressHeld = false;
                Module.ccall("cpymo_emscripten_gesture_on_two_double_press_end",
                    null, [], []);
                resetGesture();
                return;
            }

            if (gesture.isLongPress) {
                resetGesture();
                return;
            }

            if (gesture.fingerCount === 1) {
                if (gesture.moved) {
                    /* Single finger swipe */
                    var touches = e.changedTouches;
                    var dx = touches[0].clientX - gesture.startX;
                    var dy = touches[0].clientY - gesture.startY;

                    var direction;
                    if (Math.abs(dx) > Math.abs(dy)) {
                        direction = dx > 0 ? 3 : 2; /* right:3, left:2 */
                    } else {
                        direction = dy > 0 ? 1 : 0; /* down:1, up:0 */
                    }
                    Module.ccall("cpymo_emscripten_gesture_on_swipe",
                        null, ["number"], [direction]);
                } else {
                    /* Single tap - check for double tap */
                    var now = Date.now();
                    var touches = e.changedTouches;
                    var tapX = touches[0].clientX;
                    var tapY = touches[0].clientY;

                    if (gesture.tapCount === 0) {
                        /* First tap */
                        gesture.tapCount = 1;
                        gesture.lastTapTime = now;
                        gesture.lastTapX = tapX;
                        gesture.lastTapY = tapY;

                        /* Wait for possible second tap */
                        setTimeout(function() {
                            if (gesture.tapCount === 1) {
                                /* Single tap confirmed: scan (move mouse) */
                                var rect = canvas.getBoundingClientRect();
                                var mx = gesture.lastTapX - rect.left;
                                var my = gesture.lastTapY - rect.top;
                                Module.ccall("cpymo_emscripten_gesture_on_tap",
                                    null, ["number", "number"], [mx, my]);
                                gesture.tapCount = 0;
                            }
                        }, DOUBLE_TAP_TIMEOUT);
                    } else if (gesture.tapCount === 1 &&
                               now - gesture.lastTapTime < DOUBLE_TAP_TIMEOUT) {
                        /* Double tap confirmed: OK */
                        gesture.tapCount = 0;
                        Module.ccall("cpymo_emscripten_gesture_on_double_tap",
                            null, [], []);
                    }
                }
            } else if (gesture.fingerCount === 2) {
                if (gesture.moved) {
                    /* Two-finger swipe */
                    var touches = e.changedTouches;
                    var cx = (touches.length >= 2) ?
                        (touches[0].clientX + touches[1].clientX) / 2 : touches[0].clientX;
                    var cy = (touches.length >= 2) ?
                        (touches[0].clientY + touches[1].clientY) / 2 : touches[0].clientY;
                    var dx = cx - gesture.twoFingerStartX;
                    var dy = cy - gesture.twoFingerStartY;

                    /* Two-finger swipe down = cancel */
                    if (dy > SWIPE_THRESHOLD && Math.abs(dy) > Math.abs(dx)) {
                        Module.ccall("cpymo_emscripten_gesture_on_two_swipe_down",
                            null, [], []);
                    }
                } else {
                    /* Two-finger tap - check for double tap */
                    var now = Date.now();
                    if (gesture.twoFingerTapCount === 0) {
                        gesture.twoFingerTapCount = 1;
                        gesture.twoFingerLastTapTime = now;

                        setTimeout(function() {
                            if (gesture.twoFingerTapCount === 1) {
                                gesture.twoFingerTapCount = 0;
                            }
                        }, TWO_FINGER_DOUBLE_TAP_TIMEOUT);
                    } else if (gesture.twoFingerTapCount === 1 &&
                               now - gesture.twoFingerLastTapTime < TWO_FINGER_DOUBLE_TAP_TIMEOUT) {
                        gesture.twoFingerTapCount = 0;
                        Module.ccall("cpymo_emscripten_gesture_on_two_double_tap",
                            null, [], []);
                    }
                }
            }

            resetGesture();
        }, opts);

        canvas.addEventListener("touchcancel", handlers.touchcancel = function(e) {
            if (gesture.twoFingerDoublePressHeld) {
                gesture.twoFingerDoublePressHeld = false;
                Module.ccall("cpymo_emscripten_gesture_on_two_double_press_end",
                    null, [], []);
            }
            resetGesture();
        });

        /* Store handlers for cleanup */
        canvas.__cpm_gesture_handlers = handlers;

        console.log('[CPyMO] Accessibility gesture system initialized');
    });
}

/* ================================================================
 * Cleanup gesture system (remove event listeners)
 * ================================================================ */
void cpymo_emscripten_gesture_free(void)
{
    EM_ASM({
        var canvas = document.querySelector("canvas");
        if (!canvas) return;

        /* Remove stored event handlers */
        if (canvas.__cpm_gesture_handlers) {
            var handlers = canvas.__cpm_gesture_handlers;
            canvas.removeEventListener("touchstart", handlers.touchstart);
            canvas.removeEventListener("touchmove", handlers.touchmove);
            canvas.removeEventListener("touchend", handlers.touchend);
            canvas.removeEventListener("touchcancel", handlers.touchcancel);
            delete canvas.__cpm_gesture_handlers;
        }
    });
}

#endif /* ENABLE_TEXT_EXTRACT */
#endif /* __EMSCRIPTEN__ */