// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#pragma once

#include <stdint.h>

namespace sampler_ns::sampler_amy_engine {

#if defined(KANPLAY_AMY_INTEGRATION) && !defined(M5UNIFIED_PC_BUILD)
bool start();
bool handlesMidi(uint8_t status);
bool sendMidi(uint8_t status, uint8_t data1, uint8_t data2);
bool readFrame(int32_t* left, int32_t* right);
bool hasActiveVoices();
uint32_t underrunCount();
void setRadioConnectionPaused(bool paused);
bool radioConnectionPaused();
static constexpr uint8_t tone_count = 8;
const char* toneName(uint8_t part, uint8_t tone);
bool previewTone(uint8_t part, uint8_t tone, uint8_t note, uint8_t velocity);
void stopPreview();
// Expose AMY's compact, built-in PCM one-shots to the existing Beat voice
// path. The returned storage is immutable flash data owned by AMY.
bool pcmSample(uint8_t preset, const int16_t** data, uint32_t* frames,
               uint32_t* sample_rate);
#else
inline bool start() { return true; }
inline bool handlesMidi(uint8_t) { return false; }
inline bool sendMidi(uint8_t, uint8_t, uint8_t) { return false; }
inline bool readFrame(int32_t* left, int32_t* right) {
  if (left) { *left = 0; }
  if (right) { *right = 0; }
  return false;
}
inline bool hasActiveVoices() { return false; }
inline uint32_t underrunCount() { return 0; }
inline void setRadioConnectionPaused(bool) {}
inline bool radioConnectionPaused() { return false; }
static constexpr uint8_t tone_count = 0;
inline const char* toneName(uint8_t, uint8_t) { return ""; }
inline bool previewTone(uint8_t, uint8_t, uint8_t, uint8_t) { return false; }
inline void stopPreview() {}
inline bool pcmSample(uint8_t, const int16_t**, uint32_t*, uint32_t*) { return false; }
#endif

} // namespace sampler_ns::sampler_amy_engine
