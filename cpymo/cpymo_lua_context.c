#include "cpymo_prelude.h"
#include "cpymo_lua_hud.h"
#include "cpymo_ui_patch.h"

#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_lua_context.h"

#ifdef LINK_WITH_ONELUA
#define l_unlikely(X) (X)
#define l_likely(X) (X)
#define MAKE_LIB
#include <onelua.c>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lauxlib.h>
#include <lualib.h>
#include <lgc.h>
#include <assert.h>
#include "../stb/stb_ds.h"
#include "cpymo_engine.h"

static const char *cpymo_lua_actor_children_table_name = "children";


cpymo_lua_context *cpymo_lua_state_get_lua_context(lua_State *l)
{ return *(cpymo_lua_context **)lua_getextraspace(l); }

int cpymo_lua_readonly_table_newindex(lua_State *l)
{ CPYMO_LUA_PANIC(l, "This table is read-only."); }

static int cpymo_lua_api_cpymo_readonly(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    if (!lua_istable(l, -1)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    cpymo_lua_mark_table_readonly(cpymo_lua_state_get_lua_context(l));
    return 1;
}

static int cpymo_lua_api_cpymo_is_skipping(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    lua_pushboolean(
        l, 
        cpymo_engine_skipping(cpymo_lua_state_get_engine(l)));
    return 1;
}

static int cpymo_lua_api_cpymo_exit(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine_exit(cpymo_lua_state_get_engine(l));
    return 0;
}

static int cpymo_lua_api_cpymo_extract_text(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    const char *str = lua_tostring(l, -1);
    if (str == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    cpymo_engine_extract_text_cstr(cpymo_lua_state_get_engine(l), str);
    return 0;
}

static int cpymo_lua_api_cpymo_extract_text_submit(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine_extract_text_submit(cpymo_lua_state_get_engine(l));
    return 0;
}

int cpymo_lua_api_cpymo_deserialize(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    const char *expr = lua_tostring(l, -1);

    lua_getglobal(l, "load");

    luaL_Buffer buf;
    luaL_buffinit(l, &buf);
    luaL_addstring(&buf, "return ");
    luaL_addstring(&buf, expr);
    luaL_pushresult(&buf);

    cpymo_lua_context *ctx = cpymo_lua_state_get_lua_context(l);

    error_t err = cpymo_lua_context_execute(ctx, 1, 1);
    CPYMO_LUA_THROW(l, err);

    err = cpymo_lua_context_execute(ctx, 0, 1);
    CPYMO_LUA_THROW(l, err);

    return 1;
}

error_t cpymo_lua_serialize(cpymo_lua_context *ctx, char **arrbuf)
{
    lua_State *l = ctx->lua_state;
    int t = lua_type(l, -1);
    error_t err;
    const char *str;
    char ch;

    switch (t) {
    case LUA_TNIL:
    case LUA_TSTRING:
    case LUA_TNUMBER:
    case LUA_TBOOLEAN:
        lua_getglobal(l, "string");
        lua_getfield(l, -1, "format");
        lua_pushstring(l, "%q");
        lua_pushvalue(l, -4);

        err = cpymo_lua_context_execute(ctx, 2, 1);
        CPYMO_THROW(err);

        str = lua_tostring(l, -1);
        while (ch = *(str++))
            arrpush(*arrbuf, ch);

        lua_pop(l, 3);

        return CPYMO_ERR_SUCC;

    case LUA_TTABLE:
        arrpush(*arrbuf, '{');

        lua_pushnil(l);
        while (lua_next(l, -2)) {

            lua_pushvalue(l, -2);

            arrpush(*arrbuf, '[');
            cpymo_lua_serialize(ctx, arrbuf);
            arrpush(*arrbuf, ']');

            arrpush(*arrbuf, '=');
            cpymo_lua_serialize(ctx, arrbuf);
            arrpush(*arrbuf, ',');
        }

        arrpush(*arrbuf, '}');
        lua_pop(l, 1);
        return CPYMO_ERR_SUCC;

    default:
        printf("[Error] Failed to serialize lua %s.\n", lua_typename(l, t));
        return CPYMO_ERR_UNSUPPORTED;
    }
}

static int cpymo_lua_api_cpymo_serialize(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    char *chbuf = NULL;
    error_t err = cpymo_lua_serialize(
        cpymo_lua_state_get_lua_context(l), &chbuf);

    if (err != CPYMO_ERR_SUCC) {
        arrfree(chbuf);
        return err;
    }

    arrpush(chbuf, '\0');
    lua_pushstring(l, chbuf);
    arrfree(chbuf);

    CPYMO_LUA_THROW(l, err);

    if (err != CPYMO_ERR_SUCC)
        CPYMO_LUA_PANIC(l, "Failed to serialize lua value.");

    return 1;
}

static int cpymo_lua_api_cpymo_timer(lua_State *l);
static int cpymo_lua_api_cpymo_yield(lua_State *l);
static int cpymo_lua_api_cpymo_wait(lua_State *l);
static int cpymo_lua_api_cpymo_every(lua_State *l);
static int cpymo_lua_api_cpymo_timer_cancel(lua_State *l);

static error_t cpymo_lua_context_create_cpymo_package(
    cpymo_lua_context *ctx, cpymo_engine *e)
{
    lua_State *l = ctx->lua_state; 
    lua_newtable(l);

    lua_pushstring(l, e->assetloader.gamedir);
    lua_setfield(l, -2, "gamedir");

    lua_pushinteger(l, CPYMO_FEATURE_LEVEL);
    lua_setfield(l, -2, "feature_level");

    lua_pushinteger(l, e->gameconfig.imagesize_w);
    lua_setfield(l, -2, "screen_width");

    lua_pushinteger(l, e->gameconfig.imagesize_h);
    lua_setfield(l, -2, "screen_height");

    const luaL_Reg cpymo_funcs[] = {
        { "readonly", &cpymo_lua_api_cpymo_readonly },
        { "is_skipping", &cpymo_lua_api_cpymo_is_skipping },
        { "extract_text", &cpymo_lua_api_cpymo_extract_text },
        { "extract_text_submit", &cpymo_lua_api_cpymo_extract_text_submit },
        { "serialize", &cpymo_lua_api_cpymo_serialize },
        { "deserialize", &cpymo_lua_api_cpymo_deserialize },
        { "exit", &cpymo_lua_api_cpymo_exit },
        { "timer", &cpymo_lua_api_cpymo_timer },
        { "every", &cpymo_lua_api_cpymo_every },
        { "timer_cancel", &cpymo_lua_api_cpymo_timer_cancel },
        { "yield", &cpymo_lua_api_cpymo_yield },
        { "wait", &cpymo_lua_api_cpymo_wait },
        { NULL, NULL }
    };
    luaL_setfuncs(l, cpymo_funcs, 0);

    void cpymo_lua_api_render_register(cpymo_lua_context *);
    cpymo_lua_api_render_register(ctx);

    void cpymo_lua_api_asset_register(cpymo_lua_context *);
    cpymo_lua_api_asset_register(ctx);

    void cpymo_lua_api_ui_register(cpymo_lua_context *);
    cpymo_lua_api_ui_register(ctx);

    void cpymo_lua_api_flags_register(cpymo_lua_context *ctx);
    cpymo_lua_api_flags_register(ctx);

    void cpymo_lua_api_script_register(cpymo_lua_context *ctx);
    cpymo_lua_api_script_register(ctx);

    void cpymo_lua_api_save_register(cpymo_lua_context *ctx);
    cpymo_lua_api_save_register(ctx);

    void cpymo_lua_api_a11y_register(cpymo_lua_context *ctx);
    cpymo_lua_api_a11y_register(ctx);
    void cpymo_lua_api_select_img_register(cpymo_lua_context *ctx);
    cpymo_lua_api_select_img_register(ctx);

    error_t cpymo_lua_api_input_register(cpymo_lua_context *ctx);
    error_t err = cpymo_lua_api_input_register(ctx);
    CPYMO_THROW(err);

    cpymo_lua_mark_table_readonly(ctx);
    lua_setglobal(l, "cpymo");

    assert(lua_gettop(l) == 0);

    return CPYMO_ERR_SUCC;
}

static int cpymo_lua_readonly_table_index(lua_State *l)
{
    const char *key = lua_tostring(l, -1);
    int ref = *(int *)lua_touserdata(l, -2);
    lua_rawgeti(l, LUA_REGISTRYINDEX, ref);
    lua_getfield(l, -1, key);    
    return 1;
}

static int cpymo_lua_readonly_table_len(lua_State *l)
{
    int ref = *(int *)lua_touserdata(l, -1);
    lua_rawgeti(l, LUA_REGISTRYINDEX, ref);
    lua_len(l, -1);
    return 1;
}

static int cpymo_lua_readonly_table_tostring(lua_State *l)
{
    int ref = *(int *)lua_touserdata(l, -1);
    lua_rawgeti(l, LUA_REGISTRYINDEX, ref);
    size_t len;
    luaL_tolstring(l, -1, &len);
    return 1;
}

static int cpymo_lua_readonly_table_gc(lua_State *l)
{
    int ref = *(int *)lua_touserdata(l, -1);
    luaL_unref(l, LUA_REGISTRYINDEX, ref);
    return 0;
}

static int cpymo_lua_readonly_table_pairs(lua_State *l)
{
    int ref = *(int *)lua_touserdata(l, -1);
    lua_pop(l, 1);

    lua_getglobal(l, "pairs");
    lua_rawgeti(l, LUA_REGISTRYINDEX, ref);

    lua_call(l, 1, 3);
    return 3;
}

static int cpymo_lua_context_create_readonly_metatable(lua_State *l)
{
    lua_newtable(l);

    const luaL_Reg funcs[] = {
        { "__newindex", &cpymo_lua_readonly_table_newindex },
        { "__index", &cpymo_lua_readonly_table_index },
        { "__len", &cpymo_lua_readonly_table_len },
        { "__tostring", &cpymo_lua_readonly_table_tostring },
        { "__gc", &cpymo_lua_readonly_table_gc },
        { "__pairs", &cpymo_lua_readonly_table_pairs },
        { NULL, NULL }
    };

    luaL_setfuncs(l, funcs, 0);

    return luaL_ref(l, LUA_REGISTRYINDEX);
}

static int cpymo_lua_pending_reserve(cpymo_lua_context *l, size_t need)
{
    if (need <= l->pending_cap) return 1;

    size_t cap = l->pending_cap ? l->pending_cap : 128;
    while (cap < need) cap += cap / 2 + 64;

    char *grown = (char *)realloc(l->pending_code, cap);
    if (grown == NULL) return 0;

    l->pending_code = grown;
    l->pending_cap = cap;
    return 1;
}

void cpymo_lua_context_discard_pending(cpymo_lua_context *l)
{
    l->pending_len = 0;
    if (l->pending_code) l->pending_code[0] = '\0';
}

int cpymo_lua_context_has_pending(const cpymo_lua_context *l)
{ return l->pending_len > 0; }

error_t cpymo_lua_context_set_instruction_budget(
    cpymo_lua_context *l, int budget)
{
    if (budget < 0) return CPYMO_ERR_INVALID_ARG;
    l->instruction_budget = budget;
    return CPYMO_ERR_SUCC;
}

static void cpymo_lua_report_error(
    const char *script_name, int line, const char *msg)
{
    printf(
        "[lua] %s(%d): %s\n",
        script_name ? script_name : "(lua)",
        line,
        msg ? msg : "(unknown error)");
}

/* True when the compiler stopped on an unfinished construct, which is the
   same test the stock Lua REPL uses to ask for another line. */
static int cpymo_lua_message_is_incomplete(const char *msg)
{
    static const char eofmark[] = "<eof>";
    const size_t marklen = sizeof(eofmark) - 1;

    if (msg == NULL) return 0;

    size_t len = strlen(msg);
    if (len < marklen) return 0;
    return memcmp(msg + len - marklen, eofmark, marklen) == 0;
}

static int cpymo_lua_traceback(lua_State *l)
{
    const char *msg = lua_tostring(l, 1);
    if (msg == NULL) msg = "(error object is not a string)";
    luaL_traceback(l, l, msg, 1);
    return 1;
}

static void cpymo_lua_instruction_hook(lua_State *l, lua_Debug *ar)
{
    (void)ar;
    luaL_error(
        l,
        "lua code exceeded the instruction budget (%d); "
        "possible infinite loop",
        cpymo_lua_state_get_lua_context(l)->instruction_budget);
}

static int cpymo_lua_api_os_exit_disabled(lua_State *l)
{
    (void)l;
    return luaL_error(
        l,
        "os.exit() is disabled in CPyMO scripts; use cpymo.exit() instead.");
}

static int cpymo_lua_api_blocking_disabled(lua_State *l)
{
    const char *name = lua_tostring(l, lua_upvalueindex(1));

    return luaL_error(
        l,
        "%s() is disabled in CPyMO scripts: it blocks the frame. Use "
        "cpymo.wait(seconds) or cpymo.yield() to hand the frame back to the "
        "engine.",
        name ? name : "this call");
}

static int cpymo_lua_api_debug_sethook_disabled(lua_State *l)
{
    (void)l;
    return luaL_error(
        l,
        "debug.sethook() is reserved by CPyMO: the engine installs the hook "
        "that stops runaway scripts.");
}

static void cpymo_lua_context_disable_call(
    lua_State *l, const char *lib, const char *field)
{
    int lib_index;

    lua_getglobal(l, lib);
    if (!lua_istable(l, -1)) {
        lua_pop(l, 1);
        return;
    }

    lib_index = lua_gettop(l);
    lua_getfield(l, lib_index, field);
    if (lua_isfunction(l, -1)) {
        /* the name becomes the closure upvalue and is consumed */
        lua_pushstring(l, field);
        lua_pushcclosure(l, cpymo_lua_api_blocking_disabled, 1);
        lua_setfield(l, lib_index, field);
    }

    lua_settop(l, lib_index - 1);
}

/* io.lines() without a file name reads stdin, which never returns in a game.
   Keep the ordinary form working. */
static int cpymo_lua_api_io_lines(lua_State *l)
{
    if (lua_isnoneornil(l, 1))
        return luaL_error(
            l,
            "io.lines() without a file name reads stdin and would block the "
            "frame; pass a file name.");

    lua_pushvalue(l, lua_upvalueindex(1));
    lua_insert(l, 1);
    lua_call(l, lua_gettop(l) - 1, LUA_MULTRET);
    return lua_gettop(l);
}

static void cpymo_lua_context_wrap_io_lines(lua_State *l)
{
    int io_index;

    lua_getglobal(l, "io");
    if (!lua_istable(l, -1)) {
        lua_pop(l, 1);
        return;
    }

    io_index = lua_gettop(l);
    lua_getfield(l, io_index, "lines");
    if (lua_isfunction(l, -1)) {
        /* the stock lines() becomes the wrapper upvalue */
        lua_pushcclosure(l, cpymo_lua_api_io_lines, 1);
        lua_setfield(l, io_index, "lines");
    }

    lua_settop(l, io_index - 1);
}

static void cpymo_lua_context_harden_stdlib(lua_State *l)
{
    lua_getglobal(l, "os");
    if (lua_istable(l, -1)) {
        lua_pushcfunction(l, cpymo_lua_api_os_exit_disabled);
        lua_setfield(l, -2, "exit");
    }
    lua_pop(l, 1);

    lua_getglobal(l, "debug");
    if (lua_istable(l, -1)) {
        lua_pushcfunction(l, cpymo_lua_api_debug_sethook_disabled);
        lua_setfield(l, -2, "sethook");
    }
    lua_pop(l, 1);

    /* Everything below can stall a frame for ever; each replacement explains
       the frame-friendly alternative. */
    cpymo_lua_context_disable_call(l, "os", "execute");
    cpymo_lua_context_disable_call(l, "io", "read");
    cpymo_lua_context_disable_call(l, "io", "popen");

    cpymo_lua_context_wrap_io_lines(l);
}

/* ---------------------------------------------------------------- timers */
/*
 * A timer is a Lua function plus its arguments, fired once the game clock
 * reaches trigger_at. `interval > 0` makes it repeat. The clock only advances
 * while the engine runs the script branch of cpymo_engine_update, so menus,
 * and file loading pause timers for free. Callbacks run inside the normal
 * frame update, use the same traceback handler and instruction budget as a
 * #lua block, and a failing callback only prints: the game keeps going.
 */

typedef struct cpymo_lua_timer {
    struct cpymo_lua_timer *next;
    int id;
    int func_ref;
    int args_ref;
    double trigger_at;
    double interval;      /* 0 for a one-shot timer */
    bool cancelled;
} cpymo_lua_timer;

static void cpymo_lua_timer_unref(cpymo_lua_context *l, cpymo_lua_timer *t)
{
    if (t->func_ref != LUA_NOREF && t->func_ref != LUA_REFNIL)
        luaL_unref(l->lua_state, LUA_REGISTRYINDEX, t->func_ref);
    if (t->args_ref != LUA_NOREF && t->args_ref != LUA_REFNIL)
        luaL_unref(l->lua_state, LUA_REGISTRYINDEX, t->args_ref);
    t->func_ref = LUA_NOREF;
    t->args_ref = LUA_NOREF;
}

/* cpymo.timer(delay, fn, ...) / cpymo.every(interval, fn, ...) */
static int cpymo_lua_timer_register(lua_State *l, double interval)
{
    cpymo_lua_context *ctx = cpymo_lua_state_get_lua_context(l);

    luaL_checktype(l, 1, LUA_TNUMBER);
    luaL_checktype(l, 2, LUA_TFUNCTION);

    double delay = (double)lua_tonumber(l, 1);
    if (!(delay >= 0.0))
        return luaL_error(l, "cpymo.timer: delay must be a non-negative number.");
    if (delay > 86400.0)
        return luaL_error(l, "cpymo.timer: delay must not exceed one day.");

    lua_pushvalue(l, 2);
    int func_ref = luaL_ref(l, LUA_REGISTRYINDEX);

    int nargs = lua_gettop(l) - 2;
    int args_ref = LUA_NOREF;
    if (nargs > 0) {
        lua_createtable(l, nargs, 0);
        for (int i = 1; i <= nargs; ++i) {
            lua_pushvalue(l, i + 2);
            lua_rawseti(l, -2, i);
        }
        args_ref = luaL_ref(l, LUA_REGISTRYINDEX);
    }

    cpymo_lua_timer *t = (cpymo_lua_timer *)malloc(sizeof(*t));
    if (t == NULL) {
        luaL_unref(l, LUA_REGISTRYINDEX, func_ref);
        if (args_ref != LUA_NOREF) luaL_unref(l, LUA_REGISTRYINDEX, args_ref);
        return luaL_error(l, "cpymo.timer: out of memory.");
    }

    t->id = ctx->timer_next_id++;
    t->func_ref = func_ref;
    t->args_ref = args_ref;
    t->interval = interval;
    t->trigger_at = ctx->timer_clock + delay;
    t->cancelled = false;
    t->next = ctx->timers;
    ctx->timers = t;

    lua_pushinteger(l, t->id);
    return 1;
}

static int cpymo_lua_api_cpymo_timer(lua_State *l)
{ return cpymo_lua_timer_register(l, 0.0); }

static int cpymo_lua_api_cpymo_every(lua_State *l)
{
    luaL_checktype(l, 1, LUA_TNUMBER);
    double interval = (double)lua_tonumber(l, 1);
    if (!(interval > 0.0))
        return luaL_error(l, "cpymo.every: interval must be greater than 0.");
    if (interval > 86400.0)
        return luaL_error(l, "cpymo.every: interval must not exceed one day.");
    return cpymo_lua_timer_register(l, interval);
}

static int cpymo_lua_api_cpymo_timer_cancel(lua_State *l)
{
    cpymo_lua_context *ctx = cpymo_lua_state_get_lua_context(l);
    int id = (int)luaL_checkinteger(l, 1);

    /* A repeating timer that is firing right now has already left the list,
       so the running slot has to be checked as well. */
    if (ctx->timer_running && ctx->timer_running->id == id) {
        ctx->timer_running->cancelled = true;
        lua_pushboolean(l, 1);
        return 1;
    }

    for (cpymo_lua_timer *t = ctx->timers; t; t = t->next) {
        if (t->id == id) {
            t->cancelled = true;
            lua_pushboolean(l, 1);
            return 1;
        }
    }

    lua_pushboolean(l, 0);
    return 1;
}

static void cpymo_lua_timer_invoke(cpymo_lua_context *l, cpymo_lua_timer *t)
{
    lua_State *L = l->lua_state;
    int base = lua_gettop(L);

    lua_pushcfunction(L, cpymo_lua_traceback);
    int msgh = base + 1;

    lua_rawgeti(L, LUA_REGISTRYINDEX, t->func_ref);

    int nargs = 0;
    if (t->args_ref != LUA_NOREF && t->args_ref != LUA_REFNIL) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, t->args_ref);
        int argtab = lua_gettop(L);
        nargs = (int)lua_rawlen(L, argtab);
        for (int i = 1; i <= nargs; ++i)
            lua_rawgeti(L, argtab, i);
        lua_remove(L, argtab);
    }

    if (l->instruction_budget > 0)
        lua_sethook(
            L, cpymo_lua_instruction_hook,
            LUA_MASKCOUNT, l->instruction_budget);

    int status = lua_pcall(L, nargs, 0, msgh);

    lua_sethook(L, NULL, 0, 0);

    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        printf("[lua] timer %d failed: %s\n", t->id, msg ? msg : "(unknown)");
    }

    lua_settop(L, base);
}

/* ------------------------------------------------------------------------ */
/* Suspended #lua blocks                                                    */
/* ------------------------------------------------------------------------ */

static void cpymo_lua_block_abort(cpymo_lua_context *l)
{
    if (l->block_co_ref > 0 && l->lua_state != NULL)
        luaL_unref(l->lua_state, LUA_REGISTRYINDEX, l->block_co_ref);
    l->block_co_ref = 0;
    l->block_resume_at = 0.0;
}

static void cpymo_lua_block_resume(cpymo_lua_context *l)
{
    lua_State *L = l->lua_state;
    if (L == NULL || l->block_co_ref <= 0) return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, l->block_co_ref);
    lua_State *co = lua_tothread(L, -1);
    lua_pop(L, 1);

    if (co == NULL) {
        cpymo_lua_block_abort(l);
        return;
    }

    if (l->instruction_budget > 0)
        lua_sethook(
            co,
            cpymo_lua_instruction_hook,
            LUA_MASKCOUNT,
            l->instruction_budget);

    int nresults = 0;
    int status = lua_resume(co, L, 0, &nresults);

    lua_sethook(co, NULL, 0, 0);

    if (status == LUA_YIELD) {
        /* cpymo.wait(sec) yields one number; cpymo.yield() yields nothing
           and simply continues on the next frame. */
        double delay = 0.0;
        if (nresults >= 1 && lua_isnumber(co, -1))
            delay = (double)lua_tonumber(co, -1);
        if (!(delay > 0.0)) delay = 0.0;

        l->block_resume_at = l->timer_clock + delay;
        lua_settop(co, 0);
        return;
    }

    if (status != LUA_OK) {
        const char *msg = lua_tostring(co, -1);
        luaL_traceback(L, co, msg ? msg : "(error)", 1);
        cpymo_lua_report_error(
            l->block_chunkname + 1, l->block_line, lua_tostring(L, -1));
        lua_pop(L, 1);
    }

    cpymo_lua_block_abort(l);
}

static int cpymo_lua_api_cpymo_yield(lua_State *l)
{
    if (!lua_isyieldable(l))
        return luaL_error(
            l,
            "cpymo.yield() only suspends a #lua block; a timer callback must "
            "return to the engine instead.");

    return lua_yield(l, 0);
}

static int cpymo_lua_api_cpymo_wait(lua_State *l)
{
    lua_Number sec;

    if (!lua_isyieldable(l))
        return luaL_error(
            l,
            "cpymo.wait() only suspends a #lua block; a timer callback must "
            "return to the engine instead.");

    sec = luaL_checknumber(l, 1);
    if (!(sec >= 0.0))
        return luaL_error(l, "cpymo.wait: seconds must not be negative.");

    lua_pushnumber(l, sec);
    return lua_yield(l, 1);
}

error_t cpymo_lua_block_update(cpymo_lua_context *l)
{
    if (l->lua_state == NULL) return CPYMO_ERR_SUCC;
    if (l->block_co_ref <= 0) return CPYMO_ERR_SUCC;
    if (l->timer_clock < l->block_resume_at) return CPYMO_ERR_SUCC;
    cpymo_lua_block_resume(l);
    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_timers_update(cpymo_lua_context *l, float delta_time)
{
    if (l->lua_state == NULL) return CPYMO_ERR_SUCC;

    double dt = (double)delta_time;
    if (dt < 0.0) dt = 0.0;
    l->timer_clock += dt;

    /* Reap the timers cancelled since the last frame. */
    cpymo_lua_timer **pp = &l->timers;
    while (*pp) {
        if ((*pp)->cancelled) {
            cpymo_lua_timer *dead = *pp;
            *pp = dead->next;
            cpymo_lua_timer_unref(l, dead);
            free(dead);
        }
        else pp = &(*pp)->next;
    }

    if (dt <= 0.0) return CPYMO_ERR_SUCC;

    /* Detach everything that is due before running anything, so a callback
       may register or cancel timers without disturbing this walk. */
    cpymo_lua_timer *due = NULL;
    pp = &l->timers;
    while (*pp) {
        cpymo_lua_timer *t = *pp;
        if (t->trigger_at <= l->timer_clock) {
            *pp = t->next;
            t->next = due;
            due = t;
        }
        else pp = &t->next;
    }

    while (due) {
        cpymo_lua_timer *t = due;
        due = t->next;
        t->next = NULL;

        if (!t->cancelled) {
            l->timer_running = t;
            cpymo_lua_timer_invoke(l, t);
            l->timer_running = NULL;
        }

        if (t->cancelled || t->interval <= 0.0) {
            cpymo_lua_timer_unref(l, t);
            free(t);
        }
        else {
            t->trigger_at = l->timer_clock + t->interval;
            t->next = l->timers;
            l->timers = t;
        }
    }

    return CPYMO_ERR_SUCC;
}

void cpymo_lua_timers_free(cpymo_lua_context *l)
{
    cpymo_lua_timer *t = l->timers;
    while (t) {
        cpymo_lua_timer *next = t->next;
        if (l->lua_state) cpymo_lua_timer_unref(l, t);
        free(t);
        t = next;
    }
    l->timers = NULL;
    l->timer_running = NULL;
}


error_t cpymo_lua_context_execute_script_block(
    cpymo_lua_context *l,
    cpymo_str lua_code,
    const char *script_name,
    int line)
{
    if (l->lua_state == NULL) return CPYMO_ERR_SUCC;

    lua_State *L = l->lua_state;

    if (cpymo_lua_context_has_pending(l) == 0) {
        l->pending_line = line;
        snprintf(
            l->pending_chunkname,
            sizeof(l->pending_chunkname),
            "=%s",
            script_name ? script_name : "(lua)");
    }

    if (!cpymo_lua_pending_reserve(l, l->pending_len + lua_code.len + 2))
        return CPYMO_ERR_OUT_OF_MEM;

    if (lua_code.len > 0) {
        memcpy(l->pending_code + l->pending_len, lua_code.begin, lua_code.len);
        l->pending_len += lua_code.len;
    }

    l->pending_code[l->pending_len++] = '\n';
    l->pending_code[l->pending_len] = '\0';

    if (l->pending_len > (1024u * 1024u)) {
        cpymo_lua_report_error(
            script_name,
            l->pending_line,
            "accumulated #lua block exceeds 1 MiB; discarded.");
        cpymo_lua_context_discard_pending(l);
        return CPYMO_ERR_SUCC;
    }

    /* Run the block inside a coroutine of its own, so that cpymo.yield() and
       cpymo.wait() can hand the frame back and continue later. */
    cpymo_lua_block_abort(l);

    lua_State *co = lua_newthread(L);
    l->block_co_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    l->block_line = l->pending_line;
    snprintf(
        l->block_chunkname, sizeof(l->block_chunkname), "%s", l->pending_chunkname);

    int status = luaL_loadbuffer(
        co, l->pending_code, l->pending_len, l->pending_chunkname);

    if (status != LUA_OK) {
        const char *msg = lua_tostring(co, -1);

        if (status == LUA_ERRSYNTAX && cpymo_lua_message_is_incomplete(msg)) {
            cpymo_lua_block_abort(l);
            return CPYMO_ERR_SUCC; /* wait for the rest of the block */
        }

        cpymo_lua_report_error(l->block_chunkname + 1, l->block_line, msg);
        cpymo_lua_block_abort(l);
        cpymo_lua_context_discard_pending(l);
        return CPYMO_ERR_SUCC; /* soft failure: keep playing */
    }

    cpymo_lua_context_discard_pending(l);

    cpymo_lua_block_resume(l);
    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_context_init(cpymo_lua_context *l, cpymo_engine *e)
{
    cpymo_ui_patch_init(&l->ui_patch);
    l->ui_click_ref = LUA_NOREF;
    l->pending_code = NULL;
    l->pending_len = 0;
    l->pending_cap = 0;
    l->pending_chunkname[0] = '\0';
    l->pending_line = 0;
    l->instruction_budget = CPYMO_LUA_DEFAULT_INSTRUCTION_BUDGET;

    l->timers = NULL;
    l->timer_running = NULL;
    l->timer_next_id = 1;
    l->timer_clock = 0.0;

    if (e->feature_level < 1) {
        l->lua_state = NULL;
        return CPYMO_ERR_SUCC;
    }

    l->engine = e;

    l->lua_state = luaL_newstate();
    if (l->lua_state == NULL) return CPYMO_ERR_OUT_OF_MEM;
    
    *(cpymo_lua_context **)lua_getextraspace(l->lua_state) = l;

    luaL_openlibs(l->lua_state);

    cpymo_lua_context_harden_stdlib(l->lua_state);

    l->readonly_metatable = 
        cpymo_lua_context_create_readonly_metatable(l->lua_state);
    l->script_wait_callback_ref = LUA_REFNIL;
    l->script_wait_function_ref = LUA_REFNIL;

    error_t err = cpymo_lua_context_create_cpymo_package(l, e);
    CPYMO_THROW(err);

    lua_newtable(l->lua_state);
    lua_setglobal(l->lua_state, "main");

    return CPYMO_ERR_SUCC;
}

void cpymo_lua_context_free(cpymo_lua_context *l)
{
    if (l->pending_code) {
        free(l->pending_code);
        l->pending_code = NULL;
        l->pending_len = 0;
        l->pending_cap = 0;
    }

    if (l->lua_state) {
        cpymo_lua_hud_free_all(&l->hud_items);
        cpymo_ui_patch_free(&l->ui_patch);
        cpymo_lua_timers_free(l);
        cpymo_lua_get_main_actor(l);
        cpymo_lua_actor_free(l);
        if (l->lua_state && l->ui_click_ref != LUA_NOREF) {
        luaL_unref(l->lua_state, LUA_REGISTRYINDEX, l->ui_click_ref);
        l->ui_click_ref = LUA_NOREF;
    }

    lua_close(l->lua_state);
    } 
}

void cpymo_lua_gc_full(cpymo_lua_context *l)
{ luaC_fullgc(l->lua_state, true); }

cpymo_engine *cpymo_lua_state_get_engine(lua_State *l)
{ return cpymo_lua_state_get_lua_context(l)->engine; }

error_t cpymo_lua_error_conv(int lua_error)
{
    switch (lua_error) {
        case LUA_OK: return CPYMO_ERR_SUCC;
        case LUA_ERRSYNTAX: return CPYMO_ERR_SYNTAX_ERROR;
        case LUA_ERRMEM: return CPYMO_ERR_OUT_OF_MEM;
        default: return CPYMO_ERR_UNKNOWN;
    }
}

error_t cpymo_lua_context_push_lua_code(
    cpymo_lua_context *l, 
    cpymo_str code)
{
    assert(l->lua_state);
    error_t err = cpymo_lua_error_conv(
        luaL_loadbuffer(l->lua_state, code.begin, code.len, "=(lua)"));

    if (err != CPYMO_ERR_SUCC) {
        const char *errstr = lua_tostring(l->lua_state, -1);
        puts(errstr);
        lua_pop(l->lua_state, 1);
        return err;
    }
    
    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_context_execute(
    cpymo_lua_context *l, int nargs, int nresults)
{
    assert(l->lua_state);
    error_t err = cpymo_lua_error_conv(
        lua_pcall(l->lua_state, nargs, nresults, 0));

    if (err != CPYMO_ERR_SUCC) {
        const char *errstr = lua_tostring(l->lua_state, -1);
        puts(errstr);
        lua_pop(l->lua_state, 1);
        return err;
    }
    
    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_tree_boardcast(
    cpymo_lua_context *lcontext,
    const char *children_table_name,
    const char *preorder_function_name,
    int (*preorder_arg_setter)(lua_State *l, void *userdata),
    void *preorder_arg_setter_userdata,
    const char *postorder_function_name,
    int (*postorder_arg_setter)(lua_State *l, void *userdata),
    void *postorder_arg_setter_userdata)
{
    lua_State *l = lcontext->lua_state;

    if (!lua_istable(l, -1)) return CPYMO_ERR_SUCC;

    lua_getfield(l, -1, preorder_function_name);
    if (lua_isfunction(l, -1)) {
        int nargs = 1;
        lua_pushvalue(l, -2);
        if (preorder_arg_setter)
            nargs = 1 + preorder_arg_setter(l, preorder_arg_setter_userdata);
        error_t err = cpymo_lua_context_execute(lcontext, nargs, 0);
        CPYMO_THROW(err);
    }
    else {
        lua_pop(l, 1);
    }

    if (children_table_name) {
        lua_getfield(l, -1, children_table_name);
        if (lua_istable(l, -1)) {
            lua_pushnil(l);
            while (lua_next(l, -2)) {
                error_t err = cpymo_lua_tree_boardcast(
                    lcontext,
                    children_table_name,
                    preorder_function_name,
                    preorder_arg_setter,
                    preorder_arg_setter_userdata,
                    postorder_function_name,
                    postorder_arg_setter,
                    postorder_arg_setter_userdata);
                lua_pop(l, 1);
            }
        }
        lua_pop(l, 1);
    }
    
    lua_getfield(l, -1, postorder_function_name);
    if (lua_isfunction(l, -1)) {
        int nargs = 1;
        lua_pushvalue(l, -2);
        if (postorder_arg_setter)
            nargs = 1 + postorder_arg_setter(l, postorder_arg_setter_userdata);
        error_t err = cpymo_lua_context_execute(lcontext, nargs, 0);
        CPYMO_THROW(err);
    }
    else {
        lua_pop(l, 1);
    }

    return CPYMO_ERR_SUCC;
}

void cpymo_lua_mark_table_readonly(cpymo_lua_context *l)
{
    int ref = luaL_ref(l->lua_state, LUA_REGISTRYINDEX);
    int *userdata = (int *)lua_newuserdata(l->lua_state, sizeof(ref));
    *userdata = ref;
    lua_rawgeti(l->lua_state, LUA_REGISTRYINDEX, l->readonly_metatable);
    lua_setmetatable(l->lua_state, -2);
}

static int cpymo_lua_actor_update_arg_setter(
    lua_State *s, void *p_float_deltatime)
{
    lua_pushnumber(s, (lua_Number)(*((float *)p_float_deltatime)));
    return 1;
}

error_t cpymo_lua_actor_update(
    cpymo_lua_context *l,
    float delta_time)
{
    return cpymo_lua_tree_boardcast(
        l,
        cpymo_lua_actor_children_table_name,
        "update",
        &cpymo_lua_actor_update_arg_setter,
        (void *)&delta_time,
        "late_update",
        &cpymo_lua_actor_update_arg_setter,
        (void *)&delta_time);
}

void cpymo_lua_actor_draw(
    const cpymo_lua_context *l)
{
    cpymo_lua_tree_boardcast(
        (cpymo_lua_context *)l,
        cpymo_lua_actor_children_table_name,
        "draw", NULL, NULL,
        "late_draw", NULL, NULL);
}

void cpymo_lua_actor_free(
    cpymo_lua_context *l)
{
    cpymo_lua_tree_boardcast(
        l,
        cpymo_lua_actor_children_table_name,
        "free", NULL, NULL,
        "late_free", NULL, NULL);
}

void cpymo_lua_get_main_actor(const cpymo_lua_context *l)
{
    lua_getglobal(l->lua_state, "main");
}

error_t cpymo_lua_actor_update_main(
    cpymo_lua_context *l,
    float delta_time)
{
    if (l->lua_state == NULL) return CPYMO_ERR_SUCC;

    error_t timer_err = cpymo_lua_timers_update(l, delta_time);
    cpymo_lua_block_update(l);
    CPYMO_THROW(timer_err);

    cpymo_lua_get_main_actor(l);
    error_t err = cpymo_lua_actor_update(l, delta_time);
    #ifdef LEAKCHECK
    lua_pop(l->lua_state, 1);
    #else
    lua_pop(l->lua_state, -1);
    #endif
    return err;
}

void cpymo_lua_actor_draw_main(
    const cpymo_lua_context *l)
{
    if (l->lua_state == NULL) return;
    cpymo_lua_get_main_actor((cpymo_lua_context *)l);
    cpymo_lua_actor_draw(l);
    #ifdef LEAKCHECK
    lua_pop(l->lua_state, 1);
    #else
    lua_pop(l->lua_state, -1);
    #endif
}

#ifdef LEAKCHECK
void cpymo_lua_context_leakcheck(cpymo_lua_context *l)
{
    if (l->lua_state == NULL) return;
    int num = lua_gettop(l->lua_state);
    if (!num) return;

    printf("[LEAKCHECK] Lua stack leaking detected: %d objects leaked.\n", num);
}
#endif

#define POPCHECK(L) \
    if (lua_gettop(L) < 0) return CPYMO_ERR_INVALID_ARG;

error_t cpymo_lua_pop_float(
    lua_State *l,
    float *v)
{
    POPCHECK(l);

    int succeed;
    lua_Number x = lua_tonumberx(l, -1, &succeed);
    if (!succeed) return CPYMO_ERR_INVALID_ARG;

    lua_pop(l, 1);

    if (succeed) *v = (float)x;

    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_pop_lightuserdata(
    lua_State *l,
    const void **v)
{
    POPCHECK(l);

    if (lua_type(l, -1) != LUA_TLIGHTUSERDATA)
        return CPYMO_ERR_INVALID_ARG;
    
    *v = lua_topointer(l, -1);
    lua_pop(l, 1);

    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_pop_rect(
    lua_State *l,
    float *x,
    float *y,
    float *w,
    float *h)
{
    POPCHECK(l);
    
    int succeed[4];

    if (!lua_istable(l, -1)) 
        return CPYMO_ERR_INVALID_ARG;

    
    lua_Number rect[4];

    lua_getfield(l, -1, "x");
    rect[0] = lua_tonumberx(l, -1, succeed);
    lua_pop(l, 1);

    lua_getfield(l, -1, "y");
    rect[1] = lua_tonumberx(l, -1, succeed + 1);
    lua_pop(l, 1);

    lua_getfield(l, -1, "w");
    rect[2] = lua_tonumberx(l, -1, succeed + 2);
    lua_pop(l, 1);

    lua_getfield(l, -1, "h");
    rect[3] = lua_tonumberx(l, -1, succeed + 3);
    lua_pop(l, 2);

    if (!succeed[0] || !succeed[1] || !succeed[2] || !succeed[3])
        return CPYMO_ERR_INVALID_ARG;
    
    *x = (float)rect[0];
    *y = (float)rect[1];
    *w = (float)rect[2];
    *h = (float)rect[3];

    return CPYMO_ERR_SUCC;
}

error_t cpymo_lua_pop_color(
    lua_State *l,
    cpymo_color *col,
    float *alpha)
{
    POPCHECK(l);

    int succeed[4];

    if (!lua_istable(l, -1)) return CPYMO_ERR_INVALID_ARG;

    lua_Number color[4];

    lua_getfield(l, -1, "r");
    color[0] = lua_tonumberx(l, -1, succeed);
    lua_pop(l, 1);

    lua_getfield(l, -1, "g");
    color[1] = lua_tonumberx(l, -1, succeed + 1);
    lua_pop(l, 1);

    lua_getfield(l, -1, "b");
    color[2] = lua_tonumberx(l, -1, succeed + 2);
    lua_pop(l, 1);

    succeed[3] = 1;
    if (lua_getfield(l, -1, "a") == LUA_TNIL) color[3] = 255.0f;
    else color[3] = lua_tonumberx(l, -1, succeed + 3);
    lua_pop(l, 2);

    if (!succeed[0] || !succeed[1] || !succeed[2] || !succeed[3])
        return CPYMO_ERR_INVALID_ARG;

    col->r = (uint8_t)color[0];
    col->g = (uint8_t)color[1];
    col->b = (uint8_t)color[2];
    *alpha = (float)color[3] / 255.0f;

    return CPYMO_ERR_SUCC;
}

#endif

#if CPYMO_FEATURE_LEVEL >= 1

/* Returns 1 when a handler was invoked, 0 when the page has none: a click on
   an item added by the patch layer then has no effect at all. */
int cpymo_lua_ui_page_click(cpymo_lua_context *l, const char *page, int index)
{
    if (l == NULL || l->lua_state == NULL) return 0;
    if (l->ui_click_ref == LUA_NOREF) return 0;
    if (page == NULL) return 0;

    lua_State *L = l->lua_state;
    int top = lua_gettop(L);
    int called = 0;

    /* 各页派发过来的是「页内追加项序号」，回调统一收全局序号，
       与 cpymo.ui.item_add 的返回值一致。 */
    size_t base = cpymo_ui_patch_get_base(&l->ui_patch, page);
    int global_index = index;
    if (base != (size_t)-1 && index >= 0) global_index = (int)base + index;

    lua_rawgeti(L, LUA_REGISTRYINDEX, l->ui_click_ref);
    lua_getfield(L, -1, page);
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, page);
        lua_pushinteger(L, global_index);
        if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            printf("[cpymo] on_item_click(%s, %d) failed: %s\n",
                page, index, err ? err : "(unknown)");
        }
        called = 1;
    }

    lua_settop(L, top);
    return called;
}

#endif
