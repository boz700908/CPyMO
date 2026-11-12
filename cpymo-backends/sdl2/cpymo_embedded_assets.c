#include "cpymo_embedded_assets.h"

#include <string.h>

#ifndef CPYMO_EMBEDDED_ASSETS
/* 没有资源被编进来时（iOS、Switch 等仍从文件或 bundle 读取）提供空表。 */
const struct cpymo_embedded_asset_entry cpymo_embedded_asset_table[] = {
    { NULL, NULL, 0 }
};
const unsigned cpymo_embedded_asset_count = 0;
#endif

SDL_AudioSpec *cpymo_embedded_load_wav(
    const char *name, SDL_AudioSpec *spec, Uint8 **buf, Uint32 *len)
{
    unsigned i;

    if (name == NULL || spec == NULL || buf == NULL || len == NULL) return NULL;

    for (i = 0; i < cpymo_embedded_asset_count; ++i) {
        const struct cpymo_embedded_asset_entry *e = &cpymo_embedded_asset_table[i];
        SDL_RWops *rw;

        if (e->name == NULL || e->data == NULL) continue;
        if (strcmp(e->name, name) != 0) continue;

        rw = SDL_RWFromConstMem(e->data, (int)e->len);
        if (rw == NULL) return NULL;
        return SDL_LoadWAV_RW(rw, 1, spec, buf, len);
    }

    return NULL;
}
