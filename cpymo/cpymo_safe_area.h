#ifndef INCLUDE_CPYMO_SAFE_AREA
#define INCLUDE_CPYMO_SAFE_AREA

#include "cpymo_geom.h"

/* 屏幕安全区内边距，单位是逻辑像素（画布坐标）。
 *
 * 刘海、圆角、系统手势条会遮住画面边缘，界面元素应当避开这些区域。
 * 由后端注册一个 provider 上报真实数值；未注册（或未上报）时全部为 0，
 * 表示整幅画布都安全 —— 桌面与掌机即属此列。 */
typedef struct {
	float left, top, right, bottom;
} cpymo_safe_area;

typedef void (*cpymo_safe_area_provider)(
	cpymo_safe_area *out, int logical_w, int logical_h);

void cpymo_safe_area_set_provider(cpymo_safe_area_provider provider);

/* 取当前安全区；insets 已被规整为非负，且不会把画布挤没。 */
void cpymo_safe_area_get(
	cpymo_safe_area *out, int logical_w, int logical_h);

/* 扣掉安全区后可放置界面元素的矩形。
 *
 * 某个轴上剩下的空间不足画布的 min_ratio（一般传 0.5）时，
 * 判定为该轴的 insets 不可信，直接在那一轴退回整屏。 */
cpymo_rect cpymo_safe_area_usable_rect(
	const cpymo_safe_area *sa, float logical_w, float logical_h, float min_ratio);

#endif
