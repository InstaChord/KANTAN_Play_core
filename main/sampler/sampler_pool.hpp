// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_POOL_HPP
#define KANTAN_SAMPLER_POOL_HPP

#include <stdint.h>
#include <stddef.h>

#include "sampler_define.hpp"

namespace sampler_ns {
//-------------------------------------------------------------------------
enum class sample_sustain_mode_t : uint8_t {
  off,
  automatic,
  manual,
};

// A PCM asset may back more than one Pad.  Chop slices retain the same asset
// and only describe their own range, so a long phrase is never duplicated in
// PSRAM.  Regular imports still receive one private asset each.
struct sample_asset_t {
  int16_t* pcm = nullptr;
  uint32_t frames = 0;
  uint16_t references = 0;

  bool isValid(void) const { return pcm != nullptr && frames != 0; }
  size_t bytes(void) const { return (size_t)frames * sizeof(int16_t); }
};

// PSRAM上のサンプルプール管理
//
// 設計方針 (レスポンス最優先):
//   - 演奏中に使う全サンプルを PSRAM に常駐させ、再生時に SD へアクセスしない
//   - 内部フォーマットは 16bit / mono。44.1kHzインポートは48kHzへ事前変換し、
//     通常ピッチ時の再生補間を省略する。録音など他レートは元レートを維持する。
//   - プール上限 5MB。Audio Beat・Wi-Fi/TLS・Webサーバー用にPSRAMの余白を残す

struct sample_slot_t {
  static constexpr const uint8_t waveform_bins = 96;
  char name[24] = { 0 };
  char file_path[80] = { 0 };  // Kit保存用のSD上WAVパス。録音直後など未保存PCMは空。
  int16_t* pcm = nullptr;   // PSRAM上のモノラルPCM (未使用時 nullptr)
  sample_asset_t* asset = nullptr;  // pcmの所有者。Chop Sliceでは複数Padで共有する。
  uint32_t frames = 0;
  uint32_t sample_rate = 44100;
  uint32_t start_frame = 0;
  uint32_t end_frame = 0;    // 0ならサンプル末尾
  uint16_t volume_q8 = 256;  // 256 = 100%
  uint16_t pitch_q8 = 256;   // 256 = 100%, 128 = 50%, 512 = 200%
  uint8_t base_note = 60;    // C4。外部MIDI Pad音源の基準ノート
  bool base_note_auto = true;
  bool synth_sustain_auto = false;
  uint8_t synth_sustain_confidence = 0;
  uint32_t synth_loop_start = 0;  // Melody/Chord用。slot先頭からのフレーム
  uint32_t synth_loop_end = 0;
  uint16_t synth_loop_crossfade = 0;
  sample_sustain_mode_t synth_sustain_mode = sample_sustain_mode_t::automatic;
  uint16_t synth_attack_ms = 0;
  uint16_t synth_release_ms = 120;
  uint16_t synth_delay_100us = 0;
  uint16_t synth_hold_ms = 0;
  uint16_t synth_decay_ms = 0;
  uint16_t synth_sustain_level_q15 = 32768;
  int16_t synth_tune_cents = 0;
  uint16_t synth_tune_scale_q12 = 4096;  // centsから事前計算する演奏用倍率
  bool reverse = false;
  bool hold_enabled = false;
  // Enabled Sampler Pads share one choke group. Pitched synth parts that use
  // the same PCM remain independent from this performance setting.
  bool choke_enabled = false;
  bool loop_enabled = false;
  // true: repeat the edited playback range immediately in the audio voice.
  // false: repeat on the Note Grid using loop_grid_half_steps.
  bool loop_whole_sample = false;
  uint8_t loop_grid_half_steps = 8;  // Note Grid x 4.0 (0.5 step units)
  // Chop元の等分位置。実際のPCM先頭と分離し、前後の音を
  // 残したまま拍頭だけをNote Gridへ合わせる。
  bool beat_anchor_enabled = false;
  uint32_t beat_anchor_frame = 0;
  // Chopped Pads keep their musical relationship even after their original
  // long source Asset is compacted or a Project is reloaded. Playback tempo
  // is applied independently from the user's Pitch setting.
  uint16_t chop_group_id = 0;       // 0 = regular Sample
  uint8_t chop_slice_index = 0;
  uint8_t chop_slice_count = 0;
  uint32_t chop_native_loop_msec = 0;
  uint16_t chop_tempo_q8 = 256;
  // 登録時に作る縮小波形。Pad再描画時のPCM全走査を避ける。
  // PCMアドレスは削除後の読み込みで再利用されるため、描画キャッシュは
  // この世代番号で別の音声であることを判定する。保存データには含めない。
  uint32_t waveform_revision = 0;
  int16_t waveform_min[waveform_bins] = { 0 };
  int16_t waveform_max[waveform_bins] = { 0 };

  bool isValid(void) const { return pcm != nullptr && frames != 0; }
  size_t bytes(void) const { return (size_t)frames * sizeof(int16_t); }
  float seconds(void) const { return sample_rate ? (float)frames / sample_rate : 0.0f; }
  uint32_t playStart(void) const { return start_frame < frames ? start_frame : 0; }
  uint32_t playEnd(void) const { return (end_frame > playStart() && end_frame <= frames) ? end_frame : frames; }
  uint32_t playFrames(void) const { return playEnd() - playStart(); }
  bool beatAnchorValid(void) const {
    return beat_anchor_enabled && beat_anchor_frame >= playStart()
        && beat_anchor_frame < playEnd() && !reverse;
  }
  bool isChopSlice(void) const {
    return chop_group_id != 0 && chop_slice_count != 0 && chop_native_loop_msec != 0;
  }
  uint16_t samplerPlaybackPitchQ8(void) const {
    const uint32_t tempo = isChopSlice() ? chop_tempo_q8 : 256u;
    const uint32_t combined = ((uint32_t)pitch_q8 * tempo + 128u) >> 8;
    return (uint16_t)(combined < 32u ? 32u : combined > 2048u ? 2048u : combined);
  }
};

// Only KANTAN Synth can own a second PCM layer. Keep its descriptor compact:
// normal Pad names, paths and 96-bin waveform previews would otherwise waste
// about 1.5 KiB of scarce internal RAM for three invisible layer descriptors.
struct synth_layer_slot_t {
  int16_t* pcm = nullptr;
  sample_asset_t* asset = nullptr;
  uint32_t frames = 0;
  uint32_t sample_rate = 48000;
  uint32_t start_frame = 0;
  uint32_t end_frame = 0;
  uint16_t volume_q8 = 0;
  uint16_t pitch_q8 = 256;
  uint8_t base_note = 60;
  uint32_t synth_loop_start = 0;
  uint32_t synth_loop_end = 0;
  uint16_t synth_loop_crossfade = 0;
  sample_sustain_mode_t synth_sustain_mode = sample_sustain_mode_t::off;
  uint16_t synth_attack_ms = 0;
  uint16_t synth_release_ms = 120;
  uint16_t synth_delay_100us = 0;
  uint16_t synth_hold_ms = 0;
  uint16_t synth_decay_ms = 0;
  uint16_t synth_sustain_level_q15 = 32768;
  int16_t synth_tune_cents = 0;
  uint16_t synth_tune_scale_q12 = 4096;

  bool isValid(void) const { return pcm != nullptr && frames != 0; }
  uint32_t playStart(void) const { return start_frame < frames ? start_frame : 0; }
  uint32_t playEnd(void) const {
    return (end_frame > playStart() && end_frame <= frames) ? end_frame : frames;
  }
  uint32_t playFrames(void) const { return playEnd() - playStart(); }
};

class sampler_pool_t {
public:
  using progress_callback_t = void (*)();
  static constexpr const size_t pool_budget_bytes = 5 * 1024 * 1024;
  static constexpr const uint32_t max_sample_sec = 20;  // Long Chop素材を含む上限
  static constexpr const uint8_t synth_source_count = 3; // Melody / Chord / Bass
  static constexpr const uint8_t asset_capacity = 30;   // 12 Pad + 3 x 2 Synth + Chop素材/変換の余白

  static sample_slot_t slot[def::pad::pad_count];
  static sample_slot_t synth_source[synth_source_count];
  static synth_layer_slot_t synth_layer2[synth_source_count];
  static uint8_t synth_layer_count[synth_source_count];

  static size_t usedBytes(void);
  static size_t freeBytes(void);

  // Shared Chop Assets can become sparse after their source Pad or some
  // slices are deleted. Compact the remaining Pad ranges into one smaller
  // shared Asset. Call only while no audio voice can read Pad PCM.
  static bool compactionPending(void);
  static size_t compactableBytes(void);
  static size_t compactSparseAssets(void);

  // WAVデータ(PCM16 mono/stereo 〜48kHz)をモノラル変換してスロットへ登録
  // 上限秒数・プール残量に収まらない場合は末尾を切り詰める
  static bool loadWav(uint8_t index, const char* display_name, const uint8_t* wav_data, size_t wav_size);
  // KANTAN Synth toneを検証して登録する。Converterが設定した基準音、
  // Loop、Envelope、Gainを維持し、自動解析や正規化は行わない。
  static bool loadKtSynth(uint8_t index, const char* display_name,
                          const uint8_t* file_data, size_t file_size);
  static bool loadSynthWav(uint8_t synth_index, const char* display_name,
                           const uint8_t* wav_data, size_t wav_size);
  static bool loadSynthWavPreserved(uint8_t synth_index, const char* display_name,
                                    const uint8_t* wav_data, size_t wav_size);
  static bool loadSynthPcmOwned(uint8_t synth_index, const char* display_name,
                                int16_t* pcm_data, uint32_t frames,
                                uint32_t sample_rate);
  static bool loadSynthKtSynth(uint8_t synth_index, const char* display_name,
                               const uint8_t* file_data, size_t file_size);
  // Reuse an already decoded synth PCM asset while keeping independent
  // per-part metadata. Intended for identical immutable builtin tones.
  static bool shareSynth(uint8_t destination, uint8_t source,
                         const char* display_name,
                         const uint8_t* file_data, size_t file_size);
  static void eraseSynth(uint8_t synth_index);
  // Optional UI hook for long import conversions. The audio data path stays
  // independent from the UI; callers clear this immediately after import.
  static void setProgressCallback(progress_callback_t callback);

  // PCM16 monoをスロットへ登録する。録音済みバッファや編集結果の登録に使う。
  static bool loadPcm(uint8_t index, const char* display_name, const int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);

  // PCMの振幅を変更せずに登録する。1つの素材を複数Padへ切り分ける時など、
  // 元素材内の音量バランスを保つ必要がある処理に限定して使用する。
  static bool loadPcmPreserved(uint8_t index, const char* display_name, const int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);

  // マイク録音用。通常のインポート素材より少し大きい基準へ正規化するが、
  // 多重発音とマスター・リミッターのため十分なヘッドルームは残す。
  static bool loadRecordedPcm(uint8_t index, const char* display_name, const int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);

  // PSRAM上に確保済みのPCM16 monoをスロットへ登録し、所有権を引き取る。
  // 成功時のみ pcm_data をプールが解放する。失敗時は呼び出し元が解放すること。
  static bool loadPcmOwned(uint8_t index, const char* display_name, int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);
  static bool loadPcmOwnedPreserved(uint8_t index, const char* display_name,
                                    int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);
  static bool loadRecordedPcmOwned(uint8_t index, const char* display_name,
                                   int16_t* pcm_data, uint32_t frames, uint32_t sample_rate);

  // Chop確定用。既存Assetの一部分を新しいPadへ割り当てる。PCMはコピーせず、
  // Assetの参照数で寿命を管理するため、複数のLong素材を安全に共存できる。
  static bool loadSharedSlice(uint8_t index, const char* display_name,
                              sample_asset_t* asset, uint32_t asset_offset,
                              uint32_t frames, uint32_t sample_rate);

  // PCMとPad設定をそのまま複製する。移動後の「もう一度タップして複写」に使う。
  // シーケンスイベントは呼び出し側で扱うため、ここでは複製しない。
  static bool clone(uint8_t destination, uint8_t source);

  // 現在のStart/End範囲から基音を推定する。手動設定の保護は呼び出し側で判断する。
  static void analyzeBaseNote(uint8_t index);
  // 安定した伸ばし音だけをMelody/Chord用サステイン候補にする。
  // 読み込み・録音終了・トリム確定時にのみ呼び出す。
  static void analyzeSynthSustain(uint8_t index);

  // スロットを解放する (呼び出し前に該当ボイスを停止しておくこと)
  static void erase(uint8_t index);
};

// BEAT has its own compact pool. These sounds are short one-shots and never
// consume the editable SAMPLER Pad budget; keeping the ownership separate
// also lets Audio Beat release all Pattern memory immediately.
class beat_pool_t {
public:
  static constexpr const size_t pool_budget_bytes = 1536 * 1024;
  static constexpr const uint32_t max_sample_sec = 2;
  static sample_slot_t slot[def::pad::pad_count];

  static size_t usedBytes(void);
  static size_t freeBytes(void);
  static bool loadWav(uint8_t index, const char* display_name,
                      const uint8_t* wav_data, size_t wav_size);
  // Copy an immutable PCM source (for example AMY's flash-resident tiny bank)
  // into the owned Beat pool while retaining the normal normalization/UI path.
  static bool loadPcm(uint8_t index, const char* display_name,
                      const int16_t* pcm_data, uint32_t frames,
                      uint32_t sample_rate);
  static bool loadPcmOwned(uint8_t index, const char* display_name,
                           int16_t* pcm_data, uint32_t frames, uint32_t sample_rate,
                           bool recorded = false);
  static bool loadRecordedPcm(uint8_t index, const char* display_name,
                              const int16_t* pcm_data, uint32_t frames,
                              uint32_t sample_rate);
  static bool clone(uint8_t destination, uint8_t source);
  static void erase(uint8_t index);
  static void clear(void);
};

//-------------------------------------------------------------------------
} // namespace sampler_ns

#endif
