#ifndef INCLUDE_CPYMO_UI_PATCH
#define INCLUDE_CPYMO_UI_PATCH

#include "cpymo_prelude.h"
#include "cpymo_error.h"
#include <stdbool.h>
#include <stddef.h>

/* 自带页面条目补丁层。
 *
 * 每个条目由「页面键 + 序号」定位：页面键是 cpymo_ui_patch_page_* 常量，
 * 序号是该页条目在其内部表中的下标。补丁只有三种作用：
 *   1. 改写该条目显示与朗读的文本；
 *   2. 隐藏该条目（不绘制、键盘也不停留）；
 *   3. 在该页末尾追加纯文本条目（点击行为由调用方决定，本层只存文本）。
 *
 * 本层不依赖 Lua，也不改变任何默认行为：没有补丁时所有查询函数都返回原值。
 * 生命周期由 cpymo_lua_context 持有，引擎退出时统一释放。 */

#define CPYMO_UI_PATCH_PAGE_CONFIG "config"
#define CPYMO_UI_PATCH_PAGE_SAVE "save"
#define CPYMO_UI_PATCH_PAGE_LOAD "load"
#define CPYMO_UI_PATCH_PAGE_MUSIC "music"
#define CPYMO_UI_PATCH_PAGE_BACKLOG "backlog"
#define CPYMO_UI_PATCH_PAGE_ALBUM "album"
#define CPYMO_UI_PATCH_PAGE_SELECT "select"
#define CPYMO_UI_PATCH_PAGE_MSGBOX "msgbox"
#define CPYMO_UI_PATCH_PAGE_RMENU "rmenu"

typedef struct cpymo_ui_patch_entry {
    struct cpymo_ui_patch_entry *next;
    char *page;
    int index;
    char *text;
    bool hidden;
    int extra;
} cpymo_ui_patch_entry;

/* 记录该页上游拥有的条目数，用于区分“真实条目”与“追加项”。 */
typedef struct cpymo_ui_patch_base {
    struct cpymo_ui_patch_base *next;
    char *page;
    size_t count;
} cpymo_ui_patch_base;

typedef struct cpymo_ui_patch {
    cpymo_ui_patch_entry *entries;
    cpymo_ui_patch_base *bases;
    int extra_count;
} cpymo_ui_patch;

void cpymo_ui_patch_init(cpymo_ui_patch *patch);

void cpymo_ui_patch_free(cpymo_ui_patch *patch);

/* 改写条目文本；text 为空串时清除该条的改写，回到原文。 */
error_t cpymo_ui_patch_set(cpymo_ui_patch *patch, const char *page, int index, const char *text);

/* 隐藏条目；对同一序号 set 一次即可恢复显示。 */
error_t cpymo_ui_patch_hide(cpymo_ui_patch *patch, const char *page, int index);

/* 清除该页全部改写与隐藏（追加项一并清除）。page 为 NULL 时清除所有页面。 */
void cpymo_ui_patch_clear(cpymo_ui_patch *patch, const char *page);

/* 该条应显示的文本；没有改写时返回 original。原文与改写都是稳定指针。 */
const char *cpymo_ui_patch_text(const cpymo_ui_patch *patch, const char *page, int index, const char *original);

bool cpymo_ui_patch_hidden(const cpymo_ui_patch *patch, const char *page, int index);

/* 调用方先按自己的规则算出下一个候选序号 index，再用本函数跳过隐藏项：
 * index 本身可见就原样返回；否则沿 dir(+1/-1) 在 0..count-1 内回绕寻找，
 * 全部隐藏时返回 -1（调用方据此决定停下）。没有补丁时恒等于 index，故接入点行为不变。 */
int cpymo_ui_patch_visible_from(
    const cpymo_ui_patch *patch, const char *page, int index, int dir, int count);

/* 在该页末尾追加文本条目。 */
error_t cpymo_ui_patch_add_extra(cpymo_ui_patch *patch, const char *page, const char *text);

void cpymo_ui_patch_set_base(cpymo_ui_patch *patch, const char *page, size_t count);

/* 该页上游条目数；从未报告过时返回 (size_t)-1。 */
size_t cpymo_ui_patch_get_base(const cpymo_ui_patch *patch, const char *page);

/* 查询真实条目 index 上的补丁条目；无补丁返回 NULL。 */
const cpymo_ui_patch_entry *cpymo_ui_patch_entry_at(
    const cpymo_ui_patch *patch, const char *page, int index);

/* 该页被改动的真实条目数（不含追加项）。 */
size_t cpymo_ui_patch_patched_count(const cpymo_ui_patch *patch, const char *page);

size_t cpymo_ui_patch_extra_count(const cpymo_ui_patch *patch, const char *page);

const char *cpymo_ui_patch_extra_text(const cpymo_ui_patch *patch, const char *page, size_t n);
/* 该页第 n 个追加项（按页内顺序，从 0 起）；没有返回 NULL。 */
const cpymo_ui_patch_entry *cpymo_ui_patch_extra_entry(
    const cpymo_ui_patch *patch, const char *page, size_t n);


/* 页面节点总数 = 自带条目数 + 该页追加项数。 */
static inline size_t cpymo_ui_patch_total(
    const cpymo_ui_patch *patch, const char *page, size_t base)
{
    return base + cpymo_ui_patch_extra_count(patch, page);
}

/* index 落在追加项区间时返回其序号（从 0 起），否则返回 -1。 */
static inline int cpymo_ui_patch_extra_index(
    const cpymo_ui_patch *patch, const char *page, size_t base, size_t index)
{
    if (index < base) return -1;
    size_t n = cpymo_ui_patch_extra_count(patch, page);
    if (index - base >= n) return -1;
    return (int)(index - base);
}

#endif
