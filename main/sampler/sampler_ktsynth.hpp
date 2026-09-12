// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_KTSYNTH_HPP
#define KANTAN_SAMPLER_KTSYNTH_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sampler_wav.hpp"

namespace sampler_ns {

enum class ktsynth_sustain_mode_t : uint8_t {
  off = 0,
  loop = 1,
};

struct ktsynth_info_t {
  wav_info_t wav = {};
  const uint8_t* metadata = nullptr;
  uint32_t metadata_bytes = 0;
  const uint8_t* name = nullptr;
  uint16_t name_bytes = 0;
  uint32_t start_frame = 0;
  uint32_t end_frame = 0;
  uint32_t loop_start_frame = 0;
  uint32_t loop_end_frame = 0;
  uint32_t loop_crossfade_frames = 0;
  uint16_t attack_ms = 0;
  uint16_t release_ms = 120;
  int16_t tune_cents = 0;
  uint16_t default_gain_q8 = 256;
  uint8_t root_note = 60;
  ktsynth_sustain_mode_t sustain_mode = ktsynth_sustain_mode_t::off;
};

static inline uint16_t ktsynth_read_u16(const uint8_t* p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline int16_t ktsynth_read_i16(const uint8_t* p)
{
  return (int16_t)ktsynth_read_u16(p);
}

static inline uint32_t ktsynth_read_u32(const uint8_t* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
      | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t ktsynth_crc32_update(uint32_t crc, const uint8_t* data,
                                            size_t size, size_t zero_begin = SIZE_MAX,
                                            size_t zero_end = SIZE_MAX)
{
  for (size_t i = 0; i < size; ++i) {
    uint8_t value = (i >= zero_begin && i < zero_end) ? 0 : data[i];
    crc ^= value;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return crc;
}

// .ktsynth is a RIFF/WAVE file with a required fixed binary KNTN chunk.
// Coordinates use source PCM frames and end positions are exclusive.
static inline bool parse_ktsynth(const uint8_t* data, size_t size, ktsynth_info_t* out)
{
  static constexpr uint16_t fixed_header_bytes = 64;
  static constexpr uint32_t maximum_file_bytes = 2u * 1024u * 1024u;
  if (!out || !data || size < 44 || size > maximum_file_bytes) { return false; }

  wav_info_t wav;
  if (!parse_wav(data, size, &wav)
   || wav.audio_format != 1 || wav.channels != 1 || wav.bits_per_sample != 16
   || wav.sample_rate < 8000 || wav.sample_rate > 48000) {
    return false;
  }

  const uint8_t* metadata = nullptr;
  uint32_t metadata_bytes = 0;
  const uint8_t* pcm = nullptr;
  uint32_t pcm_bytes = 0;
  for (size_t pos = 12; pos + 8 <= size;) {
    const uint8_t* chunk = data + pos;
    const uint32_t chunk_bytes = ktsynth_read_u32(chunk + 4);
    const size_t body_pos = pos + 8;
    if (body_pos > size || chunk_bytes > size - body_pos) { return false; }
    if (!memcmp(chunk, "KNTN", 4)) {
      if (metadata) { return false; }
      metadata = data + body_pos;
      metadata_bytes = chunk_bytes;
    } else if (!memcmp(chunk, "data", 4)) {
      if (pcm) { return false; }
      pcm = data + body_pos;
      pcm_bytes = chunk_bytes;
    }
    const size_t advance = 8u + (size_t)chunk_bytes + (chunk_bytes & 1u);
    if (advance > size - pos) { return false; }
    pos += advance;
  }
  if (!metadata || metadata_bytes < fixed_header_bytes || !pcm
   || pcm != wav.pcm || pcm_bytes != wav.frames * sizeof(int16_t)) {
    return false;
  }
  if (memcmp(metadata, "KTS1", 4) != 0
   || ktsynth_read_u16(metadata + 4) != 1) {
    return false;
  }
  const uint16_t header_bytes = ktsynth_read_u16(metadata + 8);
  const uint16_t name_bytes = ktsynth_read_u16(metadata + 10);
  if (header_bytes < fixed_header_bytes || header_bytes > metadata_bytes
   || name_bytes > 63 || (uint32_t)header_bytes + name_bytes > metadata_bytes) {
    return false;
  }

  const uint32_t sample_rate = ktsynth_read_u32(metadata + 16);
  const uint32_t frame_count = ktsynth_read_u32(metadata + 20);
  const uint32_t start_frame = ktsynth_read_u32(metadata + 24);
  const uint32_t end_frame = ktsynth_read_u32(metadata + 28);
  const uint32_t loop_start = ktsynth_read_u32(metadata + 32);
  const uint32_t loop_end = ktsynth_read_u32(metadata + 36);
  const uint32_t crossfade = ktsynth_read_u32(metadata + 40);
  const uint32_t stored_crc = ktsynth_read_u32(metadata + 44);
  const uint16_t attack_ms = ktsynth_read_u16(metadata + 48);
  const uint16_t release_ms = ktsynth_read_u16(metadata + 50);
  const int16_t tune_cents = ktsynth_read_i16(metadata + 52);
  const uint16_t gain_q8 = ktsynth_read_u16(metadata + 54);
  const uint8_t root_note = metadata[56];
  const uint8_t sustain_mode = metadata[57];

  if (sample_rate != wav.sample_rate || frame_count != wav.frames
   || start_frame >= end_frame || end_frame > frame_count
   || root_note > 127 || sustain_mode > 1
   || tune_cents < -100 || tune_cents > 100
   || attack_ms > 5000 || release_ms < 10 || release_ms > 2000
   || gain_q8 > 512) {
    return false;
  }
  if (sustain_mode == (uint8_t)ktsynth_sustain_mode_t::loop) {
    if (loop_start < start_frame || loop_start >= loop_end || loop_end > end_frame
     || crossfade > 65535u || crossfade > (loop_end - loop_start) / 4u) {
      return false;
    }
  } else if (loop_start || loop_end || crossfade) {
    return false;
  }

  uint32_t crc = 0xFFFFFFFFu;
  crc = ktsynth_crc32_update(crc, metadata, metadata_bytes, 44, 48);
  crc = ktsynth_crc32_update(crc, pcm, pcm_bytes);
  crc ^= 0xFFFFFFFFu;
  if (crc != stored_crc) { return false; }

  *out = {};
  out->wav = wav;
  out->metadata = metadata;
  out->metadata_bytes = metadata_bytes;
  out->name = metadata + header_bytes;
  out->name_bytes = name_bytes;
  out->start_frame = start_frame;
  out->end_frame = end_frame;
  out->loop_start_frame = loop_start;
  out->loop_end_frame = loop_end;
  out->loop_crossfade_frames = crossfade;
  out->attack_ms = attack_ms;
  out->release_ms = release_ms;
  out->tune_cents = tune_cents;
  out->default_gain_q8 = gain_q8;
  out->root_note = root_note;
  out->sustain_mode = (ktsynth_sustain_mode_t)sustain_mode;
  return true;
}

} // namespace sampler_ns

#endif
