#include "cpymo_prelude.h"
#include <stdlib.h>
#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_engine.h"
#include "cpymo_accessibility.h"

#include <lauxlib.h>
#include <lualib.h>

/* ================================================================
 * cpymo.a11y
 *
 * Lua 侧无障碍入口：朗读、音效、振动。
 *
 * 朗读直接走引擎 say 使用的同一条通道
 * (cpymo_engine_extract_text + cpymo_engine_extract_text_submit)，
 * 因此继承完整的打断/去重/排队规则，而不是绕过后端另开一条。
 * UI 在这里自行绘制的文本不会被引擎自动朗读，脚本必须在
 * 焦点或内容变化时主动调用 cpymo.a11y.speak。
 *
 * 未启用 ENABLE_TEXT_EXTRACT 的构建里三个函数都是静默无效，
 * 与引擎侧的空实现保持一致。
 * ================================================================ */

static int cpymo_lua_api_a11y_speak(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    const char *str = lua_tostring(l, -1);
    if (str == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

#ifdef ENABLE_TEXT_EXTRACT
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    cpymo_engine_extract_text_cstr(e, str);
    cpymo_engine_extract_text_submit(e);
#else
    (void)str;
#endif

    return 0;
}

static int cpymo_lua_api_a11y_sound(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    int succeed = 0;
    lua_Integer sound_type = lua_tointegerx(l, -1, &succeed);
    if (!succeed || sound_type < 1 || sound_type > 3)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

#ifdef ENABLE_TEXT_EXTRACT
    cpymo_accessibility_play_sound((int)sound_type);
#endif

    return 0;
}

static int cpymo_lua_api_a11y_vibrate(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    int succeed = 0;
    lua_Integer ms = lua_tointegerx(l, -1, &succeed);
    if (!succeed || ms < 1 || ms > 1000)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

#ifdef ENABLE_TEXT_EXTRACT
    cpymo_accessibility_vibrate((int)ms);
#endif

    return 0;
}

void cpymo_lua_api_a11y_register(cpymo_lua_context *ctx)
{
    lua_State *l = ctx->lua_state;

    lua_newtable(l);

    const luaL_Reg funcs[] = {
        { "speak", &cpymo_lua_api_a11y_speak },
        { "sound", &cpymo_lua_api_a11y_sound },
        { "vibrate", &cpymo_lua_api_a11y_vibrate },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs, 0);

    lua_pushinteger(l, SOUND_ENTER);
    lua_setfield(l, -2, "SOUND_ENTER");

    lua_pushinteger(l, SOUND_MENU);
    lua_setfield(l, -2, "SOUND_MENU");

    lua_pushinteger(l, SOUND_SELECT);
    lua_setfield(l, -2, "SOUND_SELECT");

    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "a11y");
}

#endif
