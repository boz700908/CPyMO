#include "cpymo_prelude.h"
#include "cpymo_music_box.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include "cpymo_parser.h"
#include "cpymo_accessibility.h"
#include "../cpymo-backends/include/cpymo_backend_text.h"
#include "cpymo_ui_patch.h"
#include "cpymo_ui_extra.h"
#include <assert.h>
#include <stdlib.h>

#if CPYMO_FEATURE_LEVEL >= 1
#define CPYMO_MUSIC_BOX_UI_PATCH(e) ((e)->feature_level >= 1 ? &(e)->lua.ui_patch : NULL)
#else
#define CPYMO_MUSIC_BOX_UI_PATCH(e) NULL
#endif

typedef struct {
	char *music_list;

	uintptr_t music_count;
	cpymo_str *music_filename;
	cpymo_backend_text *music_title;

#ifdef ENABLE_TEXT_EXTRACT
	char **music_title_text;
#endif

	float font_size;
} cpymo_music_box;

static void cpymo_music_box_deleter(cpymo_engine *e, void *ui_)
{
	cpymo_music_box *box = (cpymo_music_box *)ui_;

	if (box->music_title)
		for (uintptr_t i = 0; i < box->music_count; ++i)
			cpymo_backend_text_free(box->music_title[i]);

#ifdef ENABLE_TEXT_EXTRACT
	if (box->music_title_text) {
		for (uintptr_t i = 0; i < box->music_count; ++i)
			if (box->music_title_text[i]) free(box->music_title_text[i]);
		free(box->music_title_text);
	}
#endif

	if (box->music_list) free(box->music_list);
	if (box->music_filename) free(box->music_filename);
}

static void *cpymo_music_box_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	cpymo_music_box *box = (cpymo_music_box *)ui_data;
	const cpymo_ui_patch *patch = CPYMO_MUSIC_BOX_UI_PATCH(e);
	uintptr_t index = cpymo_list_ui_encode_uint_node_dec(cur);

	/* 追加项接在最后一个曲目之后。 */
	if (index >= box->music_count) {
		uintptr_t k = index - box->music_count + 1;
		if (k < cpymo_ui_patch_extra_count(
			patch, CPYMO_UI_PATCH_PAGE_MUSIC))
			return cpymo_list_ui_encode_uint_node_enc(
				box->music_count + k);
		return NULL;
	}

	for (uintptr_t n = 0; n < box->music_count; ++n) {
		if (++index >= box->music_count) break;
		if (cpymo_ui_patch_hidden(
			patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)index))
			continue;
		return cpymo_list_ui_encode_uint_node_enc(index);
	}

	if (cpymo_ui_patch_extra_count(patch, CPYMO_UI_PATCH_PAGE_MUSIC) > 0)
		return cpymo_list_ui_encode_uint_node_enc(box->music_count);

	return NULL;
}

static void *cpymo_music_box_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	cpymo_music_box *box = (cpymo_music_box *)ui_data;
	const cpymo_ui_patch *patch = CPYMO_MUSIC_BOX_UI_PATCH(e);
	uintptr_t index = cpymo_list_ui_encode_uint_node_dec(cur);

	if (index >= box->music_count) {
		if (index > box->music_count)
			return cpymo_list_ui_encode_uint_node_enc(index - 1);

		/* 追加项第一条之前是最后一个可见曲目。 */
		for (uintptr_t n = box->music_count; n > 0; --n) {
			uintptr_t i = n - 1;
			if (cpymo_ui_patch_hidden(
				patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)i))
				continue;
			return cpymo_list_ui_encode_uint_node_enc(i);
		}
		return NULL;
	}

	for (uintptr_t n = 0; n < box->music_count; ++n) {
		if (index == 0) return NULL;
		--index;
		if (cpymo_ui_patch_hidden(
			patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)index))
			continue;
		return cpymo_list_ui_encode_uint_node_enc(index);
	}

	return NULL;
}

static void cpymo_musicbox_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	uintptr_t node_index = cpymo_list_ui_encode_uint_node_dec(node_to_draw);
	const cpymo_music_box *box = (cpymo_music_box *)cpymo_list_ui_data_const(e);
	const cpymo_ui_patch *patch = CPYMO_MUSIC_BOX_UI_PATCH(e);

	if (node_index >= box->music_count) {
		int extra = cpymo_ui_patch_extra_index(
			patch, CPYMO_UI_PATCH_PAGE_MUSIC,
			box->music_count, node_index);
		if (extra >= 0) {
			const char *extra_text = cpymo_ui_patch_extra_text(
				patch, CPYMO_UI_PATCH_PAGE_MUSIC, (size_t)extra);
			cpymo_ui_extra_draw_text(extra_text, 0, y, box->font_size);
		}
		return;
	}

	if (cpymo_ui_patch_hidden(
		patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)node_index))
		return;

	/* 条目改写：补丁文本优先，命中时不再绘制原曲目名。 */
	const char *patched = cpymo_ui_patch_text(
		patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)node_index, NULL);
	if (patched && *patched) {
		cpymo_ui_extra_draw_text(patched, 0, y, box->font_size);
		return;
	}

	cpymo_backend_text text = box->music_title[node_index];
	cpymo_backend_text_draw(
		text, 0, y + box->font_size, cpymo_color_white, 1.0f, 
		cpymo_backend_image_draw_type_ui_element);
}

static error_t cpymo_musicbox_ok(struct cpymo_engine *e, void *selected)
{
	const cpymo_music_box *box = (cpymo_music_box *)cpymo_list_ui_data_const(e);

	uintptr_t node_index = cpymo_list_ui_encode_uint_node_dec(selected);

	/* 追加项不是曲目，点击只派发给 Lua 回调。 */
	if (node_index >= box->music_count) {
#if CPYMO_FEATURE_LEVEL >= 1
		cpymo_lua_ui_page_click(
			&e->lua, CPYMO_UI_PATCH_PAGE_MUSIC,
			(int)(node_index - box->music_count));
#endif
		return CPYMO_ERR_SUCC;
	}

	error_t err = cpymo_audio_bgm_play(e, box->music_filename[node_index], true);
	if (err == CPYMO_ERR_SUCC) {
		cpymo_accessibility_play_sound(SOUND_ENTER);
	}
	return err;
}

#ifdef ENABLE_TEXT_EXTRACT
static error_t cpymo_musicbox_visual_help_selection_change(cpymo_engine *e, void *selected)
{
	const cpymo_music_box *box = (cpymo_music_box *)cpymo_list_ui_data_const(e);
	const cpymo_ui_patch *patch = CPYMO_MUSIC_BOX_UI_PATCH(e);
	
	uintptr_t node_index = cpymo_list_ui_encode_uint_node_dec(selected);

	if (node_index >= box->music_count) {
		int extra = cpymo_ui_patch_extra_index(
			patch, CPYMO_UI_PATCH_PAGE_MUSIC,
			box->music_count, node_index);
		if (extra >= 0) {
			const char *t = cpymo_ui_patch_extra_text(
				patch, CPYMO_UI_PATCH_PAGE_MUSIC, (size_t)extra);
			if (t && *t) cpymo_a11y_speak(t);
		}
		return CPYMO_ERR_SUCC;
	}

	if (cpymo_ui_patch_hidden(
		patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)node_index))
		return CPYMO_ERR_SUCC;

	const char *patched = cpymo_ui_patch_text(
		patch, CPYMO_UI_PATCH_PAGE_MUSIC, (int)node_index, NULL);
	if (patched && *patched) cpymo_a11y_speak(patched);
	else if (box->music_title_text && box->music_title_text[node_index])
		cpymo_a11y_speak(box->music_title_text[node_index]);

	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_music_box_enter(cpymo_engine *e)
{
	cpymo_music_box *box = NULL;

	error_t err = cpymo_list_ui_enter(
		e, (void **)&box, sizeof(cpymo_music_box), 
		&cpymo_musicbox_draw_node,
		&cpymo_musicbox_ok,
		&cpymo_music_box_deleter, 
		(void *)0, 
		&cpymo_music_box_get_next, 
		&cpymo_music_box_get_prev, 
		false,
		5);
	CPYMO_THROW(err);

	cpymo_list_ui_enable_loop(e);

	box->music_list = NULL;
	box->music_filename = NULL;
	box->music_title = NULL;
	#ifdef ENABLE_TEXT_EXTRACT
	box->music_title_text = NULL;
	#endif

	size_t music_list_size = 0;
	err = cpymo_assetloader_load_script(
		&box->music_list, &music_list_size, 
		"music_list", &e->assetloader);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	cpymo_parser p;
	cpymo_parser_init(&p, box->music_list, music_list_size);

	box->music_count = 0;

	do {
		cpymo_str music_file =
			cpymo_parser_curline_pop_commacell(&p);
		cpymo_str_trim(&music_file);
		if (music_file.len) box->music_count++;
	} while (cpymo_parser_next_line(&p));

	if (box->music_count == 0) {
		free(box->music_list);
		box->music_list = NULL;
		return CPYMO_ERR_NO_MORE_CONTENT;
	}

	cpymo_parser_reset(&p);

	box->music_filename = 
		(cpymo_str *)malloc(
			(sizeof(cpymo_str) + sizeof(cpymo_backend_text)) * box->music_count);

	if (box->music_filename == NULL) {
		free(box->music_list);
		box->music_list = NULL;
		return CPYMO_ERR_OUT_OF_MEM;
	}

	box->music_title = (cpymo_backend_text *)(box->music_filename + box->music_count);
	for (uintptr_t i = 0; i < box->music_count; ++i)
		box->music_title[i] = NULL;

	uintptr_t i = 0;
	const cpymo_gameconfig *c = &e->gameconfig;
	box->font_size = c->fontsize / 240.0f * c->imagesize_h * 0.8f;

#ifdef ENABLE_TEXT_EXTRACT
	box->music_title_text = (char **)malloc(sizeof(char *) * box->music_count);
	cpymo_list_ui_set_selection_changed_callback(
		e, &cpymo_musicbox_visual_help_selection_change);
#endif

	do {
		cpymo_str music_file =
			cpymo_parser_curline_pop_commacell(&p);

		cpymo_str music_title =
			cpymo_parser_curline_pop_commacell(&p);

		cpymo_str_trim(&music_file);
		cpymo_str_trim(&music_title);

		if (music_file.len == 0) continue;

		box->music_filename[i] = music_file;

#ifdef ENABLE_TEXT_EXTRACT
		if (box->music_title_text) 
			box->music_title_text[i] = cpymo_str_copy_malloc(music_title);
#endif

		float width;
		error_t err = cpymo_backend_text_create(&box->music_title[i], &width, music_title, box->font_size);
		if (err != CPYMO_ERR_SUCC) {
			for (uintptr_t i = 0; i < box->music_count; ++i)
				if(box->music_title[i])
					cpymo_backend_text_free(box->music_title[i]);
			free(box->music_list);
			box->music_list = NULL;
			return err;
		}

		i++;
	} while (cpymo_parser_next_line(&p) && i < box->music_count);

	assert(i == box->music_count);
	cpymo_ui_patch_set_base(
		CPYMO_MUSIC_BOX_UI_PATCH(e), CPYMO_UI_PATCH_PAGE_MUSIC,
		box->music_count);

	cpymo_list_ui_set_current_node(e, cpymo_list_ui_encode_uint_node_enc(0));

#ifdef ENABLE_TEXT_EXTRACT
	if (box->music_title_text && box->music_title_text[0])
		cpymo_a11y_speak(box->music_title_text[0]);
#endif

	return CPYMO_ERR_SUCC;
}


