// runtime/audio_miniaudio.cpp
// PC 音频后端：miniaudio 混音 + WAV/OGG 解码（stb_vorbis）。
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "runtime/audio.h"

#include <vector>

namespace r4 {
namespace {

ma_engine g_engine;
bool g_ready = false;

struct Voice {
  ma_sound sound;
  ma_decoder decoder;
};

std::vector<Voice*> g_one_shots;
Voice* g_bgm = nullptr;

Voice* NewVoice(const unsigned char* data, size_t size) {
  Voice* v = new Voice();
  if (ma_decoder_init_memory(data, size, nullptr, &v->decoder) != MA_SUCCESS) {
    delete v;
    return nullptr;
  }
  if (ma_sound_init_from_data_source(&g_engine, &v->decoder, 0, nullptr, &v->sound) !=
      MA_SUCCESS) {
    ma_decoder_uninit(&v->decoder);
    delete v;
    return nullptr;
  }
  return v;
}

void FreeVoice(Voice* v) {
  if (v == nullptr) return;
  ma_sound_uninit(&v->sound);
  ma_decoder_uninit(&v->decoder);
  delete v;
}

}  // namespace

bool AudioInit() {
  if (ma_engine_init(nullptr, &g_engine) != MA_SUCCESS) {
    g_ready = false;
    return false;
  }
  g_ready = true;
  return true;
}

void AudioShutdown() {
  AudioStop();
  if (g_ready) ma_engine_uninit(&g_engine);
  g_ready = false;
}

bool AudioReady() { return g_ready; }

const char* AudioBackend() { return g_ready ? "miniaudio" : "silent"; }

void AudioPlay(Sfx) {}

void AudioPlayClip(const unsigned char* data, size_t size, bool loop) {
  if (!g_ready || data == nullptr || size == 0) return;
  for (size_t i = 0; i < g_one_shots.size();) {
    if (ma_sound_at_end(&g_one_shots[i]->sound)) {
      FreeVoice(g_one_shots[i]);
      g_one_shots.erase(g_one_shots.begin() + static_cast<long>(i));
    } else {
      ++i;
    }
  }
  Voice* v = NewVoice(data, size);
  if (v == nullptr) return;
  if (loop) {
    if (g_bgm != nullptr) FreeVoice(g_bgm);
    ma_sound_set_looping(&v->sound, MA_TRUE);
    ma_sound_start(&v->sound);
    g_bgm = v;
  } else {
    ma_sound_start(&v->sound);
    g_one_shots.push_back(v);
  }
}

void AudioStop() {
  if (g_bgm != nullptr) {
    FreeVoice(g_bgm);
    g_bgm = nullptr;
  }
  for (Voice* v : g_one_shots) FreeVoice(v);
  g_one_shots.clear();
}

}  // namespace r4
