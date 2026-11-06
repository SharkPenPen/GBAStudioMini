// runtime/audio_psp.cpp
//
// PSP 音频占位实现。
//
// 第一版先保证 runtime/audio.h 的链接完整性；
// 后续接 sceAudio 时只替换本文件，不改 Game。
#include "runtime/audio.h"

namespace r4 {

bool AudioInit() { return false; }
void AudioShutdown() {}
bool AudioReady() { return false; }
const char* AudioBackend() { return "psp-silent"; }
void AudioPlay(Sfx sfx) { (void)sfx; }
void AudioPlayClip(const unsigned char* data, size_t size, bool loop) {
  (void)data;
  (void)size;
  (void)loop;
}
void AudioStop() {}

}  // namespace r4
