#include <stdlib.h>
#include <stdio.h>
#include "../../cpymo/cpymo_safe_area.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

/* 安全区（issue #57）：刘海、圆角、手势条、虚拟键盘占掉的边距，单位是逻辑像素。
 *
 * 桌面 / PSP / PSV / Switch / Emscripten 这类整屏整数缩放的平台，引擎没有可靠的
 * 系统 API 可问，也不该猜，因此由外部环境变量提供（也便于实机验证）：
 *
 *     PYMO_SAFE_AREA=left,top,right,bottom
 *
 * 例如 PYMO_SAFE_AREA=0,24,0,32 表示上下各留出 24 / 32 个逻辑像素。
 * iOS 有自己的真实数据源（UIKit safeAreaInsets），直接问它，不用环境变量。 */

#if defined(__APPLE__) && TARGET_OS_IPHONE
/* 由 cpymo_ios.m 提供，按视图尺寸的比例上报（0..1）。 */
extern void cpymo_ios_safe_area_ratios(
	float *left, float *top, float *right, float *bottom);
#endif

static void cpymo_backend_safe_area_provider(
	cpymo_safe_area *out, int logical_w, int logical_h)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE
	float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
	cpymo_ios_safe_area_ratios(&left, &top, &right, &bottom);

	out->left = left * (float)logical_w;
	out->top = top * (float)logical_h;
	out->right = right * (float)logical_w;
	out->bottom = bottom * (float)logical_h;
#else
	(void)logical_w;
	(void)logical_h;

	const char *env = getenv("PYMO_SAFE_AREA");
	if (env == 0) return;

	float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
	if (sscanf(env, "%f,%f,%f,%f", &left, &top, &right, &bottom) <= 0) return;

	out->left = left;
	out->top = top;
	out->right = right;
	out->bottom = bottom;
#endif
}

void cpymo_backend_safe_area_init(void)
{
	cpymo_safe_area_set_provider(&cpymo_backend_safe_area_provider);
}
