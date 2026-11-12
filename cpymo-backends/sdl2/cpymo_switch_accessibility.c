#include "cpymo_switch_accessibility.h"

#if defined(__SWITCH__) && defined(ENABLE_TEXT_EXTRACT_SWITCH_ACCESSIBILITY)

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "switch_tts.h"
#include "../include/cpymo_backend_input.h"

/* ------------------------------------------------------------------ */
/* TTS 资产目录                                                        */
/* ------------------------------------------------------------------ */

static char g_assets_dir[512];
static bool g_tts_attempted;

static void sw_items_load(void);
static void sw_items_apply(void);
static void sw_items_save(void);

static bool sw_file_exists(const char *path)
{
    FILE *f;
    if (!path || !path[0]) return false;
    f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool sw_has_engine(const char *dir)
{
    char path[640];

    if (!dir || !dir[0]) return false;
    snprintf(path, sizeof(path), "%s/rtld.bin", dir);
    if (!sw_file_exists(path)) return false;
    snprintf(path, sizeof(path), "%s/main.bin", dir);
    if (!sw_file_exists(path)) return false;
    snprintf(path, sizeof(path), "%s/sdk.bin", dir);
    return sw_file_exists(path);
}

static bool sw_try_dir(const char *dir)
{
    if (!sw_has_engine(dir)) return false;
    strncpy(g_assets_dir, dir, sizeof(g_assets_dir) - 1);
    g_assets_dir[sizeof(g_assets_dir) - 1] = '\0';
    return true;
}

/* 候选目录：环境变量 > nro 同级的 tts/ > sdmc:/pymogames/tts > sdmc:/switch/tts */
static bool sw_locate_assets(void)
{
    char path[640];
    char *base;

    if (g_assets_dir[0]) return true;

    if (sw_try_dir(getenv("CPYMO_SWITCH_TTS_DIR"))) return true;

    base = SDL_GetBasePath();
    if (base) {
        snprintf(path, sizeof(path), "%stts", base);
        SDL_free(base);
        if (sw_try_dir(path)) return true;
    }

    if (sw_try_dir("sdmc:/pymogames/tts")) return true;
    if (sw_try_dir("sdmc:/switch/tts")) return true;
    if (sw_try_dir("sdmc:/switch/cpymo/tts")) return true;

    return false;
}

bool cpymo_switch_accessibility_tts_start(void)
{
    if (g_tts_attempted) return cpymo_switch_tts_ready();
    g_tts_attempted = true;

    if (!sw_locate_assets()) {
        printf("[a11y] 找不到 TTS 资产目录（rtld.bin / main.bin / sdk.bin）\n");
        printf("[a11y] 已尝试：$CPYMO_SWITCH_TTS_DIR / <nro>/tts / "
               "sdmc:/pymogames/tts / sdmc:/switch/tts\n");
        return false;
    }

    printf("[a11y] TTS 资产目录：%s\n", g_assets_dir);

    sw_items_load();

    if (!cpymo_switch_tts_init(g_assets_dir)) {
        printf("[a11y] TTS 初始化失败：%s\n", cpymo_switch_tts_last_error());
        return false;
    }

    printf("[a11y] TTS 引擎就绪\n");
    sw_items_apply();
    return true;
}

void cpymo_switch_accessibility_tts_stop(void)
{
    cpymo_switch_tts_free();
}

const char *cpymo_switch_accessibility_assets_dir(void)
{
    return g_assets_dir;
}

bool cpymo_switch_accessibility_ready(void)
{
    return cpymo_switch_tts_ready();
}

const char *cpymo_switch_accessibility_last_error(void)
{
    if (cpymo_switch_tts_ready()) return "";
    if (!g_tts_attempted) return "TTS 未初始化";
    if (!g_assets_dir[0]) return "找不到 TTS 资产目录";
    return cpymo_switch_tts_last_error();
}

/* ------------------------------------------------------------------ */
/* TTS 参数：设置页写入，落盘到 sdmc:/pymogames/tts.conf                */
/* ------------------------------------------------------------------ */

#define SW_TTS_CONF_PATH "sdmc:/pymogames/tts.conf"

static const char *const sw_emotion_cats[CPYMO_SW_TTS_EMOTION_MAX + 1] = {
    NULL, "neutral", "happiness", "sadness", "anger", "excitement"
};

/* 与 CPYMO_SW_TTS_ITEM_* 顺序一致；默认值与引擎 params_default 对齐 */
static int g_items[CPYMO_SW_TTS_ITEM_COUNT] = { 135, 150, 90, 10, 2 };

static int sw_item_min(int item)
{
    switch (item) {
        case CPYMO_SW_TTS_ITEM_RATE:
        case CPYMO_SW_TTS_ITEM_PITCH:  return 50;
        case CPYMO_SW_TTS_ITEM_VOLUME: return 0;
        case CPYMO_SW_TTS_ITEM_VOICE:  return 1;
        default:                       return 0;
    }
}

static int sw_item_max(int item)
{
    switch (item) {
        case CPYMO_SW_TTS_ITEM_RATE:
        case CPYMO_SW_TTS_ITEM_PITCH:  return 400;
        case CPYMO_SW_TTS_ITEM_VOLUME: return 100;
        case CPYMO_SW_TTS_ITEM_VOICE:  return 20;
        default:                       return CPYMO_SW_TTS_EMOTION_MAX;
    }
}

static void sw_item_clamp(int item, int *val)
{
    if (*val < sw_item_min(item)) *val = sw_item_min(item);
    if (*val > sw_item_max(item)) *val = sw_item_max(item);
}

static void sw_items_apply(void)
{
    cpymo_switch_tts_params p;

    if (!cpymo_switch_tts_ready()) return;
    cpymo_switch_tts_get_params(&p);
    p.rate = g_items[CPYMO_SW_TTS_ITEM_RATE];
    p.pitch = g_items[CPYMO_SW_TTS_ITEM_PITCH];
    p.volume = g_items[CPYMO_SW_TTS_ITEM_VOLUME];
    p.voice = g_items[CPYMO_SW_TTS_ITEM_VOICE];

    if (g_items[CPYMO_SW_TTS_ITEM_EMOTION] == 0) {
        p.emotion_level = 0;
    } else {
        p.emotion_level = 1;
        snprintf(p.emotion_category, sizeof(p.emotion_category), "%s",
                 sw_emotion_cats[g_items[CPYMO_SW_TTS_ITEM_EMOTION]]);
    }

    cpymo_switch_tts_set_params(&p);
    printf("[a11y] TTS 参数：rate=%d pitch=%d volume=%d voice=%d emotion=%d\n",
           p.rate, p.pitch, p.volume, p.voice,
           g_items[CPYMO_SW_TTS_ITEM_EMOTION]);
}

static void sw_items_load(void)
{
    FILE *f;
    char line[128];

    f = fopen(SW_TTS_CONF_PATH, "r");
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        int key, val;

        if (!eq) continue;
        *eq = '\0';
        val = (int)strtol(eq + 1, NULL, 10);

        if (!strcmp(line, "rate"))          key = CPYMO_SW_TTS_ITEM_RATE;
        else if (!strcmp(line, "pitch"))    key = CPYMO_SW_TTS_ITEM_PITCH;
        else if (!strcmp(line, "volume"))   key = CPYMO_SW_TTS_ITEM_VOLUME;
        else if (!strcmp(line, "voice"))    key = CPYMO_SW_TTS_ITEM_VOICE;
        else if (!strcmp(line, "emotion"))  key = CPYMO_SW_TTS_ITEM_EMOTION;
        else continue;

        sw_item_clamp(key, &val);
        g_items[key] = val;
    }

    fclose(f);
}

static void sw_items_save(void)
{
    FILE *f = fopen(SW_TTS_CONF_PATH, "w");
    if (!f) {
        printf("[a11y] 无法写入 %s\n", SW_TTS_CONF_PATH);
        return;
    }
    fprintf(f, "rate=%d\npitch=%d\nvolume=%d\nvoice=%d\nemotion=%d\n",
            g_items[CPYMO_SW_TTS_ITEM_RATE],
            g_items[CPYMO_SW_TTS_ITEM_PITCH],
            g_items[CPYMO_SW_TTS_ITEM_VOLUME],
            g_items[CPYMO_SW_TTS_ITEM_VOICE],
            g_items[CPYMO_SW_TTS_ITEM_EMOTION]);
    fclose(f);
}

int cpymo_switch_accessibility_tts_item_get(int item)
{
    if (item < 0 || item >= CPYMO_SW_TTS_ITEM_COUNT) return 0;
    return g_items[item];
}

void cpymo_switch_accessibility_tts_item_set(int item, int val)
{
    if (item < 0 || item >= CPYMO_SW_TTS_ITEM_COUNT) return;
    sw_item_clamp(item, &val);
    if (g_items[item] == val) return;
    g_items[item] = val;
    sw_items_apply();
    sw_items_save();
}

/* ------------------------------------------------------------------ */
/* 触摸手势：复刻 iOS / Android / Web 的语义动作表                      */
/*                                                                     */
/*   单指上下左右滑动     方向键                                        */
/*   单指轻点             移动指针但不点击（本平台不提交动作）           */
/*   单指双击             确认                                          */
/*   单指按住 0.5s        取消                                          */
/*   双指双击             快进（SKIP_HOLD_START … END）                 */
/*   双指向左滑动         复制                                          */
/*   双指向右滑动         追加复制                                      */
/*   双指向下滑动         取消                                          */
/* ------------------------------------------------------------------ */

#define SW_TOUCH_MAX        2
#define SW_MOVE_THRESHOLD   0.06f   /* 归一化坐标，约合 32/533 像素 */
#define SW_TAP_MS           300
#define SW_HOLD_MS          500
#define SW_TWO_HOLD_MS      200

typedef struct {
    SDL_FingerID id;
    float x, y;
    bool down;
} sw_finger;

static sw_finger g_fingers[SW_TOUCH_MAX];
static int g_active;
static int g_max_active;
static float g_start_x, g_start_y;
static float g_cur_x, g_cur_y;
static Uint32 g_start_ticks;
static Uint32 g_last_single_tap_ms;
static Uint32 g_last_two_tap_ms;
static bool g_moved;
static bool g_hold_fired;
static bool g_two_double;

static void sw_reset_gesture(void)
{
    memset(g_fingers, 0, sizeof(g_fingers));
    g_active = 0;
    g_max_active = 0;
    g_moved = false;
    g_hold_fired = false;
    g_two_double = false;
}

static sw_finger *sw_find_finger(SDL_FingerID id, bool create)
{
    int i;

    for (i = 0; i < SW_TOUCH_MAX; i++)
        if (g_fingers[i].down && g_fingers[i].id == id) return &g_fingers[i];

    if (!create) return NULL;

    for (i = 0; i < SW_TOUCH_MAX; i++) {
        if (!g_fingers[i].down) {
            g_fingers[i].down = true;
            g_fingers[i].id = id;
            return &g_fingers[i];
        }
    }
    return NULL;
}

static void sw_update_centroid(void)
{
    int i, n = 0;
    float sx = 0.0f, sy = 0.0f;

    for (i = 0; i < SW_TOUCH_MAX; i++) {
        if (!g_fingers[i].down) continue;
        sx += g_fingers[i].x;
        sy += g_fingers[i].y;
        n++;
    }
    if (n == 0) return;
    g_cur_x = sx / (float)n;
    g_cur_y = sy / (float)n;
}

static void sw_enqueue(cpymo_accessibility_action action)
{
    cpymo_accessibility_enqueue_action(action);
}

static void sw_finger_down(const SDL_Event *event)
{
    sw_finger *f;
    Uint32 now = SDL_GetTicks();

    f = sw_find_finger(event->tfinger.fingerId, true);
    if (!f) return;                     /* 超过两指，忽略 */

    f->x = event->tfinger.x;
    f->y = event->tfinger.y;
    g_active++;
    if (g_active > g_max_active) g_max_active = g_active;

    if (g_active == 1) {
        g_start_ticks = now;
        g_moved = false;
        g_hold_fired = false;
        g_two_double = false;
        g_start_x = f->x;
        g_start_y = f->y;
        g_cur_x = f->x;
        g_cur_y = f->y;
    } else if (g_active == 2) {
        g_two_double = (g_last_two_tap_ms != 0) &&
                       (now - g_last_two_tap_ms) < SW_TAP_MS;
        g_start_ticks = now;
        g_moved = false;
        g_hold_fired = false;
        sw_update_centroid();
        g_start_x = g_cur_x;
        g_start_y = g_cur_y;
    }
}

static void sw_finger_motion(const SDL_Event *event)
{
    sw_finger *f = sw_find_finger(event->tfinger.fingerId, false);
    float dx, dy;

    if (!f) return;
    f->x = event->tfinger.x;
    f->y = event->tfinger.y;
    sw_update_centroid();

    dx = g_cur_x - g_start_x;
    dy = g_cur_y - g_start_y;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    if (dx > SW_MOVE_THRESHOLD || dy > SW_MOVE_THRESHOLD) g_moved = true;
}

static void sw_finger_up(const SDL_Event *event)
{
    sw_finger *f = sw_find_finger(event->tfinger.fingerId, false);
    Uint32 now = SDL_GetTicks();
    float dx, dy;

    if (!f) return;

    f->down = false;
    if (g_active > 0) g_active--;
    if (g_active > 0) {
        sw_update_centroid();
        return;                          /* 等最后一根手指抬起再判定 */
    }

    dx = g_cur_x - g_start_x;
    dy = g_cur_y - g_start_y;

    if (g_moved) {
        bool horizontal = (dx < 0.0f ? -dx : dx) > (dy < 0.0f ? -dy : dy);

        if (g_max_active == 1) {
            if (horizontal)
                sw_enqueue(dx > 0.0f ? CPYMO_ACCESSIBILITY_ACTION_RIGHT
                                     : CPYMO_ACCESSIBILITY_ACTION_LEFT);
            else
                sw_enqueue(dy > 0.0f ? CPYMO_ACCESSIBILITY_ACTION_DOWN
                                     : CPYMO_ACCESSIBILITY_ACTION_UP);
        } else if (g_max_active == 2) {
            if (horizontal)
                sw_enqueue(dx > 0.0f ? CPYMO_ACCESSIBILITY_ACTION_APPEND_COPY
                                     : CPYMO_ACCESSIBILITY_ACTION_COPY);
            else if (dy > 0.0f)
                sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_CANCEL);
        }
    } else if (g_max_active == 1) {
        if (g_hold_fired) {
            /* 长按取消已经在 tick 里发过 */
        } else if (g_last_single_tap_ms != 0 &&
                   (now - g_last_single_tap_ms) < SW_TAP_MS) {
            sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_OK);
            g_last_single_tap_ms = 0;
        } else {
            /* 单指轻点 = 移动指针但不点击，本平台不产生语义动作 */
            g_last_single_tap_ms = now;
        }
    } else if (g_max_active == 2) {
        if (g_hold_fired && g_two_double)
            sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_END);
    }

    if (g_max_active == 2) g_last_two_tap_ms = now;
    sw_reset_gesture();
}

void cpymo_switch_accessibility_handle_event(const union SDL_Event *event)
{
    if (!event) return;

    switch (event->type) {
        case SDL_FINGERDOWN:   sw_finger_down(event); break;
        case SDL_FINGERMOTION: sw_finger_motion(event); break;
        case SDL_FINGERUP:     sw_finger_up(event); break;
        default: break;
    }
}

void cpymo_switch_accessibility_tick(void)
{
    Uint32 now;

    if (g_active <= 0 || g_moved || g_hold_fired) return;

    now = SDL_GetTicks();

    if (g_max_active == 2) {
        if (g_two_double) {
            if ((now - g_start_ticks) >= SW_TWO_HOLD_MS) {
                sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_SKIP_HOLD_START);
                g_hold_fired = true;
            }
        } else if ((now - g_start_ticks) >= SW_HOLD_MS) {
            sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_CANCEL);
            g_hold_fired = true;
        }
    } else if ((now - g_start_ticks) >= SW_HOLD_MS) {
        sw_enqueue(CPYMO_ACCESSIBILITY_ACTION_CANCEL);
        g_hold_fired = true;
    }
}

#else

bool cpymo_switch_accessibility_tts_start(void) { return false; }
void cpymo_switch_accessibility_tts_stop(void) {}
void cpymo_switch_accessibility_handle_event(const union SDL_Event *event) { (void)event; }
void cpymo_switch_accessibility_tick(void) {}
const char *cpymo_switch_accessibility_assets_dir(void) { return ""; }
bool cpymo_switch_accessibility_ready(void) { return false; }
const char *cpymo_switch_accessibility_last_error(void) { return ""; }
int cpymo_switch_accessibility_tts_item_get(int item) { (void)item; return 0; }
void cpymo_switch_accessibility_tts_item_set(int item, int val) { (void)item; (void)val; }

#endif
