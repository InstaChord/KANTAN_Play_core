// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#if defined (KANPLAY_SAMPLER)

#include <M5Unified.h>
#include <algorithm>
#include <math.h>

#include "sampler_pool.hpp"
#include "sampler_ktsynth.hpp"
#include "sampler_wav.hpp"

namespace sampler_ns {

static sampler_pool_t::progress_callback_t progress_callback = nullptr;
static bool pool_compaction_pending = false;

static inline void report_import_progress(uint32_t index, uint32_t interval_mask = 0x07FFu)
{
  if ((index & interval_mask) == 0 && progress_callback) { progress_callback(); }
}

void sampler_pool_t::setProgressCallback(progress_callback_t callback)
{
  progress_callback = callback;
}
//-------------------------------------------------------------------------

sample_slot_t sampler_pool_t::slot[def::pad::pad_count];
sample_slot_t sampler_pool_t::synth_source[sampler_pool_t::synth_source_count];
sample_slot_t beat_pool_t::slot[def::pad::pad_count];
static sample_asset_t sampler_assets[sampler_pool_t::asset_capacity];

static int16_t* pool_alloc(size_t bytes)
{
#if defined (M5UNIFIED_PC_BUILD)
  return (int16_t*)malloc(bytes);
#else
  return (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#endif
}

static void pool_free(int16_t* ptr)
{
  if (ptr) { free(ptr); }
}

static sample_asset_t* pool_create_asset(uint32_t frames)
{
  if (frames == 0) { return nullptr; }
  for (auto& asset : sampler_assets) {
    if (asset.pcm != nullptr) { continue; }
    int16_t* pcm = pool_alloc((size_t)frames * sizeof(int16_t));
    if (!pcm) { return nullptr; }
    asset = {};
    asset.pcm = pcm;
    asset.frames = frames;
    return &asset;
  }
  return nullptr;
}

static sample_asset_t* pool_adopt_asset(int16_t* pcm, uint32_t frames)
{
  if (!pcm || frames == 0) { return nullptr; }
  for (auto& asset : sampler_assets) {
    if (asset.pcm != nullptr) { continue; }
    asset = {};
    asset.pcm = pcm;
    asset.frames = frames;
    return &asset;
  }
  return nullptr;
}

static void pool_retain_asset(sample_asset_t* asset)
{
  if (asset && asset->references != UINT16_MAX) { ++asset->references; }
}

static void pool_release_asset(sample_asset_t* asset)
{
  if (!asset || asset->references == 0) { return; }
  if (--asset->references != 0) { return; }
  pool_free(asset->pcm);
  *asset = {};
}

// 100%のPadを複数同時に鳴らしても余裕が残るよう、通常素材はピークを約-12 dBFSへ揃える。
// 小さすぎる録音はノイズまで持ち上げないため、増幅量を最大8倍に抑える。
static void normalize_pcm_for_pad(int16_t* pcm, uint32_t frames, uint32_t target_peak = 8192)
{
  static constexpr uint32_t minimum_peak = 256;
  static constexpr uint32_t maximum_gain_q16 = 8u << 16;
  if (!pcm || frames == 0) { return; }

  uint32_t peak = 0;
  for (uint32_t i = 0; i < frames; ++i) {
    int32_t value = pcm[i];
    uint32_t magnitude = value < 0 ? (uint32_t)-value : (uint32_t)value;
    if (magnitude > peak) { peak = magnitude; }
    report_import_progress(i);
  }
  if (peak < minimum_peak) { return; }

  uint32_t gain_q16 = (uint32_t)(((uint64_t)target_peak << 16) / peak);
  if (gain_q16 > maximum_gain_q16) { gain_q16 = maximum_gain_q16; }
  if (gain_q16 == (1u << 16)) { return; }
  for (uint32_t i = 0; i < frames; ++i) {
    int32_t value = (int32_t)(((int64_t)pcm[i] * gain_q16) >> 16);
    if (value > INT16_MAX) { value = INT16_MAX; }
    if (value < INT16_MIN) { value = INT16_MIN; }
    pcm[i] = (int16_t)value;
    report_import_progress(i);
  }
}

// Pad音源を鍵盤で鳴らすための基音推定。読み込み時だけ実行し、演奏中には
// 一切負荷を持ち込まない。アタックを避けた約250msを8kHz相当に間引き、
// 自己相関の最初の強いピークから単音素材の周期を得る。
static uint8_t detect_base_note(const int16_t* pcm, uint32_t frames, uint32_t sample_rate)
{
  static constexpr uint8_t fallback_note = 60;  // C3
  static constexpr uint32_t analysis_rate = 8000;
  static constexpr uint32_t min_frequency = 65;    // C2付近
  static constexpr uint32_t max_frequency = 1046;  // C6付近
  static constexpr uint16_t confidence_min_q12 = 2050;  // 正規化相関 0.5
  if (!pcm || sample_rate == 0 || frames < sample_rate / 12) { return fallback_note; }

  const uint32_t stride = std::max<uint32_t>(1, sample_rate / analysis_rate);
  const uint32_t effective_rate = sample_rate / stride;
  const uint32_t start = std::min<uint32_t>(frames, sample_rate / 25);  // attackを40ms除外
  const uint32_t available = (frames > start) ? (frames - start) / stride : 0;
  const uint32_t count = std::min<uint32_t>(2048, available);
  if (count < 512 || effective_rate <= max_frequency) { return fallback_note; }

  int64_t sum = 0;
  for (uint32_t i = 0; i < count; ++i) { sum += pcm[start + i * stride]; }
  const int32_t mean = (int32_t)(sum / (int64_t)count);

  uint64_t signal_energy = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const int32_t value = (int32_t)pcm[start + i * stride] - mean;
    signal_energy += (int64_t)value * value;
  }
  // 無音や小さなノイズを音程付き素材と誤認しない。
  if (signal_energy / count < 256u * 256u) { return fallback_note; }

  const uint32_t min_lag = std::max<uint32_t>(4, effective_rate / max_frequency);
  const uint32_t max_lag = std::min<uint32_t>(count / 2, effective_rate / min_frequency);
  if (max_lag <= min_lag + 2 || max_lag - min_lag >= 192) { return fallback_note; }

  uint16_t scores[192] = {};
  uint16_t best_score = 0;
  uint32_t best_lag = 0;
  for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
    int64_t correlation = 0;
    uint64_t energy_a = 0;
    uint64_t energy_b = 0;
    for (uint32_t i = lag; i < count; ++i) {
      const int32_t a = (int32_t)pcm[start + i * stride] - mean;
      const int32_t b = (int32_t)pcm[start + (i - lag) * stride] - mean;
      correlation += (int64_t)a * b;
      energy_a += (int64_t)a * a;
      energy_b += (int64_t)b * b;
    }
    if (correlation <= 0) { continue; }
    const uint64_t energy = std::min(energy_a, energy_b);
    if (energy == 0) { continue; }
    const uint16_t score = (uint16_t)std::min<int64_t>(4095, (correlation << 12) / (int64_t)energy);
    scores[lag - min_lag] = score;
    if (score > best_score) {
      best_score = score;
      best_lag = lag;
    }
    report_import_progress(lag, 0x0Fu);
  }
  if (best_score < confidence_min_q12 || best_lag == 0) { return fallback_note; }

  // 倍音の山ではなく、十分に強い最初の局所ピークを基音として選ぶ。
  const uint16_t fundamental_threshold = (uint16_t)((best_score * 3u) / 4u);
  for (uint32_t lag = min_lag + 1; lag < max_lag; ++lag) {
    const uint16_t previous = scores[lag - min_lag - 1];
    const uint16_t current = scores[lag - min_lag];
    const uint16_t next = scores[lag - min_lag + 1];
    if (current >= fundamental_threshold && current >= previous && current > next) {
      best_lag = lag;
      break;
    }
  }

  const float frequency = (float)effective_rate / (float)best_lag;
  const int note = (int)lroundf(69.0f + 12.0f * log2f(frequency / 440.0f));
  return (note >= 24 && note <= 96) ? (uint8_t)note : fallback_note;
}

struct sustain_window_t {
  uint32_t energy = 0;
  uint16_t periodicity_q12 = 0;
  uint16_t brightness_q12 = 0;
  uint16_t lag = 0;
};

// A compact stationarity test used only while importing/editing. Periodicity
// permits vocal vibrato, while energy and spectral-change proxies reject
// decays, phrases and evolving effects that should retain their original form.
static sustain_window_t analyze_sustain_window(const int16_t* pcm, uint32_t start,
                                                uint32_t stride, uint32_t count,
                                                uint32_t effective_rate)
{
  sustain_window_t result;
  if (!pcm || count < 128 || effective_rate < 2000) { return result; }
  int64_t sum = 0;
  uint64_t abs_sum = 0;
  uint64_t diff_sum = 0;
  int32_t previous = pcm[start];
  for (uint32_t i = 0; i < count; ++i) {
    int32_t value = pcm[start + i * stride];
    sum += value;
    abs_sum += (uint32_t)abs(value);
    if (i) { diff_sum += (uint32_t)abs(value - previous); }
    previous = value;
  }
  int32_t mean = (int32_t)(sum / count);
  uint64_t total_energy = 0;
  for (uint32_t i = 0; i < count; ++i) {
    int32_t value = (int32_t)pcm[start + i * stride] - mean;
    total_energy += (int64_t)value * value;
  }
  result.energy = (uint32_t)std::min<uint64_t>(UINT32_MAX, total_energy / count);
  result.brightness_q12 = abs_sum
    ? (uint16_t)std::min<uint64_t>(4095, (diff_sum << 12) / abs_sum) : 0;
  if (result.energy < 192u * 192u) { return result; }

  const uint32_t min_lag = std::max<uint32_t>(4, effective_rate / 1100);
  const uint32_t max_lag = std::min<uint32_t>(count / 3, effective_rate / 65);
  for (uint32_t lag = min_lag; lag <= max_lag; ++lag) {
    int64_t correlation = 0;
    uint64_t energy_a = 0;
    uint64_t energy_b = 0;
    for (uint32_t i = lag; i < count; ++i) {
      int32_t a = (int32_t)pcm[start + i * stride] - mean;
      int32_t b = (int32_t)pcm[start + (i - lag) * stride] - mean;
      correlation += (int64_t)a * b;
      energy_a += (int64_t)a * a;
      energy_b += (int64_t)b * b;
    }
    if (correlation <= 0) { continue; }
    uint64_t energy = std::min(energy_a, energy_b);
    if (!energy) { continue; }
    uint16_t score = (uint16_t)std::min<int64_t>(4095, (correlation << 12) / (int64_t)energy);
    if (score > result.periodicity_q12) {
      result.periodicity_q12 = score;
      result.lag = (uint16_t)lag;
    }
    report_import_progress(lag, 0x0Fu);
  }
  return result;
}

static uint32_t find_loop_zero_crossing(const int16_t* pcm, uint32_t begin, uint32_t end,
                                        uint32_t target, uint32_t radius, bool rising)
{
  uint32_t lo = target > radius ? target - radius : begin;
  uint32_t hi = std::min<uint32_t>(end - 1, target + radius);
  lo = std::max<uint32_t>(begin + 1, lo);
  uint32_t best = target;
  uint32_t best_distance = UINT32_MAX;
  for (uint32_t i = lo; i <= hi; ++i) {
    bool crossing = rising ? (pcm[i - 1] <= 0 && pcm[i] > 0)
                           : (pcm[i - 1] >= 0 && pcm[i] < 0);
    if (!crossing) { continue; }
    uint32_t distance = i > target ? i - target : target - i;
    if (distance < best_distance) { best = i; best_distance = distance; }
    report_import_progress(i, 0x7Fu);
  }
  return best;
}

// Select matching zero crossings rather than merely the nearest two. This
// runs only after import/recording/edit, never in the audio callback. Keeping
// the candidate set small makes it cheap while comparing a few milliseconds
// of real waveform shape removes many otherwise obvious loop joins.
static uint8_t collect_loop_zero_crossings(const int16_t* pcm, uint32_t begin, uint32_t end,
                                           uint32_t target, uint32_t radius, bool rising,
                                           uint32_t* candidates, uint8_t capacity)
{
  if (!pcm || !candidates || capacity == 0 || end <= begin + 2) { return 0; }
  uint32_t lo = target > radius ? target - radius : begin;
  uint32_t hi = std::min<uint32_t>(end - 1, target + radius);
  lo = std::max<uint32_t>(begin + 1, lo);
  uint8_t count = 0;
  for (uint32_t i = lo; i <= hi; ++i) {
    const bool crossing = rising ? (pcm[i - 1] <= 0 && pcm[i] > 0)
                                 : (pcm[i - 1] >= 0 && pcm[i] < 0);
    if (!crossing) { continue; }
    if (count < capacity) { candidates[count++] = i; }
    report_import_progress(i, 0x7Fu);
  }
  return count;
}

static uint32_t loop_boundary_difference(const int16_t* pcm, uint32_t a, uint32_t b,
                                         uint32_t half_window)
{
  uint32_t score = 0;
  int32_t previous_a = pcm[a - half_window];
  int32_t previous_b = pcm[b - half_window];
  for (uint32_t i = 0; i < half_window * 2; ++i) {
    const int32_t current_a = pcm[a - half_window + i];
    const int32_t current_b = pcm[b - half_window + i];
    // Shape and slope are both relevant: equal amplitudes with opposite
    // movement still make a conspicuous join on organ and vocal material.
    score += (uint32_t)abs(current_a - current_b);
    score += (uint32_t)abs((current_a - previous_a) - (current_b - previous_b)) >> 1;
    previous_a = current_a;
    previous_b = current_b;
  }
  return score;
}

static bool find_matched_loop_points(const int16_t* pcm, uint32_t begin, uint32_t end,
                                     uint32_t start_target, uint32_t end_target,
                                     uint32_t start_radius, uint32_t end_radius,
                                     uint32_t sample_rate, bool rising,
                                     uint32_t* loop_start, uint32_t* loop_end)
{
  uint32_t starts[24] = {};
  uint32_t ends[48] = {};
  const uint8_t start_count = collect_loop_zero_crossings(pcm, begin, end, start_target,
                                                            start_radius, rising, starts, 24);
  const uint8_t end_count = collect_loop_zero_crossings(pcm, begin, end, end_target,
                                                          end_radius, rising, ends, 48);
  const uint32_t half_window = std::clamp<uint32_t>(sample_rate / 600, 32, 96);
  const uint32_t minimum_length = std::max<uint32_t>(32, sample_rate / 5);
  uint32_t best_score = UINT32_MAX;
  uint32_t best_start = 0;
  uint32_t best_end = 0;
  for (uint8_t s = 0; s < start_count; ++s) {
    if (starts[s] < begin + half_window || starts[s] + half_window >= end) { continue; }
    for (uint8_t e = 0; e < end_count; ++e) {
      if (ends[e] <= starts[s] + minimum_length
       || ends[e] < begin + half_window || ends[e] + half_window >= end) { continue; }
      const uint32_t score = loop_boundary_difference(pcm, starts[s], ends[e], half_window);
      if (score < best_score) {
        best_score = score;
        best_start = starts[s];
        best_end = ends[e];
      }
    }
  }
  if (best_start == 0 || best_end == 0) { return false; }
  if (loop_start) { *loop_start = best_start; }
  if (loop_end) { *loop_end = best_end; }
  return true;
}

static void analyze_synth_sustain(sample_slot_t& slot)
{
  slot.synth_sustain_auto = false;
  slot.synth_sustain_confidence = 0;
  slot.synth_loop_start = 0;
  slot.synth_loop_end = 0;
  slot.synth_loop_crossfade = 0;
  if (!slot.isValid() || slot.sample_rate < 8000) { return; }
  const uint32_t begin = slot.playStart();
  const uint32_t end = slot.playEnd();
  const uint32_t frames = end - begin;

  if (frames < slot.sample_rate * 11 / 20) { return; }  // 550ms

  static constexpr uint8_t window_count = 6;
  const uint32_t stride = std::max<uint32_t>(1, slot.sample_rate / 4000);
  const uint32_t effective_rate = slot.sample_rate / stride;
  const uint32_t analysis_frames = std::min<uint32_t>(frames / 5, slot.sample_rate * 3 / 20);
  const uint32_t sample_count = analysis_frames / stride;
  if (sample_count < 256) { return; }
  sustain_window_t windows[window_count] = {};
  uint16_t voiced_lags[window_count] = {};
  uint8_t voiced = 0;
  uint32_t periodicity_sum = 0;
  uint32_t energy_min = UINT32_MAX;
  uint32_t energy_max = 0;
  uint16_t brightness_min = UINT16_MAX;
  uint16_t brightness_max = 0;
  const uint32_t travel = frames > analysis_frames ? frames - analysis_frames : 0;
  for (uint8_t i = 0; i < window_count; ++i) {
    // Analyze from 25% through 87.5%, avoiding the initial attack.
    uint32_t offset = (uint32_t)(((uint64_t)travel * (i + 2)) / 8);
    windows[i] = analyze_sustain_window(slot.pcm, begin + offset, stride,
                                        sample_count, effective_rate);
    energy_min = std::min(energy_min, windows[i].energy);
    energy_max = std::max(energy_max, windows[i].energy);
    brightness_min = std::min(brightness_min, windows[i].brightness_q12);
    brightness_max = std::max(brightness_max, windows[i].brightness_q12);
    if (windows[i].periodicity_q12 >= 1840 && windows[i].lag) {
      voiced_lags[voiced++] = windows[i].lag;
      periodicity_sum += windows[i].periodicity_q12;
    }
    report_import_progress(i, 0x01u);
  }
  if (voiced < 4 || energy_min == 0 || energy_max > energy_min * 5u) { return; }
  // A decay of more than roughly 6dB through the analyzed sustain area is not
  // treated as a held tone, even if its pitch remains easy to detect.
  if ((uint64_t)windows[window_count - 1].energy * 2u < windows[0].energy) { return; }
  if (brightness_min == 0 || brightness_max > brightness_min + brightness_min / 2u) { return; }

  std::sort(voiced_lags, voiced_lags + voiced);
  uint16_t median_lag = voiced_lags[voiced / 2];
  uint16_t lag_spread = voiced_lags[voiced - 1] - voiced_lags[0];
  // About +/- one semitone of drift plus normal vibrato remains acceptable.
  if (!median_lag || lag_spread > std::max<uint16_t>(2, median_lag / 6)) { return; }

  uint32_t desired_length = std::clamp<uint32_t>(frames / 2,
    slot.sample_rate / 4, slot.sample_rate * 3 / 4);
  uint32_t loop_start_target = begin + frames * 7 / 20;
  uint32_t tail_margin = std::min<uint32_t>(slot.sample_rate / 20, frames / 12);
  if (loop_start_target + desired_length + tail_margin > end) {
    loop_start_target = end - desired_length - tail_margin;
  }
  const uint32_t radius = std::max<uint32_t>(8, slot.sample_rate / 100);
  bool rising = slot.pcm[loop_start_target] >= slot.pcm[loop_start_target - 1];
  uint32_t loop_start = find_loop_zero_crossing(slot.pcm, begin, end,
                                                loop_start_target, radius, rising);
  uint32_t loop_end_target = std::min<uint32_t>(end - tail_margin, loop_start + desired_length);
  uint32_t loop_end = find_loop_zero_crossing(slot.pcm, loop_start + slot.sample_rate / 5,
                                              end, loop_end_target, radius * 2, rising);
  if (loop_end <= loop_start + slot.sample_rate / 5 || loop_end > end) { return; }

  uint32_t matched_start = loop_start;
  uint32_t matched_end = loop_end;
  if (find_matched_loop_points(slot.pcm, begin, end, loop_start_target, loop_end_target,
                               radius, radius * 2, slot.sample_rate, rising,
                               &matched_start, &matched_end)) {
    loop_start = matched_start;
    loop_end = matched_end;
  }

  slot.synth_sustain_auto = true;
  uint32_t average_periodicity = periodicity_sum / voiced;
  slot.synth_sustain_confidence = (uint8_t)std::clamp<int>(
    ((int)average_periodicity - 1600) * 100 / 2495, 1, 100);
  slot.synth_loop_start = loop_start;
  slot.synth_loop_end = loop_end;
  slot.synth_loop_crossfade = (uint16_t)std::min<uint32_t>(
    slot.sample_rate / 125, (loop_end - loop_start) / 10);  // about 8ms
}

static void build_waveform_cache(sample_slot_t& slot)
{
  static uint32_t next_revision = 1;
  slot.waveform_revision = next_revision++;
  if (next_revision == 0) { next_revision = 1; }
  for (uint8_t bin = 0; bin < sample_slot_t::waveform_bins; ++bin) {
    uint32_t start = ((uint64_t)bin * slot.frames) / sample_slot_t::waveform_bins;
    uint32_t end = ((uint64_t)(bin + 1) * slot.frames) / sample_slot_t::waveform_bins;
    if (end <= start) { end = start + 1; }
    if (end > slot.frames) { end = slot.frames; }
    int16_t min_value = INT16_MAX;
    int16_t max_value = INT16_MIN;
    for (uint32_t i = start; i < end; ++i) {
      int16_t value = slot.pcm[i];
      if (value < min_value) { min_value = value; }
      if (value > max_value) { max_value = value; }
    }
    slot.waveform_min[bin] = min_value;
    slot.waveform_max[bin] = max_value;
    report_import_progress(bin, 0x0Fu);
  }
}

// Every import path (WAV, recording PCM, MP3 PCM and Chop) enters through one
// of the loaders below.  Rebuild the slot from its declaration defaults here
// so a newly assigned sound can never inherit edit/synth parameters from the
// Pad it replaced.
static void initialize_new_sample_slot(sample_slot_t& slot, int16_t* pcm,
                                       uint32_t frames, uint32_t sample_rate,
                                       const char* display_name)
{
  slot = sample_slot_t{};
  slot.pcm = pcm;
  slot.frames = frames;
  slot.sample_rate = sample_rate;
  slot.end_frame = frames;
  snprintf(slot.name, sizeof(slot.name), "%s", display_name ? display_name : "");
}

static void initialize_asset_sample_slot(sample_slot_t& slot, sample_asset_t* asset,
                                         uint32_t asset_offset, uint32_t frames,
                                         uint32_t sample_rate, const char* display_name,
                                         bool retain_asset = true)
{
  slot = sample_slot_t{};
  if (!asset || !asset->isValid() || asset_offset >= asset->frames) { return; }
  frames = std::min<uint32_t>(frames, asset->frames - asset_offset);
  if (frames == 0) { return; }
  if (retain_asset) { pool_retain_asset(asset); }
  slot.asset = asset;
  slot.pcm = asset->pcm + asset_offset;
  slot.frames = frames;
  slot.sample_rate = sample_rate;
  slot.end_frame = frames;
  snprintf(slot.name, sizeof(slot.name), "%s", display_name ? display_name : "");
}

size_t sampler_pool_t::usedBytes(void)
{
  size_t used = 0;
  for (const auto& asset : sampler_assets) { used += asset.bytes(); }
  return used;
}

size_t sampler_pool_t::freeBytes(void)
{
  size_t used = usedBytes();
  return (used < pool_budget_bytes) ? (pool_budget_bytes - used) : 0;
}

static size_t compactable_asset_bytes(const sample_asset_t& asset)
{
  if (!asset.isValid() || asset.references == 0) { return 0; }
  uint32_t packed_frames = 0;
  uint16_t slot_references = 0;
  for (const auto& sample : sampler_pool_t::slot) {
    if (sample.asset != &asset) { continue; }
    if (!sample.isValid() || sample.pcm < asset.pcm
     || sample.pcm + sample.frames > asset.pcm + asset.frames) {
      return 0;
    }
    if (UINT32_MAX - packed_frames < sample.frames) { return 0; }
    packed_frames += sample.frames;
    ++slot_references;
  }
  // Every retained reference must belong to a stable Pad slot. A temporary
  // retain during Chop replacement is deliberately not compacted.
  if (slot_references == 0 || slot_references != asset.references
   || packed_frames >= asset.frames) { return 0; }
  return (size_t)(asset.frames - packed_frames) * sizeof(int16_t);
}

bool sampler_pool_t::compactionPending(void)
{
  return pool_compaction_pending;
}

size_t sampler_pool_t::compactableBytes(void)
{
  size_t bytes = 0;
  for (const auto& asset : sampler_assets) { bytes += compactable_asset_bytes(asset); }
  return bytes;
}

size_t sampler_pool_t::compactSparseAssets(void)
{
  static constexpr size_t minimum_reclaim_bytes = 64 * 1024;
  size_t reclaimed = 0;
  bool retry_needed = false;
  for (auto& asset : sampler_assets) {
    const size_t reclaimable = compactable_asset_bytes(asset);
    if (reclaimable < minimum_reclaim_bytes) { continue; }

    sample_slot_t* retained_slots[def::pad::pad_count] = {};
    uint8_t retained_count = 0;
    uint32_t packed_frames = 0;
    for (auto& sample : slot) {
      if (sample.asset != &asset) { continue; }
      retained_slots[retained_count++] = &sample;
      packed_frames += sample.frames;
    }
    if (retained_count == 0 || packed_frames == 0) { continue; }

    int16_t* packed_pcm = pool_alloc((size_t)packed_frames * sizeof(int16_t));
    if (!packed_pcm) {
      retry_needed = true;
      continue;
    }
    uint32_t destination = 0;
    for (uint8_t i = 0; i < retained_count; ++i) {
      auto* sample = retained_slots[i];
      memcpy(packed_pcm + destination, sample->pcm,
             (size_t)sample->frames * sizeof(int16_t));
      sample->pcm = packed_pcm + destination;
      destination += sample->frames;
    }
    int16_t* former_pcm = asset.pcm;
    asset.pcm = packed_pcm;
    asset.frames = packed_frames;
    pool_free(former_pcm);
    reclaimed += reclaimable;
  }
  pool_compaction_pending = retry_needed || compactableBytes() >= minimum_reclaim_bytes;
  return reclaimed;
}

void sampler_pool_t::analyzeBaseNote(uint8_t index)
{
  if (index >= def::pad::pad_count) { return; }
  auto& s = slot[index];
  if (!s.isValid() || s.playFrames() == 0) { return; }
  s.base_note = detect_base_note(s.pcm + s.playStart(), s.playFrames(), s.sample_rate);
  s.base_note_auto = true;
}

void sampler_pool_t::analyzeSynthSustain(uint8_t index)
{
  if (index >= def::pad::pad_count) { return; }
  analyze_synth_sustain(slot[index]);
}

bool sampler_pool_t::loadWav(uint8_t index, const char* display_name, const uint8_t* wav_data, size_t wav_size)
{
  if (index >= def::pad::pad_count) { return false; }

  wav_info_t info;
  if (!parse_wav(wav_data, wav_size, &info)) { return false; }

  erase(index);

  // 44.1kHz素材だけは48kHzへ事前変換する。通常ピッチ再生時に
  // I2Sタスクが補間せずに済むため、多重発音時の負荷を大きく抑えられる。
  const uint32_t target_rate = info.sample_rate == 44100 ? 48000 : info.sample_rate;
  uint32_t source_frames = info.frames;
  uint32_t source_frames_max = info.sample_rate * max_sample_sec;
  if (source_frames > source_frames_max) { source_frames = source_frames_max; }
  uint32_t frames = resampled_frame_count(source_frames, info.sample_rate, target_rate);
  size_t free_bytes = freeBytes();
  if ((size_t)frames * sizeof(int16_t) > free_bytes) {
    frames = free_bytes / sizeof(int16_t);
  }
  if (frames < 16) { return false; }  // プール枯渇

  sample_asset_t* asset = pool_create_asset(frames);
  if (asset == nullptr) { return false; }
  int16_t* pcm = asset->pcm;

  // モノラル化と必要時の48kHz変換を一度だけ行う。
  for (uint32_t i = 0; i < frames; ++i) {
    pcm[i] = wav_resampled_mono_frame(info, i, target_rate);
    report_import_progress(i);
  }
  normalize_pcm_for_pad(pcm, frames);

  auto& s = slot[index];
  initialize_asset_sample_slot(s, asset, 0, frames, target_rate, display_name);
  analyzeBaseNote(index);
  analyzeSynthSustain(index);
  build_waveform_cache(s);
  return true;
}

static uint32_t remap_ktsynth_frame(uint32_t frame, uint32_t source_rate,
                                    uint32_t target_rate, uint32_t target_frames)
{
  if (source_rate == target_rate) { return std::min(frame, target_frames); }
  const uint32_t mapped = (uint32_t)(((uint64_t)frame * target_rate + source_rate / 2u)
                                     / source_rate);
  return std::min(mapped, target_frames);
}

bool sampler_pool_t::loadKtSynth(uint8_t index, const char* display_name,
                                 const uint8_t* file_data, size_t file_size)
{
  if (index >= def::pad::pad_count) { return false; }
  ktsynth_info_t info;
  if (!parse_ktsynth(file_data, file_size, &info)) { return false; }

  const uint32_t target_rate = info.wav.sample_rate == 44100 ? 48000 : info.wav.sample_rate;
  const uint32_t target_frames = resampled_frame_count(info.wav.frames,
                                                        info.wav.sample_rate, target_rate);
  if (target_frames < 16 || target_frames > target_rate * max_sample_sec) { return false; }
  const size_t new_bytes = (size_t)target_frames * sizeof(int16_t);
  const size_t replacing_bytes = slot[index].asset && slot[index].asset->references == 1
    ? slot[index].asset->bytes() : 0;
  if (new_bytes > freeBytes() + replacing_bytes) { return false; }

  erase(index);
  sample_asset_t* asset = pool_create_asset(target_frames);
  if (!asset) { return false; }
  for (uint32_t i = 0; i < target_frames; ++i) {
    asset->pcm[i] = wav_resampled_mono_frame(info.wav, i, target_rate);
    report_import_progress(i);
  }

  char authored_name[64] = {};
  const size_t copy_name_bytes = std::min<size_t>(info.name_bytes, sizeof(authored_name) - 1);
  if (copy_name_bytes) { memcpy(authored_name, info.name, copy_name_bytes); }
  auto& sample = slot[index];
  initialize_asset_sample_slot(sample, asset, 0, target_frames, target_rate,
                               authored_name[0] ? authored_name : display_name);
  sample.start_frame = remap_ktsynth_frame(info.start_frame, info.wav.sample_rate,
                                            target_rate, target_frames);
  sample.end_frame = remap_ktsynth_frame(info.end_frame, info.wav.sample_rate,
                                          target_rate, target_frames);
  sample.base_note = info.root_note;
  sample.base_note_auto = false;
  sample.synth_attack_ms = info.attack_ms;
  sample.synth_release_ms = info.release_ms;
  sample.synth_tune_cents = info.tune_cents;
  sample.synth_tune_scale_q12 = (uint16_t)std::clamp<int>(
    (int)lroundf(powf(2.0f, (float)info.tune_cents / 1200.0f) * 4096.0f), 2048, 8192);
  sample.volume_q8 = info.default_gain_q8;
  if (info.sustain_mode == ktsynth_sustain_mode_t::loop) {
    sample.synth_sustain_mode = sample_sustain_mode_t::manual;
    sample.synth_loop_start = remap_ktsynth_frame(info.loop_start_frame,
                                                   info.wav.sample_rate,
                                                   target_rate, target_frames);
    sample.synth_loop_end = remap_ktsynth_frame(info.loop_end_frame,
                                                 info.wav.sample_rate,
                                                 target_rate, target_frames);
    const uint32_t crossfade = remap_ktsynth_frame(info.loop_crossfade_frames,
                                                    info.wav.sample_rate,
                                                    target_rate, target_frames);
    sample.synth_loop_crossfade = (uint16_t)std::min<uint32_t>(crossfade, UINT16_MAX);
  } else {
    sample.synth_sustain_mode = sample_sustain_mode_t::off;
    sample.synth_loop_start = 0;
    sample.synth_loop_end = 0;
    sample.synth_loop_crossfade = 0;
  }
  build_waveform_cache(sample);
  return sample.isValid();
}

static size_t replaceable_slot_bytes(const sample_slot_t& sample)
{
  return sample.asset && sample.asset->references == 1 ? sample.asset->bytes() : 0;
}

static void erase_synth_source_slot(sample_slot_t& sample)
{
  if (sample.asset) {
    M5.delay(8);
    pool_release_asset(sample.asset);
  } else if (sample.pcm) {
    M5.delay(8);
    pool_free(sample.pcm);
  }
  sample = {};
}

static bool load_synth_wav_slot(sample_slot_t& destination, const char* display_name,
                                const uint8_t* wav_data, size_t wav_size,
                                bool preserve_pcm = false)
{
  wav_info_t info;
  if (!parse_wav(wav_data, wav_size, &info)) { return false; }
  const uint32_t target_rate = info.sample_rate == 44100 ? 48000 : info.sample_rate;
  uint32_t source_frames = std::min<uint32_t>(info.frames,
                                               info.sample_rate * sampler_pool_t::max_sample_sec);
  const uint32_t frames = resampled_frame_count(source_frames, info.sample_rate, target_rate);
  if (frames < 16 || (size_t)frames * sizeof(int16_t)
       > sampler_pool_t::freeBytes() + replaceable_slot_bytes(destination)) {
    return false;
  }
  erase_synth_source_slot(destination);
  sample_asset_t* asset = pool_create_asset(frames);
  if (!asset) { return false; }
  for (uint32_t i = 0; i < frames; ++i) {
    asset->pcm[i] = wav_resampled_mono_frame(info, i, target_rate);
    report_import_progress(i);
  }
  if (!preserve_pcm) { normalize_pcm_for_pad(asset->pcm, frames); }
  initialize_asset_sample_slot(destination, asset, 0, frames, target_rate, display_name);
  destination.base_note = detect_base_note(destination.pcm, destination.frames,
                                            destination.sample_rate);
  destination.base_note_auto = true;
  analyze_synth_sustain(destination);
  build_waveform_cache(destination);
  return destination.isValid();
}

static bool load_synth_ktsynth_slot(sample_slot_t& destination, const char* display_name,
                                    const uint8_t* file_data, size_t file_size)
{
  ktsynth_info_t info;
  if (!parse_ktsynth(file_data, file_size, &info)) { return false; }
  const uint32_t target_rate = info.wav.sample_rate == 44100 ? 48000 : info.wav.sample_rate;
  const uint32_t frames = resampled_frame_count(info.wav.frames, info.wav.sample_rate, target_rate);
  if (frames < 16 || frames > target_rate * sampler_pool_t::max_sample_sec
   || (size_t)frames * sizeof(int16_t)
       > sampler_pool_t::freeBytes() + replaceable_slot_bytes(destination)) {
    return false;
  }
  erase_synth_source_slot(destination);
  sample_asset_t* asset = pool_create_asset(frames);
  if (!asset) { return false; }
  for (uint32_t i = 0; i < frames; ++i) {
    asset->pcm[i] = wav_resampled_mono_frame(info.wav, i, target_rate);
    report_import_progress(i);
  }
  char authored_name[64] = {};
  const size_t copy_name_bytes = std::min<size_t>(info.name_bytes, sizeof(authored_name) - 1);
  if (copy_name_bytes) { memcpy(authored_name, info.name, copy_name_bytes); }
  initialize_asset_sample_slot(destination, asset, 0, frames, target_rate,
                               authored_name[0] ? authored_name : display_name);
  destination.start_frame = remap_ktsynth_frame(info.start_frame, info.wav.sample_rate,
                                                 target_rate, frames);
  destination.end_frame = remap_ktsynth_frame(info.end_frame, info.wav.sample_rate,
                                               target_rate, frames);
  destination.base_note = info.root_note;
  destination.base_note_auto = false;
  destination.synth_attack_ms = info.attack_ms;
  destination.synth_release_ms = info.release_ms;
  destination.synth_tune_cents = info.tune_cents;
  destination.synth_tune_scale_q12 = (uint16_t)std::clamp<int>(
    (int)lroundf(powf(2.0f, (float)info.tune_cents / 1200.0f) * 4096.0f), 2048, 8192);
  destination.volume_q8 = info.default_gain_q8;
  if (info.sustain_mode == ktsynth_sustain_mode_t::loop) {
    destination.synth_sustain_mode = sample_sustain_mode_t::manual;
    destination.synth_loop_start = remap_ktsynth_frame(info.loop_start_frame,
                                                        info.wav.sample_rate,
                                                        target_rate, frames);
    destination.synth_loop_end = remap_ktsynth_frame(info.loop_end_frame,
                                                      info.wav.sample_rate,
                                                      target_rate, frames);
    destination.synth_loop_crossfade = (uint16_t)std::min<uint32_t>(
      remap_ktsynth_frame(info.loop_crossfade_frames, info.wav.sample_rate,
                           target_rate, frames), UINT16_MAX);
  } else {
    destination.synth_sustain_mode = sample_sustain_mode_t::off;
  }
  build_waveform_cache(destination);
  return destination.isValid();
}

bool sampler_pool_t::loadSynthWav(uint8_t synth_index, const char* display_name,
                                  const uint8_t* wav_data, size_t wav_size)
{
  return synth_index < synth_source_count
      && load_synth_wav_slot(synth_source[synth_index], display_name, wav_data, wav_size);
}

bool sampler_pool_t::loadSynthWavPreserved(uint8_t synth_index, const char* display_name,
                                           const uint8_t* wav_data, size_t wav_size)
{
  return synth_index < synth_source_count
      && load_synth_wav_slot(synth_source[synth_index], display_name,
                              wav_data, wav_size, true);
}

bool sampler_pool_t::loadSynthPcmOwned(uint8_t synth_index, const char* display_name,
                                       int16_t* pcm_data, uint32_t frames,
                                       uint32_t sample_rate)
{
  if (synth_index >= synth_source_count || !pcm_data || frames < 16
   || sample_rate == 0 || sample_rate > 48000
   || frames > sample_rate * max_sample_sec) {
    return false;
  }
  auto& destination = synth_source[synth_index];
  const size_t new_bytes = (size_t)frames * sizeof(int16_t);
  if (new_bytes > freeBytes() + replaceable_slot_bytes(destination)) { return false; }
  erase_synth_source_slot(destination);
  normalize_pcm_for_pad(pcm_data, frames);
  sample_asset_t* asset = pool_adopt_asset(pcm_data, frames);
  if (!asset) { return false; }
  initialize_asset_sample_slot(destination, asset, 0, frames, sample_rate, display_name);
  destination.base_note = detect_base_note(destination.pcm, destination.frames,
                                            destination.sample_rate);
  destination.base_note_auto = true;
  analyze_synth_sustain(destination);
  build_waveform_cache(destination);
  return destination.isValid();
}

bool sampler_pool_t::loadSynthKtSynth(uint8_t synth_index, const char* display_name,
                                      const uint8_t* file_data, size_t file_size)
{
  return synth_index < synth_source_count
      && load_synth_ktsynth_slot(synth_source[synth_index], display_name,
                                  file_data, file_size);
}

void sampler_pool_t::eraseSynth(uint8_t synth_index)
{
  if (synth_index < synth_source_count) { erase_synth_source_slot(synth_source[synth_index]); }
}

static bool load_pcm_for_pad(uint8_t index, const char* display_name, const int16_t* pcm_data,
                             uint32_t frames, uint32_t sample_rate, uint32_t target_peak,
                             bool normalize = true)
{
  if (index >= def::pad::pad_count || pcm_data == nullptr || frames == 0 || sample_rate == 0 || sample_rate > 48000) {
    return false;
  }

  sampler_pool_t::erase(index);

  uint32_t frames_max = sample_rate * sampler_pool_t::max_sample_sec;
  if (frames > frames_max) { frames = frames_max; }
  size_t free_bytes = sampler_pool_t::freeBytes();
  if ((size_t)frames * sizeof(int16_t) > free_bytes) {
    frames = free_bytes / sizeof(int16_t);
  }
  if (frames < 16) { return false; }

  sample_asset_t* asset = pool_create_asset(frames);
  if (asset == nullptr) { return false; }
  int16_t* pcm = asset->pcm;
  memcpy(pcm, pcm_data, (size_t)frames * sizeof(int16_t));
  if (normalize) { normalize_pcm_for_pad(pcm, frames, target_peak); }

  auto& s = sampler_pool_t::slot[index];
  initialize_asset_sample_slot(s, asset, 0, frames, sample_rate, display_name);
  sampler_pool_t::analyzeBaseNote(index);
  sampler_pool_t::analyzeSynthSustain(index);
  build_waveform_cache(s);
  return true;
}

bool sampler_pool_t::loadPcm(uint8_t index, const char* display_name, const int16_t* pcm_data, uint32_t frames, uint32_t sample_rate)
{
  return load_pcm_for_pad(index, display_name, pcm_data, frames, sample_rate, 8192);
}

bool sampler_pool_t::loadPcmPreserved(uint8_t index, const char* display_name, const int16_t* pcm_data,
                                      uint32_t frames, uint32_t sample_rate)
{
  return load_pcm_for_pad(index, display_name, pcm_data, frames, sample_rate, 0, false);
}

bool sampler_pool_t::loadRecordedPcm(uint8_t index, const char* display_name, const int16_t* pcm_data, uint32_t frames, uint32_t sample_rate)
{
  // 通常素材より約2dB大きい-10dBFS相当。録音した音を埋もれにくくする一方、
  // 複数Padの同時演奏でも出力リミッターが常時働きにくい水準に留める。
  return load_pcm_for_pad(index, display_name, pcm_data, frames, sample_rate, 10240);
}

static bool load_pcm_owned_for_pad(uint8_t index, const char* display_name, int16_t* pcm_data,
                                   uint32_t frames, uint32_t sample_rate,
                                   uint32_t normalize_peak)
{
  if (index >= def::pad::pad_count || pcm_data == nullptr || frames < 16 || sample_rate == 0 || sample_rate > 48000) {
    return false;
  }
  uint32_t frames_max = sample_rate * sampler_pool_t::max_sample_sec;
  if (frames > frames_max) { frames = frames_max; }

  // 録音バッファなど、すでに確保済みのPCMもプール上限に含める。
  // BGMとWi-Fi/TLSが必要とするPSRAMを食い切らないようにする。
  const size_t new_bytes = (size_t)frames * sizeof(int16_t);
  // A shared Chop Asset is released only after its final Slice disappears.
  // Count only storage that this replacement can actually return to the pool.
  const size_t replacing_bytes = sampler_pool_t::slot[index].asset
    ? (sampler_pool_t::slot[index].asset->references == 1
        ? sampler_pool_t::slot[index].asset->bytes() : 0)
    : sampler_pool_t::slot[index].bytes();
  if (new_bytes > sampler_pool_t::freeBytes() + replacing_bytes) { return false; }

  sampler_pool_t::erase(index);
  if (normalize_peak) { normalize_pcm_for_pad(pcm_data, frames, normalize_peak); }

  sample_asset_t* asset = pool_adopt_asset(pcm_data, frames);
  if (!asset) { return false; }

  auto& s = sampler_pool_t::slot[index];
  initialize_asset_sample_slot(s, asset, 0, frames, sample_rate, display_name);
  sampler_pool_t::analyzeBaseNote(index);
  sampler_pool_t::analyzeSynthSustain(index);
  build_waveform_cache(s);
  return true;
}

bool sampler_pool_t::loadPcmOwned(uint8_t index, const char* display_name,
                                  int16_t* pcm_data, uint32_t frames, uint32_t sample_rate)
{
  return load_pcm_owned_for_pad(index, display_name, pcm_data, frames, sample_rate, 8192);
}

bool sampler_pool_t::loadPcmOwnedPreserved(uint8_t index, const char* display_name,
                                           int16_t* pcm_data, uint32_t frames, uint32_t sample_rate)
{
  return load_pcm_owned_for_pad(index, display_name, pcm_data, frames, sample_rate, 0);
}

bool sampler_pool_t::loadRecordedPcmOwned(uint8_t index, const char* display_name,
                                          int16_t* pcm_data, uint32_t frames,
                                          uint32_t sample_rate)
{
  return load_pcm_owned_for_pad(index, display_name, pcm_data, frames, sample_rate, 10240);
}

bool sampler_pool_t::loadSharedSlice(uint8_t index, const char* display_name,
                                     sample_asset_t* asset, uint32_t asset_offset,
                                     uint32_t frames, uint32_t sample_rate)
{
  if (index >= def::pad::pad_count || !asset || !asset->isValid()
   || asset_offset >= asset->frames || frames < 16 || sample_rate == 0) {
    return false;
  }
  frames = std::min<uint32_t>(frames, asset->frames - asset_offset);
  // Retain before release: index may itself be the former source Pad.
  pool_retain_asset(asset);
  erase(index);
  initialize_asset_sample_slot(slot[index], asset, asset_offset, frames,
                               sample_rate, display_name, false);
  build_waveform_cache(slot[index]);
  return slot[index].isValid();
}

bool sampler_pool_t::clone(uint8_t destination, uint8_t source)
{
  if (destination >= def::pad::pad_count || source >= def::pad::pad_count
   || destination == source || slot[destination].isValid() || !slot[source].isValid()) {
    return false;
  }
  const auto& src = slot[source];
  const uint32_t copy_start = src.playStart();
  const uint32_t copy_end = src.playEnd();
  if (copy_end <= copy_start) { return false; }
  const uint32_t copy_frames = copy_end - copy_start;
  const size_t bytes = (size_t)copy_frames * sizeof(int16_t);
  if (bytes == 0 || bytes > freeBytes()) { return false; }
  sample_asset_t* asset = pool_create_asset(copy_frames);
  if (!asset) { return false; }
  int16_t* pcm = asset->pcm;
  memcpy(pcm, src.pcm + copy_start, bytes);

  // A short edited clip behaves like a true duplicate: retain all settings
  // whose coordinates survive the Start--End bake.  Longer copies become a
  // clean independent sound so a 20-second source cannot accidentally bring
  // an expensive sustain/repeat setup along with it.
  const bool copy_all_parameters = copy_frames <= src.sample_rate * 3u;
  sample_slot_t duplicate = src;
  duplicate.pcm = pcm;
  duplicate.asset = asset;
  duplicate.frames = copy_frames;
  duplicate.start_frame = 0;
  duplicate.end_frame = copy_frames;
  duplicate.file_path[0] = '\0';
  duplicate.beat_anchor_enabled = src.beatAnchorValid()
    && src.beat_anchor_frame >= copy_start && src.beat_anchor_frame < copy_end;
  duplicate.beat_anchor_frame = duplicate.beat_anchor_enabled
    ? src.beat_anchor_frame - copy_start : 0;

  const bool loop_range_survives = src.synth_loop_start >= copy_start
    && src.synth_loop_end > src.synth_loop_start && src.synth_loop_end <= copy_end;
  if (copy_all_parameters && loop_range_survives) {
    duplicate.synth_loop_start = src.synth_loop_start - copy_start;
    duplicate.synth_loop_end = src.synth_loop_end - copy_start;
  } else {
    duplicate.synth_sustain_auto = false;
    duplicate.synth_sustain_confidence = 0;
    duplicate.synth_sustain_mode = sample_sustain_mode_t::off;
    duplicate.synth_loop_start = 0;
    duplicate.synth_loop_end = 0;
    duplicate.synth_loop_crossfade = 0;
  }
  if (!copy_all_parameters) {
    duplicate.hold_enabled = false;
    duplicate.loop_enabled = false;
    duplicate.loop_whole_sample = false;
    duplicate.choke_enabled = false;
    duplicate.synth_attack_ms = 0;
    duplicate.synth_release_ms = 120;
    duplicate.synth_tune_cents = 0;
    duplicate.synth_tune_scale_q12 = 4096;
  }
  pool_retain_asset(asset);
  slot[destination] = duplicate;
  if (duplicate.base_note_auto) { analyzeBaseNote(destination); }
  build_waveform_cache(slot[destination]);
  return true;
}

void sampler_pool_t::erase(uint8_t index)
{
  if (index >= def::pad::pad_count) { return; }
  auto& s = slot[index];
  if (s.asset) {
    // 再生ボイスが停止済みでも、オーディオタスクが現在のブロックを処理し終えるのを待つ
    M5.delay(8);
    sample_asset_t* former_asset = s.asset;
    pool_release_asset(former_asset);
    if (former_asset->isValid()) { pool_compaction_pending = true; }
  } else if (s.pcm) {
    // Beat pool and temporary slots own PCM directly; Sample slots use assets.
    M5.delay(8);
    pool_free(s.pcm);
  }
  // Keep the empty-pad definition in one place.  Besides making deletion
  // complete today, this prevents a newly added per-sample parameter from
  // accidentally surviving into the next recording/import.
  s = sample_slot_t{};
}

size_t beat_pool_t::usedBytes(void)
{
  size_t used = 0;
  for (const auto& s : slot) { used += s.bytes(); }
  return used;
}

size_t beat_pool_t::freeBytes(void)
{
  const size_t used = usedBytes();
  return used < pool_budget_bytes ? pool_budget_bytes - used : 0;
}

bool beat_pool_t::loadWav(uint8_t index, const char* display_name,
                          const uint8_t* wav_data, size_t wav_size)
{
  if (index >= def::pad::pad_count) { return false; }
  wav_info_t info;
  if (!parse_wav(wav_data, wav_size, &info)) { return false; }

  const uint32_t target_rate = info.sample_rate == 44100 ? 48000 : info.sample_rate;
  const uint32_t source_frames = std::min<uint32_t>(info.frames,
                                                     info.sample_rate * max_sample_sec);
  uint32_t frames = resampled_frame_count(source_frames, info.sample_rate, target_rate);
  const size_t replacing = slot[index].bytes();
  const size_t available = freeBytes() + replacing;
  if ((size_t)frames * sizeof(int16_t) > available) {
    frames = available / sizeof(int16_t);
  }
  if (frames < 16) { return false; }

  int16_t* pcm = pool_alloc((size_t)frames * sizeof(int16_t));
  if (!pcm) { return false; }
  for (uint32_t frame = 0; frame < frames; ++frame) {
    pcm[frame] = wav_resampled_mono_frame(info, frame, target_rate);
    report_import_progress(frame);
  }
  // Beat one-shots use the same conservative -12dBFS reference as Sample.
  // The master limiter remains the final protection for dense patterns.
  normalize_pcm_for_pad(pcm, frames);

  erase(index);
  initialize_new_sample_slot(slot[index], pcm, frames, target_rate, display_name);
  slot[index].synth_sustain_mode = sample_sustain_mode_t::off;
  slot[index].synth_release_ms = 10;
  build_waveform_cache(slot[index]);
  return true;
}

bool beat_pool_t::loadPcmOwned(uint8_t index, const char* display_name,
                               int16_t* pcm_data, uint32_t frames,
                               uint32_t sample_rate, bool recorded)
{
  if (index >= def::pad::pad_count || !pcm_data || sample_rate == 0) { return false; }
  frames = std::min<uint32_t>(frames, sample_rate * max_sample_sec);
  if (frames < 16 || (size_t)frames * sizeof(int16_t) > freeBytes() + slot[index].bytes()) {
    return false;
  }
  normalize_pcm_for_pad(pcm_data, frames, recorded ? 10240 : 8192);
  erase(index);
  initialize_new_sample_slot(slot[index], pcm_data, frames, sample_rate, display_name);
  slot[index].synth_sustain_mode = sample_sustain_mode_t::off;
  slot[index].synth_release_ms = 10;
  build_waveform_cache(slot[index]);
  return true;
}

bool beat_pool_t::loadPcm(uint8_t index, const char* display_name,
                          const int16_t* pcm_data, uint32_t frames,
                          uint32_t sample_rate)
{
  if (index >= def::pad::pad_count || !pcm_data || sample_rate == 0) { return false; }
  frames = std::min<uint32_t>(frames, sample_rate * max_sample_sec);
  if (frames < 16 || (size_t)frames * sizeof(int16_t) > freeBytes() + slot[index].bytes()) {
    return false;
  }
  int16_t* copy = pool_alloc((size_t)frames * sizeof(int16_t));
  if (!copy) { return false; }
  memcpy(copy, pcm_data, (size_t)frames * sizeof(int16_t));
  if (!loadPcmOwned(index, display_name, copy, frames, sample_rate)) {
    pool_free(copy);
    return false;
  }
  return true;
}

bool beat_pool_t::loadRecordedPcm(uint8_t index, const char* display_name,
                                  const int16_t* pcm_data, uint32_t frames,
                                  uint32_t sample_rate)
{
  if (index >= def::pad::pad_count || !pcm_data || sample_rate == 0) { return false; }
  frames = std::min<uint32_t>(frames, sample_rate * max_sample_sec);
  if (frames < 16 || (size_t)frames * sizeof(int16_t) > freeBytes() + slot[index].bytes()) {
    return false;
  }
  int16_t* copy = pool_alloc((size_t)frames * sizeof(int16_t));
  if (!copy) { return false; }
  memcpy(copy, pcm_data, (size_t)frames * sizeof(int16_t));
  if (!loadPcmOwned(index, display_name, copy, frames, sample_rate, true)) {
    pool_free(copy);
    return false;
  }
  return true;
}

bool beat_pool_t::clone(uint8_t destination, uint8_t source)
{
  if (destination >= def::pad::pad_count || source >= def::pad::pad_count
   || destination == source || !slot[source].isValid()) { return false; }
  const auto original = slot[source];
  if (original.bytes() > freeBytes() + slot[destination].bytes()) { return false; }
  int16_t* copy = pool_alloc(original.bytes());
  if (!copy) { return false; }
  memcpy(copy, original.pcm, original.bytes());
  erase(destination);
  slot[destination] = original;
  slot[destination].pcm = copy;
  slot[destination].asset = nullptr;
  build_waveform_cache(slot[destination]);
  return true;
}

void beat_pool_t::erase(uint8_t index)
{
  if (index >= def::pad::pad_count) { return; }
  auto& s = slot[index];
  if (s.pcm) {
    M5.delay(8);
    pool_free(s.pcm);
  }
  s = sample_slot_t{};
}

void beat_pool_t::clear(void)
{
  for (uint8_t index = 0; index < def::pad::pad_count; ++index) { erase(index); }
}

//-------------------------------------------------------------------------
} // namespace sampler_ns

#endif // defined (KANPLAY_SAMPLER)
