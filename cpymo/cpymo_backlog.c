#include "cpymo_prelude.h"
#include "cpymo_backlog.h"
#include "cpymo_engine.h"
#include "cpymo_list_ui.h"
#include "cpymo_accessibility.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>

#ifndef CPYMO_BACKLOG_MAX_RECORDS
#define CPYMO_BACKLOG_MAX_RECORDS 64
#endif

typedef struct cpymo_backlog_record {
	bool owning_name;
	cpymo_backend_text name, text_render;
	char vo_filename[32];
	char *text;
	float font_size;

	char *name_text;
} cpymo_backlog_record;

error_t cpymo_backlog_init(cpymo_backlog *b)
{
	b->records = (cpymo_backlog_record *)malloc(CPYMO_BACKLOG_MAX_RECORDS * sizeof(b->records[0]));
	if (b->records == NULL) return CPYMO_ERR_OUT_OF_MEM;
	b->next_record_to_write = 0;
	b->pending_vo_filename[0] = '\0';
	b->owning_name = false;
	b->pending_name = NULL;

	b->pending_name_text = NULL;

	for (size_t i = 0; i < CPYMO_BACKLOG_MAX_RECORDS; i++) {
		b->records[i].vo_filename[0] = '\0';
		b->records[i].owning_name = false;
		b->records[i].name = NULL;
		b->records[i].text = NULL;
		b->records[i].text_render = NULL;

		b->records[i].name_text = NULL;
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_backlog_record_clean(cpymo_backlog_record *rec)
{
	if (rec->owning_name && rec->name) {
		cpymo_backend_text_free(rec->name);

		if (rec->name_text) free(rec->name_text);
	}

	if (rec->text_render) cpymo_backend_text_free(rec->text_render);
	rec->text_render = NULL;

	if (rec->text) free(rec->text);
	rec->text = NULL;

	rec->owning_name = false;
	rec->name = NULL;
	rec->vo_filename[0] = '\0';

	rec->name_text = NULL;
}

void cpymo_backlog_free(cpymo_backlog *b)
{
	if (b->owning_name && b->pending_name) {
		cpymo_backend_text_free(b->pending_name);

		if (b->pending_name_text) free(b->pending_name_text);
	}

	for (size_t i = 0; i < CPYMO_BACKLOG_MAX_RECORDS; i++) {
		cpymo_backlog_record *rec = &b->records[i];
		cpymo_backlog_record_clean(rec);
	}

	free(b->records);
}

void cpymo_backlog_record_write_vo(cpymo_backlog *b, cpymo_str vo)
{
	cpymo_str_copy(
		b->pending_vo_filename, sizeof(b->pending_vo_filename), vo);
}

void cpymo_backlog_record_write_name(
	cpymo_backlog *b, cpymo_backend_text name, cpymo_str name_text)
{
	if (b->owning_name && b->pending_name) {
		cpymo_backend_text_free(b->pending_name);
		if (b->pending_name_text) free(b->pending_name_text);
	}

	b->owning_name = name == NULL ? false : true;
	b->pending_name = name;

	b->pending_name_text = 
		name == NULL ? NULL : cpymo_str_copy_malloc(name_text);
}

error_t cpymo_backlog_record_write_text(
	cpymo_backlog *b,
	char *text,
	float fontsize)
{
	cpymo_backlog_record *rec = &b->records[b->next_record_to_write];

	cpymo_backlog_record_clean(rec);

	rec->name = b->pending_name;
	rec->owning_name = b->owning_name;

	rec->name_text = b->pending_name_text;

	b->owning_name = false;
	rec->text = text;
	strcpy(rec->vo_filename, b->pending_vo_filename);
	b->pending_vo_filename[0] = '\0';
	rec->font_size = fontsize;

	b->next_record_to_write = (b->next_record_to_write + 1) % CPYMO_BACKLOG_MAX_RECORDS;
	return CPYMO_ERR_SUCC;
}

size_t cpymo_backlog_record_count(const cpymo_backlog *b)
{
	size_t n = 0;
	for (size_t i = 0; i < CPYMO_BACKLOG_MAX_RECORDS; ++i)
		if (b->records[i].text) ++n;
	return n;
}

static const cpymo_backlog_record *cpymo_backlog_record_at(
	const cpymo_backlog *b, size_t logical_index)
{
	size_t i = b->next_record_to_write;
	for (size_t k = 0; k < CPYMO_BACKLOG_MAX_RECORDS; ++k) {
		const cpymo_backlog_record *rec = &b->records[i];
		if (rec->text) {
			if (logical_index == 0) return rec;
			--logical_index;
		}

		i = (i + 1) % CPYMO_BACKLOG_MAX_RECORDS;
	}

	return NULL;
}

bool cpymo_backlog_record_get(
	const cpymo_backlog *b,
	size_t logical_index,
	const char **name_text,
	const char **vo_filename,
	const char **text,
	float *font_size)
{
	const cpymo_backlog_record *rec =
		cpymo_backlog_record_at(b, logical_index);
	if (rec == NULL) return false;

	if (name_text) *name_text = rec->name_text;
	if (vo_filename) *vo_filename = rec->vo_filename;
	if (text) *text = rec->text;
	if (font_size) *font_size = rec->font_size;

	return true;
}

error_t cpymo_backlog_record_restore(
	cpymo_backlog *b,
	const char *name_text,
	const char *vo_filename,
	char *text,
	float font_size)
{
	if (!(font_size > 0.0f)) font_size = 32.0f;

	if (text == NULL) {
		text = (char *)malloc(sizeof(char));
		if (text == NULL) return CPYMO_ERR_OUT_OF_MEM;
		text[0] = '\0';
	}

	if (name_text && *name_text) {
		cpymo_backend_text name = NULL;
		float w = 0;
		error_t err = cpymo_backend_text_create(
			&name, &w, cpymo_str_pure(name_text), font_size);
		if (err != CPYMO_ERR_SUCC) {
			free(text);
			return err;
		}

		cpymo_backlog_record_write_name(
			b, name, cpymo_str_pure(name_text));
	}

	if (vo_filename && *vo_filename)
		cpymo_backlog_record_write_vo(b, cpymo_str_pure(vo_filename));

	return cpymo_backlog_record_write_text(b, text, font_size);
}

static void cpymo_backlog_ui_draw_node(const cpymo_engine *e, const void *node_to_draw, float y)
{
	const cpymo_backlog_record *rec = &e->backlog.records[
		cpymo_list_ui_encode_uint_node_dec(node_to_draw)];

	const float font_size = rec->font_size;
	y += font_size;
	if (rec->name) {
		cpymo_backend_text_draw(
			rec->name, 0, y, cpymo_color_white,
			1.0, cpymo_backend_image_draw_type_ui_element);

		y += font_size;
	}

	if (rec->text_render == NULL) {
		float w;
		cpymo_backlog_record *rec_mut = (cpymo_backlog_record *)rec;
		error_t err = cpymo_backend_text_create(
			&rec_mut->text_render, 
			&w, 
			cpymo_str_pure(rec->text), 
			rec->font_size);
		if (err != CPYMO_ERR_SUCC) 
			rec_mut->text_render = NULL;
	}

	if (rec->text_render) 
		cpymo_backend_text_draw(
			rec->text_render, 
			0, y, 
			cpymo_color_white, 
			1, 
			cpymo_backend_image_draw_type_ui_element);
}

static error_t cpymo_backlog_ui_ok(struct cpymo_engine *e, void *selected)
{
	const cpymo_backlog_record *rec = &e->backlog.records[
		cpymo_list_ui_encode_uint_node_dec(selected)];
	if (rec->vo_filename[0]) {
		error_t err = cpymo_audio_vo_play(e, cpymo_str_pure(rec->vo_filename));
		if (err == CPYMO_ERR_SUCC)
			cpymo_accessibility_play_sound(SOUND_ENTER);
		return err;
	}

	return CPYMO_ERR_SUCC;
}

static void *cpymo_backlog_ui_get_next(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	size_t index = (size_t)cpymo_list_ui_encode_uint_node_dec(cur);
	if (index) index--;
	else index = CPYMO_BACKLOG_MAX_RECORDS - 1;

	if (index == e->backlog.next_record_to_write) return NULL;
	if (e->backlog.records[index].text == NULL) return NULL;

	return cpymo_list_ui_encode_uint_node_enc(index);
}

static void *cpymo_backlog_ui_get_prev(const cpymo_engine *e, const void *ui_data, const void *cur)
{
	size_t index = (size_t)cpymo_list_ui_encode_uint_node_dec(cur);
	if (index >= CPYMO_BACKLOG_MAX_RECORDS - 1) index = 0;
	else index++;

	if (index == e->backlog.next_record_to_write) return NULL;
	if (e->backlog.records[index].text == NULL) return NULL;

	return cpymo_list_ui_encode_uint_node_enc(index);
}

typedef struct {
	bool press_key_down_to_close;
	size_t last_played_index;
} cpymo_backlog_ui;

static error_t cpymo_backlog_ui_update(cpymo_engine *e, float dt, void *selected)
{
	cpymo_backlog_ui *ui = (cpymo_backlog_ui * )cpymo_list_ui_data(e);
	if (cpymo_backlog_ui_get_prev(e, cpymo_list_ui_data(e), selected) == NULL) {
		if (CPYMO_INPUT_JUST_RELEASED(e, down)) {
			if (ui->press_key_down_to_close) cpymo_list_ui_exit(e);
			else ui->press_key_down_to_close = true;
		}
	}
	else {
		ui->press_key_down_to_close = false;
	}

	/* Issue #57: replay the voice of the record under the cursor. */
	if (selected) {
		size_t index = (size_t)cpymo_list_ui_encode_uint_node_dec(selected);
		if (index != ui->last_played_index) {
			ui->last_played_index = index;
			const cpymo_backlog_record *rec = &e->backlog.records[index];
			if (rec->vo_filename[0])
				cpymo_audio_vo_play(e, cpymo_str_pure(rec->vo_filename));
			else cpymo_audio_vo_stop(e);
		}
	}

	return CPYMO_ERR_SUCC;
}

#ifdef ENABLE_TEXT_EXTRACT
static void cpymo_backlog_ui_extract_text(cpymo_engine *e, size_t index)
{
	if (e->backlog.records[index].text) {
		if (e->backlog.records[index].name_text) {
			cpymo_engine_extract_text_cstr(
				e, e->backlog.records[index].name_text);
			cpymo_engine_extract_text_cstr(e, "\n");
		}
		cpymo_engine_extract_text_cstr(e, e->backlog.records[index].text);
		cpymo_engine_extract_text_submit(e);
	}
}

static error_t cpymo_backlog_ui_selection_changed(cpymo_engine *e, void *selected)
{
	if (selected) {
		size_t index = (size_t)cpymo_list_ui_encode_uint_node_dec(selected);
		cpymo_backlog_ui_extract_text(e, index);
	}
	return CPYMO_ERR_SUCC;
}
#endif

error_t cpymo_backlog_ui_enter(cpymo_engine *e)
{
	cpymo_backlog_ui *ui = NULL;

	size_t first = e->backlog.next_record_to_write;
	if (first) first--;
	else first = CPYMO_BACKLOG_MAX_RECORDS - 1;

	error_t err = cpymo_list_ui_enter(
		e,
		(void **)&ui,
		sizeof(cpymo_backlog_ui),
		&cpymo_backlog_ui_draw_node,
		&cpymo_backlog_ui_ok,
		&cpymo_ui_empty_deleter,
		cpymo_list_ui_encode_uint_node_enc(first),
		&cpymo_backlog_ui_get_next,
		&cpymo_backlog_ui_get_prev,
		true,
		3);
	CPYMO_THROW(err);

	cpymo_list_ui_set_custom_update(e, &cpymo_backlog_ui_update);

#ifdef ENABLE_TEXT_EXTRACT
	cpymo_list_ui_set_selection_changed_callback(
		e, &cpymo_backlog_ui_selection_changed);
	cpymo_backlog_ui_extract_text(e, first);
#endif

	ui->press_key_down_to_close = true;
	ui->last_played_index = (size_t)-1;

	return CPYMO_ERR_SUCC;
}

