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
    bus = source[source.index("struct mixed_buses_t {"):source.index("static inline", source.index("struct mixed_buses_t {"))]
    code = [f"namespace {name} {{", "namespace pcm_render = sampler_ns::pcm_render;", voice,
            "static voice_t voices[30]; static volatile uint32_t active_voice_mask;",
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
  v.envelope_q15 = config & 8 ? 0 : 32768;
  v.attack_step_q15 = 7 + config % 300;
  v.release_step_q15 = 11 + config % 500;
  v.auto_release_frames = config & 16 ? 100 + config % 1000 : 0;
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
    reference::active_voice_mask = current::active_voice_mask = 0;
    const unsigned count = 1 + trial % 30;
    for (unsigned v = 0; v < count; ++v) {
      uint32_t config = random32();
      if (trial < 80) { config &= ~(64u | 128u | 256u | 512u | 1024u); }
      configure(reference::voices[v], config, v);
      configure(current::voices[v], config, v);
      reference::activate_voice(v); current::activate_voice(v);
    }
    for (unsigned block = 0; block < 100; ++block) {
      if (block == 30) for (unsigned v = 0; v < count; v += 2) {
        reference::voices[v].release_requested = current::voices[v].release_requested = true;
      }
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
  puts("PASS: 720,000 stereo frames, 1-30 voices, cached/uncached, loops, On/Off, envelopes, pitch, reverse, seek, fades and filters");
#if defined(SAMPLER_TEST_SANITIZED)
  return 0;
#endif
  for (unsigned count : {1u, 4u, 7u, 12u}) {
    reference::active_voice_mask = current::active_voice_mask = 0;
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
#include <stdint.h>
#include <stdio.h>
#include <chrono>
#include "main/sampler/sampler_pcm_render.hpp"
struct sampler_audio_t { static constexpr unsigned max_voice = 30, fx_target_beat = 1, fx_target_parts = 2; };
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
