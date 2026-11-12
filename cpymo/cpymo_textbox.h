#ifndef INCLUDE_CPYMO_TEXTBOX
#define INCLUDE_CPYMO_TEXTBOX

#include "../cpymo-backends/include/cpymo_backend_text.h"
#include "cpymo_backlog.h"

struct cpymo_engine;

struct cpymo_textbox_line;

typedef struct {
	size_t chars_pool_max_size, chars_pool_size, max_lines;
	cpymo_backend_text *chars_pool;
	float *chars_x_pool;
	struct cpymo_textbox_line *lines;
	size_t active_line;
	float x, y, w, h, char_size, alpha, typing_x;
	/* 词级折行（issue #50）：上一个字符是否空白、当前词从当前字符起的剩余宽度 */
	bool prev_is_space;
	float word_remain_w;
	cpymo_color col;
	cpymo_str remain_text;

	/* issue #57：文本副本。textbox 只保存切片指针，调用方传入栈或短命缓冲
	 * 时会在逐字显示过程中悬垂，故由 textbox 自持一份拷贝。 */
	char *owned_text;

	char *backlog_buf;
	size_t backlog_buf_size, backlog_buf_max_size;

	float timer;
	bool draw_cursor;

	cpymo_backlog *backlog;
} cpymo_textbox;

error_t cpymo_textbox_init(
	cpymo_textbox *out,
	float x, float y,
	float width, float height,
	float character_size,
	cpymo_color col,
	float alpha,
	cpymo_str text,
	cpymo_backlog *backlog);

void cpymo_textbox_free(
	cpymo_textbox *, cpymo_backlog *write_to_backlog);

void cpymo_textbox_draw(
	const struct cpymo_engine *,
	const cpymo_textbox *, 
	enum cpymo_backend_image_draw_type drawtype);

error_t cpymo_textbox_clear_page(cpymo_textbox *, cpymo_backlog *write_to_backlog);

static inline bool cpymo_textbox_all_finished(cpymo_textbox *tb)
{ return tb->remain_text.len == 0; }

void cpymo_textbox_finalize(cpymo_textbox *tb);

bool cpymo_textbox_wait_text_fadein(struct cpymo_engine *, float, cpymo_textbox *which_textbox);
bool cpymo_textbox_wait_text_reading(struct cpymo_engine *, float, cpymo_textbox *which_textbox);

#endif
