#include "cpymo_prelude.h"
#include <stdio.h>
#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_engine.h"
#include "cpymo_select_img.h"
#include "cpymo_accessibility.h"
#include "cpymo_hash_flags.h"
#include "cpymo_vars.h"

#include <lauxlib.h>
#include <lualib.h>

/* ================================================================
 * cpymo.select_img
 *
 * 把引擎内建的图片选择界面（脚本里的 select_img / select_imgs 命令）
 * 交给 Lua 驱动：
 *
 *   cpymo.select_img.begin(filename, count)
 *   cpymo.select_img.select(x, y, enabled [, hash])
 *   cpymo.select_img.finish(init_pos [, callback])
 *   cpymo.select_img.active()            -> bool
 *   cpymo.select_img.current()           -> int (无活动界面时 -1)
 *   cpymo.select_img.cancel()
 *
 * begin/select/finish 的调用顺序与数量必须和脚本命令一致：
 * select 必须恰好调用 count 次，finish 时的 current_selection 才会
 * 等于 all_selections（cpymo_select_img_configuare_end 有对应断言）。
 *
 * 已有活动界面（begin 过但未 finish/cancel）时再调 begin 返回
 * CPYMO_ERR_INVALID_ARG。cancel 会清空界面，正在等待中的选择也会
 * 随之结束（cpymo_select_img_wait 在候选数组为 NULL 时返回 true）。
 *
 * finish 之后引擎主循环会自动 update/draw 这个界面（cpymo_engine.c
 * 无条件调用 cpymo_select_img_update/draw，未配置时内部早退），
 * 因此 Lua 不需要手工绘制。焦点移动/确认的朗读与音效由
 * cpymo_select_img_update 自身完成（cpymo_a11y_speak + SOUND_*），
 * 与脚本路径完全一致。
 *
 * 传入 callback 时，确认选择会先执行默认行为（SOUND_ENTER、按
 * save_enabled 记录 hash、写入 PyMO 变量 FSEL），再调用
 * callback(sel, hash, save_enabled)，因此与脚本路径语义一致，
 * 只是多了一次 Lua 回调。callback 在等待回调中执行，不在 Lua
 * 调用栈内，重入安全。
 * ================================================================ */

static const char cpymo_lua_api_select_img_cb_key = 0;

static void cpymo_lua_api_select_img_clear_cb(lua_State *l)
{
    lua_rawgetp(l, LUA_REGISTRYINDEX, &cpymo_lua_api_select_img_cb_key);
    if (lua_isfunction(l, -1)) {
        lua_pop(l, 1);
        lua_pushnil(l);
        lua_rawsetp(l, LUA_REGISTRYINDEX, &cpymo_lua_api_select_img_cb_key);
    }
    else lua_pop(l, 1);
}

static error_t cpymo_lua_api_select_img_ok_callback(
    cpymo_engine *e, int sel, uint64_t hash, bool save_enabled);

static int cpymo_lua_api_select_img_begin(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 2);

    const char *filename = lua_tostring(l, 1);
    if (filename == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    lua_Integer count = lua_tointeger(l, 2);
    if (count <= 0) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    if (e->select_img.selections != NULL)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    cpymo_select_img_reset(&e->select_img);
    e->select_img.all_selections = 0;
    cpymo_lua_api_select_img_clear_cb(l);

    error_t err = cpymo_select_img_configuare_begin(
        &e->select_img, (size_t)count, cpymo_str_pure(filename),
        &e->assetloader, &e->gameconfig);
    CPYMO_LUA_THROW(l, err);

    return 0;
}

static int cpymo_lua_api_select_img_select(lua_State *l)
{
    const int argc = lua_gettop(l);
    if (argc != 3 && argc != 4)
        CPYMO_LUA_PANIC(l, "This function needs 3 or 4 argument(s).");

    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    if (e->select_img.selections == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    if (e->select_img.current_selection >= (int)e->select_img.all_selections)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    float x = (float)lua_tonumber(l, 1);
    float y = (float)lua_tonumber(l, 2);
    bool enabled = lua_toboolean(l, 3) != 0;

    uint64_t hash = 0;
    if (argc == 4 && !lua_isnil(l, 4))
        hash = (uint64_t)lua_tointeger(l, 4);

    cpymo_select_img_configuare_select_img_selection(e, x, y, enabled, hash);
    return 0;
}

static int cpymo_lua_api_select_img_finish(lua_State *l)
{
    const int argc = lua_gettop(l);
    if (argc != 1 && argc != 2)
        CPYMO_LUA_PANIC(l, "This function needs 1 or 2 argument(s).");

    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    cpymo_select_img *sel = &e->select_img;

    if (sel->selections == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    if (sel->current_selection != (int)sel->all_selections)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    int init_pos = (int)lua_tointeger(l, 1);

    if (argc == 2 && !lua_isnil(l, 2)) {
        if (lua_type(l, 2) != LUA_TFUNCTION)
            CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

        cpymo_lua_api_select_img_clear_cb(l);
        lua_pushvalue(l, 2);
        lua_rawsetp(l, LUA_REGISTRYINDEX, &cpymo_lua_api_select_img_cb_key);
        cpymo_select_img_configuare_set_ok_callback(
            sel, &cpymo_lua_api_select_img_ok_callback);
    }

    cpymo_select_img_configuare_end(sel, &e->wait, e, init_pos);
    return 0;
}

static int cpymo_lua_api_select_img_active(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    lua_pushboolean(l, e->select_img.selections != NULL);
    return 1;
}

static int cpymo_lua_api_select_img_current(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    lua_pushinteger(
        l,
        e->select_img.selections != NULL
            ? (lua_Integer)e->select_img.current_selection
            : (lua_Integer)-1);

    return 1;
}

static int cpymo_lua_api_select_img_cancel(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    cpymo_select_img_reset(&e->select_img);
    e->select_img.all_selections = 0;
    cpymo_lua_api_select_img_clear_cb(l);
    cpymo_engine_request_redraw(e);

    return 0;
}

static error_t cpymo_lua_api_select_img_ok_callback(
    cpymo_engine *e, int sel, uint64_t hash, bool save_enabled)
{
    /* 与 cpymo_select_img_ok_callback_default 一致的基础行为 */
#ifdef ENABLE_TEXT_EXTRACT
    cpymo_accessibility_play_sound(SOUND_ENTER);
#endif

    if (save_enabled)
        cpymo_hash_flags_add(&e->flags, hash);

    error_t err = cpymo_vars_set(&e->vars, cpymo_str_pure("FSEL"), sel);
    if (err != CPYMO_ERR_SUCC) return err;

    lua_State *l = e->lua.lua_state;
    if (l == NULL) return CPYMO_ERR_SUCC;

    lua_rawgetp(l, LUA_REGISTRYINDEX, &cpymo_lua_api_select_img_cb_key);
    if (!lua_isfunction(l, -1)) {
        lua_pop(l, 1);
        return CPYMO_ERR_SUCC;
    }

    lua_pushinteger(l, sel);
    lua_pushinteger(l, (lua_Integer)hash);
    lua_pushboolean(l, save_enabled);

    if (lua_pcall(l, 3, 0, 0) != LUA_OK) {
        const char *msg = lua_tostring(l, -1);
        printf("[Lua] select_img callback error: %s\n", msg ? msg : "(non-string error)");
        lua_pop(l, 1);
        return CPYMO_ERR_UNKNOWN;
    }

    return CPYMO_ERR_SUCC;
}

/* cpymo.select_img.options() -> array of the selections configured so far.
 * index is the order the select() calls were made in (1 based), which is not
 * the value the ok callback receives. */
static int cpymo_lua_api_select_img_options(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);

    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    const struct cpymo_select_img *sel = &e->select_img;

    lua_newtable(l);
    lua_Integer n = 0;
    int count = cpymo_select_img_selection_count(sel);
    for (int i = 0; i < count; i++) {
        struct cpymo_select_img_selection_info info;
        if (!cpymo_select_img_selection_info_at(sel, i, &info)) continue;

        lua_newtable(l);
        lua_pushinteger(l, (lua_Integer)(i + 1));
        lua_setfield(l, -2, "index");
        lua_pushnumber(l, (lua_Number)info.x);
        lua_setfield(l, -2, "x");
        lua_pushnumber(l, (lua_Number)info.y);
        lua_setfield(l, -2, "y");
        lua_pushinteger(l, (lua_Integer)info.w);
        lua_setfield(l, -2, "w");
        lua_pushinteger(l, (lua_Integer)info.h);
        lua_setfield(l, -2, "h");
        lua_pushboolean(l, info.enabled);
        lua_setfield(l, -2, "enabled");
        lua_pushinteger(l, (lua_Integer)info.hash);
        lua_setfield(l, -2, "hash");
        lua_rawseti(l, -2, ++n);
    }

    return 1;
}

void cpymo_lua_api_select_img_register(cpymo_lua_context *ctx)
{
    lua_State *l = ctx->lua_state;

    lua_newtable(l);

    const luaL_Reg funcs[] = {
        { "begin", &cpymo_lua_api_select_img_begin },
        { "select", &cpymo_lua_api_select_img_select },
        { "finish", &cpymo_lua_api_select_img_finish },
        { "active", &cpymo_lua_api_select_img_active },
        { "current", &cpymo_lua_api_select_img_current },
        { "options", &cpymo_lua_api_select_img_options },
        { "cancel", &cpymo_lua_api_select_img_cancel },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs, 0);

    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "select_img");
}

#endif
