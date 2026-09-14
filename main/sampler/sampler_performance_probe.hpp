// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_PERFORMANCE_PROBE_HPP
#define KANTAN_SAMPLER_PERFORMANCE_PROBE_HPP

#include <stdint.h>

namespace sampler_ns::performance_probe {

enum class metric_t : uint8_t {
  input_press_queue,
  input_release_queue,
  live_note_on_to_audio,
  live_note_off_to_audio,
  rec_note_on_to_audio,
  rec_note_off_to_audio,
  rec_event_store,
  rec_dispatch_batch,
  voice_allocation,
  audio_block,
  audio_1_2_voices,
  audio_3_4_voices,
  audio_5_6_voices,
  audio_7_plus_voices,
  count,
};

enum class counter_t : uint8_t {
  audio_deadline_miss,
  i2s_read_error,
  i2s_write_error,
  count,
};

#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
void record(metric_t metric, uint32_t usec);
void count(counter_t counter);
void recordInputEdge(bool pressed, uint32_t event_msec);
uint32_t clockUsec(void);
uint32_t eventUsec(uint32_t event_msec);
void reportIfDue(uint32_t now_msec);
#else
inline void record(metric_t, uint32_t) {}
inline void count(counter_t) {}
inline void recordInputEdge(bool, uint32_t) {}
inline uint32_t clockUsec(void) { return 0; }
inline uint32_t eventUsec(uint32_t) { return 0; }
inline void reportIfDue(uint32_t) {}
#endif

} // namespace sampler_ns::performance_probe

#endif
