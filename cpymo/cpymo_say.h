#ifndef INCLUDE_CPYMO_SAY
#define INCLUDE_CPYMO_SAY

#include "cpymo_textbox.h"
#include "cpymo_assetloader.h"
#include "cpymo_key_hold.h"

struct cpymo_engine;

typedef struct {
	cpymo_backend_image msgbox, namebox, msg_cursor;
	int msgbox_w, msgbox_h, namebox_w, namebox_h, msg_cursor_w, msg_cursor_h;

	cpymo_textbox textbox;
	bool textbox_usable;

	bool active;
	bool lazy_init;

	bool hide_window;

	cpymo_backend_text name;
	float name_width;

	cpymo_key_hold key_mouse_button;

	char *msgbox_name, *namebox_name;

	char *current_name, *current_text;

	bool current_say_is_already_read;

	bool auto_mode;
	float auto_mode_timer;

	/* 对话框附加层（电影画幅、小头像、无框引言）：默认全关，
	 * 只有脚本显式下命令才启用，旧脚本的表现不受任何影响。 */
	bool no_box;
	bool film_mode;
	cpymo_backend_image avatar;
	int avatar_w, avatar_h;
	char *avatar_name;
} cpymo_say;

void cpymo_say_init(cpymo_say *);
void cpymo_say_free(cpymo_say *);

void cpymo_say_draw(const struct cpymo_engine *);

error_t cpymo_say_load_msgbox_and_namebox_image(
	cpymo_say *, cpymo_str msgbox, cpymo_str namebox, cpymo_assetloader *);

error_t cpymo_say_start(
	struct cpymo_engine *,
	cpymo_str name, 
	cpymo_str text);

/* 同 cpymo_say_start，但不显示对话框（intro）。 */
error_t cpymo_say_start_intro(struct cpymo_engine *, cpymo_str text);

void cpymo_say_film_mode(struct cpymo_engine *, bool on);

/* name 为空表示关闭小头像。 */
error_t cpymo_say_set_avatar(struct cpymo_engine *, cpymo_str name);

/* 画在所有内容之上的附加层（目前只有电影画幅）。 */
void cpymo_say_dialog_ext_draw(const struct cpymo_engine *);

void cpymo_say_hidewindow_until_click(struct cpymo_engine *e);

void cpymo_say_stop_auto_mode(struct cpymo_engine *e);

#endif
