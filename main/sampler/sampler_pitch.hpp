// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_PITCH_HPP
#define KANTAN_SAMPLER_PITCH_HPP

#include <stdint.h>
#include <limits.h>

namespace sampler_ns::sampler_pitch {

// One octave of equal-tempered ratios in Q16. The remaining octaves are
// exact binary shifts, so every MIDI note remains distinct even when the
// authored PCM root is near either end of the 0-127 note range.
static constexpr uint32_t semitone_ratio_q16[12] = {
  65536, 69433, 73562, 77936, 82570, 87480,
  92682, 98199, 104032, 110218, 116772, 123715,
};

inline uint32_t note_ratio_q16(uint8_t root_note, uint8_t note)
{
  int delta = (int)note - (int)root_note;
  int octave = delta / 12;
  int semitone = delta % 12;
  if (semitone < 0) {
    semitone += 12;
    --octave;
  }
  uint64_t ratio = semitone_ratio_q16[semitone];
  if (octave >= 0) {
    ratio <<= octave;
  } else {
    const unsigned shift = (unsigned)-octave;
    ratio = (ratio + (1ull << (shift - 1))) >> shift;
  }
  if (ratio < 1) { return 1; }
  return ratio > UINT32_MAX ? UINT32_MAX : (uint32_t)ratio;
}

inline uint32_t note_pitch_q16(uint16_t base_pitch_q8, uint8_t root_note, uint8_t note)
{
  const uint64_t scaled = (uint64_t)note_ratio_q16(root_note, note) * base_pitch_q8;
  const uint64_t rounded = (scaled + 128u) >> 8;
  if (rounded < 1) { return 1; }
  return rounded > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded;
}

} // namespace sampler_ns::sampler_pitch

#endif
