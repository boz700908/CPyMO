#ifndef INCLUDE_CPYMO_LUA_HUD
#define INCLUDE_CPYMO_LUA_HUD

#include "cpymo_error.h"
#include "cpymo_color.h"
#include "cpymo_str.h"
#include <stdbool.h>

/* Persistent, non-focusable on-screen readouts driven by Lua
 * (cpymo.ui.hud / cpymo.ui.hud_remove).
 *
 * They are drawn last, on every page: over the running game and over any UI
 * that is open, so a script can keep something like a countdown visible while
 * the player is inside the config screen.  A readout never takes focus and is
 * never clickable; it is pure output.
 *
 * When the text of a readout changes it is announced through
 * cpymo_a11y_speak(), so the existing accessibility layer reads the new value
 * out without the script having to speak it itself.
 *
 * Only present when CPYMO_FEATURE_LEVEL >= 1. */

struct cpymo_lua_hud_item;

struct cpymo_lua_hud_options {
    /* Position of the top-left corner in logical screen coordinates.
     * When has_pos is false the item is placed at the top-right corner and
     * stacked below the readouts that were created before it. */
    bool has_pos;
    float x, y;

    /* Font size in logical screen units. 0 means "pick a size from the
     * logical screen height". */
    float size;

    /* Text colour. Defaults to white. */
    cpymo_color color;

    /* When true the readout is drawn but never announced. */
    bool silent;
};

/* Create or update the readout whose identifier is id.
 * text is copied.  Returns CPYMO_ERR_SUCC on success, or an error code when
 * the item could not be allocated. */
error_t cpymo_lua_hud_set(
    struct cpymo_lua_hud_item **head,
    const char *id,
    cpymo_str text,
    const struct cpymo_lua_hud_options *options);

/* Remove the readout whose identifier is id. Returns true when one was
 * removed. */
bool cpymo_lua_hud_remove(
    struct cpymo_lua_hud_item **head,
    const char *id);

void cpymo_lua_hud_free_all(struct cpymo_lua_hud_item **head);

/* Draw every readout.  Safe to call with head == NULL or an empty list. */
void cpymo_lua_hud_draw_all(
    const struct cpymo_lua_hud_item *head,
    int logical_screen_w,
    int logical_screen_h);

#endif
