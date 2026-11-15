#include "cpymo_ui_extra.h"
#include "cpymo_engine.h"
#include "../cpymo-backends/include/cpymo_backend_text.h"

void cpymo_ui_extra_draw_text(const char *text, float x, float y, float char_size)
{
    if (text == NULL || *text == '\0') return;

    cpymo_backend_text t;
    float w;
    if (cpymo_backend_text_create(
            &t, &w, cpymo_str_pure(text), char_size) != CPYMO_ERR_SUCC)
        return;

    cpymo_backend_text_draw(
        t, x, y + char_size, cpymo_color_white, 1.0f,
        cpymo_backend_image_draw_type_ui_element);

    cpymo_backend_text_free(t);
}
