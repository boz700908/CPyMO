#ifndef INCLUDE_CPYMO_BACKEND_INPUT
#define INCLUDE_CPYMO_BACKEND_INPUT

#include <stdbool.h>

typedef struct {
	float mouse_x;
	float mouse_y;

	bool mouse_position_useable : 1;
	bool mouse_button : 1;

	float mouse_wheel_delta;

	bool up : 1;
	bool down : 1;
	bool left : 1;
	bool right : 1;
	bool ok : 1;
	bool cancel : 1;
	bool skip : 1;
	bool hide_window : 1;
	bool copy : 1;
	bool append_copy : 1;
} cpymo_input;

#ifdef ENABLE_TEXT_EXTRACT
/* Native gesture adapters enqueue semantic actions; the shared SDL2 input
 * backend consumes them once per frame into cpymo_input. */
typedef enum {
	CPYMO_ACCESSIBILITY_ACTION_NONE = 0,
	CPYMO_ACCESSIBILITY_ACTION_UP,
	CPYMO_ACCESSIBILITY_ACTION_DOWN,
	CPYMO_ACCESSIBILITY_ACTION_LEFT,
	CPYMO_ACCESSIBILITY_ACTION_RIGHT,
	CPYMO_ACCESSIBILITY_ACTION_OK,
	CPYMO_ACCESSIBILITY_ACTION_CANCEL,
	CPYMO_ACCESSIBILITY_ACTION_SKIP,
	CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_START,
	CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_END,
	CPYMO_ACCESSIBILITY_ACTION_COPY,
	CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY
} cpymo_accessibility_action;

void cpymo_accessibility_enqueue_action(cpymo_accessibility_action action);
#endif

/* Mouse Coord
 *
 * LeftTop - (0, 0)
 * RightBottom - (gameconfig.imagesize.w - 1, gameconfig.imagesize.h - 1)
 *
 * returns true if mouse position is useable.
 * returns false if mouse position is not useable.
 */
cpymo_input cpymo_input_snapshot();

/* 文本输入（后端可选能力）
 *
 * `input` 命令用：进入输入模式后，后端把本帧收到的字符按 UTF-8 交给引擎，
 * 其中控制字符固定为 '\b'(8) 退格、'\n'(10) 确认、27 取消。
 * poll 返回的内容只在本帧有效，引擎取走后由后端清空。
 *
 * 后端可以完全不实现：下面用弱符号声明，链接后地址为 NULL 时引擎侧
 * 打一条警告并跳过该命令。不支持弱符号的编译器（MSVC）下是普通声明。
 */
#if defined(__APPLE__) && !defined(CPYMO_BACKEND_INPUT_TEXT_IMPL)
/* Mach-O 的 ld 不接受「弱声明但从未定义」的符号（ELF 上会解析为 NULL）。
 * weak_import 声明的缺失符号在 Darwin 上同样解析为 NULL，语义一致。
 * 实现方在 include 前定义 CPYMO_BACKEND_INPUT_TEXT_IMPL 关闭该属性，
 * 因为 weak_import 不允许出现在定义上。 */
#define CPYMO_BACKEND_INPUT_TEXT_WEAK __attribute__((weak_import))
#elif defined(__GNUC__) || defined(__clang__)
#define CPYMO_BACKEND_INPUT_TEXT_WEAK __attribute__((weak))
#else
#define CPYMO_BACKEND_INPUT_TEXT_WEAK
#endif

CPYMO_BACKEND_INPUT_TEXT_WEAK void cpymo_backend_input_text_begin(void);
CPYMO_BACKEND_INPUT_TEXT_WEAK void cpymo_backend_input_text_end(void);
CPYMO_BACKEND_INPUT_TEXT_WEAK const char *cpymo_backend_input_text_poll(void);

#endif
