#include "cpymo_safe_area.h"

static cpymo_safe_area_provider cpymo_safe_area_current = 0;

void cpymo_safe_area_set_provider(cpymo_safe_area_provider provider)
{
	cpymo_safe_area_current = provider;
}

void cpymo_safe_area_get(
	cpymo_safe_area *out, int logical_w, int logical_h)
{
	out->left = 0.0f;
	out->top = 0.0f;
	out->right = 0.0f;
	out->bottom = 0.0f;

	if (cpymo_safe_area_current)
		cpymo_safe_area_current(out, logical_w, logical_h);

	if (out->left < 0.0f) out->left = 0.0f;
	if (out->top < 0.0f) out->top = 0.0f;
	if (out->right < 0.0f) out->right = 0.0f;
	if (out->bottom < 0.0f) out->bottom = 0.0f;

	/* 留不下一半画布就当后端给错了，忽略它。 */
	if (out->left + out->right >= (float)logical_w) {
		out->left = 0.0f;
		out->right = 0.0f;
	}
	if (out->top + out->bottom >= (float)logical_h) {
		out->top = 0.0f;
		out->bottom = 0.0f;
	}
}

cpymo_rect cpymo_safe_area_usable_rect(
	const cpymo_safe_area *sa, float logical_w, float logical_h, float min_ratio)
{
	float left = sa->left;
	float top = sa->top;
	float w = logical_w - sa->left - sa->right;
	float h = logical_h - sa->top - sa->bottom;

	if (w < logical_w * min_ratio) {
		left = 0.0f;
		w = logical_w;
	}

	if (h < logical_h * min_ratio) {
		top = 0.0f;
		h = logical_h;
	}

	return cpymo_rect_make(left, top, w, h);
}
