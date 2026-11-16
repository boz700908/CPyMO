#include "cpymo_prelude.h"
#include "cpymo_msgbox_ui.h"
#include "cpymo_engine.h"
#include "cpymo_localization.h"
#include "cpymo_ui_patch.h"
#include "cpymo_key_hold.h"
#include "cpymo_accessibility.h"
#include "cpymo_safe_area.h"
#include <stdlib.h>
#include <string.h>

#ifdef __3DS__
extern bool fill_screen_enabled;
const extern bool fill_screen;
const extern bool enhanced_3ds_display_mode;
#endif

#if CPYMO_FEATURE_LEVEL >= 1
#define CPYMO_MSGBOX_UI_PATCH(e) \
	((e)->feature_level >= 1 ? &(e)->lua.ui_patch : NULL)
#else
#define CPYMO_MSGBOX_UI_PATCH(e) NULL
#endif

/* 条目序号：0 消息文本，1 确认，2 取消，3 起是追加按钮。 */
#define CPYMO_MSGBOX_ITEM_MESSAGE 0
#define CPYMO_MSGBOX_ITEM_OK 1
#define CPYMO_MSGBOX_ITEM_CANCEL 2
#define CPYMO_MSGBOX_ITEM_EXTRA_BASE 3

typedef struct {
	cpymo_backend_text message;
	float message_width;

	cpymo_backend_text confirm_btn;
	float confirm_btn_width;
	cpymo_backend_text cancel_btn;
	float cancel_btn_width;

	int selection;
	/* The action that opened this dialog may still be visible as an input
	 * release on the next update. Consume that inherited confirmation once. */
	bool ignore_initial_confirm_input;

	error_t(*okcancel_callback)(cpymo_engine *e, void *data, bool);
	void *okcancel_callback_data;
	cpymo_key_hold mouse_button;

	/* 条目补丁：隐藏标记与追加按钮；无补丁时保持初始值。 */
	bool hide_message, hide_ok, hide_cancel;
	size_t extra_count;
	cpymo_backend_text *extra_btns;
	float *extra_btn_widths;
} cpymo_msgbox_ui;


static error_t cpymo_msgbox_ui_okcancel_finish(cpymo_engine *e, bool is_ok)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)cpymo_ui_data(e);
	error_t(*func)(cpymo_engine *, void *, bool) = ui->okcancel_callback;
	void *data = ui->okcancel_callback_data;
	ui->okcancel_callback = NULL;
	ui->okcancel_callback_data = NULL;
	cpymo_ui_exit(e);
	return func ? func(e, data, is_ok) : CPYMO_ERR_SUCC;
}

/* 消息框视口：扣掉安全区后的可用范围；未注册 provider 时就是整屏。 */
static void cpymo_msgbox_ui_viewport(
	const cpymo_engine *e, float *out_left, float *out_top,
	float *out_width, float *out_height)
{
	const float w = (float)e->gameconfig.imagesize_w;
	const float h = (float)e->gameconfig.imagesize_h;
	cpymo_safe_area sa;
	cpymo_safe_area_get(&sa, e->gameconfig.imagesize_w, e->gameconfig.imagesize_h);
	const cpymo_rect view = cpymo_safe_area_usable_rect(&sa, w, h, 0.5f);

	*out_left = view.x;
	*out_top = view.y;
	*out_width = view.w;
	*out_height = view.h;
}

static void cpymo_msgbox_ui_get_btn_rect(
	float *out_xywh,
	float txt_width,
	float font_size,
	float screen_left,
	float screen_w,
	float y)
{
	float btn_width = txt_width * 3.0f;
	float btn_height = font_size * 1.5f;

	out_xywh[0] = screen_left + screen_w / 2 - btn_width / 2;
	out_xywh[1] = y;
	out_xywh[2] = btn_width;
	out_xywh[3] = btn_height;
}

/* ---------------- 条目补丁 ---------------- */

/* 条目最终文本：有改写用改写后的，否则用原文。 */
static cpymo_str cpymo_msgbox_ui_entry_str(
	const cpymo_engine *e, int index, cpymo_str fallback)
{
	const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
	if (!patch) return fallback;

	const char *t = cpymo_ui_patch_text(
		patch, CPYMO_UI_PATCH_PAGE_MSGBOX, index, fallback.begin);
	return t == fallback.begin ? fallback : cpymo_str_pure(t);
}

static bool cpymo_msgbox_ui_entry_hidden(const cpymo_engine *e, int index)
{
	const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
	return patch && cpymo_ui_patch_hidden(
		patch, CPYMO_UI_PATCH_PAGE_MSGBOX, index);
}

/* 确认/取消/追加按钮的行距，与上游取消按钮间距一致。 */
static float cpymo_msgbox_ui_row_step(float fontsize)
{
	return fontsize * 1.75f + (8.0f / (fontsize / 32));
}

static float cpymo_msgbox_ui_buttons_y(
	const cpymo_msgbox_ui *ui, float vtop, float vh)
{
	return vtop + (ui->cancel_btn ? vh * 0.5f : vh * 0.618f);
}

/* selection 编码：0 确认、1 取消、2+k 追加项 k，-1 尚未选中。 */
static int cpymo_msgbox_ui_ordinal_of(const cpymo_msgbox_ui *ui, int sel)
{
	int ordinal = 0;
	if (sel < 0) return -1;
	if (sel == CPYMO_MSGBOX_ITEM_OK) return 0;
	if (!ui->hide_ok) ++ordinal;
	if (sel == CPYMO_MSGBOX_ITEM_CANCEL) return ordinal;
	if (ui->cancel_btn && !ui->hide_cancel) ++ordinal;
	if (sel >= CPYMO_MSGBOX_ITEM_EXTRA_BASE)
		return ordinal + (sel - CPYMO_MSGBOX_ITEM_EXTRA_BASE);
	return -1;
}

static int cpymo_msgbox_ui_selection_count(const cpymo_msgbox_ui *ui)
{
	int n = 0;
	if (!ui->hide_ok) ++n;
	if (ui->cancel_btn && !ui->hide_cancel) ++n;
	return n + (int)ui->extra_count;
}

static int cpymo_msgbox_ui_selection_at(
	const cpymo_msgbox_ui *ui, int ordinal)
{
	if (ordinal < 0) return -1;
	if (!ui->hide_ok) {
		if (ordinal == 0) return CPYMO_MSGBOX_ITEM_OK;
		--ordinal;
	}
	if (ui->cancel_btn && !ui->hide_cancel) {
		if (ordinal == 0) return CPYMO_MSGBOX_ITEM_CANCEL;
		--ordinal;
	}
	if (ordinal >= 0 && (size_t)ordinal < ui->extra_count)
		return CPYMO_MSGBOX_ITEM_EXTRA_BASE + ordinal;
	return -1;
}

/* 只在可见条目之间移动；没有任何可见项时返回 -1。 */
static int cpymo_msgbox_ui_step(
	const cpymo_msgbox_ui *ui, int sel, int dir)
{
	const int total = cpymo_msgbox_ui_selection_count(ui);
	if (total <= 0) return -1;

	int ordinal = cpymo_msgbox_ui_ordinal_of(ui, sel);
	if (ordinal < 0) ordinal = dir > 0 ? 0 : total - 1;
	else ordinal = (ordinal + dir + total) % total;

	return cpymo_msgbox_ui_selection_at(ui, ordinal);
}

static void cpymo_msgbox_ui_speak_selection(cpymo_engine *e, int sel)
{
	if (sel < 0) return;

	const cpymo_localization *l = cpymo_localization_get(e);
	const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
	const char *t = NULL;

	if (sel == CPYMO_MSGBOX_ITEM_OK) t = l->msgbox_ok;
	else if (sel == CPYMO_MSGBOX_ITEM_CANCEL) t = l->msgbox_cancel;
	else if (sel >= CPYMO_MSGBOX_ITEM_EXTRA_BASE && patch)
		t = cpymo_ui_patch_extra_text(
			patch, CPYMO_UI_PATCH_PAGE_MSGBOX,
			(size_t)(sel - CPYMO_MSGBOX_ITEM_EXTRA_BASE));

	/* 改写过的按钮文字优先于本地化原文朗读。 */
	if (patch && sel < CPYMO_MSGBOX_ITEM_EXTRA_BASE) {
		const char *patched = cpymo_ui_patch_text(
			patch, CPYMO_UI_PATCH_PAGE_MSGBOX, sel, NULL);
		if (patched) t = patched;
	}

	if (t) { cpymo_a11y_speak(t); }
}

static int cpymo_msgbox_ui_get_mouse_selection(cpymo_engine *e)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)cpymo_ui_data(e);
	float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);

	float screen_left, screen_top, screen_w, screen_h;
	cpymo_msgbox_ui_viewport(e, &screen_left, &screen_top, &screen_w, &screen_h);

	const cpymo_input *input = &e->input;
	if (!input->mouse_position_useable)
		input = &e->prev_input;

	float btn_width =
		ui->confirm_btn_width > ui->cancel_btn_width ?
		ui->confirm_btn_width : ui->cancel_btn_width;

	if (input->mouse_position_useable) {
		float xywh[4];
		float x = input->mouse_x;
		float y = input->mouse_y;
		const float step = cpymo_msgbox_ui_row_step(fontsize);
		const float y0 = cpymo_msgbox_ui_buttons_y(ui, screen_top, screen_h);

		if (!ui->hide_ok) {
			cpymo_msgbox_ui_get_btn_rect(
				xywh, btn_width, fontsize, screen_left, screen_w, y0);
			if (x >= xywh[0] && x <= xywh[0] + xywh[2] &&
				y >= xywh[1] && y <= xywh[1] + xywh[3])
				return CPYMO_MSGBOX_ITEM_OK;
		}

		if (ui->cancel_btn && !ui->hide_cancel) {
			cpymo_msgbox_ui_get_btn_rect(
				xywh, btn_width, fontsize, screen_left, screen_w, y0 + step);
			if (x >= xywh[0] && x <= xywh[0] + xywh[2] &&
				y >= xywh[1] && y <= xywh[1] + xywh[3])
				return CPYMO_MSGBOX_ITEM_CANCEL;
		}

		for (size_t k = 0; k < ui->extra_count; ++k) {
			cpymo_msgbox_ui_get_btn_rect(
				xywh, btn_width, fontsize, screen_left, screen_w,
				y0 + step * (float)(k + 2));
			if (x >= xywh[0] && x <= xywh[0] + xywh[2] &&
				y >= xywh[1] && y <= xywh[1] + xywh[3])
				return CPYMO_MSGBOX_ITEM_EXTRA_BASE + (int)k;
		}
	}

	return -1;
}

static error_t cpymo_msgbox_ui_update(cpymo_engine *e, void *ui_data, float dt)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)ui_data;
	if (ui->ignore_initial_confirm_input) {
		if (!e->input.ok && !e->input.mouse_button)
			ui->ignore_initial_confirm_input = false;
		return CPYMO_ERR_SUCC;
	}

	enum cpymo_key_hold_result mbs = cpymo_key_hold_update(
		e, &ui->mouse_button, dt, e->input.mouse_button);
	
	if (ui->cancel_btn && !ui->hide_cancel &&
		(CPYMO_INPUT_JUST_RELEASED(e, cancel) ||
			mbs == cpymo_key_hold_result_holding)) {
		cpymo_accessibility_play_sound(SOUND_ENTER);
		cpymo_msgbox_ui_okcancel_finish(e, false);
		return CPYMO_ERR_SUCC;
	}

	if ((CPYMO_INPUT_JUST_PRESSED(e, up) || CPYMO_INPUT_JUST_PRESSED(e, down))) {
		if (CPYMO_MSGBOX_UI_PATCH(e) == NULL) {
			/* 无补丁时保持上游行为。 */
			if (ui->selection == -1) {
				if (ui->cancel_btn) ui->selection = 1;
				else ui->selection = 0;
			}
			else if (ui->selection == 0 && ui->cancel_btn) ui->selection = 1;
			else if (ui->selection == 1 && ui->cancel_btn) ui->selection = 0;
		}
		else {
			const int dir = CPYMO_INPUT_JUST_PRESSED(e, up) ? -1 : 1;
			ui->selection = cpymo_msgbox_ui_step(ui, ui->selection, dir);
		}

		cpymo_engine_request_redraw(e);
		cpymo_accessibility_play_sound(SOUND_SELECT);
		cpymo_msgbox_ui_speak_selection(e, ui->selection);
	}
	else if (CPYMO_INPUT_JUST_PRESSED(e, ok)) {
		if (ui->cancel_btn == NULL) {
			ui->selection = 0;
		}

		cpymo_engine_request_redraw(e);
	}
	else if (CPYMO_INPUT_JUST_PRESSED(e, mouse_button)) {
		ui->selection = cpymo_msgbox_ui_get_mouse_selection(e);
		cpymo_engine_request_redraw(e);
	}
	else if (CPYMO_INPUT_JUST_RELEASED(e, ok) && ui->selection != -1) {
		cpymo_engine_request_redraw(e);
		cpymo_accessibility_play_sound(SOUND_ENTER);
#if CPYMO_FEATURE_LEVEL >= 1
		if (ui->selection >= CPYMO_MSGBOX_ITEM_EXTRA_BASE) {
			const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
			/* Without an on_item_click handler the click must not close the
			   message box: an added button only does what its author wrote. */
			if (patch && cpymo_lua_ui_page_click(
				&e->lua, CPYMO_UI_PATCH_PAGE_MSGBOX,
				ui->selection - CPYMO_MSGBOX_ITEM_EXTRA_BASE))
				return cpymo_msgbox_ui_okcancel_finish(e, false);
			return CPYMO_ERR_SUCC;
		}
#endif
		return cpymo_msgbox_ui_okcancel_finish(e, !ui->selection);
	}
	else if (CPYMO_INPUT_JUST_RELEASED(e, mouse_button)) {
		cpymo_engine_request_redraw(e);
		int mouse_sel = cpymo_msgbox_ui_get_mouse_selection(e);
		if (mouse_sel < 0) return CPYMO_ERR_SUCC;
		cpymo_accessibility_play_sound(SOUND_ENTER);
#if CPYMO_FEATURE_LEVEL >= 1
		if (mouse_sel >= CPYMO_MSGBOX_ITEM_EXTRA_BASE) {
			const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
			/* Same rule as the keyboard path: no handler, no effect. */
			if (patch && cpymo_lua_ui_page_click(
				&e->lua, CPYMO_UI_PATCH_PAGE_MSGBOX,
				mouse_sel - CPYMO_MSGBOX_ITEM_EXTRA_BASE))
				return cpymo_msgbox_ui_okcancel_finish(e, false);
			return CPYMO_ERR_SUCC;
		}
#endif
		return cpymo_msgbox_ui_okcancel_finish(e, !mouse_sel);
	}

	if (cpymo_input_mouse_moved(e)) {
		int mouse_sel = cpymo_msgbox_ui_get_mouse_selection(e);
		if (mouse_sel != ui->selection) {
			ui->selection = mouse_sel;
			cpymo_engine_request_redraw(e);

			if (ui->selection >= 0)
				cpymo_msgbox_ui_speak_selection(e, ui->selection);
		}
	}

	return CPYMO_ERR_SUCC;
}

static void cpymo_msgbox_ui_draw_btn(
	cpymo_backend_text btn_txt,
	float btn_width,
	float txt_width, 
	float font_size, 
	float screen_left,
	float screen_w,
	float screen_h,
	float y, 
	bool sel,
	bool key_down)
{
	float xywh[4];
	cpymo_msgbox_ui_get_btn_rect(xywh, btn_width, font_size, screen_left, screen_w, y);

	cpymo_color gray;
	gray.r = 128;
	gray.g = 128;
	gray.b = 128;

	cpymo_backend_image_fill_rects(
		xywh,
		1,
		sel ? (key_down ? gray : cpymo_color_white) : cpymo_color_black,
		sel ? 1.0f : 0.5f,
		cpymo_backend_image_draw_type_ui_element);

	cpymo_backend_text_draw(
		btn_txt,
		screen_left + screen_w / 2 - txt_width / 2,
		y + xywh[3] / 2 + font_size / 2 - font_size * 0.15f,
		sel ? cpymo_color_black : cpymo_color_white,
		1.0f,
		cpymo_backend_image_draw_type_ui_element);
}

static void cpymo_msgbox_ui_draw(const cpymo_engine *e, const void *ui_data)
{
	cpymo_bg_draw(e);
	cpymo_scroll_draw(&e->scroll);

	#ifdef __3DS__
	if (fill_screen && !enhanced_3ds_display_mode)
		fill_screen_enabled = false;
	#endif

	float screen_w = (float)e->gameconfig.imagesize_w, screen_h = (float)e->gameconfig.imagesize_h;
	float vleft, vtop, vw, vh;
	cpymo_msgbox_ui_viewport(e, &vleft, &vtop, &vw, &vh);
	float xywh[] = { -100, -100, screen_w + 200, screen_h + 200 };
	cpymo_backend_image_fill_rects(xywh, 1, cpymo_color_black, 0.5f, cpymo_backend_image_draw_type_bg);

	const cpymo_msgbox_ui *ui = (const cpymo_msgbox_ui *)ui_data;
	float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);

	if (!ui->hide_message) {
		float x = vleft + (vw - ui->message_width) / 2;
		float y = vtop + vh * 0.3f + fontsize / 2;
		cpymo_backend_text_draw(ui->message, x, y, cpymo_color_white, 1.0f, cpymo_backend_image_draw_type_ui_element);
	}

	bool key_down = e->input.mouse_button || e->input.ok;

	float btn_width = ui->confirm_btn_width > ui->cancel_btn_width ? ui->confirm_btn_width : ui->cancel_btn_width;

	const float step = cpymo_msgbox_ui_row_step(fontsize);
	const float y0 = cpymo_msgbox_ui_buttons_y(ui, vtop, vh);

	if (!ui->hide_ok) {
		cpymo_msgbox_ui_draw_btn(
			ui->confirm_btn, 
			btn_width,
			ui->confirm_btn_width, 
			fontsize, 
			vleft,
			vw, 
			screen_h,
			y0,
			ui->selection == CPYMO_MSGBOX_ITEM_OK,
			key_down);
	}

	if (ui->cancel_btn && !ui->hide_cancel) {
		cpymo_msgbox_ui_draw_btn(
			ui->cancel_btn,
			btn_width,
			ui->cancel_btn_width,
			fontsize,
			vleft,
			vw,
			screen_h,
			y0 + step,
			ui->selection == CPYMO_MSGBOX_ITEM_CANCEL,
			key_down);
	}

	for (size_t k = 0; k < ui->extra_count; ++k) {
		if (!ui->extra_btns[k]) continue;
		cpymo_msgbox_ui_draw_btn(
			ui->extra_btns[k],
			btn_width,
			ui->extra_btn_widths[k],
			fontsize,
			vleft,
			vw,
			screen_h,
			y0 + step * (float)(k + 2),
			ui->selection == CPYMO_MSGBOX_ITEM_EXTRA_BASE + (int)k,
			key_down);
	}

	#ifdef __3DS__
	fill_screen_enabled = true;
	#endif
}

static void cpymo_msgbox_ui_delete(struct cpymo_engine *e, void *ui_data)
{
	cpymo_msgbox_ui *ui = (cpymo_msgbox_ui *)ui_data;

	if (ui->okcancel_callback) {
		ui->okcancel_callback(e, ui->okcancel_callback_data, false);
		ui->okcancel_callback = NULL;
		ui->okcancel_callback_data = NULL;
	}
	
	if (ui->message) cpymo_backend_text_free(ui->message);
	if (ui->confirm_btn) cpymo_backend_text_free(ui->confirm_btn);
	if (ui->cancel_btn) cpymo_backend_text_free(ui->cancel_btn);

	for (size_t k = 0; k < ui->extra_count; ++k)
		if (ui->extra_btns[k]) cpymo_backend_text_free(ui->extra_btns[k]);
	free(ui->extra_btns);
	free(ui->extra_btn_widths);
}

static error_t cpymo_msgbox_ui_default_confirm(
	struct cpymo_engine *e, void *d, bool b)
{
	return CPYMO_ERR_SUCC;
}

error_t cpymo_msgbox_ui_enter(
	cpymo_engine *e, 
	cpymo_str message, 
	error_t(*okcancel_callback)(cpymo_engine *e, void *data, bool is_confirm), 
	void * okcancel_callback_data)
{
	cpymo_msgbox_ui *ui = NULL;
	error_t err = cpymo_ui_enter(
		(void **)&ui,
		e,
		sizeof(cpymo_msgbox_ui),
		&cpymo_msgbox_ui_update,
		&cpymo_msgbox_ui_draw,
		&cpymo_msgbox_ui_delete);
	CPYMO_THROW(err);

	ui->message = NULL;
	ui->confirm_btn = NULL;
	ui->cancel_btn = NULL;
	ui->cancel_btn_width = 0;
	ui->confirm_btn_width = 0;

	ui->hide_message = false;
	ui->hide_ok = false;
	ui->hide_cancel = false;
	ui->extra_count = 0;
	ui->extra_btns = NULL;
	ui->extra_btn_widths = NULL;

	ui->okcancel_callback = okcancel_callback;
	ui->okcancel_callback_data = okcancel_callback_data;
	/* The visible and spoken first button is always Confirm.  Starting on
	 * Cancel made directional navigation disagree with the dialog layout. */
	ui->selection = 0;
	ui->ignore_initial_confirm_input =
		e->input.ok || e->prev_input.ok ||
		e->input.mouse_button || e->prev_input.mouse_button;
	cpymo_key_hold_init(&ui->mouse_button, e->input.mouse_button);

	float fontsize = cpymo_gameconfig_font_size(&e->gameconfig);
	const cpymo_ui_patch *patch = CPYMO_MSGBOX_UI_PATCH(e);
	if (patch) cpymo_ui_patch_set_base(
		(cpymo_ui_patch *)patch, CPYMO_UI_PATCH_PAGE_MSGBOX,
		CPYMO_MSGBOX_ITEM_EXTRA_BASE);

	err = cpymo_backend_text_create(
		&ui->message, &ui->message_width,
		cpymo_msgbox_ui_entry_str(e, CPYMO_MSGBOX_ITEM_MESSAGE, message),
		fontsize);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	const cpymo_localization *l = cpymo_localization_get(e);

	err = cpymo_backend_text_create(
		&ui->confirm_btn, 
		&ui->confirm_btn_width, 
		cpymo_msgbox_ui_entry_str(
			e, CPYMO_MSGBOX_ITEM_OK, cpymo_str_pure(l->msgbox_ok)), 
		fontsize);
	if (err != CPYMO_ERR_SUCC) {
		cpymo_ui_exit(e);
		return err;
	}

	if (okcancel_callback) {
		err = cpymo_backend_text_create(
			&ui->cancel_btn,
			&ui->cancel_btn_width,
			cpymo_msgbox_ui_entry_str(
				e, CPYMO_MSGBOX_ITEM_CANCEL,
				cpymo_str_pure(l->msgbox_cancel)),
			fontsize);
		if (err != CPYMO_ERR_SUCC) {
			cpymo_ui_exit(e);
			return err;
		}
	}
	else {
		ui->okcancel_callback = &cpymo_msgbox_ui_default_confirm;
	}

	/* 追加按钮：只作可点击扩展项，不改变确认/取消语义。 */
	if (patch) {
		const size_t n = cpymo_ui_patch_extra_count(
			patch, CPYMO_UI_PATCH_PAGE_MSGBOX);
		if (n > 0) {
			cpymo_backend_text *btns = (cpymo_backend_text *)calloc(
				n, sizeof(cpymo_backend_text));
			float *widths = (float *)calloc(n, sizeof(float));
			if (btns && widths) {
				ui->extra_btns = btns;
				ui->extra_btn_widths = widths;
				ui->extra_count = n;
				for (size_t k = 0; k < n; ++k) {
					const char *t = cpymo_ui_patch_extra_text(
						patch, CPYMO_UI_PATCH_PAGE_MSGBOX, k);
					if (t && *t)
						cpymo_backend_text_create(
							&ui->extra_btns[k],
							&ui->extra_btn_widths[k],
							cpymo_str_pure(t), fontsize);
				}
			}
			else { free(btns); free(widths); }
		}
	}

	ui->hide_message = cpymo_msgbox_ui_entry_hidden(
		e, CPYMO_MSGBOX_ITEM_MESSAGE);
	ui->hide_ok = cpymo_msgbox_ui_entry_hidden(e, CPYMO_MSGBOX_ITEM_OK);
	ui->hide_cancel = cpymo_msgbox_ui_entry_hidden(
		e, CPYMO_MSGBOX_ITEM_CANCEL);
	/* 确认框至少要留一个能退出的按钮。 */
	if (ui->hide_ok && (ui->hide_cancel || ui->cancel_btn == NULL))
		ui->hide_ok = false;
	if (ui->hide_cancel && ui->hide_ok)
		ui->hide_cancel = false;
	if (ui->hide_ok || ui->hide_cancel)
		ui->selection = cpymo_msgbox_ui_step(ui, -1, 1);

	cpymo_accessibility_play_sound(SOUND_ENTER);
	cpymo_engine_extract_text(e, message);
	cpymo_engine_extract_text_cstr(e, l->msgbox_ok);
	if (ui->cancel_btn) { cpymo_engine_extract_text_cstr(e, l->msgbox_cancel); }
	cpymo_engine_extract_text_submit(e);

	return err;
}

