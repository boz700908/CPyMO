#include "cpymo_prelude.h"
#if CPYMO_FEATURE_LEVEL >= 1
#include "cpymo_engine.h"

#include <lauxlib.h>
#include <lualib.h>

static int cpymo_lua_api_render_request_redraw(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 0);
    cpymo_engine *e = cpymo_lua_state_get_engine(l);
    if (e) cpymo_engine_request_redraw(e);
    return 0;
}

typedef struct {
    cpymo_backend_text text;
    float width;
    float fontsize;
} cpymo_lua_api_render_class_text;

static error_t cpymo_lua_api_render_class_text_constructor(
    lua_State *l,
    cpymo_backend_text backend_text,
    float width,
    float fontsize)
{
    cpymo_lua_api_render_class_text *text_obj =
        lua_newuserdata(l, sizeof(cpymo_lua_api_render_class_text));
    if (text_obj == NULL) return CPYMO_ERR_OUT_OF_MEM;

    text_obj->text = backend_text;
    text_obj->width = width;
    text_obj->fontsize = fontsize;

    luaL_getmetatable(l, "cpymo_render_text");
    lua_setmetatable(l, -2);

    return CPYMO_ERR_SUCC;
}

static int cpymo_lua_api_render_create_text(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 2);
    const char *str = lua_tostring(l, -2);
    if (str == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    int succeed;
    float fontsize = (float)lua_tonumberx(l, -1, &succeed);
    if (!succeed) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    cpymo_backend_text out_text;
    float width;
    error_t err = cpymo_backend_text_create(
        &out_text, &width, cpymo_str_pure(str), fontsize);
    CPYMO_LUA_THROW(l, err);

    err = cpymo_lua_api_render_class_text_constructor(
        l, out_text, width, fontsize);
    if (err != CPYMO_ERR_SUCC) {
        cpymo_backend_text_free(out_text);
        CPYMO_LUA_THROW(l, err);
    }

    return 1;
}

static int cpymo_lua_api_render_class_text_get_size(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    cpymo_lua_api_render_class_text *text_obj =
        (cpymo_lua_api_render_class_text *)lua_touserdata(l, -1);

    lua_pushnumber(l, text_obj->width);
    lua_pushnumber(l, text_obj->fontsize);
    return 2;
}

static int cpymo_lua_api_render_class_text_free(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    cpymo_lua_api_render_class_text *text_obj =
        (cpymo_lua_api_render_class_text *)lua_touserdata(l, -1);

    if (text_obj) {
        if (text_obj->text) {
            cpymo_backend_text_free(text_obj->text);
            text_obj->text = NULL;
        }

        text_obj->width = 0;
        text_obj->fontsize = 0;
    }

    return 0;
}

static int cpymo_lua_api_render_class_text_draw(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 5);

    const void *draw_type;
    error_t err = cpymo_lua_pop_lightuserdata(l, &draw_type);
    CPYMO_LUA_THROW(l, err);

    cpymo_color col;
    float alpha;
    err = cpymo_lua_pop_color(l, &col, &alpha);
    CPYMO_LUA_THROW(l, err);

    float y_baseline, x;
    err = cpymo_lua_pop_float(l, &y_baseline);
    CPYMO_LUA_THROW(l, err);

    err = cpymo_lua_pop_float(l, &x);
    CPYMO_LUA_THROW(l, err);

    cpymo_lua_api_render_class_text *text_obj = 
        (cpymo_lua_api_render_class_text *)lua_touserdata(l, -1);
    
    if (text_obj->text)
        cpymo_backend_text_draw(
            text_obj->text,
            x, y_baseline,
            col, alpha,
            (enum cpymo_backend_image_draw_type)(intptr_t)draw_type);

    return 0;
}

typedef struct {
    cpymo_backend_image image;
    int w, h;
} cpymo_lua_api_render_class_image;

error_t cpymo_lua_api_render_class_image_constructor(
    lua_State *l, cpymo_backend_image image, int w, int h)
{
    cpymo_lua_api_render_class_image *img = lua_newuserdata(
        l, sizeof(cpymo_lua_api_render_class_image));
    if (img == NULL) return CPYMO_ERR_OUT_OF_MEM;

    img->image = image;
    img->w = w;
    img->h = h;
    luaL_getmetatable(l, "cpymo_render_image");
    lua_setmetatable(l, -2);

    return 0;
}

static int cpymo_lua_api_render_class_image_get_size(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    cpymo_lua_api_render_class_image *img = 
        (cpymo_lua_api_render_class_image *)lua_touserdata(l, -1);

    int w = 0;
    int h = 0;

    if (img) {
        w = img->w;
        h = img->h;
    }

    lua_pop(l, 1);
    lua_pushinteger(l, w);
    lua_pushinteger(l, h);
    return 2;
}

static int cpymo_lua_api_render_class_image_draw(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 5);
    const void *draw_type;
    error_t err = cpymo_lua_pop_lightuserdata(l, &draw_type);
    CPYMO_LUA_THROW(l, err);

    float alpha;
    err = cpymo_lua_pop_float(l, &alpha);
    CPYMO_LUA_THROW(l, err);

    float srcx = 0, srcy = 0, srcw, srch;
    bool src_is_nil = lua_isnil(l, -1);
    if (!src_is_nil) {
        err = cpymo_lua_pop_rect(l, &srcx, &srcy, &srcw, &srch);
        CPYMO_LUA_THROW(l, err);
    }
    else lua_pop(l, 1);

    float dstx = 0, dsty = 0, dstw, dsth;
    err = cpymo_lua_pop_rect(l, &dstx, &dsty, &dstw, &dsth);
    CPYMO_LUA_THROW(l, err);
    
    cpymo_lua_api_render_class_image *img =
        (cpymo_lua_api_render_class_image *)lua_touserdata(l, -1);
    lua_pop(l, 1);

    if (src_is_nil) {
        srcw = (float)img->w;
        srch = (float)img->h;
    }

    if (img->image)
        cpymo_backend_image_draw(
            dstx, dsty, dstw, dsth, img->image,
            (int)srcx, (int)srcy, (int)srcw, (int)srch, alpha / 255.0f, 
            (enum cpymo_backend_image_draw_type)(intptr_t)draw_type);

    return 0;
}

static int cpymo_lua_api_render_class_image_free(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);
    cpymo_lua_api_render_class_image *img = 
        (cpymo_lua_api_render_class_image *)lua_touserdata(l, 1);
    
    if (img) {
        img->w = 0;
        img->h = 0;
        if (img->image) 
            cpymo_backend_image_free(img->image);
        img->image = NULL;
    }

    return 0;
}

static int cpymo_lua_api_render_fill_rect(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 3);

    const void *draw_type;
    error_t err = cpymo_lua_pop_lightuserdata(l, &draw_type);
    CPYMO_LUA_THROW(l, err);

    cpymo_color col;
    float alpha;
    err = cpymo_lua_pop_color(l, &col, &alpha);
    CPYMO_LUA_THROW(l, err);

    float r[4];
    err = cpymo_lua_pop_rect(l, r, r + 1, r + 2, r + 3);
    CPYMO_LUA_THROW(l, err);

    cpymo_backend_image_fill_rects(r, 1, col, alpha, 
        (enum cpymo_backend_image_draw_type)(intptr_t)draw_type);
    return 0;
}

error_t cpymo_lua_api_render_class_masktrans_constructor(
    lua_State *l, cpymo_backend_masktrans masktrans)
{
    cpymo_backend_masktrans *masktrans_luaobj =
        (cpymo_backend_masktrans *)lua_newuserdata(
            l, sizeof(cpymo_backend_masktrans));
    if (masktrans_luaobj == NULL) CPYMO_LUA_THROW(l, CPYMO_ERR_OUT_OF_MEM);
    *masktrans_luaobj = masktrans;

    luaL_getmetatable(l, "cpymo_render_masktrans");
    lua_setmetatable(l, -2);

    return CPYMO_ERR_SUCC;
}

static int cpymo_lua_api_render_class_masktrans_draw(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 3);
    if (!lua_isboolean(l, -1)) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    int succ;
    bool inverse = lua_toboolean(l, -1);
    float lerp = lua_tonumberx(l, -2, &succ);
    if (!succ) CPYMO_LUA_THROW(l, CPYMO_ERR_INVALID_ARG);

    cpymo_backend_masktrans *lua_obj = 
        (cpymo_backend_masktrans *)lua_touserdata(l, -3);
    
    if (*lua_obj) 
        cpymo_backend_masktrans_draw(
            *lua_obj, cpymo_utils_clampf(lerp, 0.0f, 1.0f), inverse);

    return 0;
}

static int cpymo_lua_api_render_class_masktrans_free(lua_State *l)
{
    CPYMO_LUA_ARG_COUNT(l, 1);

    cpymo_backend_masktrans *lua_obj = 
        (cpymo_backend_masktrans *)lua_touserdata(l, -1);

    if (*lua_obj) {
        cpymo_backend_masktrans_free(*lua_obj);
        *lua_obj = NULL;
    }

    return 0;
}

void cpymo_lua_api_render_register(cpymo_lua_context *ctx)
{
    lua_State *l = ctx->lua_state;

    // class `cpymo_render_image`
    luaL_newmetatable(l, "cpymo_render_image");
    lua_newtable(l);
    const luaL_Reg funcs_class_image[] = {
        { "get_size", &cpymo_lua_api_render_class_image_get_size },
        { "draw", &cpymo_lua_api_render_class_image_draw },
        { "free", &cpymo_lua_api_render_class_image_free },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs_class_image, 0);
    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "__index");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_image_free);
    lua_setfield(l, -2, "__gc");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_image_free);
    lua_setfield(l, -2, "__close");
    cpymo_lua_mark_table_readonly(ctx);
    lua_pop(l, 1);

    // class `cpymo_render_text`
    luaL_newmetatable(l, "cpymo_render_text");
    lua_newtable(l);
    const luaL_Reg funcs_class_text[] = {
        { "get_size", &cpymo_lua_api_render_class_text_get_size },
        { "draw", &cpymo_lua_api_render_class_text_draw },
        { "free", &cpymo_lua_api_render_class_text_free },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs_class_text, 0);
    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "__index");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_text_free);
    lua_setfield(l, -2, "__gc");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_text_free);
    lua_setfield(l, -2, "__close");
    cpymo_lua_mark_table_readonly(ctx);
    lua_pop(l, 1);

    // class `cpymo_render_masktrans`
    luaL_newmetatable(l, "cpymo_render_masktrans");
    lua_newtable(l);
    const luaL_Reg funcs_class_masktrans[] = {
        { "draw", &cpymo_lua_api_render_class_masktrans_draw },
        { "free", &cpymo_lua_api_render_class_masktrans_free },
        { NULL, NULL }
    };
    luaL_setfuncs(l, funcs_class_masktrans, 0);
    cpymo_lua_mark_table_readonly(ctx);
    lua_setfield(l, -2, "__index");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_masktrans_free);
    lua_setfield(l, -2, "__gc");
    lua_pushcfunction(l, &cpymo_lua_api_render_class_masktrans_free);
    lua_setfield(l, -2, "__close");
    cpymo_lua_mark_table_readonly(ctx);
    lua_pop(l, 1);
    
    // package `cpymo.render`
    lua_newtable(l); {
        const luaL_Reg funcs[] = {
            { "request_redraw", &cpymo_lua_api_render_request_redraw },
            { "fill_rect", &cpymo_lua_api_render_fill_rect },
            { "create_text", &cpymo_lua_api_render_create_text },
            { NULL, NULL }
        };
        luaL_setfuncs(l, funcs, 0);

        lua_newtable(l); {
            #define BIND(DRAW_TYPE) \
                lua_pushlightuserdata( \
                    l, \
                    (void *)cpymo_backend_image_draw_type_ ## DRAW_TYPE); \
                lua_setfield(l, -2, #DRAW_TYPE);

            BIND(bg);
            BIND(chara);
            BIND(ui_bg);
            BIND(ui_element_bg);
            BIND(ui_element);
            BIND(text_say);
            BIND(text_say_textbox);
            BIND(text_text);
            BIND(titledate_bg);
            BIND(titledate_text);
            BIND(sel_img);
            BIND(effect);

            #undef BIND

            cpymo_lua_mark_table_readonly(ctx);
        } lua_setfield(l, -2, "semantic");
    
        cpymo_lua_mark_table_readonly(ctx);
    } lua_setfield(l, -2, "render");
}


#endif
