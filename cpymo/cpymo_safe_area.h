#ifndef INCLUDE_CPYMO_SAFE_AREA
#define INCLUDE_CPYMO_SAFE_AREA

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

#endif
