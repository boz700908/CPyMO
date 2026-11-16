#include "cpymo_prelude.h"
#include "cpymo_lua_hud.h"
#include "../cpymo-backends/include/cpymo_backend_text.h"
#include "../cpymo-backends/include/cpymo_backend_image.h"

#include <stdlib.h>
#include <string.h>

struct cpymo_lua_hud_item {
    struct cpymo_lua_hud_item *next;
    char *id;
    char *text;

    cpymo_backend_text backend;
    float width;

    float size;
    float x, y;
    cpymo_color color;
    bool silent;

    bool has_pos;
    bool laid_out;
};

static char * cpymo_lua_hud_strdup(cpymo_str s)
{
    char *r = (char *)malloc(s.len + 1);
    if (!r) return NULL;
    if (s.len) memcpy(r, s.begin, s.len);
    r[s.len] = '\0';
    return r;
}

static struct cpymo_lua_hud_item * cpymo_lua_hud_find(
    struct cpymo_lua_hud_item *head, const char *id)
{
    while (head) {
        if (head->id && strcmp(head->id, id) == 0) return head;
        head = head->next;
    }
    return NULL;
}

static float cpymo_lua_hud_default_size(int logical_screen_h)
{
    float s = (float)logical_screen_h / 28.0f;
    if (s < 8.0f) s = 8.0f;
    return s;
}

/* Rebuild the renderable text of an item, if it is out of date. */
static void cpymo_lua_hud_sync_backend(
    struct cpymo_lua_hud_item *item, float default_size)
{
    if (item->backend) return;

    float size = item->size > 0.0f ? item->size : default_size;
    cpymo_backend_text_create(
        &item->backend, &item->width,
        cpymo_str_pure(item->text ? item->text : ""), size);
}

error_t cpymo_lua_hud_set(
    struct cpymo_lua_hud_item **head,
    const char *id,
    cpymo_str text,
    const struct cpymo_lua_hud_options *options)
{
    if (!head || !id || !id[0]) return CPYMO_ERR_INVALID_ARG;

    struct cpymo_lua_hud_options def;
    if (!options) {
        memset(&def, 0, sizeof(def));
        def.color = cpymo_color_white;
        options = &def;
    }

    struct cpymo_lua_hud_item *item = cpymo_lua_hud_find(*head, id);
    bool created = false;

    if (!item) {
        item = (struct cpymo_lua_hud_item *)calloc(1, sizeof(*item));
        if (!item) return CPYMO_ERR_OUT_OF_MEM;

        item->id = (char *)malloc(strlen(id) + 1);
        if (!item->id) { free(item); return CPYMO_ERR_OUT_OF_MEM; }
        strcpy(item->id, id);

        item->color = options->color;
        item->has_pos = options->has_pos;
        item->size = options->size;
        item->silent = options->silent;
        if (options->has_pos) { item->x = options->x; item->y = options->y; }

        created = true;
        if (*head) {
            struct cpymo_lua_hud_item *t = *head;
            while (t->next) t = t->next;
            t->next = item;
        }
        else *head = item;
    }
    else {
        if (options->has_pos) {
            item->x = options->x;
            item->y = options->y;
            item->has_pos = true;
        }
        if (options->size > 0.0f && options->size != item->size) {
            item->size = options->size;
            if (item->backend) {
                cpymo_backend_text_free(item->backend);
                item->backend = NULL;
                item->width = 0.0f;
            }
        }
        item->color = options->color;
        item->silent = options->silent;
    }

    char *newtext = cpymo_lua_hud_strdup(text);
    if (!newtext) {
        if (created) cpymo_lua_hud_remove(head, id);
        return CPYMO_ERR_OUT_OF_MEM;
    }

    bool changed = !item->text || strcmp(item->text, newtext) != 0;

    if (changed) {
        free(item->text);
        item->text = newtext;

        if (item->backend) {
            cpymo_backend_text_free(item->backend);
            item->backend = NULL;
            item->width = 0.0f;
        }

        /* The existing accessibility layer reads the new value out; the script
         * does not have to speak it itself. */
        if (!item->silent && item->text[0]) { cpymo_a11y_speak(item->text); }
    }
    else free(newtext);

    return CPYMO_ERR_SUCC;
}

bool cpymo_lua_hud_remove(
    struct cpymo_lua_hud_item **head,
    const char *id)
{
    if (!head || !id) return false;

    struct cpymo_lua_hud_item **p = head;
    while (*p) {
        struct cpymo_lua_hud_item *item = *p;
        if (item->id && strcmp(item->id, id) == 0) {
            *p = item->next;
            if (item->backend) cpymo_backend_text_free(item->backend);
            free(item->id);
            free(item->text);
            free(item);
            return true;
        }
        p = &item->next;
    }

    return false;
}

void cpymo_lua_hud_free_all(struct cpymo_lua_hud_item **head)
{
    if (!head) return;

    struct cpymo_lua_hud_item *item = *head;
    while (item) {
        struct cpymo_lua_hud_item *next = item->next;
        if (item->backend) cpymo_backend_text_free(item->backend);
        free(item->id);
        free(item->text);
        free(item);
        item = next;
    }

    *head = NULL;
}

void cpymo_lua_hud_draw_all(
    const struct cpymo_lua_hud_item *head,
    int logical_screen_w,
    int logical_screen_h)
{
    if (!head) return;

    float default_size = cpymo_lua_hud_default_size(logical_screen_h);
    float margin = default_size * 0.5f;

    /* Yes, the list is walked through a cast: syncing the renderable text is
     * a cache fill, not a visible change of the readout. */
    struct cpymo_lua_hud_item *item = (struct cpymo_lua_hud_item *)head;

    float y = margin;
    while (item) {
        cpymo_lua_hud_sync_backend(item, default_size);

        float size = item->size > 0.0f ? item->size : default_size;
        float h = size * 1.2f;

        float x;
        if (item->has_pos) x = item->x;
        else x = (float)logical_screen_w - item->width - margin;

        if (item->has_pos) y = item->y;

        if (item->backend && item->text && item->text[0]) {
            /* A dark strip behind the text, so a readout stays readable over
             * any background. */
            float rect[4] = {
                x - margin * 0.5f,
                y - margin * 0.25f,
                item->width + margin,
                h + margin * 0.5f
            };
            cpymo_backend_image_fill_rects(
                rect, 1, cpymo_color_black, 0.45f,
                cpymo_backend_image_draw_type_ui_element_bg);

            cpymo_backend_text_draw(
                item->backend, x, y + size,
                item->color, 1.0f,
                cpymo_backend_image_draw_type_titledate_text);
        }

        if (!item->has_pos) y += h + margin * 0.5f;
        item = item->next;
    }
}


static void cpymo_lua_hud_info_of(
    const struct cpymo_lua_hud_item *item,
    struct cpymo_lua_hud_info *out)
{
    out->id = item->id;
    out->text = item->text;
    out->has_pos = item->has_pos;
    out->x = item->x;
    out->y = item->y;
    out->size = item->size;
    out->color = item->color;
    out->silent = item->silent;
}

size_t cpymo_lua_hud_count(const struct cpymo_lua_hud_item *head)
{
    size_t n = 0;
    for (; head; head = head->next) ++n;
    return n;
}

bool cpymo_lua_hud_get(
    const struct cpymo_lua_hud_item *head,
    const char *id,
    struct cpymo_lua_hud_info *out)
{
    if (!out || !id) return false;

    const struct cpymo_lua_hud_item *item =
        cpymo_lua_hud_find((struct cpymo_lua_hud_item *)head, id);
    if (!item) return false;

    cpymo_lua_hud_info_of(item, out);
    return true;
}

bool cpymo_lua_hud_at(
    const struct cpymo_lua_hud_item *head,
    int index,
    struct cpymo_lua_hud_info *out)
{
    if (!out || index < 0) return false;

    int i = 0;
    for (const struct cpymo_lua_hud_item *item = head; item; item = item->next) {
        if (i++ == index) {
            cpymo_lua_hud_info_of(item, out);
            return true;
        }
    }

    return false;
}
