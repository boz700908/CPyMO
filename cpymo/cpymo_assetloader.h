#ifndef INCLUDE_CPYMO_ASSETLOADER
#define INCLUDE_CPYMO_ASSETLOADER

#include "cpymo_package.h"
#include "cpymo_gameconfig.h"
#include <cpymo_backend_image.h>
#include <cpymo_backend_masktrans.h>
#include "cpymo_parser.h"
#include <stddef.h>

typedef struct {
	bool use_pkg_bg, use_pkg_chara, use_pkg_se, use_pkg_voice;
	cpymo_package pkg_bg, pkg_chara, pkg_se, pkg_voice;
	const cpymo_gameconfig *game_config;
	const char *gamedir;
} cpymo_assetloader;

error_t cpymo_assetloader_init(cpymo_assetloader *out, const cpymo_gameconfig *config, const char *gamedir);
void cpymo_assetloader_free(cpymo_assetloader *loader);

error_t cpymo_assetloader_load_bg_pixels(void **px, int *w, int *h, cpymo_parser_stream_span name, const cpymo_assetloader *l);
error_t cpymo_assetloader_load_bg_image(cpymo_backend_image *img, int *w, int *h, cpymo_parser_stream_span name, const cpymo_assetloader *loader);
error_t cpymo_assetloader_load_chara_image(cpymo_backend_image *img, int *w, int *h, cpymo_parser_stream_span name, const cpymo_assetloader *loader);
error_t cpymo_assetloader_load_script(char **out_buffer, size_t *buf_size, const char *script_name, const cpymo_assetloader *loader);
error_t cpymo_assetloader_load_system_masktrans(cpymo_backend_masktrans *out, cpymo_parser_stream_span name, const cpymo_assetloader *loader);

error_t cpymo_assetloader_get_bgm_path(char **out_str, cpymo_parser_stream_span bgm_name, const cpymo_assetloader *loader);
error_t cpymo_assetloader_get_se_path(char **out_str, cpymo_parser_stream_span se_name, const cpymo_assetloader *l);
error_t cpymo_assetloader_get_vo_path(char **out_str, cpymo_parser_stream_span vo_name, const cpymo_assetloader *l);
error_t cpymo_assetloader_get_video_path(char **out_str, cpymo_parser_stream_span movie_name, const cpymo_assetloader *l);

error_t cpymo_assetloader_load_system_image(
	cpymo_backend_image *out_image, 
	int *w, int *h,
	cpymo_parser_stream_span asset_name, 
	const cpymo_assetloader *loader);

#endif