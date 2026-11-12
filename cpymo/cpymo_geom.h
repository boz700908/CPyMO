#ifndef INCLUDE_CPYMO_GEOM
#define INCLUDE_CPYMO_GEOM

#include <stdbool.h>

/* 二维点与轴对齐矩形，坐标单位是画布逻辑像素。 */

typedef struct cpymo_vec2 {
	float x, y;
} cpymo_vec2;

typedef struct cpymo_rect {
	float x, y, w, h;
} cpymo_rect;

static inline cpymo_vec2 cpymo_vec2_make(float x, float y)
{
	cpymo_vec2 v;
	v.x = x;
	v.y = y;
	return v;
}

static inline cpymo_vec2 cpymo_vec2_add(cpymo_vec2 a, cpymo_vec2 b)
{
	return cpymo_vec2_make(a.x + b.x, a.y + b.y);
}

static inline cpymo_vec2 cpymo_vec2_sub(cpymo_vec2 a, cpymo_vec2 b)
{
	return cpymo_vec2_make(a.x - b.x, a.y - b.y);
}

static inline cpymo_vec2 cpymo_vec2_scale(cpymo_vec2 v, float s)
{
	return cpymo_vec2_make(v.x * s, v.y * s);
}

static inline cpymo_rect cpymo_rect_make(float x, float y, float w, float h)
{
	cpymo_rect r;
	r.x = x;
	r.y = y;
	r.w = w;
	r.h = h;
	return r;
}

static inline float cpymo_rect_right(const cpymo_rect *r)
{
	return r->x + r->w;
}

static inline float cpymo_rect_bottom(const cpymo_rect *r)
{
	return r->y + r->h;
}

static inline cpymo_vec2 cpymo_rect_center(const cpymo_rect *r)
{
	return cpymo_vec2_make(r->x + r->w * 0.5f, r->y + r->h * 0.5f);
}

static inline bool cpymo_rect_contains(const cpymo_rect *r, cpymo_vec2 p)
{
	return p.x >= r->x && p.y >= r->y
		&& p.x < cpymo_rect_right(r) && p.y < cpymo_rect_bottom(r);
}

/* 抠掉四边内边距；宽或高被挤成非正数时退回原矩形。 */
static inline cpymo_rect cpymo_rect_inset(
	const cpymo_rect *r, float left, float top, float right, float bottom)
{
	float w = r->w - left - right;
	float h = r->h - top - bottom;
	if (w <= 0.0f || h <= 0.0f) return *r;
	return cpymo_rect_make(r->x + left, r->y + top, w, h);
}

#endif
