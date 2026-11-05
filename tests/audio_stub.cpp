#include "runtime/audio.h"

namespace r4 {

bool AudioInit() { return true; }
void AudioShutdown() {}
bool AudioReady() { return true; }
const char* AudioBackend() { return "stub"; }
void AudioPlay(Sfx) {}
void AudioPlayClip(const unsigned char*, size_t, bool) {}
void AudioStop() {}

}  // namespace r4
