#ifndef INCLUDE_CPYMO_BACKEND_INPUT
#define INCLUDE_CPYMO_BACKEND_INPUT

#include <stdbool.h>

typedef struct {
	float mouse_x;
	float mouse_y;

	bool mouse_position_useable : 1;
	bool mouse_button : 1;

	float mouse_wheel_delta;

	bool up : 1;
	bool down : 1;
	bool left : 1;
	bool right : 1;
	bool ok : 1;
	bool cancel : 1;
	bool skip : 1;
	bool hide_window : 1;
	bool copy : 1;
	bool append_copy : 1;
} cpymo_input;

#ifdef ENABLE_TEXT_EXTRACT
/* Native gesture adapters enqueue semantic actions; the shared SDL2 input
 * backend consumes them once per frame into cpymo_input. */
typedef enum {
	CPYMO_ACCESSIBILITY_ACTION_NONE = 0,
	CPYMO_ACCESSIBILITY_ACTION_UP,
	CPYMO_ACCESSIBILITY_ACTION_DOWN,
	CPYMO_ACCESSIBILITY_ACTION_LEFT,
	CPYMO_ACCESSIBILITY_ACTION_RIGHT,
	CPYMO_ACCESSIBILITY_ACTION_OK,
	CPYMO_ACCESSIBILITY_ACTION_CANCEL,
	CPYMO_ACCESSIBILITY_ACTION_SKIP,
	CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_START,
	CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_END,
	CPYMO_ACCESSIBILITY_ACTION_COPY,
	CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY
} cpymo_accessibility_action;

void cpymo_accessibility_enqueue_action(cpymo_accessibility_action action);
#endif

/* Mouse Coord
 *
 * LeftTop - (0, 0)
 * RightBottom - (gameconfig.imagesize.w - 1, gameconfig.imagesize.h - 1)
 *
 * returns true if mouse position is useable.
 * returns false if mouse position is not useable.
 */
cpymo_input cpymo_input_snapshot();

#endif
