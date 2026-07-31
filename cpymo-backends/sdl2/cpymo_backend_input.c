#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_engine.h"
#include "../include/cpymo_backend_input.h"
#include "cpymo_import_sdl2.h"

extern SDL_Renderer *renderer;
extern SDL_Window *window;
extern cpymo_engine engine;

float mouse_wheel;

SDL_GameController **gamecontrollers = NULL;
size_t gamecontrollers_count = 0;

#ifdef ENABLE_TEXT_EXTRACT
static SDL_Haptic **haptics = NULL;
static size_t haptics_count = 0;
#endif

#ifdef ENABLE_TEXT_EXTRACT
/* Different native gesture callbacks can enqueue more than one action before
 * the next frame. Keep pending actions as bits so a hold-end action cannot be
 * overwritten by a following copy or navigation action. */
static SDL_atomic_t accessibility_actions;
static bool accessibility_skip_held;

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define CPYMO_ACCESSIBILITY_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define CPYMO_ACCESSIBILITY_EXPORT
#endif

CPYMO_ACCESSIBILITY_EXPORT void cpymo_accessibility_enqueue_action(
	cpymo_accessibility_action action)
{
	if (action > CPYMO_ACCESSIBILITY_ACTION_NONE &&
		action <= CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY) {
		int old_actions;
		const int action_bit = 1 << ((int)action - 1);
		do {
			old_actions = SDL_AtomicGet(&accessibility_actions);
		} while (!SDL_AtomicCAS(&accessibility_actions, old_actions,
			old_actions | action_bit));
	}
}

void cpymo_sdl2_accessibility_vibrate(int milliseconds)
{
    /* Primary: SDL_GameControllerRumble (XInput / Xbox controllers) */
    for (size_t i = 0; i < gamecontrollers_count; ++i) {
        if (gamecontrollers[i] != NULL)
            SDL_GameControllerRumble(gamecontrollers[i], 0xFFFF, 0xFFFF, (Uint32)milliseconds);
    }

    /* Fallback: SDL_Haptic rumble (DirectInput / non-XInput controllers) */
    for (size_t i = 0; i < haptics_count; ++i) {
        if (haptics[i] != NULL)
            SDL_HapticRumblePlay(haptics[i], 1.0f, (Uint32)milliseconds);
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

#endif /* ENABLE_TEXT_EXTRACT */

cpymo_input cpymo_input_snapshot()
{
	cpymo_input out;
	memset(&out, 0, sizeof(out));

	const Uint8 *keyboard = SDL_GetKeyboardState(NULL);

	if (keyboard[SDL_SCANCODE_LALT] == 0 &&
		keyboard[SDL_SCANCODE_RALT] == 0)
	{
		out.up = keyboard[SDL_SCANCODE_UP];
		out.down = keyboard[SDL_SCANCODE_DOWN];
		out.left = keyboard[SDL_SCANCODE_LEFT];
		out.right = keyboard[SDL_SCANCODE_RIGHT];
		out.ok = 
			keyboard[SDL_SCANCODE_KP_ENTER] || 
			keyboard[SDL_SCANCODE_RETURN] || 
			keyboard[SDL_SCANCODE_SPACE];
		out.cancel = 
			keyboard[SDL_SCANCODE_ESCAPE] || 
			keyboard[SDL_SCANCODE_CANCEL] || 
			keyboard[SDL_SCANCODE_AC_BACK] ||
			keyboard[SDL_SCANCODE_MENU] ||
			keyboard[SDL_SCANCODE_APPLICATION];
		out.skip = 
			keyboard[SDL_SCANCODE_LCTRL] || 
			keyboard[SDL_SCANCODE_RCTRL];
		out.hide_window = 
			keyboard[SDL_SCANCODE_LSHIFT] || 
			keyboard[SDL_SCANCODE_RSHIFT];
	}
	else {
		out.up = 0;
		out.down = 0;
		out.left = 0;
		out.right = 0;
		out.ok = 0;
		out.cancel = 0;
		out.skip = 0;
		out.hide_window = 0;
	}

#ifdef ENABLE_TEXT_EXTRACT
	int pending_accessibility_actions = SDL_AtomicSet(&accessibility_actions, 0);
	for (int action = CPYMO_ACCESSIBILITY_ACTION_UP;
		action <= CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY; ++action) {
		if ((pending_accessibility_actions & (1 << (action - 1))) == 0)
			continue;
		switch ((cpymo_accessibility_action)action) {
		case CPYMO_ACCESSIBILITY_ACTION_UP: out.up = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_DOWN: out.down = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_LEFT: out.left = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_RIGHT: out.right = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_OK: out.ok = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_CANCEL: out.cancel = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_SKIP: out.skip = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_START: accessibility_skip_held = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_END: accessibility_skip_held = false; break;
		case CPYMO_ACCESSIBILITY_ACTION_COPY: out.copy = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY: out.append_copy = true; break;
		case CPYMO_ACCESSIBILITY_ACTION_NONE: break;
		}
	}
	out.skip = out.skip || accessibility_skip_held;
#endif

#ifdef DISABLE_MOUSE
	out.mouse_position_useable = false;
	out.mouse_button = false;
	out.mouse_wheel_delta = 0;
#else
	float scale_x, scale_y;
	SDL_RenderGetScale(renderer, &scale_x, &scale_y);

    int point_w, point_h, pixel_w, pixel_h;
    SDL_GetWindowSize(window, &point_w, &point_h);
    SDL_GetRendererOutputSize(renderer, &pixel_w, &pixel_h);
    scale_x *= (float) point_w / pixel_w;
    scale_y *= (float) point_h / pixel_h;

	int mx, my;
	Uint32 mouse_state = SDL_GetMouseState(&mx, &my);

	out.mouse_button = (mouse_state & SDL_BUTTON_LMASK) != 0;
	out.cancel |= (mouse_state & SDL_BUTTON_RMASK) != 0;
	out.mouse_position_useable = true;

	SDL_Rect viewport;
#ifndef ENABLE_SCREEN_FORCE_CENTERED
	SDL_RenderGetViewport(renderer, &viewport);
#else
	float game_w = engine.gameconfig.imagesize_w;
	float game_h = engine.gameconfig.imagesize_h;
	cpymo_utils_match_rect(
		SCREEN_WIDTH, SCREEN_HEIGHT,
		&game_w, &game_h);
	float x = 0, y = 0;
	cpymo_utils_center(SCREEN_WIDTH, SCREEN_HEIGHT, game_w, game_h, &x, &y);
	viewport.x = (int)x;
	viewport.y = (int)y;
	viewport.w = (int)game_w;
	viewport.h = (int)game_h;
#endif

	out.mouse_x = ((float)mx / scale_x - viewport.x) / (float)viewport.w;
	out.mouse_y = ((float)my / scale_y - viewport.y) / (float)viewport.h;
	out.mouse_wheel_delta = mouse_wheel;

	if (
		out.mouse_x >= 1.0f 
		|| out.mouse_x < 0.0f 
		|| out.mouse_y >= 1.0f 
		|| out.mouse_y < 0.0f 
		|| SDL_GetMouseFocus() != window)
		out.mouse_position_useable = false;

	out.mouse_x *= engine.gameconfig.imagesize_w;
	out.mouse_y *= engine.gameconfig.imagesize_h;

#endif

	#define MAP_CONTROLLER(OUT_KEY, CONTROLLER_KEY) \
		for (size_t i = 0; i < gamecontrollers_count; ++i) \
			if (gamecontrollers[i] != NULL && SDL_GameControllerGetButton(gamecontrollers[i], CONTROLLER_KEY)) { \
				OUT_KEY = true; \
				break; \
			}

	MAP_CONTROLLER(out.up, SDL_CONTROLLER_BUTTON_DPAD_UP);
	MAP_CONTROLLER(out.down, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
	MAP_CONTROLLER(out.left, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
	MAP_CONTROLLER(out.right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_START);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_GUIDE);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_BACK);

	MAP_CONTROLLER(out.hide_window, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
	MAP_CONTROLLER(out.skip, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
	
#if defined __SWITCH__ || defined __PSP__ || defined __PSV__
	MAP_CONTROLLER(out.ok, SDL_CONTROLLER_BUTTON_B);
	MAP_CONTROLLER(out.ok, SDL_CONTROLLER_BUTTON_X);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_A);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_Y);
#else
	MAP_CONTROLLER(out.ok, SDL_CONTROLLER_BUTTON_A);
	MAP_CONTROLLER(out.ok, SDL_CONTROLLER_BUTTON_Y);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_B);
	MAP_CONTROLLER(out.cancel, SDL_CONTROLLER_BUTTON_X);
#endif


	for (size_t i = 0; i < gamecontrollers_count; ++i) {
		if (gamecontrollers[i] == NULL) continue;
		if (abs(SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_TRIGGERLEFT)) > 16384)
			out.hide_window = true;
		
		if (abs(SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) > 16384)
			out.skip = true;

		Sint16 x = SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_LEFTX);
		Sint16 y = SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_LEFTY);

		Sint16 x2 = SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_RIGHTX);
		Sint16 y2 = SDL_GameControllerGetAxis(gamecontrollers[i], SDL_CONTROLLER_AXIS_RIGHTY);

		if (abs(x2) > abs(x)) x = x2;
		if (abs(y2) > abs(y)) y = y2;

		if (x > 30000) out.right = true;
		else if (x < -30000) out.left = true;

		if (y > 30000) out.down = true;
		else if (y < -30000) out.up = true;
	}

	#undef MAP_CONTROLLER
	
	return out;
}

void cpymo_input_free_joysticks() 
{
	for (size_t i = 0; i < gamecontrollers_count; ++i)
		if (gamecontrollers[i]) SDL_GameControllerClose(gamecontrollers[i]);
	if (gamecontrollers) free(gamecontrollers);
	gamecontrollers = NULL;
	gamecontrollers_count = 0;

#ifdef ENABLE_TEXT_EXTRACT
	for (size_t i = 0; i < haptics_count; ++i)
		if (haptics[i]) SDL_HapticClose(haptics[i]);
	if (haptics) free(haptics);
	haptics = NULL;
	haptics_count = 0;
#endif
}

void cpymo_input_refresh_joysticks() 
{
	cpymo_input_free_joysticks();

	int total_joysticks = SDL_NumJoysticks();

	/* Count game controllers */
	for (int i = 0; i < total_joysticks; ++i) 
		if (SDL_IsGameController(i)) 
			gamecontrollers_count++;

	if (gamecontrollers_count)
		gamecontrollers = (SDL_GameController **)malloc(sizeof(gamecontrollers[0]) * gamecontrollers_count);
	if (gamecontrollers == NULL) gamecontrollers_count = 0;

	if (gamecontrollers) {
		memset(gamecontrollers, 0, sizeof(gamecontrollers[0]) * gamecontrollers_count);
		size_t j = 0;
		for (int i = 0; i < total_joysticks; ++i)
			if (SDL_IsGameController(i))
				gamecontrollers[j++] = SDL_GameControllerOpen(i);
	}

#ifdef ENABLE_TEXT_EXTRACT
	/* Open haptic from joysticks that are NOT game controllers.
	 * Must NOT open haptic from a joystick already owned by SDL_GameController;
	 * doing so conflicts with SDL_GameControllerRumble on Windows/XInput. */
	{
		int non_gc_count = 0;
		for (int i = 0; i < total_joysticks; ++i)
			if (!SDL_IsGameController(i))
				non_gc_count++;

		if (non_gc_count) {
			haptics = (SDL_Haptic **)malloc(sizeof(haptics[0]) * non_gc_count);
			if (haptics) {
				memset(haptics, 0, sizeof(haptics[0]) * non_gc_count);
				size_t j = 0;
				for (int i = 0; i < total_joysticks; ++i) {
					if (SDL_IsGameController(i)) continue;
					SDL_Joystick *joy = SDL_JoystickOpen(i);
					if (joy) {
						SDL_Haptic *haptic = SDL_HapticOpenFromJoystick(joy);
						if (haptic && SDL_HapticRumbleInit(haptic) == 0) {
							haptics[j++] = haptic;
						} else if (haptic) {
							SDL_HapticClose(haptic);
						}
						SDL_JoystickClose(joy);
					}
				}
				haptics_count = j;
			}
		}
	}
#endif
}
