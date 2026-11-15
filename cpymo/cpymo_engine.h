#ifndef INCLUDE_CPYMO_ENGINE
#define INCLUDE_CPYMO_ENGINE

#include "../cpymo-backends/include/cpymo_backend_input.h"
#include "cpymo_assetloader.h"
#include "cpymo_gameconfig.h"
#include "cpymo_error.h"
#include "cpymo_interpreter.h"
#include "cpymo_vars.h"
#include "cpymo_wait.h"
#include "cpymo_flash.h"
#include "cpymo_fade.h"
#include "cpymo_bg.h"
#include "cpymo_anime.h"
#include "cpymo_select_img.h"
#include "cpymo_charas.h"
#include "cpymo_scroll.h"
#include "cpymo_floating_hint.h"
#include "cpymo_say.h"
#include "cpymo_input_text.h"
#include "cpymo_text.h"
#include "cpymo_hash_flags.h"
#include "cpymo_ui.h"
#include "cpymo_audio.h"
#include "cpymo_backlog.h"

#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_lua_context.h"
#endif

/* 两段式选择（YukimiScript 的 @sel/@select_* + @*_do）在收尾之前
 * 逐行收集的条目的种类，用来核对收集行与 *_do 是否配套。 */
enum cpymo_select_pending_kind {
	cpymo_select_pending_none = 0,
	cpymo_select_pending_sel,
	cpymo_select_pending_text,
	cpymo_select_pending_var,
	cpymo_select_pending_img,
	cpymo_select_pending_imgs
};

enum cpymo_chara_pending_kind {
	cpymo_chara_pending_none = 0,
	cpymo_chara_pending_chara,
	cpymo_chara_pending_chara_y,
	cpymo_chara_pending_quake,
	cpymo_chara_pending_down,
	cpymo_chara_pending_up
};

struct cpymo_engine {
	uint8_t feature_level;
	cpymo_gameconfig gameconfig;
	cpymo_assetloader assetloader;
	cpymo_vars vars;
	cpymo_interpreter *interpreter;
	cpymo_input prev_input, input;
	cpymo_wait wait;
	cpymo_flash flash;
	cpymo_fade fade;
	cpymo_bg bg;
	cpymo_anime anime;
	cpymo_select_img select_img;
	cpymo_charas charas;
	cpymo_scroll scroll;
	cpymo_floating_hint floating_hint;
	cpymo_say say;
	cpymo_input_text input_text;
	cpymo_text text;
	cpymo_hash_flags flags;
	struct cpymo_ui *ui;
	cpymo_audio_system audio;
	cpymo_backlog backlog;

	#if CPYMO_FEATURE_LEVEL >= 1
	cpymo_lua_context lua;
	#endif

	bool skipping;
	char *title;

	/* 两段式选择的条目缓存：条目以 \0 结尾，条目内字段以 \t 分隔。 */
	char *select_pending;
	size_t select_pending_len, select_pending_cap;
	size_t select_pending_count;
	uint8_t select_pending_kind;
	char *chara_pending;
	size_t chara_pending_len, chara_pending_cap;
	size_t chara_pending_count;
	uint8_t chara_pending_kind;

	bool redraw;
	bool ignore_next_mouse_button_flag;

	bool config_skip_already_read_only;

	/* #__define_character：YukimiScript 角色符号（c0…）到显示名的映射，
	 * 按 symbol\0name\0 紧凑存放，见 cpymo_engine_define_character。 */
	char *chara_names;
	size_t chara_names_len, chara_names_cap;

#ifdef ENABLE_TEXT_EXTRACT
	char *text_extract_buffer;
	size_t text_extract_buffer_size, text_extract_buffer_maxsize;
	bool text_extract_copy_latched;
#endif
};

typedef struct cpymo_engine cpymo_engine;

/* 记录/查询 #__define_character 定义的角色符号；未定义时原样返回 symbol。 */
error_t cpymo_engine_define_character(
	cpymo_engine *e, cpymo_str symbol, cpymo_str name);
cpymo_str cpymo_engine_find_character_name(
	cpymo_engine *e, cpymo_str symbol);

/* 两段式选择的条目缓存：kind 变化时自动清空重来。 */
error_t cpymo_engine_select_pending_kind(cpymo_engine *e, int kind);
error_t cpymo_engine_select_pending_push(
	cpymo_engine *e, cpymo_str f1, cpymo_str f2, cpymo_str f3, cpymo_str f4);
void cpymo_engine_select_pending_reset(cpymo_engine *e);
error_t cpymo_engine_chara_pending_kind(cpymo_engine *e, int kind);
error_t cpymo_engine_chara_pending_push(
	cpymo_engine *e, const cpymo_str *fields, size_t field_count);
void cpymo_engine_chara_pending_reset(cpymo_engine *e);

error_t cpymo_engine_init(cpymo_engine *out, const char *gamedir);
void cpymo_engine_play_startup_music_once(void);
void cpymo_engine_free(cpymo_engine *engine);
error_t cpymo_engine_update(cpymo_engine *engine, float delta_time_sec, bool *redraw);
void cpymo_engine_draw(const cpymo_engine *engine);

bool cpymo_engine_skipping(cpymo_engine *engine);

void cpymo_engine_trim_memory(cpymo_engine *e);
void cpymo_engine_request_redraw(cpymo_engine *engine);
void cpymo_engine_exit(cpymo_engine *e);

#define CPYMO_INPUT_JUST_PRESSED(PENGINE, KEY) \
	(!PENGINE->prev_input.KEY && PENGINE->input.KEY)

#define CPYMO_INPUT_JUST_RELEASED(PENGINE, KEY) \
	(PENGINE->prev_input.KEY && !PENGINE->input.KEY && \
	!PENGINE->ignore_next_mouse_button_flag)

static inline void cpymo_input_ignore_next_mouse_button_event(cpymo_engine *e)
{ e->ignore_next_mouse_button_flag = e->input.mouse_button; }

static inline bool cpymo_input_mouse_moved(cpymo_engine *engine)
{
	if (engine->input.mouse_position_useable && engine->prev_input.mouse_position_useable) {
		return engine->input.mouse_x != engine->prev_input.mouse_x
			|| engine->input.mouse_y != engine->prev_input.mouse_y;
	}
	else {
		return engine->prev_input.mouse_position_useable != engine->input.mouse_position_useable;
	}
}

static inline bool cpymo_input_foward_key_just_pressed(cpymo_engine *e)
{
	return
		CPYMO_INPUT_JUST_PRESSED(e, ok) ||
		CPYMO_INPUT_JUST_PRESSED(e, mouse_button) ||
		CPYMO_INPUT_JUST_PRESSED(e, down) ||
		CPYMO_INPUT_JUST_PRESSED(e, cancel) ||
		CPYMO_INPUT_JUST_PRESSED(e, up) ||
		cpymo_engine_skipping(e) ||
		e->input.mouse_wheel_delta < 0;
}

static inline bool cpymo_input_foward_key_just_released(cpymo_engine *e)
{
	return
		CPYMO_INPUT_JUST_RELEASED(e, ok) ||
		CPYMO_INPUT_JUST_RELEASED(e, mouse_button) ||
		CPYMO_INPUT_JUST_RELEASED(e, down) ||
		CPYMO_INPUT_JUST_RELEASED(e, cancel) ||
		CPYMO_INPUT_JUST_RELEASED(e, up) ||
		cpymo_engine_skipping(e) ||
		e->input.mouse_wheel_delta < 0;
}

#ifdef ENABLE_TEXT_EXTRACT
void cpymo_engine_extract_text(cpymo_engine *e, cpymo_str str);
void cpymo_engine_extract_text_submit(cpymo_engine *e);

static inline void cpymo_engine_extract_text_cstr(cpymo_engine *e, const char *str)
{ cpymo_engine_extract_text(e, cpymo_str_pure(str)); }

#else
#define cpymo_engine_extract_text(A, B)
static inline void cpymo_engine_extract_text_submit(cpymo_engine *e) {}
#define cpymo_engine_extract_text_cstr(A, B)
#endif

#endif

