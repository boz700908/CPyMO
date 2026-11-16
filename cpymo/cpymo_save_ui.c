#include "cpymo_prelude.h"
#include "cpymo_save_ui.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include "cpymo_save.h"
#include "cpymo_msgbox_ui.h"
#include "cpymo_localization.h"
#include "cpymo_ui_patch.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#ifndef CPYMO_MAX_SAVES
#define CPYMO_MAX_SAVES 31
#endif

typedef struct {
	cpymo_backend_text text;
#ifdef ENABLE_TEXT_EXTRACT
	char *orginal_text;
#endif
	bool is_empty_save;
} cpymo_save_ui_item;

typedef struct {
	cpymo_save_ui_item items[CPYMO_MAX_SAVES];
	bool is_load_ui;
} cpymo_save_ui;

#ifdef DISABLE_AUTOSAVE
const static size_t first_slot = 0;
#else
const static size_t first_slot = 1;
#endif

#if CPYMO_FEATURE_LEVEL >= 1
#define CPYMO_SAVE_UI_PATCH(e) ((e)->feature_level >= 1 ? &(e)->lua.ui_patch : NULL)
#else
#define CPYMO_SAVE_UI_PATCH(e) NULL
#endif

static const char *cpymo_save_ui_page(bool is_load_ui)
{
	return is_load_ui ? CPYMO_UI_PATCH_PAGE_LOAD : CPYMO_UI_PATCH_PAGE_SAVE;
}

/* 从 from 沿 dir 在该页 [first, last] 范围内找第一个未被隐藏的项；
 * 全部隐藏或走出范围时返回 -1。没有补丁时恒等于 from。 */
static int cpymo_save_ui_step_visible(
	const cpymo_engine *e, const char *page, int from, int dir, int first, int last)
{
	const cpymo_ui_patch *patch = CPYMO_SAVE_UI_PATCH(e);
	if (patch == NULL) return from;

	for (int i = from; i >= first && i <= last; i += dir) {
		if (!cpymo_ui_patch_hidden(patch, page, i)) return i;
	}

	return -1;
}

static void cpymo_save_ui_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);
	const char *page = cpymo_save_ui_page(ui->is_load_ui);
	uintptr_t node = cpymo_list_ui_encode_uint_node_dec(node_to_draw);

	/* 追加项：序号从 CPYMO_MAX_SAVES 起算。文本每帧临时创建，条目数量很少。 */
	if (node >= CPYMO_MAX_SAVES) {
		const char *text = cpymo_ui_patch_extra_text(
			CPYMO_SAVE_UI_PATCH(e), page, node - CPYMO_MAX_SAVES);
		if (text != NULL) {
			cpymo_backend_text extra;
			float w;
			if (cpymo_backend_text_create(&extra, &w, cpymo_str_pure(text),
				cpymo_gameconfig_font_size(&e->gameconfig)) == CPYMO_ERR_SUCC) {
				cpymo_backend_text_draw(
					extra,
					0,
					y + cpymo_gameconfig_font_size(&e->gameconfig),
					cpymo_color_white,
					1.0f,
					cpymo_backend_image_draw_type_ui_element);
				cpymo_backend_text_free(extra);
			}
		}
		return;
	}

	/* 被隐藏的条目保留原位（不绘制），避免行高与高亮错位。 */
	if (cpymo_ui_patch_hidden(CPYMO_SAVE_UI_PATCH(e), page, (int)node)) return;

	const cpymo_save_ui_item *item = &ui->items[node];

	cpymo_backend_text_draw(
		item->text,
		0,
		y + cpymo_gameconfig_font_size(&e->gameconfig),
		cpymo_color_white,
		1.0f,
		cpymo_backend_image_draw_type_ui_element);
}

static error_t cpymo_save_confirm(cpymo_engine *e, void *data, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	uintptr_t save_id = cpymo_list_ui_encode_uint_node_dec(data);
	error_t err = cpymo_save_write(e, (unsigned short)save_id);

	const cpymo_localization *l = cpymo_localization_get(e);

	char *msg = NULL;
	error_t errstr;
	if (err == CPYMO_ERR_SUCC) 
		errstr = l->save_already_save_to(&msg, (int)save_id);
	else 
		errstr = l->save_failed(&msg, err);
	
	if (errstr != CPYMO_ERR_SUCC) return errstr;

	cpymo_ui_exit(e);

	err = cpymo_msgbox_ui_enter(
		e,
		cpymo_str_pure(msg),
		NULL,
		NULL);

	free(msg);

	return err;
}

static error_t cpymo_save_ui_ok(cpymo_engine *e, void *selected)
{
	cpymo_save_ui *ui = (cpymo_save_ui *)cpymo_list_ui_data(e);

	uintptr_t save_id = cpymo_list_ui_encode_uint_node_dec(selected);

	/* 追加项没有存档槽位，点击只派发给 Lua 回调。 */
	if (save_id >= CPYMO_MAX_SAVES) {
#if CPYMO_FEATURE_LEVEL >= 1
		cpymo_lua_ui_page_click(
			&e->lua, cpymo_save_ui_page(ui->is_load_ui),
			(int)(save_id - CPYMO_MAX_SAVES));
#endif
		return CPYMO_ERR_SUCC;
	}

#if CPYMO_FEATURE_LEVEL >= 1
	if (cpymo_lua_ui_item_click(
		&e->lua, cpymo_save_ui_page(ui->is_load_ui), (int)save_id))
		return CPYMO_ERR_SUCC;
#endif

	if (ui->is_load_ui) {
		if (!ui->items[save_id].is_empty_save) {
			cpymo_list_ui_ignore_key_hold_exit_onetime(e);
			return cpymo_save_ui_load_savedata_yesnobox(e, (unsigned short)save_id);
		}
		else return CPYMO_ERR_SUCC;
	}
	else {
		char *msg = NULL;
		const cpymo_localization *l = cpymo_localization_get(e);
		error_t err = l->save_are_you_sure_save_to(&msg, (int)save_id);
		CPYMO_THROW(err);
		err = cpymo_msgbox_ui_enter(
			e,
			cpymo_str_pure(msg),
			&cpymo_save_confirm,
			selected);
		free(msg);
		return err;
	}
}

static void cpymo_save_ui_deleter(cpymo_engine *e, void *ui_data)
{
	cpymo_save_ui *ui = (cpymo_save_ui *)cpymo_list_ui_data(e);

	for (size_t i = 0; i < CPYMO_MAX_SAVES; ++i) {
		if (ui->items[i].text) 
			cpymo_backend_text_free(ui->items[i].text);
#ifdef ENABLE_TEXT_EXTRACT
		if (ui->items[i].orginal_text) free(ui->items[i].orginal_text);
#endif
	}
}

static void* cpymo_save_ui_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);
	const char *page = cpymo_save_ui_page(ui->is_load_ui);
	int first = ui->is_load_ui ? 0 : (int)first_slot;

	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);

	if (i >= CPYMO_MAX_SAVES) {
		uintptr_t extra = i - CPYMO_MAX_SAVES + 1;
		if (extra < cpymo_ui_patch_extra_count(CPYMO_SAVE_UI_PATCH(e), page))
			return cpymo_list_ui_encode_uint_node_enc(CPYMO_MAX_SAVES + extra);
		return NULL;
	}

	int next = cpymo_save_ui_step_visible(
		e, page, (int)i + 1, 1, first, CPYMO_MAX_SAVES - 1);
	if (next >= 0) return cpymo_list_ui_encode_uint_node_enc((uintptr_t)next);

	if (cpymo_ui_patch_extra_count(CPYMO_SAVE_UI_PATCH(e), page) > 0)
		return cpymo_list_ui_encode_uint_node_enc(CPYMO_MAX_SAVES);

	return NULL;
}

static void* cpymo_save_ui_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data_const(e);
	const char *page = cpymo_save_ui_page(ui->is_load_ui);
	int first = ui->is_load_ui ? 0 : (int)first_slot;
	int last = CPYMO_MAX_SAVES - 1;

	uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);

	int from;
	if (i >= CPYMO_MAX_SAVES) {
		if (i > CPYMO_MAX_SAVES)
			return cpymo_list_ui_encode_uint_node_enc(i - 1);
		from = last;
	}
	else from = (int)i - 1;

	int prev = cpymo_save_ui_step_visible(e, page, from, -1, first, last);
	if (prev >= 0) return cpymo_list_ui_encode_uint_node_enc((uintptr_t)prev);

	return NULL;
}

#ifdef ENABLE_TEXT_EXTRACT
static error_t cpymo_save_ui_visual_impaired_selection_changed(cpymo_engine *e, void *cur)
{
	if (cur) {
		uintptr_t i = cpymo_list_ui_encode_uint_node_dec(cur);
		const cpymo_save_ui *ui = (const cpymo_save_ui *)cpymo_list_ui_data(e);

		if (i >= CPYMO_MAX_SAVES) {
			cpymo_a11y_speak(cpymo_ui_patch_extra_text(
				CPYMO_SAVE_UI_PATCH(e),
				cpymo_save_ui_page(ui->is_load_ui),
				i - CPYMO_MAX_SAVES));
		}
		else cpymo_a11y_speak(ui->items[i].orginal_text);
	}
	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_save_ui_enter(cpymo_engine *e, bool is_load_ui)
{
	cpymo_save_ui *ui = NULL;
	uintptr_t first = is_load_ui ? 0 : first_slot;

	/* 起始项若被隐藏，向后找第一个可见项；全部隐藏时保持原位不变。 */
	int start_visible = cpymo_save_ui_step_visible(
		e, cpymo_save_ui_page(is_load_ui), (int)first, 1,
		(int)first, CPYMO_MAX_SAVES - 1);
	if (start_visible < 0) start_visible = (int)first;

	cpymo_ui_patch_set_base(
		CPYMO_SAVE_UI_PATCH(e), cpymo_save_ui_page(is_load_ui), CPYMO_MAX_SAVES);

	error_t err = cpymo_list_ui_enter(
		e,
		(void **)&ui,
		sizeof(*ui),
		&cpymo_save_ui_draw_node,
		&cpymo_save_ui_ok,
		&cpymo_save_ui_deleter,
		cpymo_list_ui_encode_uint_node_enc((uintptr_t)start_visible),
		&cpymo_save_ui_get_next,
		&cpymo_save_ui_get_prev,
		false,
		3);
	CPYMO_THROW(err);
	
	cpymo_list_ui_enable_loop(e);

#ifdef ENABLE_TEXT_EXTRACT
	cpymo_list_ui_set_selection_changed_callback(e, &cpymo_save_ui_visual_impaired_selection_changed);
	ui->items[0].orginal_text = NULL;
#endif

	ui->is_load_ui = is_load_ui;

	for (size_t i = 0; i < CPYMO_MAX_SAVES; ++i) {
		ui->items[i].text = NULL;
		ui->items[i].is_empty_save = false;
	}

	const float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
	size_t characters = (size_t)((float)e->gameconfig.imagesize_w / fontsize * 1.3f);

	char text_buf[1024];
	for (size_t i = is_load_ui ? 0 : first_slot; i < CPYMO_MAX_SAVES; ++i) {
		FILE *save = cpymo_save_open_read(e, (unsigned short)i);
		cpymo_save_title title;
		title.say_name = NULL;
		title.say_text = NULL;
		title.title = NULL;

		if (save) {
			error_t err = cpymo_save_load_title(&title, save);
			fclose(save);
			if (err != CPYMO_ERR_SUCC) ui->items[i].is_empty_save = true;
		}
		else {
			ui->items[i].is_empty_save = true;
		}

		if (!ui->items[i].is_empty_save) {
			assert(title.say_name != NULL);
			assert(title.title != NULL);
			assert(title.say_text != NULL);

			char *tmp_str = (char *)malloc(strlen(title.say_name) + strlen(title.say_text) + 16);
			if (tmp_str == NULL) {
				cpymo_ui_exit(e);
				free(title.say_name);
				free(title.say_text);
				free(title.title);
				return CPYMO_ERR_OUT_OF_MEM;
			}

			*tmp_str = '\0';
			if (strlen(title.say_name) > 0) {
				strcat(tmp_str, title.say_name);
				strcat(tmp_str, " ");
			}

			strcat(tmp_str, title.say_text);

			cpymo_str say_preview_text_tail = cpymo_str_pure(tmp_str);
			cpymo_str say_preview_text = 
				cpymo_str_split(&say_preview_text_tail, characters);

			if (say_preview_text_tail.len > 0) {
				*(char *)say_preview_text_tail.begin = '\0';

				if (say_preview_text.len > 3) {
					((char *)say_preview_text.begin)[say_preview_text.len - 1] = '.';
					((char *)say_preview_text.begin)[say_preview_text.len - 2] = '.';
					((char *)say_preview_text.begin)[say_preview_text.len - 3] = '.';
				}
			}

			const cpymo_localization *l = cpymo_localization_get(e);

			char *str = NULL;
			error_t err;

			#ifndef DISABLE_AUTOSAVE
			if (i == 0) err = l->save_auto_title(&str, title.title);
			else 
			#endif
				err = l->save_title(&str, (int)i, title.title);

			if (err != CPYMO_ERR_SUCC) {
				free(tmp_str);
				free(title.say_name);
				free(title.title);
				free(title.say_text);
				return err;
			}

			snprintf(text_buf, sizeof(text_buf), "%s\n%s", str, tmp_str);
			free(str);

			free(tmp_str);

			free(title.say_name);
			free(title.title);
			free(title.say_text);

			cpymo_utils_replace_str_newline_n(text_buf);
		}
		else {
			error_t err;
			const cpymo_localization *l = cpymo_localization_get(e);
			char *msg = NULL;

			#ifndef DISABLE_AUTOSAVE
			if (i == 0) err = l->save_auto_title(&msg, "");
			else 
			#endif
				err = l->save_title(&msg, (int)i, "");

			CPYMO_THROW(err);
			strncpy(text_buf, msg, sizeof(text_buf) - 1);
			free(msg);
		}

		const char *text_patched = cpymo_ui_patch_text(
			CPYMO_SAVE_UI_PATCH(e), cpymo_save_ui_page(ui->is_load_ui),
			(int)i, text_buf);

		float w;
		error_t err = cpymo_backend_text_create(
			&ui->items[i].text, &w, cpymo_str_pure(text_patched), fontsize);

#ifdef ENABLE_TEXT_EXTRACT
		ui->items[i].orginal_text = 
			cpymo_str_copy_malloc(cpymo_str_pure(text_patched));
#endif

		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}

#ifdef ENABLE_TEXT_EXTRACT
	if (ui->items[first].orginal_text)
		cpymo_a11y_speak(ui->items[first].orginal_text);
#endif

	return CPYMO_ERR_SUCC;
}

static error_t cpymo_save_ui_load_savedata_yesnobox_confirm(
	cpymo_engine *e, void *save_id_x, bool confirm)
{
	if (!confirm) return CPYMO_ERR_SUCC;
	FILE *file = cpymo_save_open_read(e, (unsigned short)(uintptr_t)save_id_x);
	if (file == NULL) return CPYMO_ERR_CAN_NOT_OPEN_FILE;
	error_t err = cpymo_save_load_savedata(e, file);
	fclose(file);
	return err;
}

error_t cpymo_save_ui_load_savedata_yesnobox(cpymo_engine * e, unsigned short save_id)
{
	char *str = NULL;
	error_t err = CPYMO_ERR_SUCC;
	const cpymo_localization *l = cpymo_localization_get(e);

	#ifndef DISABLE_AUTOSAVE
	if (save_id) 
	#endif
	{
		err = l->save_are_you_sure_load(&str, (int)save_id);
		CPYMO_THROW(err);
	}

	err = cpymo_msgbox_ui_enter(
		e,
		cpymo_str_pure(str ? str : l->save_are_you_sure_load_auto_save),
		&cpymo_save_ui_load_savedata_yesnobox_confirm,
		(void *)(uintptr_t)save_id);

	if (str) free(str);

	return err;
}


