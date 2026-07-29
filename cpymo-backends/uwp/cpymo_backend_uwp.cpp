#include "../../cpymo/cpymo_prelude.h"
#include "../../cpymo/cpymo_game_selector.h"
#include <string>
#include <codecvt>
#include <assert.h>
#include <SDL.h>

using namespace Windows::Storage;

template <typename T>
class maybe final {
private:
	bool is_just_;
	T value_;

public:
	maybe() : is_just_{ false } {};
	maybe(T&& v) : is_just_{ true }, value_{ std::move(v) } {};
	bool is_just() const { return is_just_; }
	T get_move_out() {
		assert(is_just_);
		is_just_ = false;
		return std::move(value_);
	}

	T& get() {
		assert(is_just_);
		return value_;
	}
};

template <typename T>
maybe<T> wait_async(Windows::Foundation::IAsyncOperation<T> ^async) {
	while(async->Status != Windows::Foundation::AsyncStatus::Completed) {
		SDL_Delay(1);
		if (async->Status == Windows::Foundation::AsyncStatus::Error) {
			return maybe<T>{};
		}
	}
	return maybe<T>{ async->GetResults() };
}

std::string w2c(Platform::String ^s) {
	std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
	return convert.to_bytes(s->Data());
}


cpymo_game_selector_item *get_game_list(const char *game_selector_dir)
{
	cpymo_game_selector_item *head = NULL, *tail = NULL;
	auto local_folder = ApplicationData::Current->LocalFolder;

	auto sub_folders = wait_async(local_folder->GetFoldersAsync());
	if (sub_folders.is_just()) {
		for (unsigned i = 0; i < sub_folders.get()->Size; ++i) {
			std::string path = w2c(sub_folders.get()->GetAt(i)->Path);

			cpymo_game_selector_item *cur = (cpymo_game_selector_item *)malloc(sizeof(cpymo_game_selector_item));
			if (cur == NULL) continue;

			memset(cur, 0, sizeof(cpymo_game_selector_item));

			char *path_c = (char *)malloc(path.size() + 1);
			if (path_c == NULL) {
				free(cur); 
				continue;
			}

			strcpy(path_c, path.c_str());
			cur->gamedir = path_c;
			cur->next = NULL;

			if (head == NULL) head = cur;
			if (tail != NULL) tail->next = cur;
			tail = cur;
		}

			
	}
	

	return head;
}

#ifdef ENABLE_TEXT_EXTRACT
#include <ppltasks.h>
#include <string>

using namespace Windows::Media::SpeechSynthesis;
using namespace Windows::Media::Playback;

/* SDL2 accessibility sound & vibration (shared with SDL2 backend) */
extern "C" {
    void cpymo_sdl2_accessibility_sound_init(void);
    void cpymo_sdl2_accessibility_sound_free(void);
}

extern "C" {

static SpeechSynthesizer^ g_speech_synthesizer = nullptr;
static MediaPlayer^ g_speech_player = nullptr;

void cpymo_backend_text_extract_init(void)
{
    g_speech_synthesizer = ref new SpeechSynthesizer();
    g_speech_player = ref new MediaPlayer();

    /* Initialize SDL2 accessibility sound system (WAV playback, aligned with Android) */
    cpymo_sdl2_accessibility_sound_init();
}

void cpymo_backend_text_extract_free(void)
{
    /* Clean up SDL2 accessibility sound system */
    cpymo_sdl2_accessibility_sound_free();

    g_speech_synthesizer = nullptr;
    g_speech_player = nullptr;
}

void cpymo_backend_text_extract(const char *text)
{
    if (text == NULL || text[0] == '\0') return;
    if (g_speech_synthesizer == nullptr) return;

    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    std::wstring wide = convert.from_bytes(text);
    auto str = ref new Platform::String(wide.c_str());

    concurrency::create_task(g_speech_synthesizer->SynthesizeTextToStreamAsync(str))
        .then([](SpeechSynthesisStream^ stream) {
            if (stream && g_speech_player) {
                g_speech_player->Source = MediaSource::CreateFromStream(stream, stream->ContentType);
                g_speech_player->Play();
            }
        });
}

} // extern "C"
#endif
