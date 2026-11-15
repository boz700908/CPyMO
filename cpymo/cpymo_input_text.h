#ifndef INCLUDE_CPYMO_INPUT_TEXT
#define INCLUDE_CPYMO_INPUT_TEXT

#include <stdbool.h>
#include <stddef.h>
#include "cpymo_error.h"
#include "cpymo_str.h"
#include "../cpymo-backends/include/cpymo_backend_text.h"

struct cpymo_engine;

/* 数字输入框（脚本命令 `input <变量> [提示]`）。
 *
 * CPyMO 的变量是 int32，所以这里只接受数字：0-9 与一个前导负号，
 * 退格删除，回车确认（写进变量，空输入按 0），ESC 取消（变量保持原值）。
 *
 * 后端可选能力：cpymo_backend_input_text_* 在 cpymo_backend_input.h 里是
 * 弱符号，没有实现的后端（3ds/psp/uwp/...）调用时地址为 NULL。此时命令
 * 打一条警告、不写变量、立刻结束，脚本继续往下走，不会卡住。
 */

#define CPYMO_INPUT_TEXT_MAX_LEN 16
#define CPYMO_INPUT_TEXT_VAR_MAX 32
#define CPYMO_INPUT_TEXT_PROMPT_MAX 128

typedef struct {
	bool active;
	bool dirty;

	char var_name[CPYMO_INPUT_TEXT_VAR_MAX];
	char prompt[CPYMO_INPUT_TEXT_PROMPT_MAX];
	char text[CPYMO_INPUT_TEXT_MAX_LEN + 1];
	size_t text_len;

	cpymo_backend_text prompt_text;
	cpymo_backend_text value_text;
} cpymo_input_text;

void cpymo_input_text_init(cpymo_input_text *it);
void cpymo_input_text_free(cpymo_input_text *it);

/* 进入输入模式。提示文本会念一次（无障碍）。 */
error_t cpymo_input_text_start(
	struct cpymo_engine *e, cpymo_str var_name, cpymo_str prompt);

/* cpymo_wait_for 回调：处理本帧输入，完成时写变量并返回 true。 */
bool cpymo_input_text_wait_for(struct cpymo_engine *e, float delta_time);

void cpymo_input_text_draw(
	const cpymo_input_text *it, const struct cpymo_engine *e);

#endif
