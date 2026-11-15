#include "cpymo_prelude.h"
#include "cpymo_input_text.h"
#include "cpymo_engine.h"
#include "cpymo_vars.h"
#include "cpymo_gameconfig.h"
#include "cpymo_color.h"
#include "../cpymo-backends/include/cpymo_backend_input.h"
#include "../cpymo-backends/include/cpymo_backend_image.h"
#include <string.h>
#include <stdio.h>

static void release_prompt_text(cpymo_input_text *it)
{
	if (it->prompt_text) {
		cpymo_backend_text_free(it->prompt_text);
		it->prompt_text = NULL;
	}
}

static void release_value_text(cpymo_input_text *it)
{
	if (it->value_text) {
		cpymo_backend_text_free(it->value_text);
		it->value_text = NULL;
	}
}

static void rebuild_value_text(cpymo_input_text *it, struct cpymo_engine *e)
{
	release_value_text(it);

	char buf[CPYMO_INPUT_TEXT_MAX_LEN + 2];
	memcpy(buf, it->text, it->text_len);
	buf[it->text_len] = '_';
	buf[it->text_len + 1] = 0;

	float width = 0;
	float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
	if (cpymo_backend_text_create(
		&it->value_text, &width, cpymo_str_pure(buf), fontsize) != CPYMO_ERR_SUCC)
		it->value_text = NULL;

	it->dirty = false;
}

static void rebuild_texts(cpymo_input_text *it, struct cpymo_engine *e)
{
	release_prompt_text(it);

	if (it->prompt[0]) {
		float width = 0;
		float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
		if (cpymo_backend_text_create(
			&it->prompt_text, &width, cpymo_str_pure(it->prompt), fontsize)
			!= CPYMO_ERR_SUCC)
			it->prompt_text = NULL;
	}

	rebuild_value_text(it, e);
}

void cpymo_input_text_init(cpymo_input_text *it)
{
	memset(it, 0, sizeof(*it));
}

void cpymo_input_text_free(cpymo_input_text *it)
{
	if (it->active && cpymo_backend_input_text_end)
		cpymo_backend_input_text_end();

	release_prompt_text(it);
	release_value_text(it);
	memset(it, 0, sizeof(*it));
}

error_t cpymo_input_text_start(
	struct cpymo_engine *e, cpymo_str var_name, cpymo_str prompt)
{
	cpymo_input_text *it = &e->input_text;

	if (var_name.len == 0 || var_name.len >= sizeof(it->var_name))
		return CPYMO_ERR_INVALID_ARG;

	if (it->active && cpymo_backend_input_text_end)
		cpymo_backend_input_text_end();

	release_prompt_text(it);
	release_value_text(it);
	memset(it, 0, sizeof(*it));

	cpymo_str_copy(it->var_name, sizeof(it->var_name), var_name);
	if (prompt.len > 0) cpymo_str_copy(it->prompt, sizeof(it->prompt), prompt);

	it->active = true;
	it->dirty = true;

	if (cpymo_backend_input_text_begin) cpymo_backend_input_text_begin();
	else printf("[Warning] Backend does not support text input, "
		"the \"input\" command will be skipped.\n");

	if (it->prompt[0]) cpymo_a11y_speak(it->prompt);

	rebuild_texts(it, e);
	cpymo_engine_request_redraw(e);
	return CPYMO_ERR_SUCC;
}

static void append_char(cpymo_input_text *it, char c)
{
	if (it->text_len >= CPYMO_INPUT_TEXT_MAX_LEN) return;
	it->text[it->text_len++] = c;
	it->text[it->text_len] = 0;
	it->dirty = true;
}

static void drop_last_char(cpymo_input_text *it)
{
	if (it->text_len == 0) return;
	it->text[--it->text_len] = 0;
	it->dirty = true;
}

static void finish_input(struct cpymo_engine *e, bool confirmed)
{
	cpymo_input_text *it = &e->input_text;

	if (confirmed) {
		cpymo_val value = (cpymo_val)cpymo_str_atoi(cpymo_str_pure(it->text));
		error_t err = cpymo_vars_set(
			&e->vars, cpymo_str_pure(it->var_name), value);

		if (err != CPYMO_ERR_SUCC)
			printf("[Warning] input: can not set variable \"%s\".\n", it->var_name);
	}

	if (cpymo_backend_input_text_end) cpymo_backend_input_text_end();

	it->active = false;
	release_prompt_text(it);
	release_value_text(it);
	cpymo_engine_request_redraw(e);
}

bool cpymo_input_text_wait_for(struct cpymo_engine *e, float delta_time)
{
	(void)delta_time;
	cpymo_input_text *it = &e->input_text;

	if (!it->active) return true;

	if (cpymo_backend_input_text_poll == NULL) {
		printf("[Warning] Backend does not support text input, "
			"the \"input\" command is skipped.\n");
		finish_input(e, false);
		return true;
	}

	const char *chars = cpymo_backend_input_text_poll();
	bool finish_now = false;
	bool confirmed = false;

	while (chars && *chars) {
		unsigned char c = (unsigned char)(*chars++);

		if (c == '\b' || c == 127) drop_last_char(it);
		else if (c == '\n' || c == '\r') { finish_now = true; confirmed = true; break; }
		else if (c == 27) { finish_now = true; confirmed = false; break; }
		else if (c >= '0' && c <= '9') append_char(it, (char)c);
		else if (c == '-' && it->text_len == 0) append_char(it, '-');
	}

	if (it->dirty && !finish_now) rebuild_value_text(it, e);

	if (finish_now) {
		finish_input(e, confirmed);
		return true;
	}

	return false;
}

void cpymo_input_text_draw(
	const cpymo_input_text *it, const struct cpymo_engine *e)
{
	if (!it->active) return;

	float w = (float)e->gameconfig.imagesize_w;
	float h = (float)e->gameconfig.imagesize_h;
	float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);

	float box_w = w * 0.62f;
	float box_h = fontsize * 3.4f;
	float box_x = (w - box_w) * 0.5f;
	float box_y = h * 0.70f;

	cpymo_color box_color;
	box_color.r = 0;
	box_color.g = 0;
	box_color.b = 0;

	cpymo_color text_color;
	text_color.r = 255;
	text_color.g = 255;
	text_color.b = 255;

	float xywh[] = { box_x, box_y, box_w, box_h };
	cpymo_backend_image_fill_rects(
		xywh, 1, box_color, 0.72f,
		cpymo_backend_image_draw_type_ui_element_bg);

	float text_x = box_x + fontsize * 0.6f;

	if (it->prompt_text)
		cpymo_backend_text_draw(
			it->prompt_text,
			text_x,
			box_y + fontsize * 1.4f,
			text_color, 1.0f,
			cpymo_backend_image_draw_type_text_text);

	if (it->value_text)
		cpymo_backend_text_draw(
			it->value_text,
			text_x,
			box_y + fontsize * 2.8f,
			text_color, 1.0f,
			cpymo_backend_image_draw_type_text_text);
}
