#ifndef INCLUDE_CPYMO_UI_EXTRA
#define INCLUDE_CPYMO_UI_EXTRA

#include "cpymo_prelude.h"

/* 列表类页面追加项的公共绘制。y 传入行顶坐标，函数内部按字符高度下移基线；
 * 文本每帧临时创建后端文本，条目数量很少。text 为空或 NULL 时不绘制。 */
void cpymo_ui_extra_draw_text(const char *text, float x, float y, float char_size);

#endif
