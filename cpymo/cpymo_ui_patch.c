#include "cpymo_ui_patch.h"

#include <stdlib.h>
#include <string.h>

static bool cpymo_ui_patch_page_is(const cpymo_ui_patch_entry *e, const char *page)
{
    return e->page != NULL && page != NULL && strcmp(e->page, page) == 0;
}

static cpymo_ui_patch_entry *cpymo_ui_patch_find(
    cpymo_ui_patch *patch, const char *page, int index, int extra)
{
    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->index == index && e->extra == extra && cpymo_ui_patch_page_is(e, page)) {
            return e;
        }
    }
    return NULL;
}

static void cpymo_ui_patch_entry_free(cpymo_ui_patch_entry *e)
{
    if (e) {
        free(e->page);
        free(e->text);
        free(e);
    }
}

static void cpymo_ui_patch_remove(cpymo_ui_patch *patch, cpymo_ui_patch_entry *target)
{
    cpymo_ui_patch_entry *prev = NULL;
    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e == target) {
            if (prev) prev->next = e->next;
            else patch->entries = e->next;
            cpymo_ui_patch_entry_free(e);
            return;
        }
        prev = e;
    }
}

static char *cpymo_ui_patch_strdup(const char *s)
{
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static cpymo_ui_patch_entry *cpymo_ui_patch_add_entry(
    cpymo_ui_patch *patch, const char *page, int index, int extra)
{
    cpymo_ui_patch_entry *e = (cpymo_ui_patch_entry *)malloc(sizeof(*e));
    if (e == NULL) return NULL;

    e->page = cpymo_ui_patch_strdup(page);
    if (e->page == NULL) {
        free(e);
        return NULL;
    }

    e->index = index;
    e->text = NULL;
    e->hidden = false;
    e->extra = extra;
    e->next = patch->entries;
    patch->entries = e;
    return e;
}

void cpymo_ui_patch_init(cpymo_ui_patch *patch)
{
    if (patch == NULL) return;
    patch->entries = NULL;
    patch->bases = NULL;
    patch->extra_count = 0;
}

void cpymo_ui_patch_free(cpymo_ui_patch *patch)
{
    if (patch == NULL) return;

    cpymo_ui_patch_entry *e = patch->entries;
    while (e) {
        cpymo_ui_patch_entry *next = e->next;
        cpymo_ui_patch_entry_free(e);
        e = next;
    }

    cpymo_ui_patch_base *b = patch->bases;
    while (b) {
        cpymo_ui_patch_base *next = b->next;
        free(b->page);
        free(b);
        b = next;
    }

    patch->entries = NULL;
    patch->bases = NULL;
    patch->extra_count = 0;
}

error_t cpymo_ui_patch_set(cpymo_ui_patch *patch, const char *page, int index, const char *text)
{
    if (patch == NULL || page == NULL || index < 0) return CPYMO_ERR_INVALID_ARG;

    cpymo_ui_patch_entry *e = cpymo_ui_patch_find(patch, page, index, -1);

    if (text == NULL || text[0] == '\0') {
        if (e) {
            e->hidden = false;
            cpymo_ui_patch_remove(patch, e);
        }
        return CPYMO_ERR_SUCC;
    }

    if (e && e->text != NULL && strcmp(e->text, text) == 0) {
        e->hidden = false;
        return CPYMO_ERR_SUCC;
    }

    char *copy = cpymo_ui_patch_strdup(text);
    if (copy == NULL) return CPYMO_ERR_OUT_OF_MEM;

    if (e == NULL) {
        e = cpymo_ui_patch_add_entry(patch, page, index, -1);
        if (e == NULL) {
            free(copy);
            return CPYMO_ERR_OUT_OF_MEM;
        }
    }

    free(e->text);
    e->text = copy;
    e->hidden = false;
    return CPYMO_ERR_SUCC;
}

error_t cpymo_ui_patch_hide(cpymo_ui_patch *patch, const char *page, int index)
{
    if (patch == NULL || page == NULL || index < 0) return CPYMO_ERR_INVALID_ARG;

    cpymo_ui_patch_entry *e = cpymo_ui_patch_find(patch, page, index, -1);
    if (e == NULL) {
        e = cpymo_ui_patch_add_entry(patch, page, index, -1);
        if (e == NULL) return CPYMO_ERR_OUT_OF_MEM;
    }

    e->hidden = true;
    return CPYMO_ERR_SUCC;
}

void cpymo_ui_patch_clear(cpymo_ui_patch *patch, const char *page)
{
    if (patch == NULL) return;

    cpymo_ui_patch_entry *e = patch->entries;
    while (e) {
        cpymo_ui_patch_entry *next = e->next;
        if (page == NULL || cpymo_ui_patch_page_is(e, page)) {
            cpymo_ui_patch_remove(patch, e);
        }
        e = next;
    }
}

const char *cpymo_ui_patch_text(
    const cpymo_ui_patch *patch, const char *page, int index, const char *original)
{
    if (patch == NULL || page == NULL || index < 0) return original;

    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 && !e->hidden && e->index == index &&
            e->text != NULL && cpymo_ui_patch_page_is(e, page)) {
            return e->text;
        }
    }

    return original;
}

bool cpymo_ui_patch_hidden(const cpymo_ui_patch *patch, const char *page, int index)
{
    if (patch == NULL || page == NULL || index < 0) return false;

    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 && e->hidden && e->index == index &&
            cpymo_ui_patch_page_is(e, page)) {
            return true;
        }
    }

    return false;
}

int cpymo_ui_patch_visible_from(
    const cpymo_ui_patch *patch, const char *page, int index, int dir, int count)
{
    if (count <= 0 || dir == 0) return index;
    if (!cpymo_ui_patch_hidden(patch, page, index)) return index;

    int step = dir > 0 ? 1 : -1;
    int cur = index;

    for (int i = 0; i < count; ++i) {
        cur += step;
        if (cur < 0) cur = count - 1;
        else if (cur >= count) cur = 0;

        if (!cpymo_ui_patch_hidden(patch, page, cur)) return cur;
    }

    return -1;
}

error_t cpymo_ui_patch_add_extra(cpymo_ui_patch *patch, const char *page, const char *text)
{
    if (patch == NULL || page == NULL || text == NULL || text[0] == '\0') {
        return CPYMO_ERR_INVALID_ARG;
    }

    cpymo_ui_patch_entry *e =
        cpymo_ui_patch_add_entry(patch, page, patch->extra_count, patch->extra_count);
    if (e == NULL) return CPYMO_ERR_OUT_OF_MEM;

    e->text = cpymo_ui_patch_strdup(text);
    if (e->text == NULL) {
        cpymo_ui_patch_remove(patch, e);
        return CPYMO_ERR_OUT_OF_MEM;
    }

    patch->extra_count++;
    return CPYMO_ERR_SUCC;
}

size_t cpymo_ui_patch_extra_count(const cpymo_ui_patch *patch, const char *page)
{
    if (patch == NULL || page == NULL) return 0;

    size_t count = 0;
    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra >= 0 && cpymo_ui_patch_page_is(e, page)) count++;
    }

    return count;
}

const char *cpymo_ui_patch_extra_text(
    const cpymo_ui_patch *patch, const char *page, size_t n)
{
    if (patch == NULL || page == NULL) return NULL;

    for (cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 || !cpymo_ui_patch_page_is(e, page)) continue;

        size_t rank = 0;
        for (cpymo_ui_patch_entry *f = patch->entries; f; f = f->next) {
            if (f->extra < 0 || !cpymo_ui_patch_page_is(f, page)) continue;
            if (f->extra < e->extra) rank++;
        }

        if (rank == n) return e->text;
    }

    return NULL;
}

void cpymo_ui_patch_set_base(cpymo_ui_patch *patch, const char *page, size_t count)
{
    if (patch == NULL || page == NULL) return;

    for (cpymo_ui_patch_base *b = patch->bases; b; b = b->next) {
        if (strcmp(b->page, page) == 0) {
            b->count = count;
            return;
        }
    }

    cpymo_ui_patch_base *b = (cpymo_ui_patch_base *)malloc(sizeof(*b));
    if (b == NULL) return;

    b->page = cpymo_ui_patch_strdup(page);
    if (b->page == NULL) {
        free(b);
        return;
    }

    b->count = count;
    b->next = patch->bases;
    patch->bases = b;
}

size_t cpymo_ui_patch_get_base(const cpymo_ui_patch *patch, const char *page)
{
    if (patch == NULL || page == NULL) return (size_t)-1;

    for (const cpymo_ui_patch_base *b = patch->bases; b; b = b->next) {
        if (strcmp(b->page, page) == 0) return b->count;
    }

    return (size_t)-1;
}

const cpymo_ui_patch_entry *cpymo_ui_patch_entry_at(
    const cpymo_ui_patch *patch, const char *page, int index)
{
    if (patch == NULL || page == NULL || index < 0) return NULL;

    for (const cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 && e->index == index && cpymo_ui_patch_page_is(e, page)) {
            return e;
        }
    }

    return NULL;
}

size_t cpymo_ui_patch_patched_count(const cpymo_ui_patch *patch, const char *page)
{
    if (patch == NULL || page == NULL) return 0;

    size_t count = 0;
    for (const cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 && cpymo_ui_patch_page_is(e, page)) count++;
    }

    return count;
}

const cpymo_ui_patch_entry *cpymo_ui_patch_extra_entry(
    const cpymo_ui_patch *patch, const char *page, size_t n)
{
    if (patch == NULL || page == NULL) return NULL;

    for (const cpymo_ui_patch_entry *e = patch->entries; e; e = e->next) {
        if (e->extra < 0 || !cpymo_ui_patch_page_is(e, page)) continue;

        size_t rank = 0;
        for (const cpymo_ui_patch_entry *f = patch->entries; f; f = f->next) {
            if (f->extra < 0 || !cpymo_ui_patch_page_is(f, page)) continue;
            if (f->extra < e->extra) rank++;
        }

        if (rank == n) return e;
    }

    return NULL;
}
