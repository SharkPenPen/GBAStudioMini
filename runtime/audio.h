// runtime/audio.h
// 最小音效接口；PC 用 miniaudio。
#pragma once

#include <cstddef>

namespace r4 {

enum class Sfx : int {
  kCoin = 0,
  kHurt,
  kClear,
  kJump,
  kTransition,
  kCount
};

bool AudioInit();
void AudioShutdown();
bool AudioReady();
const char* AudioBackend();
void AudioPlay(Sfx sfx);
void AudioPlayClip(const unsigned char* data, size_t size, bool loop);
void AudioStop();

}  // namespace r4
