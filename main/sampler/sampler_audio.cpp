// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

// サンプル再生エンジン (KANPLAY_SAMPLER ビルド時のみ有効)
// I2S 初期化部は main/task_i2s.cpp と同一構成 (ES8388がI2Sマスター、48kHz/32bit/stereo)

#if defined (KANPLAY_SAMPLER)

#include <M5Unified.h>

#include "sampler_audio.hpp"
#include "sampler_amy_engine.hpp"
#include "sampler_performance_probe.hpp"
#include "sampler_pcm_render.hpp"

#include "../common_define.hpp"
#include "../system_registry.hpp"

#if !defined (M5UNIFIED_PC_BUILD)

#if __has_include(<driver/i2s_std.h>)
 #include <driver/i2s_std.h>
#else
 #include <driver/i2s.h>
#endif

#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

#endif

namespace sampler_ns {
namespace kp = kanplay_ns;
//-------------------------------------------------------------------------

static constexpr const uint16_t i2s_dma_frame_num = 96;
static constexpr const uint32_t output_sample_rate = sampler_audio_t::sample_rate;

//-------------------------------------------------------------------------
// ボイス管理

struct voice_t {
  const int16_t* pcm = nullptr;  // モノラルPCMデータ先頭
  uint32_t frames = 0;           // 総フレーム数
  int64_t pos_fp = 0;            // 再生位置 (16.16固定小数、フレーム単位)
  volatile uint32_t frame_for_ui = 0;  // UI参照用の現在フレーム (PCM範囲内)
  volatile uint32_t seek_frame = 0;
  volatile bool seek_pending = false;
  uint32_t seek_target_frame = 0;
  uint16_t seek_fade_remaining = 0;
  uint8_t seek_fade_state = 0;  // 0=idle, 1=fade out, 2=fade in
  uint32_t nominal_step_fp = 0;  // Voice固有の音程。演奏中Pitch Bendの基準。
  uint32_t base_step_fp = 0;     // FXなしの再生ステップ
  uint32_t step_fp = 0;          // 現在の再生ステップ
  volatile int32_t playback_rate_q16 = 65536;  // テープ速度。負値は逆方向。
  volatile uint8_t tone_cutoff = 127;
  volatile uint8_t tone_resonance = 0;
  int32_t tone_filter_1 = 0;
  int32_t tone_filter_2 = 0;
  bool loop = false;
  uint32_t loop_start_frame = 0;
  uint32_t loop_end_frame = 0;
  uint16_t loop_crossfade_frames = 0;
  const int16_t* attack_cache_pcm = nullptr;
  uint32_t attack_cache_frames = 0;
  const int16_t* sustain_cache_pcm = nullptr;
  uint32_t sustain_cache_frames = 0;
  uint8_t sustain_cache_stride = 1;
  uint8_t sustain_cache_slot = 0xFF;
  bool reverse = false;
  uint16_t volume_q8 = 256;
  volatile uint16_t target_volume_q8 = 256;
  // Chord Pad synthesis can run four sustained, pitch-shifted PCM voices at
  // once. Nearest-neighbour reads keep that expressive mode within the I2S
  // deadline; one-note Melody/Bass retains the higher-quality interpolation.
  bool linear_interpolation = true;
  // A divider of two renders a Pad-sourced chord at 24kHz internally while
  // retaining the 48kHz output envelope. This halves PSRAM reads for the four
  // simultaneous chord voices without affecting one-note instruments.
  uint8_t render_divider = 1;
  uint8_t render_phase = 0;
  int32_t render_sample = 0;
  bool render_sample_valid = false;
  uint16_t envelope_q15 = 32768;
  uint16_t sustain_level_q15 = 32768;
  // Q15.16 keeps sub-Q15 steps for multi-second envelopes. The I2S path only
  // adds/subtracts uint32_t values; all division remains at Note On/Off.
  uint32_t envelope_level_q16 = 32768u << 16;
  uint32_t attack_step_q16 = 0;
  uint32_t decay_step_q16 = 0;
  uint32_t release_step_q16 = 0;
  uint32_t envelope_phase_frames = 0;
  uint32_t attack_frames_total = 1;
  uint32_t hold_frames_total = 0;
  uint32_t decay_frames_total = 1;
  uint32_t release_frames_total = 1;
  uint8_t envelope_phase = 4; // sampler_ns::pcm_render::envelope_sustain
  uint32_t auto_release_frames = 0;
  // Chopped samples retain a short region on both sides of their musical
  // Anchor. These source-frame coordinates turn those retained regions into
  // a real overlap without shortening the Anchor-to-Anchor beat interval.
  uint32_t edge_fade_in_end = 0;
  uint32_t edge_fade_out_start = UINT32_MAX;
  // Set by the input/loop task and consumed by the I2S task. This must remain
  // observable on every audio frame; otherwise a dense Pad-sourced chord can
  // keep a cached false value after its physical Note Off.
  volatile bool release_requested = false;
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
  volatile uint32_t probe_edge_usec = 0;
  volatile uint8_t probe_kind = 0; // 1 live-on, 2 live-off, 3 rec-on, 4 rec-off
#endif
  volatile bool active = false;
  uint8_t fx_target = sampler_audio_t::fx_target_parts;
};

static voice_t voices[sampler_audio_t::max_voice];
// Most performance frames use only a small subset of the 22 available
// voices. Keep a lock-free active set so the I2S callback never burns time
// checking every dormant slot. A 32-bit mask is atomic on ESP32-S3.
static volatile uint32_t active_voice_mask[2] = {};
static volatile uint8_t active_fx_target_mask = sampler_audio_t::fx_target_all;

struct deck_stream_t {
  int16_t* pcm = nullptr;
  uint32_t capacity = 0;
  volatile uint32_t read_frames = 0;
  volatile uint32_t write_frames = 0;
  volatile uint32_t underruns = 0;
  uint16_t volume_q8 = 256;
  volatile uint16_t target_volume_q8 = 256;
  volatile bool playing = false;
};
static deck_stream_t deck_stream;

static inline bool read_deck_stream_frame(int64_t* left, int64_t* right)
{
  *left = 0;
  *right = 0;
  if (!deck_stream.playing || !deck_stream.pcm || deck_stream.capacity == 0) { return false; }
  const uint32_t read = __atomic_load_n(&deck_stream.read_frames, __ATOMIC_RELAXED);
  const uint32_t write = __atomic_load_n(&deck_stream.write_frames, __ATOMIC_ACQUIRE);
  if (read == write) {
    __atomic_fetch_add(&deck_stream.underruns, 1u, __ATOMIC_RELAXED);
    return false;
  }
  const uint32_t index = read % deck_stream.capacity;
  if (deck_stream.volume_q8 < deck_stream.target_volume_q8) { ++deck_stream.volume_q8; }
  else if (deck_stream.volume_q8 > deck_stream.target_volume_q8) { --deck_stream.volume_q8; }
  *left = ((int64_t)deck_stream.pcm[index * 2] << 16) * deck_stream.volume_q8 >> 8;
  *right = ((int64_t)deck_stream.pcm[index * 2 + 1] << 16) * deck_stream.volume_q8 >> 8;
  __atomic_store_n(&deck_stream.read_frames, read + 1u, __ATOMIC_RELEASE);
  return true;
}

static inline void activate_voice(uint8_t voice)
{
  __atomic_fetch_or(&active_voice_mask[voice >> 5], 1u << (voice & 31u), __ATOMIC_RELEASE);
}

static inline void deactivate_voice(uint8_t voice)
{
  __atomic_fetch_and(&active_voice_mask[voice >> 5],
                     ~(1u << (voice & 31u)), __ATOMIC_RELEASE);
}

// Melody, Chord and Bass each get one internal-RAM source cache. A chord's
// four voices share its attack and sustain windows rather than repeatedly
// reading the same PSRAM data during dense loop recording.
// 0-2 primary parts, 3-5 optional second layers, 6-8 Sampler Pads. The
// allocation budget remains 48 KiB, so adding descriptors cannot consume BLE
// or display heap; uncached layers continue directly from PSRAM.
static constexpr uint8_t synth_sustain_cache_count = 9;
static constexpr uint32_t synth_sustain_cache_max_frames = 8192;
static constexpr uint32_t synth_attack_cache_max_frames = 4096;
struct synth_sustain_cache_t {
  int16_t* pcm = nullptr;
  int16_t* attack_pcm = nullptr;
  const int16_t* source = nullptr;
  uint32_t start = 0;
  uint32_t end = 0;
  uint32_t frames = 0;
  uint32_t attack_frames = 0;
  uint32_t capacity = 0;
  uint32_t attack_capacity = 0;
  uint8_t stride = 1;
};
static synth_sustain_cache_t synth_sustain_cache[synth_sustain_cache_count];

// Optional accelerators must not consume the heap needed by BLE/Mic tasks.
// Allocate the actual window, not 24KB for every selected or previewed tone.
static constexpr size_t synth_cache_budget_bytes = 48u * 1024u;
static constexpr size_t synth_cache_internal_headroom = 48u * 1024u;
static constexpr size_t synth_cache_contiguous_headroom = 16u * 1024u;

static bool synth_cache_can_allocate(size_t bytes)
{
  size_t used = 0;
  for (const auto& cache : synth_sustain_cache) {
    used += (cache.capacity + cache.attack_capacity) * sizeof(int16_t);
  }
  if (used + bytes > synth_cache_budget_bytes) { return false; }
#if !defined(M5UNIFIED_PC_BUILD)
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  if (heap_caps_get_free_size(caps) < bytes + synth_cache_internal_headroom
   || heap_caps_get_largest_free_block(caps) < bytes + synth_cache_contiguous_headroom) {
    return false;
  }
#endif
  return true;
}

static void reserve_synth_cache_window(int16_t*& pcm, uint32_t& capacity,
                                       uint32_t frames)
{
  if (frames <= capacity) { return; }
  // Caller checked that no voice can still read this cache.
  free(pcm);
  pcm = nullptr;
  capacity = 0;
  const size_t bytes = frames * sizeof(int16_t);
  if (!synth_cache_can_allocate(bytes)) { return; }
#if defined(M5UNIFIED_PC_BUILD)
  pcm = (int16_t*)malloc(bytes);
#else
  pcm = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
  capacity = pcm ? frames : 0;
}

static void reset_synth_sustain_cache_entry(synth_sustain_cache_t& entry)
{
  entry.source = nullptr;
  entry.start = 0;
  entry.end = 0;
  entry.frames = 0;
  entry.attack_frames = 0;
  entry.stride = 1;
}

static inline int16_t voice_pcm_at(const voice_t& v, uint32_t index)
{
  if (v.attack_cache_pcm && index < v.attack_cache_frames) {
    return v.attack_cache_pcm[index];
  }
  if (v.sustain_cache_pcm && index >= v.loop_start_frame && index < v.loop_end_frame) {
    const uint32_t cache_index = (index - v.loop_start_frame) / v.sustain_cache_stride;
    if (cache_index < v.sustain_cache_frames) { return v.sustain_cache_pcm[cache_index]; }
  }
  const uint32_t sample_index = v.reverse ? (v.frames - 1 - index) : index;
  return v.pcm[sample_index];
}
// Never expose codec/I2S startup transients.  sampler_app releases this only
// after restoring the kit and selecting the input route.
static volatile bool output_muted = true;
static volatile bool external_input_monitor = false;
static volatile uint16_t output_fade_target_q15 = 0;
static volatile uint16_t output_fade_step_q15 = 32768;
static uint16_t output_fade_q15 = 0;
static volatile uint16_t output_gain_q8 = 256;
// The UI/input task raises this around a physical performance hit.  It is
// deliberately a single volatile flag: the I2S task must not take a lock or
// allocate just to make room for a new attack.
static volatile bool performance_priority = false;
// The mixer owns a 1ms I2S block. Keep a compact estimate so optional work
// such as live visualisation and future motion/Delay FX can yield before they
// threaten an audible deadline.
static volatile uint8_t processing_load_q8 = 0;
static volatile uint8_t processing_peak_q8 = 0;
static volatile bool live_wave_capture_enabled = false;
static volatile bool live_wave_clear_pending = false;

struct fx_state_t {
  volatile bool active = false;
  volatile int8_t param = 0;
};

static constexpr uint8_t audio_fx_count = 4;
static constexpr uint8_t audio_fx_tempo = 0;
static constexpr uint8_t audio_fx_filter = 1;
static constexpr uint8_t audio_fx_gater = 2;
static constexpr uint8_t audio_fx_crusher = 3;
static fx_state_t fx[audio_fx_count];
static volatile uint16_t fx_speed_ratio_q8 = 256;
static int32_t filter_l = 0;
static int32_t filter_r = 0;
static volatile uint32_t fx_quantize_step_frames = output_sample_rate / 8u;
static uint32_t gater_phase = 0;
static int32_t crusher_hold_l = 0;
static int32_t crusher_hold_r = 0;
static uint8_t crusher_hold_remaining = 0;
static int32_t limiter_gain_q15 = 32768;

// Tape Stop, Master Scratch, Master Repeat and Grid Delay share one final-mix
// Deck Buffer. It becomes a
// rolling history only in FX mode; the dry transport and voices continue
// underneath, ready to rejoin through a short crossfade.
// A record-player-style stop needs enough travel for the descending pitch to
// register musically. The UI normally sets this to two Note Grids; 660ms is
// the free-play fallback. Reserve for the slowest supported two-grid stop,
// but only use the chosen part of this PSRAM buffer during a performance.
static constexpr uint32_t tape_stop_default_ramp_frames = output_sample_rate * 660u / 1000u;
static constexpr uint32_t tape_stop_max_ramp_frames = output_sample_rate * 2000u / 1000u;
static constexpr uint32_t tape_stop_release_frames = output_sample_rate * 12u / 1000u;
static constexpr uint32_t tape_stop_buffer_frames = tape_stop_max_ramp_frames + 384u;
enum class tape_stop_state_t : uint8_t { idle, slowing, stopped, releasing };
struct tape_stop_t {
  volatile bool requested = false;
  int16_t* pcm = nullptr;  // interleaved stereo, allocated once but idle when unused
  uint32_t capacity = 0;
  int64_t read_fp = 0;
  uint32_t elapsed_frames = 0;
  uint32_t ramp_frames = tape_stop_default_ramp_frames;
  volatile uint32_t requested_ramp_frames = tape_stop_default_ramp_frames;
  uint32_t release_frames = 0;
  tape_stop_state_t state = tape_stop_state_t::idle;
};
static tape_stop_t tape_stop;

struct deck_buffer_t {
  volatile bool enabled = false;
  volatile bool reset_pending = false;
  uint64_t write_frames = 0;
  uint32_t valid_frames = 0;
  uint32_t recent_frames = 0;
};
static deck_buffer_t deck_buffer;

enum class master_scratch_state_t : uint8_t { idle, active, releasing };
struct master_scratch_t {
  volatile bool requested = false;
  volatile int16_t rate_q8 = 0;
  int64_t read_fp = 0;
  uint16_t fade_frames = 0;
  master_scratch_state_t state = master_scratch_state_t::idle;
};
static master_scratch_t master_scratch;
static constexpr uint16_t deck_crossfade_frames = output_sample_rate * 10u / 1000u;
static constexpr uint32_t scratch_headroom_frames = output_sample_rate * 72u / 1000u;

enum class master_repeat_state_t : uint8_t { idle, capturing, active, releasing };
struct master_repeat_t {
  volatile bool requested = false;
  volatile uint32_t requested_frames = 1;
  volatile uint32_t requested_capture_frames = 1;
  uint64_t start_frame = 0;
  int64_t read_fp = 0;
  uint32_t repeat_frames = 1;
  uint32_t capture_frames = 1;
  uint16_t fade_frames = 0;
  master_repeat_state_t state = master_repeat_state_t::idle;
};
static master_repeat_t master_repeat;
// Leave enough untouched frames after the largest captured window for the
// release crossfade while normal Deck writes resume.
static constexpr uint32_t repeat_deck_guard_frames = deck_crossfade_frames + 160u;
static constexpr uint16_t repeat_wrap_crossfade_frames = 32u;

enum class master_delay_state_t : uint8_t { idle, active, releasing };
struct master_delay_t {
  volatile bool requested = false;
  volatile bool cancel_pending = false;
  volatile uint32_t requested_frames = output_sample_rate / 4u;
  uint32_t delay_frames = output_sample_rate / 4u;
  uint32_t previous_frames = output_sample_rate / 4u;
  uint16_t transition_frames = 0;
  uint32_t release_frames = 0;
  uint32_t release_limit_frames = 0;
  master_delay_state_t state = master_delay_state_t::idle;
};
static master_delay_t master_delay;
static constexpr uint16_t delay_wet_q15 = 16384;       // 50%
static constexpr uint16_t delay_feedback_q15 = 13107;  // 40%
static constexpr uint16_t delay_transition_frames = output_sample_rate * 10u / 1000u;
static constexpr uint32_t delay_max_tail_frames = output_sample_rate * 2u;

struct recorder_t {
  volatile bool active = false;
  volatile bool overflow = false;
  int16_t* buffer = nullptr;
  volatile uint32_t frames = 0;
  uint32_t capacity = 0;
  uint32_t level_l = 0;
  uint32_t level_r = 0;
  uint32_t level_mix = 0;
};

static recorder_t recorder;
struct output_capture_t {
  volatile bool active = false;
  int16_t* buffer = nullptr;
  uint32_t capacity = 0;
  volatile uint32_t frames = 0;
};
static output_capture_t output_capture;
struct output_stream_capture_t {
  volatile bool active = false;
  volatile bool overflow = false;
  int16_t* buffer = nullptr;
  uint32_t capacity = 0;
  volatile uint32_t write_pos = 0;
  volatile uint32_t read_pos = 0;
};
static output_stream_capture_t output_stream_capture;

static inline uint32_t pitch_step_fp(uint32_t base_step, uint8_t voice_target)
{
  if (!fx[0].active || fx_speed_ratio_q8 == 256
   || (active_fx_target_mask & voice_target) == 0) { return base_step; }
  return (uint32_t)(((uint64_t)base_step * fx_speed_ratio_q8) >> 8);
}

static void update_voice_steps(void)
{
  for (auto& voice : voices) {
    if (voice.active) { voice.step_fp = pitch_step_fp(voice.base_step_fp, voice.fx_target); }
  }
}

static bool prepare_voice(uint8_t voice, const int16_t* pcm, uint32_t frames, uint32_t sample_rate,
                           bool loop, bool reverse, uint16_t volume_q8, uint32_t pitch_q16,
                           uint32_t start_frame, uint32_t edge_fade_in_end,
                           uint32_t edge_fade_out_start, uint32_t probe_edge_usec,
                           uint8_t probe_kind)
{
  if (voice >= sampler_audio_t::max_voice || pcm == nullptr || frames == 0 || sample_rate == 0) { return false; }

  auto& v = voices[voice];
  deactivate_voice(voice);
  v.active = false;  // 再生中の再トリガに備え一旦停止してから書き換える
  v.pcm = pcm;
  v.frames = frames;
  // Q16 retains a distinct playback step across the complete MIDI range,
  // including high-root PCM shifted down by more than two octaves.
  pitch_q16 = std::max<uint32_t>(1, pitch_q16);
  v.nominal_step_fp = (uint32_t)std::max<uint64_t>(1,
    ((uint64_t)sample_rate * pitch_q16) / output_sample_rate);
  v.base_step_fp = v.nominal_step_fp;
  v.step_fp = pitch_step_fp(v.base_step_fp, v.fx_target);
  v.playback_rate_q16 = 65536;
  v.tone_cutoff = 127;
  v.tone_resonance = 0;
  v.tone_filter_1 = 0;
  v.tone_filter_2 = 0;
  if (start_frame >= frames) { start_frame = 0; }
  v.pos_fp = (int64_t)start_frame << 16;
  v.frame_for_ui = start_frame;
  v.seek_pending = false;
  v.seek_fade_state = 0;
  v.seek_fade_remaining = 0;
  v.loop = loop;
  v.loop_start_frame = 0;
  v.loop_end_frame = frames;
  v.loop_crossfade_frames = 0;
  v.attack_cache_pcm = nullptr;
  v.attack_cache_frames = 0;
  v.sustain_cache_pcm = nullptr;
  v.sustain_cache_frames = 0;
  v.sustain_cache_stride = 1;
  v.sustain_cache_slot = 0xFF;
  v.reverse = reverse;
  v.volume_q8 = volume_q8;
  v.target_volume_q8 = volume_q8;
  v.linear_interpolation = true;
  v.render_divider = 1;
  v.render_phase = 0;
  v.render_sample = 0;
  v.render_sample_valid = false;
  v.envelope_q15 = 32768;
  v.sustain_level_q15 = 32768;
  v.envelope_level_q16 = 32768u << 16;
  v.attack_step_q16 = 0;
  v.decay_step_q16 = 0;
  v.release_step_q16 = 0;
  v.envelope_phase_frames = 0;
  v.attack_frames_total = 1;
  v.hold_frames_total = 0;
  v.decay_frames_total = 1;
  v.release_frames_total = 1;
  v.envelope_phase = pcm_render::envelope_sustain;
  v.auto_release_frames = 0;
  v.edge_fade_in_end = std::min<uint32_t>(edge_fade_in_end, frames);
  v.edge_fade_out_start = std::min<uint32_t>(edge_fade_out_start, frames);
  v.release_requested = false;
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
  v.probe_edge_usec = probe_edge_usec;
  v.probe_kind = probe_kind;
#endif
  return true;
}

bool sampler_audio_t::play(uint8_t voice, const int16_t* pcm, uint32_t frames, uint32_t sample_rate,
                           bool loop, bool reverse, uint16_t volume_q8, uint16_t pitch_q8,
                           uint32_t start_frame, uint32_t edge_fade_in_end,
                           uint32_t edge_fade_out_start, uint32_t probe_edge_usec,
                           uint8_t probe_kind)
{
  pitch_q8 = std::clamp<uint16_t>(pitch_q8, 32, 2048);
  if (!prepare_voice(voice, pcm, frames, sample_rate, loop, reverse, volume_q8,
      (uint32_t)pitch_q8 << 8, start_frame, edge_fade_in_end, edge_fade_out_start,
      probe_edge_usec, probe_kind)) { return false; }
  auto& v = voices[voice];
  v.active = true;
  activate_voice(voice);
  return true;
}

void sampler_audio_t::primeSynthSustainCache(uint8_t cache_slot, const int16_t* pcm,
                                             uint32_t sustain_start, uint32_t sustain_end,
                                             uint32_t attack_cache_limit,
                                             uint32_t sustain_cache_limit)
{
  if (cache_slot >= synth_sustain_cache_count) { return; }
  auto& cache = synth_sustain_cache[cache_slot];
  attack_cache_limit = std::clamp<uint32_t>(attack_cache_limit, 32, synth_attack_cache_max_frames);
  sustain_cache_limit = std::clamp<uint32_t>(sustain_cache_limit, 32, synth_sustain_cache_max_frames);
  if (isSynthSustainCacheInUse(cache_slot)) { return; }
  if (!pcm || sustain_end <= sustain_start || sustain_end - sustain_start < 32) {
    reset_synth_sustain_cache_entry(cache);
    return;
  }
  if (cache.source == pcm && cache.start == sustain_start && cache.end == sustain_end
   && (cache.attack_frames != 0 || cache.frames != 0)) {
    return;
  }
  const uint32_t loop_frames = sustain_end - sustain_start;
  // Sustain is the continuous multi-voice load. Reserve it first; an attack
  // can safely read the original PCM when BLE leaves little internal RAM.
  if (loop_frames <= sustain_cache_limit) {
    reserve_synth_cache_window(cache.pcm, cache.capacity, loop_frames);
  }
  reserve_synth_cache_window(cache.attack_pcm, cache.attack_capacity,
                             std::min(sustain_start, attack_cache_limit));
  cache.attack_frames = std::min<uint32_t>(sustain_start, cache.attack_capacity);
  for (uint32_t i = 0; i < cache.attack_frames; ++i) { cache.attack_pcm[i] = pcm[i]; }
  cache.source = pcm;
  cache.start = sustain_start;
  cache.end = sustain_end;
  cache.frames = 0;
  cache.stride = 1;
  // A synth loop needs sample-accurate material. Downsampling it into the
  // cache turns the cached source into a sample-and-hold waveform, which is
  // especially audible as a metallic tone at Loop In/Out. Cache only an
  // entire loop at its original resolution; longer loops stay in PSRAM.
  if (loop_frames > sustain_cache_limit) {
    return;
  }
  if (!cache.pcm) {
    return;
  }
  if (loop_frames > cache.capacity) {
    return;
  }
  for (uint32_t i = 0; i < loop_frames; ++i) {
    cache.pcm[i] = pcm[sustain_start + i];
  }
  cache.frames = loop_frames;
}

void sampler_audio_t::clearSynthSustainCache(uint8_t cache_slot)
{
  const uint8_t first = cache_slot < synth_sustain_cache_count ? cache_slot : 0;
  const uint8_t last = cache_slot < synth_sustain_cache_count
    ? (uint8_t)(cache_slot + 1) : synth_sustain_cache_count;
  for (uint8_t i = first; i < last; ++i) { reset_synth_sustain_cache_entry(synth_sustain_cache[i]); }
}

size_t sampler_audio_t::releaseUnusedSynthSustainCacheMemory(void)
{
  size_t released = 0;
  for (uint8_t slot = 0; slot < synth_sustain_cache_count; ++slot) {
    bool in_use = false;
    for (const auto& voice : voices) {
      if (voice.active && voice.sustain_cache_slot == slot) {
        in_use = true;
        break;
      }
    }
    if (in_use) { continue; }

    auto& cache = synth_sustain_cache[slot];
    if (cache.attack_pcm != nullptr) {
      released += cache.attack_capacity * sizeof(int16_t);
      free(cache.attack_pcm);
      cache.attack_pcm = nullptr;
      cache.attack_capacity = 0;
    }
    if (cache.pcm != nullptr) {
      released += cache.capacity * sizeof(int16_t);
      free(cache.pcm);
      cache.pcm = nullptr;
      cache.capacity = 0;
    }
    reset_synth_sustain_cache_entry(cache);
  }
  return released;
}

bool sampler_audio_t::isSynthSustainCacheInUse(uint8_t cache_slot)
{
  if (cache_slot >= synth_sustain_cache_count) { return false; }
  for (const auto& voice : voices) {
    if (voice.active && voice.sustain_cache_slot == cache_slot) { return true; }
  }
  return false;
}

bool sampler_audio_t::playSynth(uint8_t voice, const int16_t* pcm, uint32_t frames,
                                uint32_t sample_rate, bool sustain_loop, bool reverse,
                                uint16_t volume_q8, uint32_t pitch_q16,
                                uint16_t attack_ms, uint16_t release_ms,
                                uint32_t sustain_start, uint32_t sustain_end,
                                uint16_t sustain_crossfade, uint16_t auto_release_ms,
                                bool linear_interpolation, uint8_t render_divider,
                                uint8_t sustain_cache_slot, uint32_t probe_edge_usec,
                                uint8_t probe_kind, uint16_t delay_100us,
                                uint16_t hold_ms, uint16_t decay_ms,
                                uint16_t sustain_level_q15)
{
  if (!prepare_voice(voice, pcm, frames, sample_rate, sustain_loop, reverse,
            volume_q8, pitch_q16, 0, 0, UINT32_MAX,
            probe_edge_usec, probe_kind)) {
    return false;
  }
  auto& v = voices[voice];
  v.linear_interpolation = linear_interpolation;
  v.render_divider = std::clamp<uint8_t>(render_divider, 1, 2);
  v.render_phase = 0;
  v.render_sample_valid = false;
  if (sustain_loop && !reverse && sustain_end > sustain_start
   && sustain_end <= frames && sustain_end - sustain_start >= 32) {
    v.loop_start_frame = sustain_start;
    v.loop_end_frame = sustain_end;
    v.loop_crossfade_frames = (uint16_t)std::min<uint32_t>(
      sustain_crossfade, (sustain_end - sustain_start) / 4);
    if (sustain_cache_slot < synth_sustain_cache_count) {
      const auto& cache = synth_sustain_cache[sustain_cache_slot];
      if (cache.source == pcm && cache.start == sustain_start && cache.end == sustain_end
       && (cache.attack_frames != 0 || cache.frames != 0)) {
        if (cache.attack_frames != 0 && cache.attack_pcm) {
          v.attack_cache_pcm = cache.attack_pcm;
          v.attack_cache_frames = cache.attack_frames;
          v.sustain_cache_slot = sustain_cache_slot;
        }
        if (cache.frames != 0 && cache.pcm) {
          v.sustain_cache_pcm = cache.pcm;
          v.sustain_cache_frames = cache.frames;
          v.sustain_cache_stride = cache.stride;
          v.sustain_cache_slot = sustain_cache_slot;
        }
      }
    }
  }
  const uint32_t attack_frames = std::max<uint32_t>(1, (output_sample_rate * attack_ms) / 1000);
  const uint32_t release_frames = std::max<uint32_t>(1, (output_sample_rate * release_ms) / 1000);
  const uint32_t hold_frames = (output_sample_rate * (uint32_t)hold_ms) / 1000u;
  const uint32_t decay_frames = std::max<uint32_t>(1, (output_sample_rate * (uint32_t)decay_ms) / 1000u);
  const uint32_t delay_frames = ((uint64_t)output_sample_rate * delay_100us + 9999u) / 10000u;
  v.sustain_level_q15 = std::min<uint16_t>(32768, sustain_level_q15);
  v.envelope_q15 = attack_ms == 0 && delay_frames == 0 ? 32768 : 0;
  v.envelope_level_q16 = (uint32_t)v.envelope_q15 << 16;
  v.attack_step_q16 = attack_ms == 0 ? 0
    : std::max<uint32_t>(1, (32768u << 16) / attack_frames);
  v.decay_step_q16 = decay_ms == 0 || v.sustain_level_q15 == 32768 ? 0
    : std::max<uint32_t>(1,
        ((32768u - v.sustain_level_q15) << 16) / decay_frames);
  // Release is recalculated from the instantaneous level at Note Off.
  v.release_step_q16 = 0;
  v.attack_frames_total = attack_frames;
  v.release_frames_total = release_frames;
  v.hold_frames_total = hold_frames;
  v.decay_frames_total = decay_frames;
  if (delay_frames) {
    v.envelope_phase = pcm_render::envelope_delay;
    v.envelope_phase_frames = delay_frames;
  } else if (attack_ms) {
    v.envelope_phase = pcm_render::envelope_attack;
    v.envelope_phase_frames = attack_frames;
  } else if (hold_frames) {
    v.envelope_phase = pcm_render::envelope_hold;
    v.envelope_phase_frames = hold_frames;
  } else if (v.decay_step_q16) {
    v.envelope_phase = pcm_render::envelope_decay;
    v.envelope_phase_frames = decay_frames;
  } else {
    v.envelope_phase = pcm_render::envelope_sustain;
    pcm_render::set_envelope_level_q16(v, (uint32_t)v.sustain_level_q15 << 16);
    v.envelope_phase_frames = 0;
  }
  v.auto_release_frames = (output_sample_rate * (uint32_t)auto_release_ms) / 1000u;
  // Publish only after loop/cache/envelope parameters are complete. I2S may
  // preempt the note producer between any two of the assignments above.
  v.active = true;
  activate_voice(voice);
  return true;
}

void sampler_audio_t::release(uint8_t voice, uint32_t probe_edge_usec,
                              uint8_t probe_kind)
{
  if (voice < max_voice && voices[voice].active) {
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
    voices[voice].probe_edge_usec = probe_edge_usec;
    voices[voice].probe_kind = probe_kind;
#endif
    voices[voice].release_requested = true;
  }
}

void sampler_audio_t::stop(uint8_t voice)
{
  if (voice < max_voice) {
    deactivate_voice(voice);
    voices[voice].active = false;
  }
}

void sampler_audio_t::stopAll(void)
{
  __atomic_store_n(&active_voice_mask[0], 0, __ATOMIC_RELEASE);
  __atomic_store_n(&active_voice_mask[1], 0, __ATOMIC_RELEASE);
  for (auto& v : voices) { v.active = false; }
}

void sampler_audio_t::seek(uint8_t voice, uint32_t frame)
{
  if (voice >= max_voice || !voices[voice].active) { return; }
  auto& v = voices[voice];
  v.seek_frame = frame;
  v.seek_pending = true;
}

bool sampler_audio_t::isPlaying(uint8_t voice)
{
  return (voice < max_voice) && voices[voice].active;
}

void sampler_audio_t::setVoiceVolumeQ8(uint8_t voice, uint16_t volume_q8)
{
  if (voice >= max_voice) { return; }
  voices[voice].target_volume_q8 = std::min<uint16_t>(volume_q8, 1024);
}

void sampler_audio_t::setVoicePitchScaleQ12(uint8_t voice, uint16_t scale_q12)
{
  if (voice >= max_voice) { return; }
  if (scale_q12 < 2048) { scale_q12 = 2048; }
  if (scale_q12 > 8192) { scale_q12 = 8192; }
  auto& v = voices[voice];
  v.base_step_fp = (uint32_t)(((uint64_t)v.nominal_step_fp * scale_q12) >> 12);
  v.step_fp = pitch_step_fp(v.base_step_fp, v.fx_target);
}

void sampler_audio_t::setVoicePlaybackRateQ8(uint8_t voice, int16_t rate_q8)
{
  if (voice >= max_voice) { return; }
  if (rate_q8 < -512) { rate_q8 = -512; }
  if (rate_q8 > 512) { rate_q8 = 512; }
  voices[voice].playback_rate_q16 = (int32_t)rate_q8 << 8;
}

void sampler_audio_t::setVoicePlaybackRateQ16(uint8_t voice, int32_t rate_q16)
{
  if (voice >= max_voice) { return; }
  voices[voice].playback_rate_q16 = std::clamp<int32_t>(rate_q16, -524288, 524288);
}

void sampler_audio_t::setVoiceToneFilter(uint8_t voice, uint8_t cutoff, uint8_t resonance)
{
  if (voice >= max_voice) { return; }
  voices[voice].tone_cutoff = std::min<uint8_t>(cutoff, 127);
  voices[voice].tone_resonance = std::min<uint8_t>(resonance, 127);
}

void sampler_audio_t::setPerformancePriority(bool active)
{
  performance_priority = active;
}

uint8_t sampler_audio_t::processingLoadQ8(void)
{
  return processing_load_q8;
}

uint8_t sampler_audio_t::processingPeakQ8(void)
{
  return processing_peak_q8;
}

void sampler_audio_t::setLiveWaveCapture(bool enabled)
{
  if (live_wave_capture_enabled == enabled) { return; }
  live_wave_capture_enabled = enabled;
  // Let the I2S task clear its own shared ring before the next live scope.
  // This avoids showing stale output from the preceding Loop/Edit page.
  if (enabled) { live_wave_clear_pending = true; }
}

bool sampler_audio_t::getPlaybackPosition(uint8_t voice, uint32_t* frame, uint32_t* frames)
{
  if (voice >= max_voice || !voices[voice].active) { return false; }
  if (frame != nullptr) { *frame = voices[voice].frame_for_ui; }
  if (frames != nullptr) { *frames = voices[voice].frames; }
  return true;
}

void sampler_audio_t::setOutputMuted(bool muted)
{
  output_muted = muted;
  output_fade_target_q15 = muted ? 0 : 32768;
  // Recording changes must remain responsive, but unmuting still must not
  // create an edge at the DAC.  10ms is inaudible in normal operation.
  output_fade_step_q15 = muted ? 32768
    : (uint16_t)((32768 + output_sample_rate / 100 - 1) / (output_sample_rate / 100));
  if (muted) { output_fade_q15 = 0; }
}

void sampler_audio_t::setExternalInputMonitor(bool enabled)
{
  external_input_monitor = enabled;
  output_fade_target_q15 = enabled ? 32768 : 0;
  // Ten milliseconds removes route-switch edges without making monitoring
  // feel detached from the source.
  output_fade_step_q15 = (uint16_t)(
    (32768 + output_sample_rate / 100 - 1) / (output_sample_rate / 100));
  if (enabled) { output_fade_q15 = 0; }
}

bool sampler_audio_t::attachDeckStream(int16_t* pcm, uint32_t capacity_frames)
{
  if (!pcm || capacity_frames < 1024) { return false; }
  deck_stream.playing = false;
  deck_stream.pcm = pcm;
  deck_stream.capacity = capacity_frames;
  deck_stream.read_frames = 0;
  deck_stream.write_frames = 0;
  deck_stream.underruns = 0;
  return true;
}

void sampler_audio_t::detachDeckStream(void)
{
  deck_stream.playing = false;
  deck_stream.pcm = nullptr;
  deck_stream.capacity = 0;
  deck_stream.read_frames = 0;
  deck_stream.write_frames = 0;
}

void sampler_audio_t::setDeckStreamPlaying(bool playing)
{
  deck_stream.playing = playing;
}

void sampler_audio_t::setDeckStreamVolumeQ8(uint16_t volume_q8)
{
  deck_stream.target_volume_q8 = std::min<uint16_t>(volume_q8, 1024);
}

uint32_t sampler_audio_t::deckStreamBufferedFrames(void)
{
  const uint32_t read = __atomic_load_n(&deck_stream.read_frames, __ATOMIC_ACQUIRE);
  const uint32_t write = __atomic_load_n(&deck_stream.write_frames, __ATOMIC_ACQUIRE);
  return write - read;
}

uint32_t sampler_audio_t::deckStreamPlayedFrames(void)
{
  return __atomic_load_n(&deck_stream.read_frames, __ATOMIC_ACQUIRE);
}

uint32_t sampler_audio_t::deckStreamWritableFrames(void)
{
  if (!deck_stream.pcm || deck_stream.capacity == 0) { return 0; }
  const uint32_t buffered = deckStreamBufferedFrames();
  return buffered < deck_stream.capacity ? deck_stream.capacity - buffered : 0;
}

uint32_t sampler_audio_t::enqueueDeckStream(const int16_t* pcm, uint32_t frames)
{
  if (!pcm || !deck_stream.pcm || deck_stream.capacity == 0) { return 0; }
  frames = std::min<uint32_t>(frames, deckStreamWritableFrames());
  uint32_t write = __atomic_load_n(&deck_stream.write_frames, __ATOMIC_RELAXED);
  for (uint32_t i = 0; i < frames; ++i) {
    const uint32_t index = (write + i) % deck_stream.capacity;
    deck_stream.pcm[index * 2] = pcm[i * 2];
    deck_stream.pcm[index * 2 + 1] = pcm[i * 2 + 1];
  }
  __atomic_store_n(&deck_stream.write_frames, write + frames, __ATOMIC_RELEASE);
  return frames;
}

uint32_t sampler_audio_t::deckStreamUnderruns(void)
{
  return __atomic_load_n(&deck_stream.underruns, __ATOMIC_RELAXED);
}

void sampler_audio_t::releaseStartupMute(void)
{
  output_muted = false;
  output_fade_q15 = 0;
  output_fade_target_q15 = 32768;
  // One second from digital silence to the saved master level.  The first
  // part of this ramp is the safe initial output level, while the existing
  // master-volume slew and limiter continue to protect the final level.
  output_fade_step_q15 = 1;
}

void sampler_audio_t::setOutputGainPercent(uint8_t percent)
{
  if (percent < 100) { percent = 100; }
  if (percent > 200) { percent = 200; }
  output_gain_q8 = (uint16_t)(((uint32_t)percent << 8) / 100);
}

static int8_t clamp_fx_param(int value)
{
  if (value < -100) { value = -100; }
  if (value > 100) { value = 100; }
  return (int8_t)value;
}

void sampler_audio_t::setFx(uint8_t index, bool active, int8_t param)
{
  if (index >= audio_fx_count) { return; }
  fx[index].param = clamp_fx_param(param);
  fx[index].active = active;
  if (index == audio_fx_tempo && !active) { fx_speed_ratio_q8 = 256; }
  if (index == audio_fx_tempo) { update_voice_steps(); }
  if (index == audio_fx_gater && active) { gater_phase = 0; }
  if (index == audio_fx_crusher && active) { crusher_hold_remaining = 0; }
}

void sampler_audio_t::setFxActive(uint8_t index, bool active)
{
  if (index < audio_fx_count) {
    fx[index].active = active;
    if (index == audio_fx_tempo && !active) { fx_speed_ratio_q8 = 256; }
    if (index == audio_fx_tempo) { update_voice_steps(); }
    if (index == audio_fx_gater && active) { gater_phase = 0; }
    if (index == audio_fx_crusher && active) { crusher_hold_remaining = 0; }
  }
}

void sampler_audio_t::setFxParam(uint8_t index, int8_t param)
{
  if (index >= audio_fx_count) { return; }
  fx[index].param = clamp_fx_param(param);
  if (index == audio_fx_tempo) { update_voice_steps(); }
}

void sampler_audio_t::setFxSpeedRatioQ8(uint16_t ratio_q8)
{
  if (ratio_q8 < 128) { ratio_q8 = 128; }
  if (ratio_q8 > 512) { ratio_q8 = 512; }
  fx_speed_ratio_q8 = ratio_q8;
  if (fx[0].active) { update_voice_steps(); }
}

void sampler_audio_t::setFxQuantizeStepMs(uint32_t step_ms)
{
  fx_quantize_step_frames = std::max<uint32_t>(1,
    ((uint64_t)output_sample_rate * std::max<uint32_t>(1, step_ms)) / 1000u);
}

void sampler_audio_t::setFxTargetMask(uint8_t mask)
{
  mask &= fx_target_all;
  if (mask == 0) { mask = fx_target_all; }
  const uint8_t previous = active_fx_target_mask;
  active_fx_target_mask = mask;
  if (previous != mask) {
    // Old target audio must never leak into a later Repeat/Delay/Scratch.
    deck_buffer.reset_pending = true;
    filter_l = 0;
    filter_r = 0;
    update_voice_steps();
  }
}

uint8_t sampler_audio_t::fxTargetMask(void)
{
  return active_fx_target_mask;
}

void sampler_audio_t::setVoiceFxTarget(uint8_t voice, uint8_t target)
{
  if (voice >= max_voice) { return; }
  target &= fx_target_all;
  if (target != fx_target_beat) { target = fx_target_parts; }
  voices[voice].fx_target = target;
  if (voices[voice].active) {
    voices[voice].step_fp = pitch_step_fp(voices[voice].base_step_fp, target);
  }
}

void sampler_audio_t::setTapeStop(bool active)
{
  tape_stop.requested = active && tape_stop.pcm != nullptr;
  if (active) {
    master_scratch.requested = false;
    master_repeat.requested = false;
    master_delay.requested = false;
    master_delay.cancel_pending = true;
  }
}

void sampler_audio_t::setTapeStopDurationMs(uint32_t duration_ms)
{
  const uint32_t min_ms = 100;
  const uint32_t max_ms = 2000;
  duration_ms = std::max<uint32_t>(min_ms, std::min<uint32_t>(max_ms, duration_ms));
  tape_stop.requested_ramp_frames = (uint32_t)(((uint64_t)output_sample_rate * duration_ms) / 1000u);
}

bool sampler_audio_t::tapeStopAvailable(void)
{
  return tape_stop.pcm != nullptr;
}

void sampler_audio_t::setDeckBufferEnabled(bool enabled)
{
  if (!tape_stop.pcm) { enabled = false; }
  if (enabled && !deck_buffer.enabled) {
    // The I2S task owns the 64-bit cursor; reset it there rather than tearing
    // a write from this 32-bit UI core.
    deck_buffer.reset_pending = true;
  }
  deck_buffer.enabled = enabled;
  if (!enabled) {
    master_scratch.requested = false;
    master_repeat.requested = false;
    tape_stop.requested = false;
    master_delay.requested = false;
    master_delay.cancel_pending = true;
  }
}

void sampler_audio_t::setMasterScratch(bool active)
{
  master_scratch.requested = active && tape_stop.pcm != nullptr
                           && deck_buffer.enabled;
  if (active) {
    tape_stop.requested = false;
    master_repeat.requested = false;
    master_delay.requested = false;
    master_delay.cancel_pending = true;
  }
}

void sampler_audio_t::setMasterScratchRateQ8(int16_t rate_q8)
{
  master_scratch.rate_q8 = std::clamp<int16_t>(rate_q8, -512, 512);
}

bool sampler_audio_t::masterScratchAvailable(void)
{
  return tape_stop.pcm != nullptr;
}

void sampler_audio_t::setMasterRepeatFrames(uint32_t repeat_frames, uint32_t capture_frames)
{
  if (!tape_stop.pcm || tape_stop.capacity <= repeat_deck_guard_frames) { return; }
  const uint32_t maximum = tape_stop.capacity - repeat_deck_guard_frames;
  repeat_frames = std::clamp<uint32_t>(repeat_frames, 1, maximum);
  capture_frames = std::clamp<uint32_t>(capture_frames, repeat_frames, maximum);
  master_repeat.requested_frames = repeat_frames;
  master_repeat.requested_capture_frames = capture_frames;
}

void sampler_audio_t::setMasterRepeat(bool active)
{
  master_repeat.requested = active && tape_stop.pcm != nullptr
                          && deck_buffer.enabled;
  if (active) {
    tape_stop.requested = false;
    master_scratch.requested = false;
    master_delay.requested = false;
    master_delay.cancel_pending = true;
  }
}

bool sampler_audio_t::masterRepeatAvailable(void)
{
  return tape_stop.pcm != nullptr;
}

void sampler_audio_t::setMasterDelayFrames(uint32_t delay_frames)
{
  if (!tape_stop.pcm || tape_stop.capacity < 2) { return; }
  master_delay.requested_frames = std::clamp<uint32_t>(
    delay_frames, 1, tape_stop.capacity - 1u);
}

void sampler_audio_t::setMasterDelay(bool active)
{
  master_delay.requested = active && tape_stop.pcm != nullptr
                         && deck_buffer.enabled;
  if (active) {
    master_delay.cancel_pending = false;
    tape_stop.requested = false;
    master_scratch.requested = false;
    master_repeat.requested = false;
  }
}

bool sampler_audio_t::masterDelayAvailable(void)
{
  return tape_stop.pcm != nullptr;
}

bool sampler_audio_t::startRecording(int16_t* buffer, uint32_t capacity_frames, uint32_t initial_frames)
{
  if (buffer == nullptr || capacity_frames == 0 || initial_frames >= capacity_frames) { return false; }
  recorder.active = false;
  recorder.level_l = 0;
  recorder.level_r = 0;
  recorder.level_mix = 0;
  recorder.buffer = buffer;
  recorder.capacity = capacity_frames;
  recorder.frames = initial_frames;
  recorder.overflow = false;
  recorder.active = true;
  return true;
}

uint32_t sampler_audio_t::stopRecording(void)
{
  recorder.active = false;
  return recorder.frames;
}

uint32_t sampler_audio_t::recordingFrames(void)
{
  return recorder.frames;
}

bool sampler_audio_t::isRecording(void)
{
  return recorder.active;
}

bool sampler_audio_t::recordingOverflowed(void)
{
  return recorder.overflow;
}

bool sampler_audio_t::startOutputCapture(int16_t* buffer, uint32_t capacity_frames)
{
  if (!buffer || capacity_frames == 0 || output_stream_capture.active) { return false; }
  output_capture.active = false;
  output_capture.buffer = buffer;
  output_capture.capacity = capacity_frames;
  output_capture.frames = 0;
  output_capture.active = true;
  return true;
}

uint32_t sampler_audio_t::stopOutputCapture(void)
{
  output_capture.active = false;
  return output_capture.frames;
}

uint32_t sampler_audio_t::outputCaptureFrames(void)
{
  return output_capture.frames;
}

bool sampler_audio_t::startOutputStreamCapture(int16_t* ring, uint32_t capacity_frames)
{
  // Keep one empty frame to distinguish a full ring from an empty one.
  if (!ring || capacity_frames < 2 || output_capture.active) { return false; }
  output_stream_capture.active = false;
  output_stream_capture.buffer = ring;
  output_stream_capture.capacity = capacity_frames;
  output_stream_capture.write_pos = 0;
  output_stream_capture.read_pos = 0;
  output_stream_capture.overflow = false;
  output_stream_capture.active = true;
  return true;
}

void sampler_audio_t::stopOutputStreamCapture(void)
{
  output_stream_capture.active = false;
}

uint32_t sampler_audio_t::readOutputStreamCapture(int16_t* dst, uint32_t max_frames)
{
  if (!dst || max_frames == 0 || !output_stream_capture.buffer
   || output_stream_capture.capacity < 2) { return 0; }
  const uint32_t capacity = output_stream_capture.capacity;
  const uint32_t read = output_stream_capture.read_pos;
  const uint32_t write = output_stream_capture.write_pos;
  uint32_t available = write >= read ? write - read : capacity - read + write;
  if (available > max_frames) { available = max_frames; }
  if (available == 0) { return 0; }
  const uint32_t first = std::min<uint32_t>(available, capacity - read);
  memcpy(dst, output_stream_capture.buffer + read, first * sizeof(int16_t));
  if (available > first) {
    memcpy(dst + first, output_stream_capture.buffer, (available - first) * sizeof(int16_t));
  }
  output_stream_capture.read_pos = (read + available) % capacity;
  return available;
}

bool sampler_audio_t::outputStreamCaptureActive(void)
{
  return output_stream_capture.active;
}

bool sampler_audio_t::outputStreamCaptureOverflowed(void)
{
  return output_stream_capture.overflow;
}

static inline void capture_output_frame(int32_t left, int32_t right)
{
  if ((!output_capture.active || output_capture.frames >= output_capture.capacity)
   && !output_stream_capture.active) { return; }
  int64_t mono = ((int64_t)left + right) / 2;
  mono >>= 16;
  if (mono > INT16_MAX) { mono = INT16_MAX; }
  if (mono < INT16_MIN) { mono = INT16_MIN; }
  if (output_capture.active && output_capture.frames < output_capture.capacity) {
    output_capture.buffer[output_capture.frames++] = (int16_t)mono;
    if (output_capture.frames >= output_capture.capacity) { output_capture.active = false; }
  }

  if (!output_stream_capture.active || !output_stream_capture.buffer
   || output_stream_capture.capacity < 2) { return; }
  const uint32_t write = output_stream_capture.write_pos;
  const uint32_t next = (write + 1u) % output_stream_capture.capacity;
  if (next == output_stream_capture.read_pos) {
    // I2S must never wait for SD. Flag the incomplete take and drop only the
    // newest frame; the writer will report failure when the transport stops.
    output_stream_capture.overflow = true;
    return;
  }
  output_stream_capture.buffer[write] = (int16_t)mono;
  output_stream_capture.write_pos = next;
}

// 1フレーム分のボイス合成値を求めて加算する (16bit値を32bitフルスケールに拡張して加算)
struct mixed_buses_t {
  int64_t beat = 0;
  int64_t parts = 0;
};

static inline void record_voice_probe(voice_t& v)
{
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
    const uint32_t probe_edge_usec = v.probe_edge_usec;
    if (probe_edge_usec != 0) {
      const uint8_t kind = v.probe_kind;
      v.probe_edge_usec = 0;
      v.probe_kind = 0;
      performance_probe::metric_t metric;
      switch (kind) {
      case 1: metric = performance_probe::metric_t::live_note_on_to_audio; break;
      case 2: metric = performance_probe::metric_t::live_note_off_to_audio; break;
      case 3: metric = performance_probe::metric_t::rec_note_on_to_audio; break;
      case 4: metric = performance_probe::metric_t::rec_note_off_to_audio; break;
      default: metric = performance_probe::metric_t::count; break;
      }
      if (metric != performance_probe::metric_t::count) {
        performance_probe::record(metric, M5.micros() - probe_edge_usec);
      }
    }
#endif
}

static inline mixed_buses_t mix_voices(uint32_t active, uint8_t voice_base = 0)
{
  mixed_buses_t mixed;
  while (active) {
    const uint8_t n = voice_base + (uint8_t)__builtin_ctz(active);
    active &= active - 1;
    auto& v = voices[n];
    if (!v.active) { continue; }
    if (!pcm_render::advance_envelope(v)) {
      if (!v.active) { deactivate_voice(n); }
      continue;
    }
    // Seeking a running loop at a non-zero crossing can click. Fade only this
    // voice for two milliseconds, move its position while silent, then fade
    // it back in. The request itself still comes from the UI without touching
    // playback fields concurrently.
    static constexpr uint16_t seek_fade_frames = 96;  // 2ms at 48kHz
    if (v.seek_pending && v.seek_fade_state == 0) {
      v.seek_target_frame = v.seek_frame;
      v.seek_pending = false;
      v.seek_fade_remaining = seek_fade_frames;
      v.seek_fade_state = 1;
    }
    uint16_t seek_gain_q15 = 32768;
    if (v.seek_fade_state == 1) {
      seek_gain_q15 = (uint16_t)(((uint32_t)v.seek_fade_remaining << 15) / seek_fade_frames);
      if (--v.seek_fade_remaining == 0) {
        uint32_t target = v.seek_target_frame;
        if (target >= v.frames) { target = 0; }
        v.pos_fp = (int64_t)target << 16;
        v.frame_for_ui = target;
        v.seek_fade_remaining = seek_fade_frames;
        v.seek_fade_state = 2;
      }
    } else if (v.seek_fade_state == 2) {
      seek_gain_q15 = (uint16_t)(((uint32_t)(seek_fade_frames - v.seek_fade_remaining) << 15) / seek_fade_frames);
      if (--v.seek_fade_remaining == 0) { v.seek_fade_state = 0; }
    }
    // The sustain body must preserve the source resolution as well. A prior
    // performance-priority shortcut raised this divider to four while a new
    // note arrived. That sample-and-hold path made a held synth note acquire
    // a metallic texture exactly on the next Note On/Off. RAM source caches
    // and UI throttling provide the headroom without degrading audio here.
    const uint8_t divider = v.render_divider;
    const bool render_now = !v.render_sample_valid || divider == 1 || v.render_phase == 0;
    int32_t s = v.render_sample;
    if (render_now) {
      const uint32_t loop_end = v.loop_end_frame ? v.loop_end_frame : v.frames;
      const int64_t loop_start_fp = (int64_t)v.loop_start_frame << 16;
      const int64_t loop_end_fp = (int64_t)loop_end << 16;
      // Forward sustain playback must pass through the attack once. Being
      // before Loop In is valid until the first arrival at Loop Out. The lower
      // boundary is only a wrap condition while tape/scratch playback moves
      // backwards.
      const bool crossed_loop_end = v.playback_rate_q16 >= 0 && v.pos_fp >= loop_end_fp;
      const bool crossed_loop_start = v.playback_rate_q16 < 0 && v.pos_fp < loop_start_fp;
      if (crossed_loop_end || crossed_loop_start) {
        if (v.loop && loop_end > v.loop_start_frame) {
          if (v.pos_fp >= loop_end_fp) {
            uint32_t restart = v.loop_start_frame + v.loop_crossfade_frames;
            if (restart >= loop_end) { restart = v.loop_start_frame; }
            const int64_t restart_fp = (int64_t)restart << 16;
            const int64_t span_fp = ((int64_t)loop_end - restart) << 16;
            const int64_t over_fp = v.pos_fp - loop_end_fp;
            v.pos_fp = restart_fp + (span_fp ? over_fp % span_fp : 0);
          } else {
            const int64_t span_fp = loop_end_fp - loop_start_fp;
            const int64_t under_fp = loop_start_fp - v.pos_fp;
            const int64_t remainder = span_fp ? under_fp % span_fp : 0;
            v.pos_fp = remainder == 0 ? loop_start_fp : loop_end_fp - remainder;
          }
        } else {
          v.active = false;
          deactivate_voice(n);
          continue;
        }
      }
      uint32_t idx = (uint32_t)(v.pos_fp >> 16);
      v.frame_for_ui = idx;
      uint32_t frac = v.pos_fp & 0xFFFF;
      s = voice_pcm_at(v, idx);
      if (frac != 0 && v.linear_interpolation) {
        uint32_t idx1 = idx + 1;
        if (v.reverse) {
          idx1 = idx1 >= v.frames ? (v.loop ? 0 : idx) : idx1;
        } else {
          idx1 = idx1 >= loop_end && v.loop
            ? std::min<uint32_t>(v.loop_start_frame + v.loop_crossfade_frames, loop_end - 1)
            : (idx1 >= v.frames ? idx : idx1);
        }
        const int32_t s1 = voice_pcm_at(v, idx1);
        s = pcm_render::interpolate(s, s1, frac);
      }
      if (!v.reverse && v.playback_rate_q16 >= 0 && v.loop_crossfade_frames
       && idx >= loop_end - v.loop_crossfade_frames && idx < loop_end) {
        uint32_t offset = idx - (loop_end - v.loop_crossfade_frames);
        uint32_t head_idx = v.loop_start_frame + offset;
        if (head_idx < v.frames) {
          const int32_t head = voice_pcm_at(v, head_idx);
          const uint32_t mix_q15 = (offset << 15) / v.loop_crossfade_frames;
          s = (int32_t)(((int64_t)s * (32768 - mix_q15) + (int64_t)head * mix_q15) >> 15);
        }
      }
      v.render_sample = s;
      v.render_sample_valid = true;
    }
    if (v.volume_q8 < v.target_volume_q8) { ++v.volume_q8; }
    else if (v.volume_q8 > v.target_volume_q8) { --v.volume_q8; }
    if (v.volume_q8 != 256) { s = (int32_t)(((int64_t)s * v.volume_q8) >> 8); }
    if (v.edge_fade_in_end || v.edge_fade_out_start < v.frames) {
      uint32_t edge_gain_q15 = 32768;
      uint32_t edge_frame = v.frame_for_ui;
      if (edge_frame >= v.frames) { edge_frame = v.frames - 1; }
      if (v.edge_fade_in_end && edge_frame < v.edge_fade_in_end) {
        edge_gain_q15 = (uint32_t)(((uint64_t)edge_frame * 32768u)
                                  / v.edge_fade_in_end);
      }
      if (v.edge_fade_out_start < v.frames && edge_frame >= v.edge_fade_out_start) {
        const uint32_t fade_frames = v.frames - v.edge_fade_out_start;
        const uint32_t remaining = v.frames - edge_frame - 1u;
        const uint32_t fade_gain_q15 = fade_frames
          ? (uint32_t)(((uint64_t)remaining * 32768u) / fade_frames) : 0;
        edge_gain_q15 = std::min<uint32_t>(edge_gain_q15, fade_gain_q15);
      }
      if (edge_gain_q15 != 32768) {
        s = (int32_t)(((int64_t)s * edge_gain_q15) >> 15);
      }
    }
    if (seek_gain_q15 != 32768) { s = (int32_t)(((int64_t)s * seek_gain_q15) >> 15); }
    if (v.envelope_q15 != 32768) {
      s = (int32_t)(((int64_t)s * v.envelope_q15) >> 15);
    }
    if (v.tone_cutoff < 127 || v.tone_resonance != 0) {
      // Two cheap one-pole stages make a stable low-pass. The difference
      // between stages is the band around its cutoff; adding a little of it
      // back gives Touch Play a musical resonance without affecting any
      // other voice or requiring a biquad per playing Pad.
      // The Touch Play surface needs a clearly audible left-to-right sweep.
      // A near-zero alpha at the far left makes the low-pass genuinely dark;
      // 127 still resolves to 256, which is a transparent bypass.
      const int alpha = 4 + ((int)v.tone_cutoff * 252) / 127;
      v.tone_filter_1 += ((s - v.tone_filter_1) * alpha) >> 8;
      v.tone_filter_2 += ((v.tone_filter_1 - v.tone_filter_2) * alpha) >> 8;
      const int32_t band = v.tone_filter_1 - v.tone_filter_2;
      // At the right side of Touch Play, give the selected high band enough
      // gain to be heard as a distinct bright/resonant state. The limiter
      // after the mixer remains the final guard for several simultaneous pads.
      s = v.tone_filter_2 + (band * v.tone_resonance * 3) / 256;
    }
    if (v.fx_target == sampler_audio_t::fx_target_beat) {
      mixed.beat += (int64_t)s << 16;
    } else {
      mixed.parts += (int64_t)s << 16;
    }
    if (++v.render_phase >= divider) { v.render_phase = 0; }
    if (render_now) {
      v.pos_fp += (((int64_t)v.step_fp * v.playback_rate_q16) >> 16) * divider;
    }
  }
  return mixed;
}

// 48 stereo frames: 768 bytes of fixed internal RAM, no PCM copies or queue.
// Voice-major rendering reuses one voice's state/source while it is hot in
// cache. Event publication is observed on every 1ms block, including release.
static mixed_buses_t mixed_block[i2s_dma_frame_num / 2];
static void mix_voice_block(void)
{
  for (auto& frame : mixed_block) { frame = {}; }
  uint32_t general[2] = {};
  for (uint8_t group = 0; group < 2; ++group) {
    uint32_t active = __atomic_load_n(&active_voice_mask[group], __ATOMIC_ACQUIRE);
    while (active) {
      const uint8_t index = (group << 5) + (uint8_t)__builtin_ctz(active);
      active &= active - 1u;
      if (index >= sampler_audio_t::max_voice) { continue; }
      auto& voice = voices[index];
      if (!voice.active) { deactivate_voice(index); continue; }
      record_voice_probe(voice);
#if defined(KANPLAY_SAMPLER_PCM_REFERENCE)
      general[group] |= 1u << (index & 31u);
#else
      if (pcm_render::supports(voice)) {
        pcm_render::render(voice, mixed_block, i2s_dma_frame_num / 2,
                           voice.fx_target == sampler_audio_t::fx_target_beat);
        if (!voice.active) { deactivate_voice(index); }
      } else {
        general[group] |= 1u << (index & 31u);
      }
#endif
    }
  }
  if (general[0] || general[1]) {
    for (auto& frame : mixed_block) {
      for (uint8_t group = 0; group < 2; ++group) {
        const auto other = mix_voices(general[group], group << 5);
        frame.beat += other.beat;
        frame.parts += other.parts;
      }
    }
  }
}

static inline int32_t saturate32(int64_t value)
{
  if (value > INT32_MAX) { return INT32_MAX; }
  if (value < INT32_MIN) { return INT32_MIN; }
  return (int32_t)value;
}

static inline int64_t abs64_limit(int64_t value)
{
  if (value == INT64_MIN) { return INT64_MAX; }
  return value < 0 ? -value : value;
}

static inline void process_output_limiter(int64_t& l, int64_t& r)
{
  static constexpr const int64_t threshold = (int64_t)INT32_MAX / 4 * 3;  // 約75%。多重発音時の余裕を確保する。
  int64_t peak_l = abs64_limit(l);
  int64_t peak_r = abs64_limit(r);
  int64_t peak = peak_l > peak_r ? peak_l : peak_r;
  // A dense Pad chord can remain above the raw threshold for thousands of
  // samples. The former code performed a 64-bit division for every one of
  // those samples even when the existing limiter gain was already protecting
  // the output. First test that protected peak; the expensive exact division
  // now runs only on a genuine new overload, while recovery still happens at
  // the same gentle rate.
  // The overwhelmingly common one-shot path is already at unity gain and
  // below the threshold. Avoid even the 64-bit gain check there; this keeps
  // the inexpensive case inexpensive while the overload path below remains
  // sample-accurate.
  const bool unity_gain = limiter_gain_q15 == 32768;
  const int64_t protected_peak = unity_gain ? peak : (peak * limiter_gain_q15) >> 15;
  if (protected_peak > threshold) {
    int32_t target_gain_q15 = (int32_t)((threshold << 15) / peak);
    // Keep enough range for the theoretical 30-voice PCM bus plus SAM input.
    // A higher floor lets the final int32 conversion clip before the limiter,
    // which can erase a quieter SAM2695 note underneath a dense PCM chord.
    if (target_gain_q15 < 256) { target_gain_q15 = 256; }
    if (target_gain_q15 < limiter_gain_q15) {
      limiter_gain_q15 = target_gain_q15;  // attack: immediate
    }
  } else if (limiter_gain_q15 < 32768) {
    const int32_t diff = 32768 - limiter_gain_q15;
    limiter_gain_q15 += diff > 1024 ? diff >> 10 : 1;  // release: about 20ms
  }

  if (limiter_gain_q15 < 32768) {
    l = (l * limiter_gain_q15) >> 15;
    r = (r * limiter_gain_q15) >> 15;
  }
}

static inline int16_t tape_stop_pcm16(int64_t value)
{
  value >>= 16;
  if (value > INT16_MAX) { return INT16_MAX; }
  if (value < INT16_MIN) { return INT16_MIN; }
  return (int16_t)value;
}

static inline int32_t tape_stop_pcm32(int16_t value)
{
  return (int32_t)value << 16;
}

static inline void reset_deck_history_if_pending(void)
{
  if (!deck_buffer.reset_pending) { return; }
  deck_buffer.write_frames = 0;
  deck_buffer.valid_frames = 0;
  deck_buffer.recent_frames = 0;
  master_scratch.state = master_scratch_state_t::idle;
  master_repeat.state = master_repeat_state_t::idle;
  tape_stop.state = tape_stop_state_t::idle;
  deck_buffer.reset_pending = false;
}

static inline void write_deck_frame(int64_t l, int64_t r)
{
  if (!deck_buffer.enabled || !tape_stop.pcm || tape_stop.capacity == 0) { return; }
  reset_deck_history_if_pending();
  // Once the complete Repeat source is present, preserve it in place. The
  // dry engine still runs; Deck recording resumes as soon as Repeat releases.
  if (master_repeat.requested
   && !tape_stop.requested && !master_scratch.requested
   && master_repeat.state == master_repeat_state_t::active
   && deck_buffer.write_frames >= master_repeat.start_frame + master_repeat.capture_frames) {
    return;
  }
  // Repeat freezes the writer after capture. The first resumed frame is not
  // temporally adjacent to the old window, so Scratch may only look back into
  // frames written from this point onward.
  if (!master_repeat.requested
   && master_repeat.state == master_repeat_state_t::active) {
    deck_buffer.recent_frames = 0;
  }
  const uint32_t index = (uint32_t)(deck_buffer.write_frames % tape_stop.capacity);
  tape_stop.pcm[index * 2u] = tape_stop_pcm16(l);
  tape_stop.pcm[index * 2u + 1u] = tape_stop_pcm16(r);
  ++deck_buffer.write_frames;
  if (deck_buffer.valid_frames < tape_stop.capacity) { ++deck_buffer.valid_frames; }
  if (deck_buffer.recent_frames < tape_stop.capacity) { ++deck_buffer.recent_frames; }
}

static inline bool read_deck_frame(int64_t read_fp, int64_t& l, int64_t& r)
{
  if (!tape_stop.pcm || tape_stop.capacity == 0 || deck_buffer.valid_frames == 0) { return false; }
  const int64_t oldest = (int64_t)(deck_buffer.write_frames - deck_buffer.valid_frames);
  const int64_t newest = (int64_t)deck_buffer.write_frames - 1;
  int64_t frame = read_fp >> 16;
  if (frame < oldest) { frame = oldest; }
  if (frame > newest) { frame = newest; }
  const int64_t next_frame = std::min<int64_t>(newest, frame + 1);
  const uint32_t index = (uint32_t)((uint64_t)frame % tape_stop.capacity);
  const uint32_t next = (uint32_t)((uint64_t)next_frame % tape_stop.capacity);
  const uint16_t fraction = (uint16_t)(read_fp & 0xFFFFu);
  const int32_t left0 = tape_stop_pcm32(tape_stop.pcm[index * 2u]);
  const int32_t left1 = tape_stop_pcm32(tape_stop.pcm[next * 2u]);
  const int32_t right0 = tape_stop_pcm32(tape_stop.pcm[index * 2u + 1u]);
  const int32_t right1 = tape_stop_pcm32(tape_stop.pcm[next * 2u + 1u]);
  l = left0 + ((((int64_t)left1 - left0) * fraction) >> 16);
  r = right0 + ((((int64_t)right1 - right0) * fraction) >> 16);
  return true;
}

static inline bool read_delay_tap(uint32_t delay_frames, int64_t& l, int64_t& r)
{
  if (delay_frames == 0 || deck_buffer.valid_frames < delay_frames) { return false; }
  const uint64_t frame = deck_buffer.write_frames - delay_frames;
  return read_deck_frame((int64_t)frame << 16, l, r);
}

// Returns true when Delay owned the Deck writer for this frame. Other Deck
// effects then skip their post-limiter write, avoiding two incompatible time
// domains in the shared ring. The delayed sum itself still passes through the
// normal limiter immediately after this function.
static inline bool process_master_delay(int64_t& l, int64_t& r)
{
  if (master_delay.cancel_pending || !deck_buffer.enabled || !tape_stop.pcm) {
    if (master_delay.state != master_delay_state_t::idle) {
      deck_buffer.reset_pending = true;
    }
    master_delay.requested = false;
    master_delay.cancel_pending = false;
    master_delay.state = master_delay_state_t::idle;
    return false;
  }

  if (master_delay.requested && master_delay.state == master_delay_state_t::idle) {
    const uint32_t requested_frames = master_delay.requested_frames;
    master_delay.delay_frames = std::clamp<uint32_t>(
      requested_frames, 1, tape_stop.capacity - 1u);
    master_delay.previous_frames = master_delay.delay_frames;
    master_delay.transition_frames = 0;
    master_delay.release_frames = 0;
    master_delay.release_limit_frames = 0;
    deck_buffer.reset_pending = true;
    reset_deck_history_if_pending();
    master_delay.state = master_delay_state_t::active;
  } else if (!master_delay.requested
          && master_delay.state == master_delay_state_t::active) {
    master_delay.state = master_delay_state_t::releasing;
    master_delay.release_frames = 0;
    master_delay.release_limit_frames = std::min<uint32_t>(
      delay_max_tail_frames, std::max<uint32_t>(master_delay.delay_frames,
                                                master_delay.delay_frames * 6u));
  } else if (master_delay.requested
          && master_delay.state == master_delay_state_t::releasing) {
    // A quick second gesture should feed the still-audible tail immediately,
    // not wait for its two-second safety limit to expire.
    master_delay.state = master_delay_state_t::active;
    master_delay.release_frames = 0;
    master_delay.release_limit_frames = 0;
  }

  if (master_delay.state == master_delay_state_t::idle) { return false; }
  if (master_delay.state == master_delay_state_t::releasing
   && master_delay.release_frames >= master_delay.release_limit_frames) {
    master_delay.state = master_delay_state_t::idle;
    deck_buffer.reset_pending = true;
    return false;
  }

  if (master_delay.state == master_delay_state_t::active) {
    const uint32_t requested_frames = master_delay.requested_frames;
    const uint32_t requested = std::clamp<uint32_t>(
      requested_frames, 1, tape_stop.capacity - 1u);
    if (requested != master_delay.delay_frames) {
      master_delay.previous_frames = master_delay.delay_frames;
      master_delay.delay_frames = requested;
      master_delay.transition_frames = delay_transition_frames;
    }
  }

  int64_t delayed_l = 0;
  int64_t delayed_r = 0;
  const bool have_new = read_delay_tap(master_delay.delay_frames, delayed_l, delayed_r);
  if (master_delay.transition_frames != 0) {
    int64_t old_l = 0;
    int64_t old_r = 0;
    const bool have_old = read_delay_tap(master_delay.previous_frames, old_l, old_r);
    const uint32_t progress = delay_transition_frames - master_delay.transition_frames;
    const uint32_t new_q15 = (progress * 32768u) / delay_transition_frames;
    if (!have_new) { delayed_l = delayed_r = 0; }
    if (!have_old) { old_l = old_r = 0; }
    delayed_l = ((old_l * (32768u - new_q15)) + (delayed_l * new_q15)) >> 15;
    delayed_r = ((old_r * (32768u - new_q15)) + (delayed_r * new_q15)) >> 15;
    --master_delay.transition_frames;
  } else if (!have_new) {
    delayed_l = delayed_r = 0;
  }

  const int64_t dry_l = l;
  const int64_t dry_r = r;
  const bool accept_input = master_delay.state == master_delay_state_t::active;
  const int64_t feedback_l = (delayed_l * delay_feedback_q15) >> 15;
  const int64_t feedback_r = (delayed_r * delay_feedback_q15) >> 15;
  write_deck_frame((accept_input ? dry_l : 0) + feedback_l,
                   (accept_input ? dry_r : 0) + feedback_r);
  l = dry_l + ((delayed_l * delay_wet_q15) >> 15);
  r = dry_r + ((delayed_r * delay_wet_q15) >> 15);
  if (master_delay.state == master_delay_state_t::releasing) {
    ++master_delay.release_frames;
  }
  return true;
}

static inline void process_master_scratch(int64_t& l, int64_t& r)
{
  if (master_scratch.requested && (master_scratch.state == master_scratch_state_t::idle
                                || master_scratch.state == master_scratch_state_t::releasing)) {
    if (deck_buffer.valid_frames < 2) { return; }
    const uint32_t headroom = std::min<uint32_t>(scratch_headroom_frames,
      deck_buffer.recent_frames > 1 ? deck_buffer.recent_frames - 1 : 0);
    const int64_t start = (int64_t)deck_buffer.write_frames - 1 - headroom;
    master_scratch.read_fp = start << 16;
    master_scratch.fade_frames = 0;
    master_scratch.state = master_scratch_state_t::active;
  } else if (!master_scratch.requested
          && master_scratch.state == master_scratch_state_t::active) {
    master_scratch.fade_frames = 0;
    master_scratch.state = master_scratch_state_t::releasing;
  }
  if (master_scratch.state == master_scratch_state_t::idle) { return; }

  int64_t wet_l = l;
  int64_t wet_r = r;
  if (!read_deck_frame(master_scratch.read_fp, wet_l, wet_r)) {
    master_scratch.state = master_scratch_state_t::idle;
    return;
  }
  uint32_t wet_q15 = 32768;
  if (master_scratch.state == master_scratch_state_t::active
   && master_scratch.fade_frames < deck_crossfade_frames) {
    wet_q15 = ((uint32_t)master_scratch.fade_frames++ * 32768u) / deck_crossfade_frames;
  } else if (master_scratch.state == master_scratch_state_t::releasing) {
    const uint32_t progress = std::min<uint32_t>(master_scratch.fade_frames++, deck_crossfade_frames);
    wet_q15 = 32768u - (progress * 32768u) / deck_crossfade_frames;
    if (progress >= deck_crossfade_frames) {
      master_scratch.state = master_scratch_state_t::idle;
      wet_q15 = 0;
    }
  }
  l = ((l * (32768u - wet_q15)) + (wet_l * wet_q15)) >> 15;
  r = ((r * (32768u - wet_q15)) + (wet_r * wet_q15)) >> 15;
  master_scratch.read_fp += (int64_t)master_scratch.rate_q8 << 8;
}

static inline void update_master_repeat_length(void)
{
  const uint32_t requested_capture = master_repeat.requested_capture_frames;
  const uint32_t requested_repeat = master_repeat.requested_frames;
  const uint32_t capture = std::max<uint32_t>(1, requested_capture);
  master_repeat.capture_frames = capture;
  master_repeat.repeat_frames = std::clamp<uint32_t>(requested_repeat, 1, capture);
  if (master_repeat.state != master_repeat_state_t::active) { return; }

  const int64_t start_fp = (int64_t)master_repeat.start_frame << 16;
  const int64_t length_fp = (int64_t)master_repeat.repeat_frames << 16;
  int64_t relative = master_repeat.read_fp - start_fp;
  relative %= length_fp;
  if (relative < 0) { relative += length_fp; }
  master_repeat.read_fp = start_fp + relative;
}

static inline void process_master_repeat(int64_t& l, int64_t& r)
{
  if (master_repeat.requested && master_repeat.state == master_repeat_state_t::idle) {
    master_repeat.start_frame = deck_buffer.write_frames > 0
      ? deck_buffer.write_frames - 1 : 0;
    master_repeat.fade_frames = 0;
    update_master_repeat_length();
    master_repeat.read_fp = (int64_t)master_repeat.start_frame << 16;
    master_repeat.state = master_repeat_state_t::capturing;
  } else if (!master_repeat.requested
          && master_repeat.state != master_repeat_state_t::idle
          && master_repeat.state != master_repeat_state_t::releasing) {
    master_repeat.fade_frames = 0;
    master_repeat.state = master_repeat_state_t::releasing;
  }
  if (master_repeat.state == master_repeat_state_t::idle) { return; }

  if (master_repeat.requested
   && (master_repeat.repeat_frames != master_repeat.requested_frames
    || master_repeat.capture_frames != master_repeat.requested_capture_frames)) {
    update_master_repeat_length();
  }

  // Let the requested musical window sound once and enter the Deck before
  // replacing the output. This also captures the middle of an already-active
  // sample or synth note, which event retriggering could never reproduce.
  const uint64_t repeat_end = master_repeat.start_frame + master_repeat.repeat_frames;
  if (master_repeat.state == master_repeat_state_t::capturing) {
    if (deck_buffer.write_frames <= repeat_end) { return; }
    master_repeat.read_fp = (int64_t)master_repeat.start_frame << 16;
    master_repeat.fade_frames = 0;
    master_repeat.state = master_repeat_state_t::active;
  }

  int64_t wet_l = l;
  int64_t wet_r = r;
  if (!read_deck_frame(master_repeat.read_fp, wet_l, wet_r)) {
    master_repeat.state = master_repeat_state_t::idle;
    return;
  }
  const int64_t start_fp = (int64_t)master_repeat.start_frame << 16;
  const int64_t relative_fp = master_repeat.read_fp - start_fp;
  const uint32_t relative_frame = relative_fp > 0 ? (uint32_t)(relative_fp >> 16) : 0;
  const uint32_t wrap_frames = std::min<uint32_t>(repeat_wrap_crossfade_frames,
    master_repeat.repeat_frames / 4u);
  if (wrap_frames > 1 && relative_frame >= master_repeat.repeat_frames - wrap_frames) {
    const uint32_t wrap_pos = relative_frame - (master_repeat.repeat_frames - wrap_frames);
    int64_t head_l = wet_l;
    int64_t head_r = wet_r;
    if (read_deck_frame(start_fp, head_l, head_r)) {
      const uint32_t head_q15 = (wrap_pos * 32768u) / (wrap_frames - 1u);
      wet_l = ((wet_l * (32768u - head_q15)) + (head_l * head_q15)) >> 15;
      wet_r = ((wet_r * (32768u - head_q15)) + (head_r * head_q15)) >> 15;
    }
  }
  uint32_t wet_q15 = 32768;
  if (master_repeat.state == master_repeat_state_t::active
   && master_repeat.fade_frames < deck_crossfade_frames) {
    wet_q15 = ((uint32_t)master_repeat.fade_frames++ * 32768u) / deck_crossfade_frames;
  } else if (master_repeat.state == master_repeat_state_t::releasing) {
    const uint32_t progress = std::min<uint32_t>(master_repeat.fade_frames++, deck_crossfade_frames);
    wet_q15 = 32768u - (progress * 32768u) / deck_crossfade_frames;
    if (progress >= deck_crossfade_frames) {
      master_repeat.state = master_repeat_state_t::idle;
      wet_q15 = 0;
    }
  }
  l = ((l * (32768u - wet_q15)) + (wet_l * wet_q15)) >> 15;
  r = ((r * (32768u - wet_q15)) + (wet_r * wet_q15)) >> 15;

  if (master_repeat.state == master_repeat_state_t::active
   || master_repeat.state == master_repeat_state_t::releasing) {
    const int64_t end_fp = (int64_t)(master_repeat.start_frame + master_repeat.repeat_frames) << 16;
    master_repeat.read_fp += 1ll << 16;
    if (master_repeat.read_fp >= end_fp) { master_repeat.read_fp = start_fp; }
  }
}

static inline void process_tape_stop(int64_t& l, int64_t& r)
{
  if (tape_stop.pcm == nullptr || tape_stop.capacity == 0) { return; }
  if (tape_stop.requested && (tape_stop.state == tape_stop_state_t::idle
                           || tape_stop.state == tape_stop_state_t::releasing)) {
    tape_stop.state = tape_stop_state_t::slowing;
    tape_stop.read_fp = ((int64_t)deck_buffer.write_frames - 1) << 16;
    tape_stop.elapsed_frames = 0;
    const uint32_t requested_frames = tape_stop.requested_ramp_frames;
    tape_stop.ramp_frames = std::min<uint32_t>(requested_frames, tape_stop_max_ramp_frames);
    tape_stop.release_frames = 0;
  } else if (!tape_stop.requested && tape_stop.state != tape_stop_state_t::idle
             && tape_stop.state != tape_stop_state_t::releasing) {
    tape_stop.state = tape_stop_state_t::releasing;
    tape_stop.release_frames = 0;
  }

  if (tape_stop.state == tape_stop_state_t::idle) { return; }
  if (tape_stop.state == tape_stop_state_t::releasing) {
    const uint32_t progress = std::min<uint32_t>(tape_stop.release_frames++, tape_stop_release_frames);
    const int32_t dry_q15 = (int32_t)((progress * 32768u) / tape_stop_release_frames);
    l = (l * dry_q15) >> 15;
    r = (r * dry_q15) >> 15;
    if (progress >= tape_stop_release_frames) { tape_stop.state = tape_stop_state_t::idle; }
    return;
  }
  if (tape_stop.state == tape_stop_state_t::stopped) {
    l = 0;
    r = 0;
    return;
  }

  if (!read_deck_frame(tape_stop.read_fp, l, r)) { return; }

  const uint32_t elapsed = std::min<uint32_t>(tape_stop.elapsed_frames++, tape_stop.ramp_frames);
  const uint32_t remaining = tape_stop.ramp_frames - elapsed;
  const uint32_t rate_q16 = (uint32_t)(((uint64_t)remaining * remaining << 16)
    / ((uint64_t)tape_stop.ramp_frames * tape_stop.ramp_frames));
  tape_stop.read_fp += rate_q16;
  const uint32_t fade_frames = output_sample_rate * 36u / 1000u;
  if (remaining < fade_frames) {
    const int32_t gain_q15 = (int32_t)((remaining * 32768u) / fade_frames);
    l = (l * gain_q15) >> 15;
    r = (r * gain_q15) >> 15;
  }
  if (elapsed >= tape_stop.ramp_frames) { tape_stop.state = tape_stop_state_t::stopped; }
}

static inline void process_deck_fx(int64_t& l, int64_t& r, bool writer_consumed = false)
{
  if (writer_consumed) { return; }
  write_deck_frame(l, r);
  if (tape_stop.requested) {
    master_scratch.state = master_scratch_state_t::idle;
    master_repeat.state = master_repeat_state_t::idle;
    process_tape_stop(l, r);
    return;
  }
  if (master_scratch.requested) {
    tape_stop.state = tape_stop_state_t::idle;
    master_repeat.state = master_repeat_state_t::idle;
    process_master_scratch(l, r);
    return;
  }
  if (master_repeat.requested || master_repeat.state != master_repeat_state_t::idle) {
    tape_stop.state = tape_stop_state_t::idle;
    master_scratch.state = master_scratch_state_t::idle;
    process_master_repeat(l, r);
    return;
  }
  if (master_scratch.state != master_scratch_state_t::idle) {
    process_master_scratch(l, r);
    return;
  }
  if (tape_stop.state != tape_stop_state_t::idle) { process_tape_stop(l, r); }
}

static inline void process_master_fx(int64_t& l, int64_t& r)
{
  if (fx[audio_fx_filter].active) {
    const int32_t input_l = saturate32(l);
    const int32_t input_r = saturate32(r);
    int param = fx[audio_fx_filter].param * 2;
    if (param > 100) { param = 100; }
    if (param < -100) { param = -100; }
    if (param == 0) {
      filter_l = input_l;
      filter_r = input_r;
    } else {
      int amount = param < 0 ? -param : param;
      // A one-pole high-pass is input minus its low-pass follower.  A faster
      // follower means a higher HP cutoff, so its coefficient must move in the
      // opposite direction from the low-pass control.
      int shift = param < 0
        ? 1 + (amount * 7) / 100
        : 1 + ((100 - amount) * 6) / 100;
      filter_l = saturate32((int64_t)filter_l + (((int64_t)input_l - filter_l) >> shift));
      filter_r = saturate32((int64_t)filter_r + (((int64_t)input_r - filter_r) >> shift));
      if (param < 0) {
        const int low_gain_q8 = 256 + (amount * 106) / 100;
        l = ((int64_t)filter_l * low_gain_q8) >> 8;
        r = ((int64_t)filter_r * low_gain_q8) >> 8;
      } else {
        const int high_gain_q8 = 256 + (amount * 106) / 100;
        const int low_mix_q8 = ((100 - amount) * 64) / 100;
        const int64_t high_l = (int64_t)input_l - filter_l;
        const int64_t high_r = (int64_t)input_r - filter_r;
        l = ((high_l * high_gain_q8) >> 8) + (((int64_t)filter_l * low_mix_q8) >> 8);
        r = ((high_r * high_gain_q8) >> 8) + (((int64_t)filter_r * low_mix_q8) >> 8);
      }
    }
  } else {
    filter_l = saturate32(l);
    filter_r = saturate32(r);
  }

  if (fx[audio_fx_gater].active) {
    const uint8_t amount = (uint8_t)std::clamp<int>(fx[audio_fx_gater].param, 0, 100);
    if (amount != 0) {
      const uint8_t level = (uint8_t)std::min<int>(4, (amount - 1) / 20);
      uint32_t period = fx_quantize_step_frames;
      if (level == 0) { period *= 4u; }
      else if (level == 1) { period *= 2u; }
      else if (level == 3) { period = std::max<uint32_t>(1, period / 2u); }
      else if (level == 4) { period = std::max<uint32_t>(1, period / 4u); }
      period = std::max<uint32_t>(4, period);
      if (gater_phase >= period) { gater_phase %= period; }
      const uint32_t open_frames = std::max<uint32_t>(2, period / 2u);
      const uint32_t fade_frames = std::min<uint32_t>(96, std::max<uint32_t>(1, period / 16u));
      uint32_t gain_q15 = 0;
      if (gater_phase + fade_frames < open_frames) {
        gain_q15 = 32768;
      } else if (gater_phase < open_frames) {
        gain_q15 = ((open_frames - gater_phase) * 32768u) / fade_frames;
      } else if (gater_phase + fade_frames >= period) {
        gain_q15 = ((gater_phase + fade_frames - period) * 32768u) / fade_frames;
      }
      l = (l * gain_q15) >> 15;
      r = (r * gain_q15) >> 15;
      if (++gater_phase >= period) { gater_phase = 0; }
    }
  }

  if (fx[audio_fx_crusher].active) {
    const uint8_t amount = (uint8_t)std::clamp<int>(fx[audio_fx_crusher].param, 0, 100);
    if (amount != 0) {
      const uint8_t hold_frames = (uint8_t)(1 + (amount * 15u) / 100u);
      if (crusher_hold_remaining == 0) {
        const uint8_t removed_bits = (uint8_t)((amount * 12u) / 100u);
        const int32_t mask = removed_bits == 0 ? -1 : ~((1 << removed_bits) - 1);
        const int32_t pcm_l = saturate32(l) >> 16;
        const int32_t pcm_r = saturate32(r) >> 16;
        crusher_hold_l = saturate32((int64_t)(pcm_l & mask) * 65536ll);
        crusher_hold_r = saturate32((int64_t)(pcm_r & mask) * 65536ll);
        crusher_hold_remaining = hold_frames;
      }
      l = crusher_hold_l;
      r = crusher_hold_r;
      --crusher_hold_remaining;
    }
  }
}

static inline int16_t select_input_pcm16(int32_t left, int32_t right,
                                         int32_t mixed)
{
  int32_t mono = mixed;
  const uint32_t dominant = std::max(recorder.level_l, recorder.level_r);
  if (recorder.level_l > recorder.level_r + recorder.level_r / 2u) {
    mono = left;
  } else if (recorder.level_r > recorder.level_l + recorder.level_l / 2u) {
    mono = right;
  } else if (dominant != 0 && recorder.level_mix < dominant / 3u) {
    mono = recorder.level_l >= recorder.level_r ? left : right;
  }
  if (mono > INT16_MAX) { return INT16_MAX; }
  if (mono < INT16_MIN) { return INT16_MIN; }
  return (int16_t)mono;
}

static inline int16_t input_to_pcm16(int32_t l, int32_t r)
{
  const int32_t left = l >> 16;
  const int32_t right = r >> 16;
  const int32_t mixed = (left + right) >> 1;
  const uint32_t abs_l = (uint32_t)std::abs(left);
  const uint32_t abs_r = (uint32_t)std::abs(right);
  const uint32_t abs_mix = (uint32_t)std::abs(mixed);
  recorder.level_l = (recorder.level_l * 31u + abs_l) >> 5;
  recorder.level_r = (recorder.level_r * 31u + abs_r) >> 5;
  recorder.level_mix = (recorder.level_mix * 31u + abs_mix) >> 5;

  // A headset microphone may reach one ADC channel, or both channels with
  // opposite polarity. Preserve normal in-phase stereo mixing while avoiding
  // the 6 dB loss and cancellation in those microphone arrangements.
  return select_input_pcm16(left, right, mixed);
}

static inline void record_input_frame(int32_t l, int32_t r)
{
  if (!recorder.active) { return; }
  uint32_t pos = recorder.frames;
  if (pos >= recorder.capacity) {
    recorder.overflow = true;
    recorder.active = false;
    return;
  }
  recorder.buffer[pos] = input_to_pcm16(l, r);
  recorder.frames = pos + 1;
}

// Apply this after the limiter so fade-in cannot alter limiter detection or
// the captured external-input stream.  All output paths share this gate.
static inline uint16_t output_fade_gain_q15(void)
{
  const uint16_t gain = output_fade_q15;
  const uint16_t target = output_fade_target_q15;
  if (gain < target) {
    const uint32_t next = gain + output_fade_step_q15;
    output_fade_q15 = (uint16_t)std::min<uint32_t>(target, next);
  } else if (gain > target) {
    const uint16_t step = output_fade_step_q15;
    output_fade_q15 = gain > step ? std::max<uint16_t>(target, gain - step) : target;
  }
  return gain;
}

static inline int32_t apply_output_fade(int32_t sample, uint16_t gain_q15)
{
  return gain_q15 == 32768 ? sample : (int32_t)(((int64_t)sample * gain_q15) >> 15);
}

//-------------------------------------------------------------------------
// I2S 初期化 (task_i2s.cpp と同一設定)

#if !defined (M5UNIFIED_PC_BUILD)

// CoreS3内蔵マイク(M5.Mic/ES7210)が I2S_NUM_1 を使うため、
// KANTAN base側の出力/パススルーは I2S_NUM_0 で動かす。
static constexpr const i2s_port_t i2s_port = I2S_NUM_0;
static constexpr const uint16_t i2s_dma_desc_num = 4;

#if __has_include(<driver/i2s_std.h>)

static i2s_chan_handle_t _i2s_tx_handle = nullptr;
static i2s_chan_handle_t _i2s_rx_handle = nullptr;
static esp_err_t _i2s_init(void)
{
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)i2s_port, I2S_ROLE_SLAVE);
  chan_cfg.dma_desc_num = i2s_dma_desc_num;
  chan_cfg.dma_frame_num = i2s_dma_frame_num >> 1;
  esp_err_t err = i2s_new_channel(&chan_cfg, &_i2s_tx_handle, &_i2s_rx_handle);
  if (err != ESP_OK) { return err; }
  i2s_std_config_t i2s_config;
  memset(&i2s_config, 0, sizeof(i2s_std_config_t));
  i2s_config.clk_cfg.clk_src = i2s_clock_src_t::I2S_CLK_SRC_PLL_160M;
  i2s_config.clk_cfg.sample_rate_hz = 48000; // dummy setting
  i2s_config.slot_cfg.data_bit_width = i2s_data_bit_width_t::I2S_DATA_BIT_WIDTH_32BIT;
  i2s_config.slot_cfg.slot_bit_width = i2s_slot_bit_width_t::I2S_SLOT_BIT_WIDTH_32BIT;
  i2s_config.slot_cfg.slot_mode = i2s_slot_mode_t::I2S_SLOT_MODE_STEREO;
  i2s_config.slot_cfg.slot_mask = i2s_std_slot_mask_t::I2S_STD_SLOT_BOTH;
  i2s_config.slot_cfg.ws_width = 32;
  i2s_config.slot_cfg.ws_pol = false;
  i2s_config.slot_cfg.bit_shift = true;
#if SOC_I2S_HW_VERSION_1    // For esp32/esp32-s2
  i2s_config.slot_cfg.msb_right = false;
#else
  i2s_config.slot_cfg.left_align = true;
  i2s_config.slot_cfg.big_endian = false;
  i2s_config.slot_cfg.bit_order_lsb = false;
#endif
  i2s_config.gpio_cfg.bclk = kp::def::hw::pin::i2s_bck;
  i2s_config.gpio_cfg.ws   = kp::def::hw::pin::i2s_ws;
  i2s_config.gpio_cfg.dout = kp::def::hw::pin::i2s_out;
  i2s_config.gpio_cfg.mclk = kp::def::hw::pin::i2s_mclk;
  i2s_config.gpio_cfg.din  = kp::def::hw::pin::i2s_in;
  err = i2s_channel_init_std_mode(_i2s_tx_handle, &i2s_config);
  err = i2s_channel_init_std_mode(_i2s_rx_handle, &i2s_config);

  return ESP_OK;
}

static esp_err_t _i2s_start(void) {
  if (_i2s_tx_handle == nullptr) { return ESP_FAIL; }
  return i2s_channel_enable(_i2s_tx_handle) || i2s_channel_enable(_i2s_rx_handle);
}

static esp_err_t _i2s_write(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_channel_write(_i2s_tx_handle, buf, len, result, tick);
}

static esp_err_t _i2s_read(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_channel_read(_i2s_rx_handle, buf, len, result, tick);
}

#else

static esp_err_t _i2s_init(void)
{
    i2s_config_t i2s_config;
    memset(&i2s_config, 0, sizeof(i2s_config_t));
    i2s_config.mode                 = (i2s_mode_t)( I2S_MODE_SLAVE | I2S_MODE_TX | I2S_MODE_RX );
    i2s_config.sample_rate          = 48000; // dummy setting
    i2s_config.bits_per_sample      = i2s_bits_per_sample_t::I2S_BITS_PER_SAMPLE_32BIT;
    i2s_config.channel_format       = i2s_channel_fmt_t::I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2s_config.communication_format = i2s_comm_format_t::I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    i2s_config.tx_desc_auto_clear   = false;
    i2s_config.use_apll             = false;
    i2s_config.fixed_mclk           = 0;
    i2s_config.mclk_multiple        = i2s_mclk_multiple_t::I2S_MCLK_MULTIPLE_DEFAULT;
    i2s_config.bits_per_chan        = i2s_bits_per_chan_t::I2S_BITS_PER_CHAN_32BIT;
#if I2S_DRIVER_VERSION > 1
    i2s_config.dma_desc_num         = i2s_dma_desc_num;
    i2s_config.dma_frame_num        = i2s_dma_frame_num >> 1;
#else
    i2s_config.dma_buf_count        = i2s_dma_desc_num;
    i2s_config.dma_buf_len          = i2s_dma_frame_num >> 1;
#endif
    esp_err_t err;
    if (ESP_OK != (err = i2s_driver_install(i2s_port, &i2s_config, 0, nullptr)))
    {
      M5_LOGE("i2s_driver_install: %d", err);
      return err;
    }

    i2s_pin_config_t pin_config;
    memset(&pin_config, ~0u, sizeof(i2s_pin_config_t)); /// all pin set to I2S_PIN_NO_CHANGE
    pin_config.bck_io_num     = kp::def::hw::pin::i2s_bck;
    pin_config.ws_io_num      = kp::def::hw::pin::i2s_ws;
    pin_config.data_out_num   = kp::def::hw::pin::i2s_out;
    pin_config.data_in_num    = kp::def::hw::pin::i2s_in;
    pin_config.mck_io_num     = kp::def::hw::pin::i2s_mclk;
    err = i2s_set_pin(i2s_port, &pin_config);
    if (ESP_OK != err)
    {
      M5_LOGE("i2s_set_pin: %d", err);
      return err;
    }

  return ESP_OK;
}

static esp_err_t _i2s_start(void) {
  return i2s_start(i2s_port);
}

static esp_err_t _i2s_write(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_write(i2s_port, buf, len, result, tick);
}

static esp_err_t _i2s_read(void* buf, size_t len, size_t* result, TickType_t tick) {
  return i2s_read(i2s_port, buf, len, result, tick);
}

#endif

#endif

static int32_t* bufdata = nullptr;
static constexpr const size_t buf_size = (i2s_dma_frame_num) * sizeof(int32_t);

bool sampler_audio_t::start(void)
{
  int len = kp::system_registry->raw_wave_length;
  auto wav_buf = kp::system_registry->raw_wave;
  for (int i = 0; i < len; ++i) {
    wav_buf[i] = std::make_pair(128, 128);
  }

  // Reserved memory shared by the final-mix Deck effects. It is written
  // only while FX mode owns the Deck Buffer, keeping normal play lightweight.
  if (tape_stop.pcm == nullptr) {
    const size_t bytes = (size_t)tape_stop_buffer_frames * 2u * sizeof(int16_t);
#if defined(M5UNIFIED_PC_BUILD)
    tape_stop.pcm = (int16_t*)malloc(bytes);
#else
    tape_stop.pcm = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#endif
    if (tape_stop.pcm != nullptr) { tape_stop.capacity = tape_stop_buffer_frames; }
  }

#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "i2s", this);
  (void)thread;
#else
  // メモリブロックの断片化への対策 (task_i2s.cpp と同じ手順)
  auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  bufdata = (int32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  m5gfx::heap_free(dummy);
  if (bufdata == nullptr) {
    bufdata = (int32_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
  }
  memset(bufdata, 0, buf_size);

  _i2s_init();

  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "i2s", 1024*3, this, kp::def::system::task_priority_i2s, nullptr, kp::def::system::task_cpu_i2s);
#endif
  return true;
}

void sampler_audio_t::restoreInputRoute(void)
{
#if !defined (M5UNIFIED_PC_BUILD)
  // M5.Mic uses GPIO14 as I2S1 data-in on CoreS3. Its driver teardown does
  // not restore the GPIO matrix route that the always-running sampler I2S0
  // RX channel established at boot. Reconnect only the input signal; stopping
  // or rebuilding the live I2S channel here would create an audible gap.
  gpio_set_direction(kp::def::hw::pin::i2s_in, GPIO_MODE_INPUT);
  esp_rom_gpio_connect_in_signal(kp::def::hw::pin::i2s_in,
                                 I2S0I_SD_IN_IDX, false);
#endif
}

// 波形表示用リングバッファに min/max を記録 (task_i2s.cpp と同一形式)
static inline void push_raw_wave(int32_t min_level, int32_t max_level)
{
  min_level = ((min_level >> 16) + 32768 + 128) >> 8;
  max_level = ((max_level >> 16) + 32768 + 128) >> 8;

  auto wav_buf = kp::system_registry->raw_wave;
  auto raw_wave_pos = kp::system_registry->raw_wave_pos;
  wav_buf[raw_wave_pos ++] = std::make_pair<uint8_t, uint8_t>(min_level, max_level);
  if (raw_wave_pos >= (kp::system_registry->raw_wave_length)) {
    raw_wave_pos = 0;
  }
  kp::system_registry->raw_wave_pos = raw_wave_pos;
}

void sampler_audio_t::task_func(sampler_audio_t* me)
{
  (void)me;
#if defined (M5UNIFIED_PC_BUILD)
  // PCビルドでは実音声出力は行わず、ボイスを進めて波形表示のみ更新する
  static int32_t pcbuf[i2s_dma_frame_num];
  for (;;) {
    int32_t min_level = INT32_MAX;
    int32_t max_level = INT32_MIN;
    mix_voice_block();
    for (int i = 0; i < i2s_dma_frame_num; i += 2) {
      const mixed_buses_t mixed = mixed_block[i / 2];
      int64_t deck_l = 0;
      int64_t deck_r = 0;
      read_deck_stream_frame(&deck_l, &deck_r);
      int64_t beat_l = mixed.beat;
      int64_t beat_r = mixed.beat;
      int64_t parts_l = mixed.parts;
      int64_t parts_r = mixed.parts;
      int64_t music_l = deck_l;
      int64_t music_r = deck_r;
      const uint8_t target = active_fx_target_mask;
      int64_t ll = ((target & sampler_audio_t::fx_target_beat) ? beat_l : 0)
                 + ((target & sampler_audio_t::fx_target_parts) ? parts_l : 0)
                 + ((target & sampler_audio_t::fx_target_music) ? music_l : 0);
      int64_t rr = ((target & sampler_audio_t::fx_target_beat) ? beat_r : 0)
                 + ((target & sampler_audio_t::fx_target_parts) ? parts_r : 0)
                 + ((target & sampler_audio_t::fx_target_music) ? music_r : 0);
      int64_t dry_l = ((target & sampler_audio_t::fx_target_beat) ? 0 : beat_l)
                    + ((target & sampler_audio_t::fx_target_parts) ? 0 : parts_l)
                    + ((target & sampler_audio_t::fx_target_music) ? 0 : music_l);
      int64_t dry_r = ((target & sampler_audio_t::fx_target_beat) ? 0 : beat_r)
                    + ((target & sampler_audio_t::fx_target_parts) ? 0 : parts_r)
                    + ((target & sampler_audio_t::fx_target_music) ? 0 : music_r);
      // Keep the summed bus wide until the limiter. Saturating here destroys
      // the relative contribution of a quieter source before protection can
      // act, most visibly when SAM2695 plays under several PCM voices.
      process_master_fx(ll, rr);
      int64_t out_l = ((int64_t)ll * output_gain_q8) >> 8;
      int64_t out_r = ((int64_t)rr * output_gain_q8) >> 8;
      dry_l = (dry_l * output_gain_q8) >> 8;
      dry_r = (dry_r * output_gain_q8) >> 8;
      const bool delay_wrote_deck = process_master_delay(out_l, out_r);
      if (target == sampler_audio_t::fx_target_all) {
        process_output_limiter(out_l, out_r);
        process_deck_fx(out_l, out_r, delay_wrote_deck);
      } else {
        process_deck_fx(out_l, out_r, delay_wrote_deck);
        out_l += dry_l;
        out_r += dry_r;
        process_output_limiter(out_l, out_r);
      }
      const uint16_t fade_gain = output_muted ? 0 : output_fade_gain_q15();
      pcbuf[i  ] = apply_output_fade(saturate32(out_l), fade_gain);
      pcbuf[i+1] = apply_output_fade(saturate32(out_r), fade_gain);
      capture_output_frame(pcbuf[i], pcbuf[i+1]);
      record_input_frame(pcbuf[i], pcbuf[i+1]);
      if (min_level > pcbuf[i  ]) { min_level = pcbuf[i  ]; }
      if (max_level < pcbuf[i  ]) { max_level = pcbuf[i  ]; }
      if (min_level > pcbuf[i+1]) { min_level = pcbuf[i+1]; }
      if (max_level < pcbuf[i+1]) { max_level = pcbuf[i+1]; }
    }
    push_raw_wave(min_level, max_level);
    SDL_Delay(1);
  }
#else
  int32_t* i2sbuf = bufdata;

  size_t transfer_size;

#if __has_include(<driver/i2s_std.h>)
  do {
    i2s_channel_preload_data(_i2s_tx_handle, i2sbuf, buf_size, &transfer_size);
  } while (transfer_size == buf_size);
#endif

  _i2s_start();

  int32_t current_volume = 0;
  int shifted_volume = 0;
  uint8_t raw_wave_divider = 1;
  uint8_t raw_wave_phase = 0;

  for (;;) {
    const esp_err_t read_result = _i2s_read(i2sbuf, buf_size, &transfer_size, 128);
    if (read_result != ESP_OK || transfer_size != buf_size) {
      performance_probe::count(performance_probe::counter_t::i2s_read_error);
    }
    kp::system_registry->task_status.setWorking(kp::system_registry_t::reg_task_status_t::bitindex_t::TASK_I2S);
    const uint32_t processing_started_usec = M5.micros();
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
    const uint8_t block_voice_count = (uint8_t)(
      __builtin_popcount(__atomic_load_n(&active_voice_mask[0], __ATOMIC_ACQUIRE))
      + __builtin_popcount(__atomic_load_n(&active_voice_mask[1], __ATOMIC_ACQUIRE)));
#endif
    if (live_wave_clear_pending) {
      const int len = kp::system_registry->raw_wave_length;
      auto wav_buf = kp::system_registry->raw_wave;
      for (int i = 0; i < len; ++i) { wav_buf[i] = std::make_pair(128, 128); }
      kp::system_registry->raw_wave_pos = 0;
      live_wave_clear_pending = false;
    }

    // マスターボリュームのレンジ0~100を 1~256に変換 (task_i2s.cpp と同一)
    int32_t target_volume = kp::system_registry->user_setting.getMasterVolume() << 8;
    if (target_volume > 25600) { target_volume = 25600; }
    if (current_volume != target_volume) {
      current_volume += (target_volume - current_volume + (target_volume < current_volume ? 0 : 32)) >> 5;
      shifted_volume = current_volume / 100;
    }

    // The live wave is a visual aid only. When the preceding block consumed
    // most of its 1ms budget, update it at half rate and leave that time for
    // the next audio block. Audio, recording and loop timing stay full rate.
    const bool capture_raw_wave = live_wave_capture_enabled
                               && (++raw_wave_phase % raw_wave_divider) == 0;
    int32_t min_level = INT32_MAX;
    int32_t max_level = INT32_MIN;

    if (!output_muted) { mix_voice_block(); }

    for (int i = 0; i < i2s_dma_frame_num; i += 2) {
      // 入力(SAM音源/マイク)のパススルーにボイスをミキシング
      record_input_frame(i2sbuf[i], i2sbuf[i+1]);
      const bool monitor_output = external_input_monitor
                               && kp::system_registry->runtime_info.getHeadphoneEnabled() == 1;
      int64_t deck_l = 0;
      int64_t deck_r = 0;
      read_deck_stream_frame(&deck_l, &deck_r);
      const mixed_buses_t mixed = output_muted ? mixed_buses_t{} : mixed_block[i / 2];
      int32_t amy_l = 0;
      int32_t amy_r = 0;
      // Always drain the producer ring, including during a short output mute,
      // so resuming audio cannot replay stale AMY frames.
      sampler_amy_engine::readFrame(&amy_l, &amy_r);
      int64_t beat_l = output_muted ? 0 : mixed.beat;
      int64_t beat_r = output_muted ? 0 : mixed.beat;
      // The external SAM2695 input is always a musical Part. Beat audio and
      // pattern drums are rendered by explicitly tagged PCM voices.
      int64_t parts_l = output_muted ? 0 : (int64_t)i2sbuf[i  ] + mixed.parts + amy_l;
      int64_t parts_r = output_muted ? 0 : (int64_t)i2sbuf[i+1] + mixed.parts + amy_r;
      int64_t music_l = output_muted ? 0 : deck_l;
      int64_t music_r = output_muted ? 0 : deck_r;
      const uint8_t target = active_fx_target_mask;
      int64_t ll = ((target & sampler_audio_t::fx_target_beat) ? beat_l : 0)
                 + ((target & sampler_audio_t::fx_target_parts) ? parts_l : 0)
                 + ((target & sampler_audio_t::fx_target_music) ? music_l : 0);
      int64_t rr = ((target & sampler_audio_t::fx_target_beat) ? beat_r : 0)
                 + ((target & sampler_audio_t::fx_target_parts) ? parts_r : 0)
                 + ((target & sampler_audio_t::fx_target_music) ? music_r : 0);
      int64_t dry_l = ((target & sampler_audio_t::fx_target_beat) ? 0 : beat_l)
                    + ((target & sampler_audio_t::fx_target_parts) ? 0 : parts_l)
                    + ((target & sampler_audio_t::fx_target_music) ? 0 : music_l);
      int64_t dry_r = ((target & sampler_audio_t::fx_target_beat) ? 0 : beat_r)
                    + ((target & sampler_audio_t::fx_target_parts) ? 0 : parts_r)
                    + ((target & sampler_audio_t::fx_target_music) ? 0 : music_r);
      // SAM2695 input and PCM voices can legitimately exceed int32 while
      // summed. Preserve the 64-bit bus until process_output_limiter() scales
      // the complete mix; early saturation makes the internal synth vanish
      // behind Beat and Pad-synth layers even though its MIDI Note On arrived.
      if (!output_muted) { process_master_fx(ll, rr); }
      int64_t out_l = ((int64_t)(ll >> 8) * shifted_volume * output_gain_q8) >> 8;
      int64_t out_r = ((int64_t)(rr >> 8) * shifted_volume * output_gain_q8) >> 8;
      dry_l = ((dry_l >> 8) * shifted_volume * output_gain_q8) >> 8;
      dry_r = ((dry_r >> 8) * shifted_volume * output_gain_q8) >> 8;
      const bool delay_wrote_deck = !output_muted && process_master_delay(out_l, out_r);
      if (!output_muted && target == sampler_audio_t::fx_target_all) {
        process_output_limiter(out_l, out_r);
        process_deck_fx(out_l, out_r, delay_wrote_deck);
      } else if (!output_muted) {
        process_deck_fx(out_l, out_r, delay_wrote_deck);
        out_l += dry_l;
        out_r += dry_r;
        process_output_limiter(out_l, out_r);
      }
      if (output_muted && monitor_output) {
        // Monitor the raw ADC stream, before recording normalization or FX.
        // It is deliberately excluded from capture_output_frame()'s source
        // path and can therefore never feed itself back into the Pad take.
        const int32_t left = i2sbuf[i] >> 16;
        const int32_t right = i2sbuf[i + 1] >> 16;
        const int16_t monitor_pcm = select_input_pcm16(
          left, right, (left + right) >> 1);
        const int64_t monitor_sample = (int64_t)monitor_pcm << 16;
        out_l = ((monitor_sample >> 8) * shifted_volume * output_gain_q8) >> 8;
        out_r = out_l;
        process_output_limiter(out_l, out_r);
      }
      const uint16_t fade_gain = (!output_muted || monitor_output)
        ? output_fade_gain_q15() : 0;
      int32_t output_l = apply_output_fade(saturate32(out_l), fade_gain);
      int32_t output_r = apply_output_fade(saturate32(out_r), fade_gain);
      capture_output_frame(output_l, output_r);
      if (capture_raw_wave) {
        if (min_level > output_l) { min_level = output_l; }
        if (max_level < output_l) { max_level = output_l; }
        if (min_level > output_r) { min_level = output_r; }
        if (max_level < output_r) { max_level = output_r; }
      }
      i2sbuf[i  ] = output_l;
      i2sbuf[i+1] = output_r;
    }
    if (capture_raw_wave) { push_raw_wave(min_level, max_level); }

    const uint32_t processing_usec = M5.micros() - processing_started_usec;
#if defined(KANPLAY_SAMPLER_LATENCY_PROBE)
    // Idle blocks must not hide polyphonic overloads in the p95 histogram.
    if (block_voice_count) {
      performance_probe::record(performance_probe::metric_t::audio_block, processing_usec);
      const auto metric = block_voice_count <= 2 ? performance_probe::metric_t::audio_1_2_voices
        : block_voice_count <= 4 ? performance_probe::metric_t::audio_3_4_voices
        : block_voice_count <= 6 ? performance_probe::metric_t::audio_5_6_voices
                                : performance_probe::metric_t::audio_7_plus_voices;
      performance_probe::record(metric, processing_usec);
    }
#endif
    if (processing_usec >= 1000u) {
      performance_probe::count(performance_probe::counter_t::audio_deadline_miss);
    }
    const uint8_t instant_load = (uint8_t)std::min<uint32_t>(255, (processing_usec * 256u) / 1000u);
    const uint8_t previous_load = processing_load_q8;
    processing_load_q8 = (uint8_t)((previous_load * 7u + instant_load + 4u) >> 3);
    if (instant_load > processing_peak_q8) { processing_peak_q8 = instant_load; }
    // 75% is intentionally conservative: raising the visual divider before
    // the deadline keeps Pad attacks from competing with display work.
    raw_wave_divider = processing_load_q8 >= 192 ? 2 : 1;

    kp::system_registry->task_status.setSuspend(kp::system_registry_t::reg_task_status_t::bitindex_t::TASK_I2S);
    const esp_err_t write_result = _i2s_write(bufdata, buf_size, &transfer_size, 128);
    if (write_result != ESP_OK || transfer_size != buf_size) {
      performance_probe::count(performance_probe::counter_t::i2s_write_error);
    }
  }
#endif
}

//-------------------------------------------------------------------------
} // namespace sampler_ns

#endif // defined (KANPLAY_SAMPLER)
