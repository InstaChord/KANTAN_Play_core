// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.
#pragma once

#include <algorithm>
#include <stdint.h>
#include <stddef.h>

namespace sampler_ns::pcm_render {

// Ordinary forward PCM playback. Scratch, seeking, filters and chopped-edge
// fades keep the general renderer. Check once per voice/block, not per sample.
template <class Voice>
inline bool supports(const Voice& v)
{
  return !v.reverse && v.playback_rate_q16 == 65536 && v.pos_fp >= 0
      && !v.seek_pending && v.seek_fade_state == 0
      && v.tone_cutoff == 127 && v.tone_resonance == 0
      && !v.edge_fade_in_end && v.edge_fade_out_start >= v.frames
      && v.volume_q8 <= 512 && v.target_volume_q8 <= 512
      && v.sustain_cache_stride == 1;
}

template <class Voice>
inline int32_t sample_at(const Voice& v, uint32_t index)
{
  if (index < v.attack_cache_frames && v.attack_cache_pcm) {
    return v.attack_cache_pcm[index];
  }
  if (v.sustain_cache_pcm && index >= v.loop_start_frame
   && index - v.loop_start_frame < v.sustain_cache_frames) {
    return v.sustain_cache_pcm[index - v.loop_start_frame];
  }
  return v.pcm[index];
}

// The source is PCM16. Both weighted terms and their sum fit int32_t,
// including -32768. A difference-first multiply overflows on steep edges.
inline int32_t interpolate(int32_t a, int32_t b, uint32_t fraction)
{
  return (a * (int32_t)(65536u - fraction) + b * (int32_t)fraction) >> 16;
}

template <class Voice, class Bus>
inline void render(Voice& v, Bus* output, size_t count, bool beat)
{
  uint32_t frame = (uint32_t)(v.pos_fp >> 16);
  uint32_t fraction = (uint32_t)v.pos_fp & 65535u;
  const uint32_t end = v.loop_end_frame ? v.loop_end_frame : v.frames;
  const uint32_t restart = v.loop_start_frame + v.loop_crossfade_frames < end
    ? v.loop_start_frame + v.loop_crossfade_frames : v.loop_start_frame;
  const uint32_t span = end - restart;
  const uint8_t divider = v.render_divider;
  const uint32_t advance = v.step_fp * divider;
  const uint32_t whole_step = advance >> 16;
  const uint32_t fractional_step = advance & 65535u;
  const uint16_t target_volume = v.target_volume_q8;
  uint16_t volume = v.volume_q8;
  uint16_t envelope = v.envelope_q15;
  uint8_t phase = v.render_phase;
  bool valid = v.render_sample_valid;
  int32_t held = v.render_sample;
  uint32_t ui_frame = v.frame_for_ui;

  for (size_t i = 0; i < count; ++i) {
    const bool render_now = !valid || divider == 1 || phase == 0;
    int32_t sample = held;
    if (render_now) {
      if (frame >= end) {
        if (!v.loop || end <= v.loop_start_frame) { v.active = false; break; }
        const uint32_t over = frame - end;
        // Normal pitch crosses once. The bounded 32-bit remainder is only
        // needed for unusually short loops at very high pitch.
        frame = restart + (over < span ? over : span ? over % span : 0);
      }
      ui_frame = frame;
      sample = sample_at(v, frame);
      if (fraction && v.linear_interpolation) {
        uint32_t next = frame + 1u;
        next = next >= end && v.loop ? std::min(restart, end - 1u)
          : next >= v.frames ? frame : next;
        sample = interpolate(sample, sample_at(v, next), fraction);
      }
      if (v.loop_crossfade_frames && frame >= end - v.loop_crossfade_frames) {
        const uint32_t offset = frame - (end - v.loop_crossfade_frames);
        const uint32_t head = v.loop_start_frame + offset;
        if (head < v.frames) {
          const uint32_t blend = (offset << 15) / v.loop_crossfade_frames;
          sample = (sample * (int32_t)(32768u - blend)
                  + sample_at(v, head) * (int32_t)blend) >> 15;
        }
      }
      held = sample;
      valid = true;
    }
    if (volume < target_volume) { ++volume; }
    else if (volume > target_volume) { --volume; }
    if (volume != 256) { sample = (sample * volume) >> 8; }
    if (!v.release_requested && v.auto_release_frames
     && --v.auto_release_frames == 0) { v.release_requested = true; }
    if (v.release_requested) {
      if (envelope <= v.release_step_q15) {
        envelope = 0;
        v.active = false;
        break;
      }
      envelope -= v.release_step_q15;
    } else if (envelope < 32768) {
      envelope = (uint16_t)std::min<uint32_t>(32768u, envelope + v.attack_step_q15);
    }
    // At <= 200% gain the product still fits int32_t, including full-scale
    // negative PCM. Preserve the original per-frame envelope and smoothing.
    if (envelope != 32768) { sample = (sample * envelope) >> 15; }
    if (beat) { output[i].beat += (int64_t)sample * 65536; }
    else { output[i].parts += (int64_t)sample * 65536; }
    if (++phase >= divider) { phase = 0; }
    if (render_now) {
      fraction += fractional_step;
      frame += whole_step + (fraction >> 16);
      fraction &= 65535u;
    }
  }
  v.pos_fp = ((int64_t)frame << 16) | fraction;
  v.frame_for_ui = ui_frame;
  v.render_phase = phase;
  v.render_sample = held;
  v.render_sample_valid = valid;
  v.volume_q8 = volume;
  v.envelope_q15 = envelope;
}

} // namespace sampler_ns::pcm_render
