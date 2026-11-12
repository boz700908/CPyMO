#include "cpymo_prelude.h"
#include "cpymo_engine.h"
#include "cpymo_interpreter.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "cpymo_msgbox_ui.h"
#include "cpymo_save_global.h"
#include "cpymo_localization.h"
#include "../cpymo-backends/include/cpymo_backend_text.h"
#include "../cpymo-backends/include/cpymo_backend_startup_music.h"

static void cpymo_logo() {
	static bool logo_printed = false;
	if (logo_printed) return;
	logo_printed = true;

	puts("   __________        __  _______");
	puts("  / ____/ __ \\__  __/  |/  / __ \\");
	puts(" / /   / /_/ / / / / /|_/ / / / /");
	puts("/ /___/ ____/ /_/ / /  / / /_/ /");
	puts("\\____/_/    \\__, /_/  /_/\\____/");
	puts("           /____/");
	puts("");
}

static error_t cpymo_engine_non_pymo_warning(cpymo_engine *e) 
{
	const cpymo_localization *l = cpymo_localization_get(e);
	return cpymo_msgbox_ui_enter(
		e, cpymo_str_pure(l->mo2pymo_required), NULL, NULL);
}

static error_t cpymo_engine_version_warning(cpymo_engine *e) 
{
	const cpymo_localization *l = cpymo_localization_get(e);
	char *out = NULL;
	error_t err = l->pymo_version_not_compatible_message(
			&out, e->gameconfig.engineversion);
	CPYMO_THROW(err);

	err = cpymo_msgbox_ui_enter(
		e,
		cpymo_str_pure(out),
		NULL,
		NULL);
	free(out);

	return err;
}

/* 启动音乐（《Song of PyMO》）：只在启动进入游戏选择器时播一次。
 *
 * 只由 cpymo_engine_init_with_game_selector 调用，且整个进程只触发一次
 * （退出游戏回到选择器不会再播）。直接拉起单一游戏的构建不经过选择器，
 * 因此不播放——那种情况下开机就该直接进入游戏，不应压着游戏音乐。
 *
 * 未实现这两个符号的后端（弱符号链接后为 NULL）与自带音频图标的平台
 * （PSP 的 SND0、3DS 的 banner 音乐）什么也不做。
 */
void cpymo_engine_play_startup_music_once(void)
{
	static bool played = false;
	if (played) return;
	played = true;

	if (cpymo_backend_startup_music_play != NULL)
		cpymo_backend_startup_music_play();
}

error_t cpymo_engine_init(cpymo_engine *out, const char *gamedir)
{
	/* 启动音乐只属于游戏选择器阶段：真正进入游戏就收掉，
	 * 免得和游戏自己的开场 BGM 叠在一起。直接进游戏的构建没播过，这里是空操作。 */
	if (cpymo_backend_startup_music_stop != NULL)
		cpymo_backend_startup_music_stop();

	// init audio system
	cpymo_audio_init(&out->audio);

	// load game config
	const size_t gamedir_strlen = strlen(gamedir);
	char *path = (char *)malloc(gamedir_strlen + 16);
	if (path == NULL) return CPYMO_ERR_OUT_OF_MEM;

	strcpy(path, gamedir);
	strcpy(path + gamedir_strlen, "/gameconfig.txt");
	error_t err = cpymo_gameconfig_parse_from_file(&out->gameconfig, path);
	free(path);
	if (err != CPYMO_ERR_SUCC) return err;

	// set default volume for audio system
	{
		float v = cpymo_utils_clamp(out->gameconfig.bgmvolume, 0, 5) * 0.2f;
		cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_BGM, &out->audio, v);
		cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_SE, &out->audio, v);
		v = cpymo_utils_clamp(out->gameconfig.vovolume, 0, 5) * 0.2f;
		cpymo_audio_set_channel_volume(CPYMO_AUDIO_CHANNEL_VO, &out->audio, v);
	}

	// create asset loader
	err = cpymo_assetloader_init(&out->assetloader, &out->gameconfig, gamedir);
	if (err != CPYMO_ERR_SUCC) return err;

	// create vars
	cpymo_vars_init(&out->vars);

	// create script interpreter
	out->interpreter = (cpymo_interpreter *)malloc(sizeof(cpymo_interpreter));
	if (out->interpreter == NULL) {
		cpymo_assetloader_free(&out->assetloader);
		return CPYMO_ERR_OUT_OF_MEM;
	}

	cpymo_script *boot_script;
	err = cpymo_script_create_bootloader(&boot_script, out->gameconfig.startscript);
	if (err != CPYMO_ERR_SUCC) {
		free(out->interpreter);
		cpymo_vars_free(&out->vars);
		cpymo_assetloader_free(&out->assetloader);
		return err;
	}

	cpymo_interpreter_init(out->interpreter, boot_script, true, NULL);

	// create title
	out->title = (char *)malloc(1);
	if (out->title == NULL) {
		cpymo_interpreter_free(out->interpreter);
		free(out->interpreter);
		cpymo_vars_free(&out->vars);
		cpymo_assetloader_free(&out->assetloader);
		return CPYMO_ERR_OUT_OF_MEM;
	}
	out->title[0] = '\0';

	// init wait
	cpymo_wait_reset(&out->wait);

	// init flash
	cpymo_flash_reset(&out->flash);

	// init fade 
	cpymo_fade_reset(&out->fade);

	// init bg
	cpymo_bg_init(&out->bg);

	// init anime
	cpymo_anime_init(&out->anime);

	// init select_img/select_imgs
	cpymo_select_img_init(&out->select_img);

	// init charas
	cpymo_charas_init(&out->charas);

	// init scroll
	cpymo_scroll_init(&out->scroll);

	// init floating hint
	cpymo_floating_hint_init(&out->floating_hint);

	// init say
	cpymo_say_init(&out->say);

	// init text
	cpymo_text_init(&out->text);

	// init hash flags
	cpymo_hash_flags_init(&out->flags);

	// init ui
	out->ui = NULL;

	// init backlog
	err = cpymo_backlog_init(&out->backlog);
	if (err != CPYMO_ERR_SUCC) {
		free(out->title);
		cpymo_interpreter_free(out->interpreter);
		free(out->interpreter);
		cpymo_vars_free(&out->vars);
		cpymo_assetloader_free(&out->assetloader);
		return err;
	}

	// states
	out->skipping = false;
	out->redraw = true;
	out->ignore_next_mouse_button_flag = false;

	// default config
	out->config_skip_already_read_only = true;

	// text extract buffer
	#ifdef ENABLE_TEXT_EXTRACT
	out->text_extract_buffer = NULL;
	out->text_extract_buffer_size = 0;
	out->text_extract_buffer_maxsize = 0;
	out->text_extract_copy_latched = false;
	#endif

	// load config
	err = cpymo_save_config_load(out);
	if (err != CPYMO_ERR_SUCC && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		printf("[Error] Config data broken! %s\n", cpymo_error_message(err));
	}

	// load global save data
	err = cpymo_save_global_load(out);
	if (err != CPYMO_ERR_SUCC && err != CPYMO_ERR_CAN_NOT_OPEN_FILE) {
		printf("[Error] Global save data broken! %s\n", cpymo_error_message(err));
	}

	out->input = out->prev_input = cpymo_input_snapshot();
	out->ignore_next_mouse_button_flag = out->input.mouse_button;

	cpymo_logo();

	// checks
	if (out->gameconfig.scripttype[0] != 'p'
		|| out->gameconfig.scripttype[1] != 'y'
		|| out->gameconfig.scripttype[2] != 'm'
		|| out->gameconfig.scripttype[3] != '\0') {
		cpymo_wait_callback_after_seconds(&out->wait, 0, &cpymo_engine_non_pymo_warning);
	}

	if (!cpymo_pymo_version_compatible(
			out->gameconfig.engineversion, cpymo_pymo_version_current)) {
		cpymo_wait_callback_after_seconds(
			&out->wait, 0, 
			&cpymo_engine_version_warning);
	}

	return CPYMO_ERR_SUCC;
}

void cpymo_engine_free(cpymo_engine *engine)
{
#ifdef ENABLE_TEXT_EXTRACT
	/* R7：退出游戏时清队并停掉语音，不念任何告别内容 */
	cpymo_a11y_speech_clear();
#endif
	while (engine->ui) cpymo_ui_exit(engine);

	if (engine->assetloader.gamedir) {
		error_t err = cpymo_save_global_save(engine);
		if (err != CPYMO_ERR_SUCC)
			printf("[Error] Can not save global savedata. %s\n", cpymo_error_message(err));

		err = cpymo_save_config_save(engine);
		if (err != CPYMO_ERR_SUCC)
			printf("[Error] Can not save config. %s\n", cpymo_error_message(err));
	}
	
	cpymo_hash_flags_free(&engine->flags);
	cpymo_text_free(&engine->text);
	cpymo_say_free(&engine->say);
	cpymo_backlog_free(&engine->backlog);
	cpymo_floating_hint_free(&engine->floating_hint);
	cpymo_scroll_free(&engine->scroll);
	cpymo_charas_free(&engine->charas);
	cpymo_select_img_free(&engine->select_img);
	cpymo_anime_free(&engine->anime);
	cpymo_bg_free(&engine->bg);
	if (engine->interpreter) {
		cpymo_interpreter_free(engine->interpreter);
		free(engine->interpreter);
	}
	cpymo_vars_free(&engine->vars);
	cpymo_assetloader_free(&engine->assetloader);
	if (engine->title) free(engine->title);
	cpymo_audio_free(&engine->audio);

	#ifdef ENABLE_TEXT_EXTRACT
	if (engine->text_extract_buffer) free(engine->text_extract_buffer);
	#endif
}

bool cpymo_engine_skipping(cpymo_engine *e)
{
	bool skipping = e->input.skip || e->skipping;
	if (e->config_skip_already_read_only) {
		if (!e->say.current_say_is_already_read) {
			e->skipping = false;
			return false;
		}
	}
	return skipping;
}

void cpymo_engine_request_redraw(cpymo_engine *engine)
{
	engine->redraw = true;
}

static error_t cpymo_engine_exit_update(
	struct cpymo_engine *e, void *ui_data, float d)
{ return CPYMO_ERR_NO_MORE_CONTENT; }

void cpymo_engine_exit(cpymo_engine *e)
{
	void *dummy;
	cpymo_ui_enter(
		&dummy, e, 0, 
		&cpymo_engine_exit_update, 
		&cpymo_ui_empty_drawer,
		&cpymo_ui_empty_deleter);
}


/* ===================== 朗读队列（R1–R7） =====================
 * 规则见 /memories/tts-interrupt-rules.md：
 *   R1 按键只记录时刻与次数，本身不打断；
 *   R2 按键之后的第一条文本替换正在念的（并清空队列），从这条开头念；
 *   R3 其余文本排队，不打断；
 *   R4 玩家正在操作时（宽限窗口 700ms 内）队列只留最新一条；
 *   R5 自己持队列，引擎永远只有一条；
 *   R6 同一段文字不重复入队；
 *   R7 暂停/退出用 clear()，不念告别语。
 * 平台只管"念一条"和"停下"（cpymo_backend_text_extract / _stop），
 * 排队与打断判断全在这里，所有后端共用同一套规则。 */
#ifdef ENABLE_TEXT_EXTRACT
#define CPYMO_A11Y_QUEUE_MAX 8
#define CPYMO_A11Y_GRACE_SEC 0.7

typedef struct {
	double now;
	double last_input;
	int presses;
	char *speaking;
	char *queue[CPYMO_A11Y_QUEUE_MAX];
	int q_head;
	int q_count;
	double deadline;
} cpymo_a11y_speech_state;

static cpymo_a11y_speech_state cpymo_a11y_speech;

static char *cpymo_a11y_strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = (char *)malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static bool cpymo_a11y_is_cjk(const unsigned char *p)
{
	return (p[0] >= 0xE0);
}

/* 只用于兜底定时器，不是打断判据 */
static double cpymo_a11y_estimate_sec(const char *text)
{
	double ms = 500.0;
	const unsigned char *p = (const unsigned char *)text;
	while (*p) {
		if (cpymo_a11y_is_cjk(p)) { ms += 145.0; while ((*p & 0xC0) == 0x80) p++; }
		else ms += 70.0;
		p++;
	}
	double sec = ms / 1000.0;
	if (sec < 0.7) sec = 0.7;
	if (sec > 20.0) sec = 20.0;
	return sec;
}

static void cpymo_a11y_start(const char *text)
{
	free(cpymo_a11y_speech.speaking);
	cpymo_a11y_speech.speaking = cpymo_a11y_strdup(text);
	cpymo_a11y_speech.deadline =
		cpymo_a11y_speech.now + cpymo_a11y_estimate_sec(text) * 1.2 + 1.0;
	cpymo_backend_text_extract(text);
}

static void cpymo_a11y_queue_pop_head(void)
{
	if (cpymo_a11y_speech.q_count <= 0) return;
	free(cpymo_a11y_speech.queue[cpymo_a11y_speech.q_head]);
	cpymo_a11y_speech.queue[cpymo_a11y_speech.q_head] = NULL;
	cpymo_a11y_speech.q_head =
		(cpymo_a11y_speech.q_head + 1) % CPYMO_A11Y_QUEUE_MAX;
	cpymo_a11y_speech.q_count--;
}

static void cpymo_a11y_speech_done(void)
{
	free(cpymo_a11y_speech.speaking);
	cpymo_a11y_speech.speaking = NULL;
	if (cpymo_a11y_speech.q_count > 0) {
		char *next = cpymo_a11y_speech.queue[cpymo_a11y_speech.q_head];
		cpymo_a11y_speech.queue[cpymo_a11y_speech.q_head] = NULL;
		cpymo_a11y_speech.q_head =
			(cpymo_a11y_speech.q_head + 1) % CPYMO_A11Y_QUEUE_MAX;
		cpymo_a11y_speech.q_count--;
		cpymo_a11y_start(next);
		free(next);
	}
}

void cpymo_a11y_speech_clear(void)
{
	while (cpymo_a11y_speech.q_count > 0) cpymo_a11y_queue_pop_head();
	free(cpymo_a11y_speech.speaking);
	cpymo_a11y_speech.speaking = NULL;
	cpymo_backend_text_stop();
}

void cpymo_a11y_speech_note_input(void)
{
	cpymo_a11y_speech.presses++;
	cpymo_a11y_speech.last_input = cpymo_a11y_speech.now;
}

void cpymo_a11y_speak(const char *text)
{
	if (!text || text[0] == '\0') return;

	if (cpymo_a11y_speech.speaking &&
		strcmp(cpymo_a11y_speech.speaking, text) == 0) return;      /* R6 */
	for (int i = 0; i < cpymo_a11y_speech.q_count; i++) {
		int idx = (cpymo_a11y_speech.q_head + i) % CPYMO_A11Y_QUEUE_MAX;
		if (strcmp(cpymo_a11y_speech.queue[idx], text) == 0) return;
	}

	bool flush = cpymo_a11y_speech.presses > 0;                     /* R2 */
	cpymo_a11y_speech.presses = 0;

	if (flush) {
		while (cpymo_a11y_speech.q_count > 0) cpymo_a11y_queue_pop_head();
		cpymo_backend_text_stop();
		free(cpymo_a11y_speech.speaking);
		cpymo_a11y_speech.speaking = NULL;
		cpymo_a11y_start(text);
		return;
	}

	if (cpymo_a11y_speech.speaking || cpymo_a11y_speech.q_count > 0) {  /* R3/R4 */
		bool interacting =
			(cpymo_a11y_speech.now - cpymo_a11y_speech.last_input)
			<= CPYMO_A11Y_GRACE_SEC;
		int limit = interacting ? 1 : CPYMO_A11Y_QUEUE_MAX;
		while (cpymo_a11y_speech.q_count >= limit) cpymo_a11y_queue_pop_head();
		int tail = (cpymo_a11y_speech.q_head + cpymo_a11y_speech.q_count)
			% CPYMO_A11Y_QUEUE_MAX;
		cpymo_a11y_speech.queue[tail] = cpymo_a11y_strdup(text);
		if (cpymo_a11y_speech.queue[tail]) cpymo_a11y_speech.q_count++;
		return;
	}

	cpymo_a11y_start(text);
}

void cpymo_a11y_speech_tick(void)
{
	/* 兜底：平台不回报"念完了"时，靠估算时间推进队列（宁可晚，不可早） */
	if (cpymo_a11y_speech.speaking &&
		cpymo_a11y_speech.now >= cpymo_a11y_speech.deadline)
		cpymo_a11y_speech_done();
}
#endif /* ENABLE_TEXT_EXTRACT */

error_t cpymo_engine_update(cpymo_engine *engine, float delta_time_sec, bool * redraw)
{
	error_t err = CPYMO_ERR_SUCC;
	*redraw |= engine->redraw; engine->redraw = false;

	engine->prev_input = engine->input;
	engine->input = cpymo_input_snapshot();

#ifdef ENABLE_TEXT_EXTRACT
	/* 朗读队列的时基与 R1：只在"有新按下"时记一次输入 */
	cpymo_a11y_speech.now += (double)delta_time_sec;
	if ((!engine->prev_input.ok && engine->input.ok) ||
		(!engine->prev_input.cancel && engine->input.cancel) ||
		(!engine->prev_input.down && engine->input.down) ||
		(!engine->prev_input.up && engine->input.up) ||
		(!engine->prev_input.left && engine->input.left) ||
		(!engine->prev_input.right && engine->input.right) ||
		(!engine->prev_input.mouse_button && engine->input.mouse_button) ||
		(!engine->prev_input.hide_window && engine->input.hide_window))
		cpymo_a11y_speech_note_input();
	/* R7：切后台/暂停不念告别语，直接清队停机 */
	if (!engine->prev_input.hide_window && engine->input.hide_window)
		cpymo_a11y_speech_clear();
	cpymo_a11y_speech_tick();
#endif

	if (!engine->prev_input.mouse_button && !engine->input.mouse_button)
		engine->ignore_next_mouse_button_flag = false;

	if (engine->skipping) {
		if (engine->input.ok ||
			engine->input.cancel ||
			engine->input.down ||
			engine->input.hide_window ||
			engine->input.left ||
			engine->input.mouse_button ||
			fabs(engine->input.mouse_wheel_delta) > 0 ||
			engine->input.ok ||
			engine->input.right ||
			engine->input.skip ||
			engine->input.up)
			engine->skipping = false;
	}

	if (engine->input.hide_window != engine->prev_input.hide_window)
		cpymo_engine_request_redraw(engine);

#ifdef ENABLE_TEXT_EXTRACT
	if (!engine->input.copy && !engine->input.append_copy)
		engine->text_extract_copy_latched = false;
	else if (!engine->text_extract_copy_latched) {
		engine->text_extract_copy_latched = true;
		if (engine->input.copy)
			cpymo_backend_text_copy_last();
		else
			cpymo_backend_text_append_copy_last();
	}
#endif

	if (cpymo_ui_enabled(engine))
		err = cpymo_ui_update(engine, delta_time_sec);
	else {

		cpymo_anime_update(
			engine, 
			&engine->anime, 
			delta_time_sec);

		err = cpymo_select_img_update(
			engine, 
			&engine->select_img, 
			delta_time_sec);
		CPYMO_THROW(err);

		err = cpymo_wait_update(
			&engine->wait, 
			engine, 
			delta_time_sec);
		CPYMO_THROW(err);

		if (!cpymo_wait_is_wating(&engine->wait)) {
			if (engine->interpreter)
				err = cpymo_interpreter_execute_step(
					engine->interpreter, engine);
			else return CPYMO_ERR_NO_MORE_CONTENT;

			if (cpymo_wait_is_wating(&engine->wait)) {
				if (err == CPYMO_ERR_NO_MORE_CONTENT) 
					err = CPYMO_ERR_SUCC;
			}

			CPYMO_THROW(err);
		}
	}

	*redraw |= engine->redraw; engine->redraw = false;

	return err;
}

void cpymo_engine_draw(const cpymo_engine *engine)
{
	if (cpymo_ui_enabled(engine)) {
		cpymo_ui_draw(engine);
		return;
	}

	cpymo_bg_draw(engine);
	cpymo_scroll_draw(&engine->scroll);
	cpymo_charas_draw(engine);
	cpymo_anime_draw(&engine->anime);
	cpymo_select_img_draw(
		&engine->select_img, 
		engine->gameconfig.imagesize_w, 
		engine->gameconfig.imagesize_h,
		engine->gameconfig.grayselected);

	cpymo_floating_hint_draw(&engine->floating_hint);
	cpymo_flash_draw(engine);
	cpymo_bg_draw_transform_effect(engine);
	cpymo_fade_draw(engine);

	cpymo_text_draw(engine);
	cpymo_say_draw(engine);
}

void cpymo_engine_trim_memory(cpymo_engine *e)
{
	extern void cpymo_bg_transfer_operate(cpymo_engine *e);
	cpymo_bg_transfer_operate(e);

	extern void cpymo_charas_gc(cpymo_charas *p, bool trim_memory);
	cpymo_charas_gc(&e->charas, true);

	cpymo_anime_off(&e->anime);

	cpymo_audio_se_stop(e);
	cpymo_audio_vo_stop(e);

	#ifdef ENABLE_TEXT_EXTRACT
	e->text_extract_buffer_size = 0;
	e->text_extract_buffer_maxsize = 0;
	free(e->text_extract_buffer);
	e->text_extract_buffer = NULL;
	#endif
}

#ifdef ENABLE_TEXT_EXTRACT
void cpymo_engine_extract_text(cpymo_engine *e, cpymo_str str)
{
	size_t new_size = e->text_extract_buffer_size + str.len + 1;
	if (e->text_extract_buffer_maxsize < new_size) {
		new_size *= 2;
		char *new_buf = (char *)realloc(e->text_extract_buffer, new_size);
		if (new_buf == NULL) return;
		e->text_extract_buffer = new_buf;
		e->text_extract_buffer_maxsize = new_size;
	}

	memcpy(
		e->text_extract_buffer + e->text_extract_buffer_size, 
		str.begin, str.len);
	e->text_extract_buffer_size += str.len;
}

void cpymo_engine_extract_text_submit(cpymo_engine *e)
{
	e->text_extract_buffer[e->text_extract_buffer_size] = '\0';
	cpymo_a11y_speak(e->text_extract_buffer);
	e->text_extract_buffer_size = 0;
}

#endif
