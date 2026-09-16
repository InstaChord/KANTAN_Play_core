#!/usr/bin/env python3
"""Compare the production PCM block renderer with the pre-change renderer.

Builds an isolated C++ harness from the actual firmware source; no SDL,
ESP32, audio device or firmware upload is required. Host timings compare
algorithms only, not CoreS3 latency or PSRAM performance.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    while source.find(";", start, brace) != -1:
        start = source.index(signature, start + len(signature))
        brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def renderer(source, name, optimized):
    voice = source[source.index("struct voice_t {"):source.index("static voice_t voices")]
    if not optimized:
        voice = voice.replace(
            "  uint16_t attack_step_q15 = 0;\n  uint16_t release_step_q15 = 0;\n",
            "  uint16_t attack_step_q15 = 0;\n  uint16_t release_step_q15 = 0;\n"
            "  uint16_t sustain_level_q15 = 32768;\n"
            "  uint32_t envelope_level_q16 = 32768u << 16;\n"
            "  uint32_t attack_step_q16 = 0;\n  uint32_t decay_step_q16 = 0;\n"
            "  uint32_t release_step_q16 = 0;\n  uint32_t envelope_phase_frames = 0;\n"
            "  uint32_t attack_frames_total = 1;\n  uint32_t hold_frames_total = 0;\n"
            "  uint32_t decay_frames_total = 1;\n  uint32_t release_frames_total = 1;\n"
            "  uint8_t envelope_phase = 4;\n")
    bus = source[source.index("struct mixed_buses_t {"):source.index("static inline", source.index("struct mixed_buses_t {"))]
    voice_storage = ("static voice_t voices[39]; static volatile uint32_t active_voice_mask[2];"
                     if optimized else
                     "static voice_t voices[30]; static volatile uint32_t active_voice_mask;")
    code = [f"namespace {name} {{", "namespace pcm_render = sampler_ns::pcm_render;", voice,
            voice_storage,
            function(source, "static inline void activate_voice("),
            function(source, "static inline void deactivate_voice("),
            function(source, "static inline int16_t voice_pcm_at("), bus]
    mix = function(source, "static inline mixed_buses_t mix_voices(")
    # Correct the old interpolation overflow in the oracle as well. This is
    # separately tested with full-scale discontinuities below.
    mix = mix.replace("s += ((s1 - s) * (int32_t)frac) >> 16;",
                      "s += (int32_t)(((int64_t)(s1 - s) * frac) >> 16);")
    if optimized:
        code += [function(source, "static inline void record_voice_probe("), mix,
                 "static mixed_buses_t mixed_block[48];",
                 function(source, "static void mix_voice_block(")]
    else:
        code += [mix, "static mixed_buses_t mixed_block[48]; void mix_voice_block() { for (auto& frame : mixed_block) frame = mix_voices(); }"]
    code.append("}")
    return "\n".join(code)


HARNESS = r'''
static int16_t pcm[3][8192];
static volatile uint64_t benchmark_checksum = 0;
static uint32_t seed = 0x13579;
static uint32_t random32() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
template <class Voice> void configure(Voice& v, uint32_t config, unsigned index) {
  v = {};
  v.pcm = pcm[index % 3]; v.frames = 8192;
  v.loop = (config & 1) != 0;
  v.loop_start_frame = 64 + (config % 2000);
  v.loop_end_frame = v.loop_start_frame + 32 + (config % 3072);
  if (!v.loop) { v.loop_end_frame = v.frames; }
  v.loop_crossfade_frames = v.loop ? (config >> 2) % 8 : 0;
  v.step_fp = 16384 + (config % 500000);
  v.pos_fp = (int64_t)((config >> 5) % 8192) << 16;
  v.volume_q8 = config % 513; v.target_volume_q8 = (config >> 5) % 513;
  v.render_divider = config & 2 ? 2 : 1;
  v.linear_interpolation = config & 4;
  // The historical oracle has no DAHDSR state. Keep regression/benchmark
  // voices at full sustain; the new envelope phases are tested separately.
  v.envelope_q15 = 32768;
  v.envelope_level_q16 = 32768u << 16;
  v.attack_step_q16 = (7 + config % 300) << 16;
  v.release_step_q16 = 0;
  v.decay_step_q16 = 0; v.sustain_level_q15 = 32768;
  v.envelope_phase_frames = 100000; v.attack_frames_total = 100000;
  v.hold_frames_total = 0; v.decay_frames_total = 1;
  v.release_frames_total = 100 + config % 500;
  v.envelope_phase = v.envelope_q15 < 32768 ? sampler_ns::pcm_render::envelope_attack
                                             : sampler_ns::pcm_render::envelope_sustain;
  v.auto_release_frames = 0;
  v.fx_target = index % 3 == 0 ? 1 : 2;
  v.active = true;
  if (config & 32) {
    v.attack_cache_pcm = v.pcm; v.attack_cache_frames = v.loop_start_frame;
    v.sustain_cache_pcm = v.pcm + v.loop_start_frame;
    v.sustain_cache_frames = v.loop_end_frame - v.loop_start_frame;
  }
  // Exercise fallback alongside ordinary synth voices in the same block.
  if (config & 64) { v.reverse = true; v.attack_cache_pcm = v.sustain_cache_pcm = nullptr; }
  if (config & 128) { v.tone_cutoff = 78; v.tone_resonance = 31; }
  if (config & 256) { v.seek_frame = 100; v.seek_pending = true; }
  if (config & 512) { v.edge_fade_in_end = 150; v.edge_fade_out_start = 7800; }
  if (config & 1024) { v.playback_rate_q16 = -35000; }
}
int main() {
  for (unsigned bank = 0; bank < 3; ++bank) for (unsigned i = 0; i < 8192; ++i) {
    pcm[bank][i] = (int16_t)((int)((i * (97 + bank * 200)) % 65536) - 32768);
  }
  for (unsigned trial = 0; trial < 150; ++trial) {
    reference::active_voice_mask = 0;
    current::active_voice_mask[0] = current::active_voice_mask[1] = 0;
    const unsigned count = 1 + trial % 30;
    for (unsigned v = 0; v < count; ++v) {
      uint32_t config = random32();
      if (trial < 80) { config &= ~(64u | 128u | 256u | 512u | 1024u); }
      configure(reference::voices[v], config, v);
      configure(current::voices[v], config, v);
      reference::activate_voice(v); current::activate_voice(v);
    }
    for (unsigned block = 0; block < 100; ++block) {
      if (block == 50) for (unsigned v = 0; v < count; ++v) {
        uint32_t config = random32() & ~(64u | 128u | 256u | 512u | 1024u);
        configure(reference::voices[v], config, v); configure(current::voices[v], config, v);
        reference::activate_voice(v); current::activate_voice(v);
      }
      reference::mix_voice_block(); current::mix_voice_block();
      for (unsigned f = 0; f < 48; ++f) {
        auto a = reference::mixed_block[f];
        auto b = current::mixed_block[f];
        if (a.beat != b.beat || a.parts != b.parts) {
          fprintf(stderr, "Mismatch trial=%u block=%u frame=%u: %lld/%lld != %lld/%lld\n",
                  trial, block, f, (long long)a.beat, (long long)a.parts, (long long)b.beat, (long long)b.parts);
          return 1;
        }
      }
    }
  }
  for (unsigned voice = 30; voice < 39; ++voice) {
    reference::active_voice_mask = 0;
    current::active_voice_mask[0] = current::active_voice_mask[1] = 0;
    const uint32_t config = random32() & ~(64u | 128u | 256u | 512u | 1024u);
    configure(reference::voices[0], config, 0);
    configure(current::voices[voice], config, 0);
    reference::activate_voice(0); current::activate_voice(voice);
    reference::mix_voice_block(); current::mix_voice_block();
    for (unsigned f = 0; f < 48; ++f) {
      assert(reference::mixed_block[f].beat == current::mixed_block[f].beat);
      assert(reference::mixed_block[f].parts == current::mixed_block[f].parts);
    }
  }
  {
    current::voice_t v{};
    v.active = true; v.envelope_q15 = 0; v.envelope_level_q16 = 0;
    v.envelope_phase = sampler_ns::pcm_render::envelope_delay;
    v.envelope_phase_frames = 3; v.attack_frames_total = 1;
    v.attack_step_q16 = 32768u << 16;
    v.hold_frames_total = 2; v.decay_frames_total = 3;
    v.decay_step_q16 = 8192u << 16;
    v.sustain_level_q15 = 8192; v.release_frames_total = 4;
    assert(!sampler_ns::pcm_render::advance_envelope(v) && v.envelope_phase_frames == 2);
    assert(!sampler_ns::pcm_render::advance_envelope(v));
    assert(!sampler_ns::pcm_render::advance_envelope(v)
        && v.envelope_phase == sampler_ns::pcm_render::envelope_attack);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 32768
        && v.envelope_phase == sampler_ns::pcm_render::envelope_hold);
    assert(sampler_ns::pcm_render::advance_envelope(v));
    assert(sampler_ns::pcm_render::advance_envelope(v)
        && v.envelope_phase == sampler_ns::pcm_render::envelope_decay);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 24576);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 16384);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 8192
        && v.envelope_phase == sampler_ns::pcm_render::envelope_sustain);
    v.release_requested = true;
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 6144);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 4096);
    assert(sampler_ns::pcm_render::advance_envelope(v) && v.envelope_q15 == 2048);
    assert(!sampler_ns::pcm_render::advance_envelope(v) && !v.active);
  }
  {
    // Maximum authored Attack/Decay durations must consume their exact output
    // frame counts instead of collapsing to the old ~0.68 second Q15 limit.
    constexpr uint32_t attack_frames = 5u * 48000u;
    constexpr uint32_t decay_frames = 60u * 48000u;
    current::voice_t v{};
    v.active = true; v.envelope_q15 = 0; v.envelope_level_q16 = 0;
    v.envelope_phase = sampler_ns::pcm_render::envelope_attack;
    v.envelope_phase_frames = v.attack_frames_total = attack_frames;
    v.attack_step_q16 = sampler_ns::pcm_render::envelope_peak_q16 / attack_frames;
    v.decay_frames_total = decay_frames;
    v.decay_step_q16 = sampler_ns::pcm_render::envelope_peak_q16 / decay_frames;
    v.sustain_level_q15 = 0;
    for (uint32_t i = 1; i < attack_frames; ++i) {
      assert(sampler_ns::pcm_render::advance_envelope(v));
      assert(v.envelope_phase == sampler_ns::pcm_render::envelope_attack);
    }
    assert(sampler_ns::pcm_render::advance_envelope(v));
    assert(v.envelope_phase == sampler_ns::pcm_render::envelope_decay);
    for (uint32_t i = 1; i < decay_frames; ++i) {
      assert(sampler_ns::pcm_render::advance_envelope(v));
      assert(v.envelope_phase == sampler_ns::pcm_render::envelope_decay);
    }
    assert(sampler_ns::pcm_render::advance_envelope(v));
    assert(v.envelope_phase == sampler_ns::pcm_render::envelope_sustain
        && v.envelope_q15 == 0);
    assert(!sampler_ns::pcm_render::advance_envelope(v) && !v.active);
  }
  {
    // Note Off during another phase releases from the instantaneous level and
    // still lasts exactly the authored 10 seconds.
    constexpr uint32_t release_frames = 10u * 48000u;
    current::voice_t v{};
    v.active = true; v.envelope_q15 = 12345;
    v.envelope_level_q16 = 12345u << 16;
    v.envelope_phase = sampler_ns::pcm_render::envelope_decay;
    v.envelope_phase_frames = v.decay_frames_total = 1000000;
    v.decay_step_q16 = 1000; v.sustain_level_q15 = 0;
    v.release_frames_total = release_frames; v.release_requested = true;
    for (uint32_t i = 1; i < release_frames; ++i) {
      assert(sampler_ns::pcm_render::advance_envelope(v));
      assert(v.envelope_phase == sampler_ns::pcm_render::envelope_release);
    }
    assert(!sampler_ns::pcm_render::advance_envelope(v) && !v.active);
  }
  puts("PASS: PCM renderer, two-layer banks, exact 5s/60s/10s envelopes, Note Off, loops, pitch, seek, fades and filters");
#if defined(SAMPLER_TEST_SANITIZED)
  return 0;
#endif
  for (unsigned count : {1u, 4u, 7u, 12u}) {
    reference::active_voice_mask = 0;
    current::active_voice_mask[0] = current::active_voice_mask[1] = 0;
    for (unsigned v = 0; v < count; ++v) {
      configure(reference::voices[v], 3u, v); configure(current::voices[v], 3u, v);
      reference::voices[v].volume_q8 = reference::voices[v].target_volume_q8 = 256;
      current::voices[v].volume_q8 = current::voices[v].target_volume_q8 = 256;
      reference::activate_voice(v); current::activate_voice(v);
    }
    auto begin = std::chrono::steady_clock::now();
    for (unsigned b = 0; b < 20000; ++b) {
      reference::mix_voice_block();
      benchmark_checksum ^= (uint64_t)reference::mixed_block[b % 48].parts;
    }
    auto middle = std::chrono::steady_clock::now();
    for (unsigned b = 0; b < 20000; ++b) {
      current::mix_voice_block();
      benchmark_checksum ^= (uint64_t)current::mixed_block[b % 48].parts;
    }
    auto end = std::chrono::steady_clock::now();
    double old_time = std::chrono::duration<double, std::micro>(middle - begin).count();
    double new_time = std::chrono::duration<double, std::micro>(end - middle).count();
    printf("HOST voices=%u old=%.2fus/block new=%.2fus/block ratio=%.2f\n", count, old_time / 20000, new_time / 20000, new_time / old_time);
  }
}
'''


def main():
    source = (ROOT / "main/sampler/sampler_audio.cpp").read_text()
    baseline = subprocess.check_output(["git", "show", "2418bc88:main/sampler/sampler_audio.cpp"], cwd=ROOT, text=True)
    preamble = '''#include <algorithm>
#include <cassert>
#include <stdint.h>
#include <stdio.h>
#include <chrono>
#include "main/sampler/sampler_pcm_render.hpp"
struct sampler_audio_t { static constexpr unsigned max_voice = 39, fx_target_beat = 1, fx_target_parts = 2; };
static constexpr unsigned i2s_dma_frame_num = 96;
'''
    with tempfile.TemporaryDirectory(prefix="sampler-pcm-test-") as directory:
        path = pathlib.Path(directory)
        harness = path / "test.cpp"
        harness.write_text(preamble + renderer(baseline, "reference", False)
                           + renderer(source, "current", True) + HARNESS)
        binary = path / "test"
        flags = ["-fsanitize=address", "-DSAMPLER_TEST_SANITIZED=1"] if "--sanitize" in sys.argv else []
        subprocess.run(["c++", "-std=c++17", "-O2", "-fwrapv", *flags, "-I", str(ROOT), str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
