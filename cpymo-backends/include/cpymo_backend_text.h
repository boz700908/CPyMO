#ifndef INCLUDE_CPYMO_BACKEND_TEXT
#define INCLUDE_CPYMO_BACKEND_TEXT

#include "../../cpymo/cpymo_error.h"
#include "../../cpymo/cpymo_color.h"
#include "cpymo_backend_image.h"
#include "../../cpymo/cpymo_parser.h"

typedef void *cpymo_backend_text;

error_t cpymo_backend_text_create(
    cpymo_backend_text *out, 
    float *out_width,
    cpymo_str utf8_string, 
    float single_character_size_in_logical_screen);

void cpymo_backend_text_free(cpymo_backend_text);

void cpymo_backend_text_draw(
    cpymo_backend_text,
    float x, float y_baseline,
    cpymo_color col, float alpha,
    enum cpymo_backend_image_draw_type draw_type);

float cpymo_backend_text_width(
    cpymo_str,
    float single_character_size_in_logical_screen);

#ifdef ENABLE_TEXT_EXTRACT
void cpymo_backend_text_extract_init(void);
void cpymo_backend_text_extract_free(void);
void cpymo_backend_text_extract(const char *text);

/* 平台层：立即打断当前朗读（各后端自己实现，没有的留空） */
void cpymo_backend_text_stop(void);

/* 引擎层朗读队列（打断与排队规则 R1-R7，见 README）。
 * 引擎里所有"要念出来"的地方都用 cpymo_a11y_speak()，不要直接调
 * cpymo_backend_text_extract()：后者是平台原语，不排队也不判打断。 */
void cpymo_a11y_speak(const char *text);
void cpymo_a11y_speech_tick(void);
void cpymo_a11y_speech_note_input(void);
void cpymo_a11y_speech_clear(void);
void cpymo_backend_text_copy_last(void);
void cpymo_backend_text_append_copy_last(void);
#else
#define cpymo_backend_text_extract_init()
#define cpymo_backend_text_extract_free()
#define cpymo_backend_text_extract(A)
#define cpymo_backend_text_stop()
#define cpymo_a11y_speak(A)
#define cpymo_a11y_speech_tick()
#define cpymo_a11y_speech_note_input()
#define cpymo_a11y_speech_clear()
#define cpymo_backend_text_copy_last()
#define cpymo_backend_text_append_copy_last()
#endif

#endif
