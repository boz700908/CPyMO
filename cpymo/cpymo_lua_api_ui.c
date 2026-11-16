#include "cpymo_prelude.h"
#include <stdlib.h>
#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_engine.h"
#include "cpymo_msgbox_ui.h"
#include "cpymo_album.h"
#include "cpymo_backlog.h"
#include "cpymo_config_ui.h"
#include "cpymo_list_ui.h"
#include "cpymo_movie.h"
#include "cpymo_music_box.h"
#include "cpymo_rmenu.h"
#include "cpymo_save_ui.h"
#include "cpymo_lua_hud.h"
#include "cpymo_ui_patch.h"
#include <string.h>

#include <lauxlib.h>
#include <lualib.h>

typedef struct {
    int actor_ref;
} cpymo_lua_ui;

static void cpymo_lua_ui_draw(const cpymo_engine *e, const void *ui)
{
    const cpymo_lua_ui *lua_ui = (const cpymo_lua_ui *)ui;
    lua_rawgeti(e->lua.lua_state, LUA_REGISTRYINDEX, lua_ui->actor_ref);
    cpymo_lua_actor_draw(&e->lua);
    lua_pop(e->lua.lua_state, 1);
}

static error_t cpymo_lua_ui_update(cpymo_engine *e, void *ui, float d)
{
    const cpymo_lua_ui *lua_ui = (const cpymo_lua_ui *)ui;
    lua_rawgeti(e->lua.lua_state, LUA_REGISTRYINDEX, lua_ui->actor_ref);
    error_t err = cpymo_lua_actor_update(&e->lua, d);
    lua_pop(e->lua.lua_state, 1);
    return err;
}

static void cpymo_lua_ui_delete(cpymo_engine *e, void *ui)
{
    cpymo_lua_ui *lua_ui = (cpymo_lua_ui *)ui;
    lua_rawgeti(e->lua.lua_state, LUA_REGISTRYINDEX, lua_ui->actor_ref);
    cpymo_lua_actor_free(&e->lua);
    luaL_unref(e->lua.lua_state, LUA_REGISTRYINDEX, lua_ui->actor_ref);
    lua_pop(e->lua.lua_state, 1);
}

static int cpymo_lua_api_ui_enter(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    cpymo_lua_ui *ui = NULL;

    error_t err = cpymo_ui_enter(
        (void **)&ui, e, sizeof(cpymo_lua_ui), 
        &cpymo_lua_ui_update,
        &cpymo_lua_ui_draw,
        &cpymo_lua_ui_delete);
    CPYMO_LUA_THROW(l, err);

    if (!lua_istable(l, -1)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    ui->actor_ref = luaL_ref(l, LUA_REGISTRYINDEX);

    return 0;
}

static int cpymo_lua_api_ui_exit(lua_State *l)
{ 
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_ui_exit(cpymo_lua_state_get_engine(l)); 
    return 0;
}

static int cpymo_lua_api_ui_msgbox(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    if (!lua_isstring(l, -1)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    error_t err = cpymo_msgbox_ui_enter(
        cpymo_lua_state_get_engine(l),
        cpymo_str_pure(lua_tostring(l, -1)), 
        NULL, NULL);
    CPYMO_LUA_THROW(l, err);
    return 0;
}

typedef struct {
    lua_State *l;
    int callback_ref;
} cpymo_lua_api_ui_okcancelbox_data;

static error_t cpymo_lua_api_ui_okcancelbox_cb(cpymo_engine *e, void *x, bool c)
{
    cpymo_lua_api_ui_okcancelbox_data d = 
        *(cpymo_lua_api_ui_okcancelbox_data *)x;
    free(x);
    
    lua_rawgeti(d.l, LUA_REGISTRYINDEX, d.callback_ref);
    luaL_unref(d.l, LUA_REGISTRYINDEX, d.callback_ref);
    lua_pushboolean(d.l, c);
    return cpymo_lua_context_execute(
        cpymo_lua_state_get_lua_context(d.l), 1, 0);
}

static int cpymo_lua_api_ui_okcancelbox(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 2);
    if (!lua_isfunction(l, -1)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);
    if (!lua_isstring(l, -2)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    const char *msg = lua_tostring(l, -2);

    cpymo_lua_api_ui_okcancelbox_data *data = 
        malloc(sizeof(cpymo_lua_api_ui_okcancelbox_data));
    if (data == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_OUT_OF_MEM);

    error_t err = cpymo_msgbox_ui_enter(
        cpymo_lua_state_get_engine(l),
        cpymo_str_pure(msg),
        &cpymo_lua_api_ui_okcancelbox_cb,
        (void *)data);
    if (err != CPYMO_ERR_SUCC) {
        free(data);
        CPYMO_LUA_THROW(l, err);
    }

    data->l = l;
    data->callback_ref = luaL_ref(l, LUA_REGISTRYINDEX);

    return CPYMO_ERR_SUCC;
}

CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_backlog, cpymo_backlog_ui_enter);
CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_config, cpymo_config_ui_enter);
CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_musicbox, cpymo_music_box_enter);
CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_rmenu, cpymo_rmenu_enter);

static inline error_t cpymo_lua_api_open_save_internal(cpymo_engine *e) 
{ return cpymo_save_ui_enter(e, false); }
CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_save_ui, cpymo_lua_api_open_save_internal);

static inline error_t cpymo_lua_api_open_load_internal(cpymo_engine *e) 
{ return cpymo_save_ui_enter(e, true); }
CPYMO_LUA_MAKE_BIND_SIMPLE(cpymo_lua_api_ui_open_load_ui, cpymo_lua_api_open_load_internal);


static int cpymo_lua_api_ui_play_movie(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    const char *name = lua_tostring(l, -1);
    if (name == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    error_t err = cpymo_movie_play(
        cpymo_lua_state_get_engine(l),
        cpymo_str_pure(name));
    CPYMO_LUA_THROW(l, err);

    return 0;
}


static int cpymo_lua_api_ui_open_album(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    const char *ui_image_name = lua_tostring(l, -1);
    const char *ui_list_name = lua_tostring(l, -2);
    if (ui_image_name == NULL || ui_list_name == NULL)
        CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    error_t err = cpymo_album_enter(
        cpymo_lua_state_get_engine(l),
        cpymo_str_pure(ui_list_name),
        cpymo_str_pure(ui_image_name),
        0);
    CPYMO_LUA_THROW(l, err);
    return 0;
}

static int cpymo_lua_ui_open_load_yesnobox(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    float save_id;
    error_t err = cpymo_lua_pop_float(l, &save_id);
    CPYMO_LUA_THROW(l, err);

    if (save_id < 0) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    err = cpymo_save_ui_load_savedata_yesnobox(
        cpymo_lua_state_get_engine(l),
        (unsigned short)save_id);
    CPYMO_LUA_THROW(l, err);

    return 0;
}

static int cpymo_lua_api_ui_hud(lua_State *l)
{
    int top = lua_gettop(l);
    if (top < 2 || top > 3)
        CPYMO_LUA_PANIC(l, "cpymo.ui.hud(id, text [, options]) takes 2 or 3 arguments.");

    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    const char *id = luaL_checkstring(l, 1);
    cpymo_str text = cpymo_str_pure(luaL_checkstring(l, 2));

    struct cpymo_lua_hud_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.color = cpymo_color_white;

    if (top == 3) {
        if (!lua_istable(l, 3))
            CPYMO_LUA_PANIC(l, "The options of cpymo.ui.hud must be a table.");

        lua_getfield(l, 3, "x");
        if (lua_isnumber(l, -1)) {
            opts.has_pos = true;
            opts.x = (float)lua_tonumber(l, -1);
        }
        lua_pop(l, 1);

        lua_getfield(l, 3, "y");
        if (lua_isnumber(l, -1)) {
            opts.has_pos = true;
            opts.y = (float)lua_tonumber(l, -1);
        }
        lua_pop(l, 1);

        lua_getfield(l, 3, "size");
        if (lua_isnumber(l, -1)) opts.size = (float)lua_tonumber(l, -1);
        lua_pop(l, 1);

        lua_getfield(l, 3, "color");
        if (lua_istable(l, -1)) {
            cpymo_color col = cpymo_color_white;

            lua_getfield(l, -1, "r");
            if (lua_isnumber(l, -1)) col.r = (uint8_t)lua_tointeger(l, -1);
            lua_pop(l, 1);

            lua_getfield(l, -1, "g");
            if (lua_isnumber(l, -1)) col.g = (uint8_t)lua_tointeger(l, -1);
            lua_pop(l, 1);

            lua_getfield(l, -1, "b");
            if (lua_isnumber(l, -1)) col.b = (uint8_t)lua_tointeger(l, -1);
            lua_pop(l, 1);

            opts.color = col;
        }
        lua_pop(l, 1);

        lua_getfield(l, 3, "silent");
        if (lua_isboolean(l, -1)) opts.silent = lua_toboolean(l, -1) != 0;
        lua_pop(l, 1);
    }

    error_t err = cpymo_lua_hud_set(&e->lua.hud_items, id, text, &opts);
    CPYMO_LUA_THROW(l, err);

    return 0;
}

static int cpymo_lua_api_ui_hud_remove(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    cpymo_lua_hud_remove(&e->lua.hud_items, luaL_checkstring(l, 1));

    return 0;
}


static void cpymo_lua_api_ui_push_hud(
    lua_State *l, const struct cpymo_lua_hud_info *h)
{
    lua_newtable(l);

    lua_pushstring(l, h->id ? h->id : "");
    lua_setfield(l, -2, "id");

    lua_pushstring(l, h->text ? h->text : "");
    lua_setfield(l, -2, "text");

    lua_pushboolean(l, h->has_pos);
    lua_setfield(l, -2, "has_pos");

    lua_pushnumber(l, (lua_Number)h->x);
    lua_setfield(l, -2, "x");

    lua_pushnumber(l, (lua_Number)h->y);
    lua_setfield(l, -2, "y");

    /* 0 表示没指定字号，由屏幕高度推算。 */
    lua_pushnumber(l, (lua_Number)h->size);
    lua_setfield(l, -2, "size");

    lua_newtable(l);
    lua_pushinteger(l, (lua_Integer)h->color.r);
    lua_setfield(l, -2, "r");
    lua_pushinteger(l, (lua_Integer)h->color.g);
    lua_setfield(l, -2, "g");
    lua_pushinteger(l, (lua_Integer)h->color.b);
    lua_setfield(l, -2, "b");
    lua_setfield(l, -2, "color");

    lua_pushboolean(l, h->silent);
    lua_setfield(l, -2, "silent");
}

/* cpymo.ui.hud_get(id): 取一条常驻显示的当前内容，没有则 nil。 */
static int cpymo_lua_api_ui_hud_get(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    const char *id = luaL_checkstring(l, 1);

    struct cpymo_lua_hud_info info;
    if (!cpymo_lua_hud_get(e->lua.hud_items, id, &info)) {
        lua_pushnil(l);
        return 1;
    }

    cpymo_lua_api_ui_push_hud(l, &info);
    return 1;
}

/* cpymo.ui.huds(): 按创建顺序列出全部常驻显示。 */
static int cpymo_lua_api_ui_huds(lua_State *l)
{
    if (lua_gettop(l) != 0)
        CPYMO_LUA_PANIC(l, "cpymo.ui.huds() takes no argument.");

    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    lua_newtable(l);

    size_t n = cpymo_lua_hud_count(e->lua.hud_items);
    for (size_t i = 0; i < n; ++i) {
        struct cpymo_lua_hud_info info;
        if (!cpymo_lua_hud_at(e->lua.hud_items, (int)i, &info)) continue;
        cpymo_lua_api_ui_push_hud(l, &info);
        lua_rawseti(l, -2, (lua_Integer)(i + 1));
    }

    return 1;
}

static int cpymo_lua_api_ui_item_set(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 3)
        CPYMO_LUA_PANIC(l, "cpymo.ui.item_set(page, index, text) takes 3 arguments.");

    const char *page = luaL_checkstring(l, 1);
    int index = (int)luaL_checkinteger(l, 2);
    const char *text = luaL_checkstring(l, 3);

    error_t err = cpymo_ui_patch_set(&e->lua.ui_patch, page, index, text);
    if (err != CPYMO_ERR_SUCC)
        luaL_error(l, "cpymo.ui.item_set failed: %d", (int)err);

    return 0;
}

static int cpymo_lua_api_ui_item_hide(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 2)
        CPYMO_LUA_PANIC(l, "cpymo.ui.item_hide(page, index) takes 2 arguments.");

    const char *page = luaL_checkstring(l, 1);
    int index = (int)luaL_checkinteger(l, 2);

    error_t err = cpymo_ui_patch_hide(&e->lua.ui_patch, page, index);
    if (err != CPYMO_ERR_SUCC)
        luaL_error(l, "cpymo.ui.item_hide failed: %d", (int)err);

    return 0;
}

static int cpymo_lua_api_ui_item_clear(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    int top = lua_gettop(l);
    if (top > 1)
        CPYMO_LUA_PANIC(l, "cpymo.ui.item_clear([page]) takes 0 or 1 argument.");

    const char *page = top == 1 ? luaL_checkstring(l, 1) : NULL;
    cpymo_ui_patch_clear(&e->lua.ui_patch, page);

    return 0;
}

static int cpymo_lua_api_ui_item_add(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 2)
        CPYMO_LUA_PANIC(l, "cpymo.ui.item_add(page, text) takes 2 arguments.");

    const char *page = luaL_checkstring(l, 1);
    const char *text = luaL_checkstring(l, 2);

    int n = (int)cpymo_ui_patch_extra_count(&e->lua.ui_patch, page);

    error_t err = cpymo_ui_patch_add_extra(&e->lua.ui_patch, page, text);
    if (err != CPYMO_ERR_SUCC)
        luaL_error(l, "cpymo.ui.item_add failed: %d", (int)err);

    lua_pushinteger(l, n);
    return 1;
}

/* 把 page -> [index] 的 item_on_click 回调压栈；没有则压 nil。
   只读查询：条目从未被改写、从未注册过回调时也如实返回 nil。 */
static void cpymo_lua_api_ui_push_item_click(
    lua_State *l, cpymo_lua_context *lua, const char *page, int index)
{
    int top = lua_gettop(l);

    if (lua == NULL || page == NULL || index < 0
        || lua->ui_item_click_ref == LUA_NOREF) {
        lua_pushnil(l);
        return;
    }

    lua_rawgeti(l, LUA_REGISTRYINDEX, lua->ui_item_click_ref);
    if (lua_istable(l, -1)) {
        lua_getfield(l, -1, page);
        if (lua_istable(l, -1)) {
            lua_rawgeti(l, -1, (lua_Integer)index);
            if (lua_isfunction(l, -1)) {
                lua_pushvalue(l, -1);
                lua_insert(l, top + 1);
                lua_settop(l, top + 1);
                return;
            }
        }
    }

    lua_settop(l, top);
    lua_pushnil(l);
}

/* 把 page 的 on_item_click 回调压栈；没有则压 nil。 */
static void cpymo_lua_api_ui_push_page_click(
    lua_State *l, cpymo_lua_context *lua, const char *page)
{
    int top = lua_gettop(l);

    if (lua == NULL || page == NULL || lua->ui_click_ref == LUA_NOREF) {
        lua_pushnil(l);
        return;
    }

    lua_rawgeti(l, LUA_REGISTRYINDEX, lua->ui_click_ref);
    if (lua_istable(l, -1)) {
        lua_getfield(l, -1, page);
        if (lua_isfunction(l, -1)) {
            lua_pushvalue(l, -1);
            lua_insert(l, top + 1);
            lua_settop(l, top + 1);
            return;
        }
    }

    lua_settop(l, top);
    lua_pushnil(l);
}

static int cpymo_lua_api_ui_item_get(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    if (lua_gettop(l) != 2)
        CPYMO_LUA_PANIC(l, "cpymo.ui.item_get(page, index) takes 2 arguments.");

    const char *page = luaL_checkstring(l, 1);
    int index = (int)luaL_checkinteger(l, 2);

    const cpymo_ui_patch *patch = &e->lua.ui_patch;
    const cpymo_ui_patch_entry *item = cpymo_ui_patch_entry_at(patch, page, index);
    const cpymo_ui_patch_entry *extra = NULL;
    int extra_index = -1;

    size_t base = cpymo_ui_patch_get_base(patch, page);
    if (base != (size_t)-1 && index >= (int)base) {
        extra_index = index - (int)base;
        extra = cpymo_ui_patch_extra_entry(patch, page, (size_t)extra_index);
    }

    /* Nothing stored for this index: when the page registered its capacity the
       index is still a real item, so report what the engine draws right now
       instead of nil. The original text is cached as the page draws. */
    if (item == NULL && extra == NULL) {
        if (base == (size_t)-1 || index < 0 || index >= (int)base) {
            lua_pushnil(l);
            return 1;
        }
    }

    const cpymo_ui_patch_entry *src = extra != NULL ? extra : item;

    const char *text = NULL;
    if (src != NULL && src->text != NULL) text = src->text;
    else if (extra == NULL) text = cpymo_ui_patch_orig_text(patch, page, index);

    lua_newtable(l);

    lua_pushinteger(l, index);
    lua_setfield(l, -2, "index");

    if (text != NULL) lua_pushstring(l, text);
    else lua_pushnil(l);
    lua_setfield(l, -2, "text");

    lua_pushboolean(l, extra == NULL && item != NULL && item->hidden);
    lua_setfield(l, -2, "hidden");

    lua_pushboolean(l, extra != NULL);
    lua_setfield(l, -2, "extra");

    lua_pushinteger(l, extra_index);
    lua_setfield(l, -2, "extra_index");

    /* Whether this layer stores anything for the item (rewrite or hide). */
    lua_pushboolean(l, item != NULL || extra != NULL);
    lua_setfield(l, -2, "rewritten");

    /* 该条现在挂着的 item_on_click 回调；没挂时是 nil 而不是缺字段。 */
    cpymo_lua_api_ui_push_item_click(l, &e->lua, page, index);
    lua_setfield(l, -2, "on_click");

    return 1;
}

static int cpymo_lua_api_ui_items(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);

    if (lua_gettop(l) != 1)
        CPYMO_LUA_PANIC(l, "cpymo.ui.items(page) takes 1 argument.");

    const char *page = luaL_checkstring(l, 1);
    const cpymo_ui_patch *patch = &e->lua.ui_patch;

    size_t base = cpymo_ui_patch_get_base(patch, page);
    size_t patched = cpymo_ui_patch_patched_count(patch, page);
    size_t extras = cpymo_ui_patch_extra_count(patch, page);

    lua_newtable(l);

    lua_pushstring(l, page);
    lua_setfield(l, -2, "page");

    if (base == (size_t)-1) lua_pushnil(l);
    else lua_pushinteger(l, (lua_Integer)base);
    lua_setfield(l, -2, "base");

    lua_pushinteger(l, (lua_Integer)patched);
    lua_setfield(l, -2, "count");

    lua_pushinteger(l, (lua_Integer)extras);
    lua_setfield(l, -2, "extra_count");

    /* 页级 on_item_click 回调（管追加项）；没注册时是 nil。 */
    cpymo_lua_api_ui_push_page_click(l, &e->lua, page);
    lua_setfield(l, -2, "on_click");

    int limit = (int)(base != (size_t)-1 ? base : 0);
    for (const cpymo_ui_patch_entry *it = patch->entries; it; it = it->next) {
        if (it->extra < 0 && it->index + 1 > limit) limit = it->index + 1;
    }

    lua_newtable(l);
    int n = 0;
    for (int i = 0; i < limit; ++i) {
        const cpymo_ui_patch_entry *item = cpymo_ui_patch_entry_at(patch, page, i);
        const char *orig = cpymo_ui_patch_orig_text(patch, page, i);

        /* A real item is listed even when it was never rewritten; only entries
           above the page capacity need a patch record to exist at all. */
        int real = base == (size_t)-1 || i < (int)base;
        if (item == NULL && orig == NULL && !real) continue;

        const char *text = NULL;
        if (item != NULL && item->text != NULL) text = item->text;
        else text = orig;

        lua_newtable(l);

        lua_pushinteger(l, i);
        lua_setfield(l, -2, "index");

        if (text != NULL) lua_pushstring(l, text);
        else lua_pushnil(l);
        lua_setfield(l, -2, "text");

        lua_pushboolean(l, item != NULL && item->hidden);
        lua_setfield(l, -2, "hidden");

        lua_pushboolean(l, item != NULL);
        lua_setfield(l, -2, "rewritten");

        cpymo_lua_api_ui_push_item_click(l, &e->lua, page, i);
        lua_setfield(l, -2, "on_click");

        lua_rawseti(l, -2, (lua_Integer)(++n));
    }
    lua_setfield(l, -2, "entries");

    lua_newtable(l);
    for (size_t i = 0; i < extras; ++i) {
        const cpymo_ui_patch_entry *x = cpymo_ui_patch_extra_entry(patch, page, i);

        lua_newtable(l);

        lua_pushinteger(l, (lua_Integer)(base == (size_t)-1 ? 0 : base + i));
        lua_setfield(l, -2, "index");

        lua_pushstring(l, x != NULL && x->text != NULL ? x->text : "");
        lua_setfield(l, -2, "text");

        lua_pushboolean(l, 1);
        lua_setfield(l, -2, "extra");

        /* 追加项由页级 on_item_click 处理。 */
        cpymo_lua_api_ui_push_page_click(l, &e->lua, page);
        lua_setfield(l, -2, "on_click");

        lua_rawseti(l, -2, (lua_Integer)(i + 1));
    }
    lua_setfield(l, -2, "extras");

    return 1;
}

static int cpymo_lua_api_ui_pages(lua_State *l)
{
    if (lua_gettop(l) != 0)
        CPYMO_LUA_PANIC(l, "cpymo.ui.pages() takes no argument.");

    static const char *const pages[] = {
        CPYMO_UI_PATCH_PAGE_CONFIG,
        CPYMO_UI_PATCH_PAGE_SAVE,
        CPYMO_UI_PATCH_PAGE_LOAD,
        CPYMO_UI_PATCH_PAGE_MUSIC,
        CPYMO_UI_PATCH_PAGE_BACKLOG,
        CPYMO_UI_PATCH_PAGE_ALBUM,
        CPYMO_UI_PATCH_PAGE_SELECT,
        CPYMO_UI_PATCH_PAGE_MSGBOX,
        CPYMO_UI_PATCH_PAGE_RMENU,
    };

    lua_newtable(l);
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        lua_pushstring(l, pages[i]);
        lua_rawseti(l, -2, (lua_Integer)(i + 1));
    }

    return 1;
}

static int cpymo_lua_api_ui_on_item_click(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 2)
        CPYMO_LUA_PANIC(l, "cpymo.ui.on_item_click(page, fn) takes 2 arguments.");

    const char *page = luaL_checkstring(l, 1);
    luaL_checktype(l, 2, LUA_TFUNCTION);

    if (e->lua.ui_click_ref == LUA_NOREF) {
        lua_newtable(l);
        e->lua.ui_click_ref = luaL_ref(l, LUA_REGISTRYINDEX);
    }

    lua_rawgeti(l, LUA_REGISTRYINDEX, e->lua.ui_click_ref);
    lua_pushvalue(l, 2);
    lua_setfield(l, -2, page);
    lua_pop(l, 1);

    return 0;
}

/* cpymo.ui.item_on_click(page, index, fn): 给引擎自带条目挂点击回调。
   fn 收 (page, index)，返回 true 表示吃掉这次点击、不再执行原生动作。
   传 nil 取消该条目的回调。 */
static int cpymo_lua_api_ui_item_on_click(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 3)
        CPYMO_LUA_PANIC(l,
            "cpymo.ui.item_on_click(page, index, fn) takes 3 arguments.");

    const char *page = luaL_checkstring(l, 1);
    lua_Integer index = luaL_checkinteger(l, 2);
    if (index < 0)
        CPYMO_LUA_PANIC(l,
            "cpymo.ui.item_on_click(page, index, fn): index must be >= 0.");
    if (!lua_isnoneornil(l, 3)) luaL_checktype(l, 3, LUA_TFUNCTION);

    if (e->lua.ui_item_click_ref == LUA_NOREF) {
        lua_newtable(l);
        e->lua.ui_item_click_ref = luaL_ref(l, LUA_REGISTRYINDEX);
    }

    lua_rawgeti(l, LUA_REGISTRYINDEX, e->lua.ui_item_click_ref);
    lua_getfield(l, -1, page);
    if (!lua_istable(l, -1)) {
        lua_pop(l, 1);
        lua_newtable(l);
        lua_pushvalue(l, -1);
        lua_setfield(l, -3, page);
    }
    lua_pushvalue(l, 3);
    lua_rawseti(l, -2, index);
    lua_pop(l, 2);

    return 0;
}

static char *cpymo_lua_ui_patch_dupstr(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (r != NULL) memcpy(r, s, n);
    return r;
}

static void cpymo_lua_ui_patch_read_page(lua_State *l, int tbl, char *out, size_t out_len)
{
    out[0] = '\0';
    lua_getfield(l, tbl, "page");
    if (lua_isstring(l, -1)) {
        const char *ps = lua_tostring(l, -1);
        size_t n = strlen(ps);
        if (n + 1 <= out_len) memcpy(out, ps, n + 1);
    }
    lua_pop(l, 1);
}

static void cpymo_lua_ui_patch_push_entry(
    lua_State *l, const cpymo_ui_patch_entry *it)
{
    lua_newtable(l);

    lua_pushstring(l, it->page);
    lua_setfield(l, -2, "page");

    lua_pushinteger(l, it->index);
    lua_setfield(l, -2, "index");

    if (it->text != NULL) lua_pushstring(l, it->text);
    else lua_pushnil(l);
    lua_setfield(l, -2, "text");

    lua_pushboolean(l, it->hidden);
    lua_setfield(l, -2, "hidden");

    lua_pushboolean(l, it->extra >= 0);
    lua_setfield(l, -2, "extra");
}

static int cpymo_lua_api_ui_export_patch(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 0)
        CPYMO_LUA_PANIC(l, "cpymo.ui.export_patch() takes no argument.");

    const cpymo_ui_patch *patch = &e->lua.ui_patch;

    lua_newtable(l);

    lua_newtable(l);
    int n = 0;

    for (const cpymo_ui_patch_entry *it = patch->entries; it; it = it->next) {
        if (it->extra >= 0) continue;

        cpymo_lua_ui_patch_push_entry(l, it);
        lua_rawseti(l, -2, (lua_Integer)(++n));
    }

    /* 链表是头插：顺序遍历得到的是追加的逆序，按它导入会颠倒页内顺序。
       追加项改为按 extra 升序输出（选择排序，条目数为个位，代价可忽略）。 */
    int last_extra = -1;
    for (;;) {
        const cpymo_ui_patch_entry *pick = NULL;

        for (const cpymo_ui_patch_entry *it = patch->entries; it; it = it->next) {
            if (it->extra < 0) continue;
            if (it->extra <= last_extra) continue;
            if (pick == NULL || it->extra < pick->extra) pick = it;
        }

        if (pick == NULL) break;

        cpymo_lua_ui_patch_push_entry(l, pick);
        lua_rawseti(l, -2, (lua_Integer)(++n));
        last_extra = pick->extra;
    }

    lua_setfield(l, -2, "entries");

    lua_newtable(l);
    n = 0;
    for (const cpymo_ui_patch_base *b = patch->bases; b; b = b->next) {
        lua_newtable(l);

        lua_pushstring(l, b->page);
        lua_setfield(l, -2, "page");

        lua_pushinteger(l, (lua_Integer)b->count);
        lua_setfield(l, -2, "count");

        lua_rawseti(l, -2, (lua_Integer)(++n));
    }
    lua_setfield(l, -2, "bases");

    return 1;
}

static int cpymo_lua_api_ui_import_patch(lua_State *l)
{
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (lua_gettop(l) != 1 || !lua_istable(l, 1))
        CPYMO_LUA_PANIC(l, "cpymo.ui.import_patch(t) takes 1 table argument.");

    cpymo_ui_patch *patch = &e->lua.ui_patch;
    cpymo_ui_patch_free(patch);
    cpymo_ui_patch_init(patch);

    lua_getfield(l, 1, "entries");
    if (lua_istable(l, -1)) {
        size_t count = lua_rawlen(l, -1);
        for (size_t i = 1; i <= count; ++i) {
            lua_rawgeti(l, -1, (lua_Integer)i);
            if (lua_istable(l, -1)) {
                char page[64];
                cpymo_lua_ui_patch_read_page(l, -1, page, sizeof(page));

                char *text = NULL;
                lua_getfield(l, -1, "text");
                if (lua_isstring(l, -1)) text = cpymo_lua_ui_patch_dupstr(lua_tostring(l, -1));
                lua_pop(l, 1);

                lua_getfield(l, -1, "index");
                int index = (int)lua_tointeger(l, -1);
                lua_pop(l, 1);

                lua_getfield(l, -1, "hidden");
                bool hidden = lua_toboolean(l, -1) != 0;
                lua_pop(l, 1);

                lua_getfield(l, -1, "extra");
                bool extra = lua_toboolean(l, -1) != 0;
                lua_pop(l, 1);

                if (page[0] != '\0') {
                    if (extra)
                        cpymo_ui_patch_add_extra(patch, page, text != NULL ? text : "");
                    else {
                        if (text != NULL) cpymo_ui_patch_set(patch, page, index, text);
                        if (hidden) cpymo_ui_patch_hide(patch, page, index);
                    }
                }

                if (text != NULL) free(text);
            }
            lua_pop(l, 1);
        }
    }
    lua_pop(l, 1);

    lua_getfield(l, 1, "bases");
    if (lua_istable(l, -1)) {
        size_t count = lua_rawlen(l, -1);
        for (size_t i = 1; i <= count; ++i) {
            lua_rawgeti(l, -1, (lua_Integer)i);
            if (lua_istable(l, -1)) {
                char page[64];
                cpymo_lua_ui_patch_read_page(l, -1, page, sizeof(page));

                lua_getfield(l, -1, "count");
                size_t base_count = (size_t)lua_tointeger(l, -1);
                lua_pop(l, 1);

                if (page[0] != '\0') cpymo_ui_patch_set_base(patch, page, base_count);
            }
            lua_pop(l, 1);
        }
    }
    lua_pop(l, 1);

    return 0;
}


void cpymo_lua_api_ui_register(cpymo_lua_context *ctx)
{
    lua_State *l = ctx->lua_state;

    lua_newtable(l);

    // basic
    const luaL_Reg funcs[] = {
        { "enter", &cpymo_lua_api_ui_enter },
        { "exit", &cpymo_lua_api_ui_exit },
        { "msgbox", &cpymo_lua_api_ui_msgbox },
        { "okcancelbox", &cpymo_lua_api_ui_okcancelbox },
        { "open_backlog", &cpymo_lua_api_ui_open_backlog },
        { "open_config", &cpymo_lua_api_ui_open_config },
        { "open_musicbox", &cpymo_lua_api_ui_open_musicbox },
        { "open_rmenu", &cpymo_lua_api_ui_open_rmenu },
        { "open_save_ui", &cpymo_lua_api_ui_open_save_ui },
        { "open_load_ui", &cpymo_lua_api_ui_open_load_ui },
        { "open_album", &cpymo_lua_api_ui_open_album },
        { "play_movie", &cpymo_lua_api_ui_play_movie },
        { "open_load_yesnobox", &cpymo_lua_ui_open_load_yesnobox },
        { "hud", &cpymo_lua_api_ui_hud },
        { "hud_remove", &cpymo_lua_api_ui_hud_remove },
        { "hud_get", &cpymo_lua_api_ui_hud_get },
        { "huds", &cpymo_lua_api_ui_huds },
        { "item_set", &cpymo_lua_api_ui_item_set },
        { "item_hide", &cpymo_lua_api_ui_item_hide },
        { "item_clear", &cpymo_lua_api_ui_item_clear },
        { "item_add", &cpymo_lua_api_ui_item_add },
        { "on_item_click", &cpymo_lua_api_ui_on_item_click },
        { "item_on_click", &cpymo_lua_api_ui_item_on_click },
        { "item_get", &cpymo_lua_api_ui_item_get },
        { "items", &cpymo_lua_api_ui_items },
        { "pages", &cpymo_lua_api_ui_pages },
        { "export_patch", &cpymo_lua_api_ui_export_patch },
        { "import_patch", &cpymo_lua_api_ui_import_patch },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs, 0);

    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "ui");
}

#endif
