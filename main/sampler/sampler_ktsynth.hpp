// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_KTSYNTH_HPP
#define KANTAN_SAMPLER_KTSYNTH_HPP

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "sampler_wav.hpp"

namespace sampler_ns {

enum class ktsynth_sustain_mode_t : uint8_t { off = 0, loop = 1 };

struct ktsynth_layer_info_t {
  const int16_t* pcm = nullptr;
  uint32_t pcm_bytes = 0;
  uint32_t sample_rate = 0;
  uint32_t frame_count = 0;
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

struct ktsynth_info_t {
  static constexpr uint8_t maximum_layers = 2;
  wav_info_t wav = {};
  ktsynth_layer_info_t layer[maximum_layers] = {};
  uint8_t layer_count = 0;
  const uint8_t* metadata = nullptr;
  uint32_t metadata_bytes = 0;
  const uint8_t* name = nullptr;
  uint16_t name_bytes = 0;

  // Layer 1 aliases preserve the existing single-layer playback call sites.
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
    const uint8_t value = (i >= zero_begin && i < zero_end) ? 0 : data[i];
    crc ^= value;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return crc;
}

// KTS2 is the sole KANTAN Synth format. Layer 1 uses WAVE data and optional
// Layer 2 uses KT2D. KNTN contains a 128-byte header plus its UTF-8 name.
static inline bool parse_ktsynth(const uint8_t* data, size_t size, ktsynth_info_t* out)
{
  static constexpr uint16_t fixed_header_bytes = 128;
  static constexpr uint16_t descriptor_bytes = 48;
  static constexpr uint16_t descriptor_offset = 24;
  static constexpr uint32_t maximum_file_bytes = 2u * 1024u * 1024u;
  if (!out || !data || size < 44 || size > maximum_file_bytes) { return false; }

  // Validate the full envelope before parse_wav() examines untrusted chunks.
  if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)
   || (uint64_t)ktsynth_read_u32(data + 4) + 8u != size) {
    return false;
  }
  size_t preflight_pos = 12;
  while (preflight_pos + 8 <= size) {
    const uint32_t chunk_bytes = ktsynth_read_u32(data + preflight_pos + 4);
    const size_t body_pos = preflight_pos + 8;
    if (chunk_bytes > size - body_pos) { return false; }
    const size_t advance = 8u + (size_t)chunk_bytes + (chunk_bytes & 1u);
    if (advance > size - preflight_pos) { return false; }
    preflight_pos += advance;
  }
  if (preflight_pos != size) { return false; }

  wav_info_t wav;
  if (!parse_wav(data, size, &wav) || wav.audio_format != 1 || wav.channels != 1
   || wav.bits_per_sample != 16 || wav.sample_rate < 8000 || wav.sample_rate > 48000) {
    return false;
  }

  const uint8_t* metadata = nullptr;
  uint32_t metadata_bytes = 0;
  const uint8_t* pcm[2] = { reinterpret_cast<const uint8_t*>(wav.pcm), nullptr };
  uint32_t pcm_bytes[2] = { wav.frames * (uint32_t)sizeof(int16_t), 0 };
  uint8_t data_chunks = 0;
  for (size_t pos = 12; pos + 8 <= size;) {
    const uint8_t* chunk = data + pos;
    const uint32_t chunk_bytes = ktsynth_read_u32(chunk + 4);
    const size_t body_pos = pos + 8;
    if (!memcmp(chunk, "KNTN", 4)) {
      if (metadata) { return false; }
      metadata = data + body_pos;
      metadata_bytes = chunk_bytes;
    } else if (!memcmp(chunk, "data", 4)) {
      if (++data_chunks != 1 || data + body_pos != pcm[0] || chunk_bytes != pcm_bytes[0]) {
        return false;
      }
    } else if (!memcmp(chunk, "KT2D", 4)) {
      if (pcm[1]) { return false; }
      pcm[1] = data + body_pos;
      pcm_bytes[1] = chunk_bytes;
    }
    pos += 8u + (size_t)chunk_bytes + (chunk_bytes & 1u);
  }
  if (!metadata || metadata_bytes < fixed_header_bytes || data_chunks != 1
   || memcmp(metadata, "KTS2", 4) || ktsynth_read_u16(metadata + 4) != 2) {
    return false;
  }
  const uint16_t header_bytes = ktsynth_read_u16(metadata + 8);
  const uint16_t name_bytes = ktsynth_read_u16(metadata + 10);
  const uint32_t stored_crc = ktsynth_read_u32(metadata + 16);
  const uint8_t layer_count = metadata[20];
  if (header_bytes < fixed_header_bytes || header_bytes > metadata_bytes
   || name_bytes > 63 || (uint32_t)header_bytes + name_bytes > metadata_bytes
   || layer_count < 1 || layer_count > 2 || (layer_count == 2) != (pcm[1] != nullptr)) {
    return false;
  }

  ktsynth_info_t parsed = {};
  parsed.wav = wav;
  parsed.layer_count = layer_count;
  parsed.metadata = metadata;
  parsed.metadata_bytes = metadata_bytes;
  parsed.name = metadata + header_bytes;
  parsed.name_bytes = name_bytes;
  uint32_t gain_sum_q8 = 0;
  for (uint8_t index = 0; index < layer_count; ++index) {
    const uint8_t* d = metadata + descriptor_offset + index * descriptor_bytes;
    auto& layer = parsed.layer[index];
    layer.pcm = reinterpret_cast<const int16_t*>(pcm[index]);
    layer.pcm_bytes = pcm_bytes[index];
    layer.sample_rate = ktsynth_read_u32(d + 0);
    layer.frame_count = ktsynth_read_u32(d + 4);
    layer.start_frame = ktsynth_read_u32(d + 8);
    layer.end_frame = ktsynth_read_u32(d + 12);
    layer.loop_start_frame = ktsynth_read_u32(d + 16);
    layer.loop_end_frame = ktsynth_read_u32(d + 20);
    layer.loop_crossfade_frames = ktsynth_read_u32(d + 24);
    layer.attack_ms = ktsynth_read_u16(d + 28);
    layer.release_ms = ktsynth_read_u16(d + 30);
    layer.tune_cents = ktsynth_read_i16(d + 32);
    layer.default_gain_q8 = ktsynth_read_u16(d + 34);
    layer.root_note = d[36];
    const uint8_t sustain_mode = d[37];
    const uint8_t pcm_chunk = d[38];
    if (!layer.pcm || (layer.pcm_bytes & 1u) || layer.sample_rate < 8000
     || layer.sample_rate > 48000 || layer.frame_count != layer.pcm_bytes / 2u
     || layer.frame_count > layer.sample_rate * 20u
     || layer.start_frame >= layer.end_frame || layer.end_frame > layer.frame_count
     || layer.root_note > 127 || sustain_mode > 1 || pcm_chunk != index
     || layer.tune_cents < -100 || layer.tune_cents > 100
     || layer.attack_ms > 5000 || layer.release_ms < 10 || layer.release_ms > 2000
     || layer.default_gain_q8 > 512) {
      return false;
    }
    if (index == 0 && (layer.sample_rate != wav.sample_rate
                    || layer.frame_count != wav.frames)) { return false; }
    if (sustain_mode == (uint8_t)ktsynth_sustain_mode_t::loop) {
      if (layer.loop_start_frame < layer.start_frame
       || layer.loop_start_frame >= layer.loop_end_frame
       || layer.loop_end_frame > layer.end_frame
       || layer.loop_crossfade_frames > 65535u
       || layer.loop_crossfade_frames > (layer.loop_end_frame - layer.loop_start_frame) / 4u) {
        return false;
      }
    } else if (layer.loop_start_frame || layer.loop_end_frame || layer.loop_crossfade_frames) {
      return false;
    }
    layer.sustain_mode = (ktsynth_sustain_mode_t)sustain_mode;
    gain_sum_q8 += layer.default_gain_q8;
  }
  if (gain_sum_q8 > 512) { return false; }

  uint32_t crc = 0xFFFFFFFFu;
  crc = ktsynth_crc32_update(crc, metadata, metadata_bytes, 16, 20);
  for (uint8_t index = 0; index < layer_count; ++index) {
    crc = ktsynth_crc32_update(crc, pcm[index], pcm_bytes[index]);
  }
  crc ^= 0xFFFFFFFFu;
  if (crc != stored_crc) { return false; }

  const auto& primary = parsed.layer[0];
  parsed.start_frame = primary.start_frame;
  parsed.end_frame = primary.end_frame;
  parsed.loop_start_frame = primary.loop_start_frame;
  parsed.loop_end_frame = primary.loop_end_frame;
  parsed.loop_crossfade_frames = primary.loop_crossfade_frames;
  parsed.attack_ms = primary.attack_ms;
  parsed.release_ms = primary.release_ms;
  parsed.tune_cents = primary.tune_cents;
  parsed.default_gain_q8 = primary.default_gain_q8;
  parsed.root_note = primary.root_note;
  parsed.sustain_mode = primary.sustain_mode;
  *out = parsed;
  return true;
}

} // namespace sampler_ns
#endif
