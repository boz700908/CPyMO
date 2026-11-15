#ifndef INCLUDE_CPYMO_TWEEN
#define INCLUDE_CPYMO_TWEEN

#include <stdbool.h>
#include "cpymo_utils.h"

typedef struct {
	float all_time, current_time;
	float begin_value, end_value;
} cpymo_tween;

static inline void cpymo_tween_assign(cpymo_tween *tween, float value)
{
	tween->all_time = 1.0f;
	tween->current_time = 1.0f;
	tween->begin_value = value;
	tween->end_value = value;
}

static inline cpymo_tween cpymo_tween_create_value(float all_time, float begin_value, float end_value)
{
#ifdef LOW_FRAME_RATE
	all_time = 0;
#endif

	cpymo_tween t;
	if (all_time <= 0.0f) {
		cpymo_tween_assign(&t, end_value);
		return t;
	}

	t.all_time = all_time;
	t.current_time = 0;
	t.begin_value = begin_value;
	t.end_value = end_value;

	return t;
}

static inline cpymo_tween cpymo_tween_create(float all_time)
{ return cpymo_tween_create_value(all_time, 0, 1); }

static inline void cpymo_tween_update(cpymo_tween *tween, float time)
{ tween->current_time += time; }

static inline float cpymo_tween_progress(const cpymo_tween *tween)
{ return cpymo_utils_clampf(tween->current_time / tween->all_time, 0.0f, 1.0f); }

static inline float cpymo_tween_value(const cpymo_tween *tween)
{ return cpymo_utils_lerp(tween->begin_value, tween->end_value, cpymo_tween_progress(tween)); }

static inline void cpymo_tween_to(cpymo_tween *tween, float target, float time)
{ 

	*tween = cpymo_tween_create_value(time, cpymo_tween_value(tween), target); 
}

static inline bool cpymo_tween_finished(const cpymo_tween *tween)
{ return tween->current_time >= tween->all_time; }

static inline bool cpymo_tween_finished_after(const cpymo_tween *tween, float after_seconds)
{ return tween->current_time + after_seconds >= tween->all_time; }

static inline float cpymo_tween_progress_after(const cpymo_tween *tween, float after_seconds)
{ return cpymo_utils_clampf((tween->current_time + after_seconds) / tween->all_time, 0.0f, 1.0f); }

static inline float cpymo_tween_value_after(const cpymo_tween *tween, float after_seconds)
{ return cpymo_utils_lerp(tween->begin_value, tween->end_value, cpymo_tween_progress_after(tween, after_seconds)); }

static inline void cpymo_tween_finish(cpymo_tween *tween)
{ tween->current_time = tween->all_time; }

/* 缓动曲线。纯函数，不参与 cpymo_tween 的状态，现有补间行为一处未改。*/
typedef enum {
	cpymo_tween_ease_linear = 0,
	cpymo_tween_ease_in,
	cpymo_tween_ease_out,
	cpymo_tween_ease_in_out
} cpymo_tween_ease;

static inline float cpymo_tween_ease_apply(cpymo_tween_ease ease, float progress)
{
	switch (ease) {
	case cpymo_tween_ease_in:
		return progress * progress;
	case cpymo_tween_ease_out:
		return -progress * (progress - 2.0f);
	case cpymo_tween_ease_in_out:
		if (progress < 0.5f) return 2.0f * progress * progress;
		return -2.0f * progress * progress + 4.0f * progress - 1.0f;
	default:
		return progress;
	}
}

static inline float cpymo_tween_value_eased(const cpymo_tween *tween, cpymo_tween_ease ease)
{
	return cpymo_utils_lerp(
		tween->begin_value, tween->end_value,
		cpymo_tween_ease_apply(ease, cpymo_tween_progress(tween)));
}

#endif
