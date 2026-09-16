// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_AUDIO_HPP
#define KANTAN_SAMPLER_AUDIO_HPP

#include <stdint.h>
#include <stddef.h>

namespace sampler_ns {
//-------------------------------------------------------------------------
// サンプル再生エンジン
// KANTAN Play base のオーディオ経路 (SI5351 → ES8388(I2Sマスター) → アンプ) を
// task_i2s.cpp と同一の I2S スレーブ設定で使用し、入力(SAM音源/マイク)の
// パススルーに対して PCM16 WAV のボイスをミキシングして出力する。
// 実効サンプルレートは 48kHz (MCLK 6.144MHz / 128)。
class sampler_audio_t {
public:
  enum fx_target_mask_t : uint8_t {
    fx_target_beat = 1,
    fx_target_parts = 2,
    fx_target_music = 4,
    fx_target_live = fx_target_beat | fx_target_parts,
    fx_target_all = fx_target_live | fx_target_music,
  };
  // Existing roles remain at 0-29. Voices 30-37 are a dedicated second-PCM
  // bank paired with the eight pitched voices; they do not reduce logical
  // Melody/Chord/Bass/BLE polyphony. Voice 38 is the isolated Layer 2 menu
  // audition voice, paired with the existing Layer 1 audition voice.
  static constexpr const size_t max_voice = 39;
  static constexpr const uint32_t sample_rate = 48000;

  bool start(void);

  // モノラルPCM16を指定ボイスで再生開始 (再生中なら先頭から再トリガ)
  // pcm はプール(PSRAM)上に常駐しているデータであること
  static bool play(uint8_t voice, const int16_t* pcm, uint32_t frames, uint32_t sample_rate,
                   bool loop = false, bool reverse = false, uint16_t volume_q8 = 256,
                   uint16_t pitch_q8 = 256, uint32_t start_frame = 0,
                   uint32_t edge_fade_in_end = 0,
                   uint32_t edge_fade_out_start = UINT32_MAX,
                   uint32_t probe_edge_usec = 0, uint8_t probe_kind = 0);
  // Melody/Chord用。保持音は軽量DAHDSR Envelopeを通し、必要なら
  // 検出済みの安定区間をsustain loopする。pitch_q16の高精度指定により、
  // 高いRoot NoteのPCMでもMIDI全域を折り返さず再生する。
  static bool playSynth(uint8_t voice, const int16_t* pcm, uint32_t frames, uint32_t sample_rate,
                        bool sustain_loop, bool reverse, uint16_t volume_q8,
                        uint32_t pitch_q16, uint16_t attack_ms = 5, uint16_t release_ms = 12,
                        uint32_t sustain_start = 0, uint32_t sustain_end = 0,
                        uint16_t sustain_crossfade = 0, uint16_t auto_release_ms = 0,
                        bool linear_interpolation = true, uint8_t render_divider = 1,
                        uint8_t sustain_cache_slot = 0xFF,
                        uint32_t probe_edge_usec = 0, uint8_t probe_kind = 0,
                        uint16_t delay_100us = 0, uint16_t hold_ms = 0,
                        uint16_t decay_ms = 0, uint16_t sustain_level_q15 = 32768);
  // Called when a Pad synth sound or its loop range changes. The I2S task
  // only reads this prepared internal-RAM cache; it never allocates/copies.
  static void primeSynthSustainCache(uint8_t cache_slot, const int16_t* pcm,
                                     uint32_t sustain_start, uint32_t sustain_end,
                                     uint32_t attack_cache_limit = 4096,
                                     uint32_t sustain_cache_limit = 8192);
  static void clearSynthSustainCache(uint8_t cache_slot = 0xFF);
  // Release retained internal-RAM buffers which no active voice references.
  // Radio/GATT setup uses this to obtain a contiguous temporary workspace;
  // callers may prime the required caches again after the connection settles.
  static size_t releaseUnusedSynthSustainCacheMemory(void);
  // True while a running voice still refers to this internal-RAM cache.
  // Callers use it to avoid replacing an audible cache entry.
  static bool isSynthSustainCacheInUse(uint8_t cache_slot);
  static void release(uint8_t voice, uint32_t probe_edge_usec = 0,
                      uint8_t probe_kind = 0);
  static void stop(uint8_t voice);
  static void stopAll(void);
  // Queue a position correction for the audio task. Unlike play(), this does
  // not reconfigure the voice from the UI task while it is being mixed.
  static void seek(uint8_t voice, uint32_t frame);
  static bool isPlaying(uint8_t voice);
  // Runtime mixer gain for an already playing voice. The audio task slews to
  // the target over a few milliseconds to prevent clicks.
  static void setVoiceVolumeQ8(uint8_t voice, uint16_t volume_q8);
  // Runtime pitch scale for an already playing voice. 4096 is neutral; this
  // lets the Melody lever bend Pad sounds without retriggering their attack.
  static void setVoicePitchScaleQ12(uint8_t voice, uint16_t scale_q12);
  // Per-voice tape speed. 256 is normal forward playback, zero holds the
  // current frame, and negative values run the PCM backwards.
  static void setVoicePlaybackRateQ8(uint8_t voice, int16_t rate_q8);
  // High precision variant used by a Music-fitted Audio Beat. 65536 is
  // normal speed; Q16 avoids accumulating the visible Q8 rounding error over
  // a multi-minute track.
  static void setVoicePlaybackRateQ16(uint8_t voice, int32_t rate_q16);
  // A lightweight per-voice low-pass/resonance pair for Touch Play. Unlike
  // the global FX Filter, this never changes the Audio Beat or other Pad voices.
  static void setVoiceToneFilter(uint8_t voice, uint8_t cutoff, uint8_t resonance);
  // Live pad input temporarily takes precedence over already-sustaining Pad
  // synth voices. Their loop bodies use a lighter renderer while this is on;
  // attacks, envelopes and timing remain at the output rate.
  static void setPerformancePriority(bool active);
  // I2S mixer processing time as a fraction of its 1ms audio block. This is
  // a cheap moving estimate for optional visual/FX work, not an audio clock.
  static uint8_t processingLoadQ8(void);
  static uint8_t processingPeakQ8(void);
  // Enable the raw output-wave ring only for the Sampler Play live scope.
  // Loop/Piano-roll and edit pages do not consume it.
  static void setLiveWaveCapture(bool enabled);
  // UI用の軽量な再生位置。frameはplay()へ渡したPCM範囲の先頭からのフレーム数。
  static bool getPlaybackPosition(uint8_t voice, uint32_t* frame, uint32_t* frames);
  static void setOutputMuted(bool muted);
  // Return the pre-recording external input directly to headphones. The I2S
  // task suppresses this path immediately if the headphone plug is removed.
  static void setExternalInputMonitor(bool enabled);
  // Keep the physical output silent while the codec/I2S path and saved kit
  // are brought up, then raise it slowly from silence.  This is intentionally
  // separate from the short fades used by recording.
  static void releaseStartupMute(void);
  // ES8388のアナログ出力は上限を超えず、後段のリミッターで保護する。
  static void setOutputGainPercent(uint8_t percent);

  // Stereo SD music stream. A producer task fills the PSRAM ring while the
  // I2S task consumes it without ever touching the filesystem or decoder.
  static bool attachDeckStream(int16_t* interleaved_stereo, uint32_t capacity_frames);
  static void detachDeckStream(void);
  static void setDeckStreamPlaying(bool playing);
  // Runtime gain for the stereo Music stream. The I2S task slews to this
  // target over a few milliseconds so Mixer mute/fades never click.
  static void setDeckStreamVolumeQ8(uint16_t volume_q8);
  static uint32_t deckStreamWritableFrames(void);
  static uint32_t deckStreamBufferedFrames(void);
  // Frames actually consumed by I2S since the current Deck ring was attached.
  // Decoder look-ahead and SD recovery do not affect this audible clock.
  static uint32_t deckStreamPlayedFrames(void);
  static uint32_t enqueueDeckStream(const int16_t* interleaved_stereo, uint32_t frames);
  static uint32_t deckStreamUnderruns(void);

  // Lightweight bus FX. index: 0=Tempo, 1=Filter, 2=Gater, 3=Crusher.
  // Repeat/Delay/Tape Stop use their dedicated final-mix paths below.
  static void setFx(uint8_t index, bool active, int8_t param);
  static void setFxActive(uint8_t index, bool active);
  static void setFxParam(uint8_t index, int8_t param);
  // Speed FX is smoothed by sampler_app.  Supplying its instantaneous ratio
  // directly keeps all active voices in lockstep with the loop transport.
  static void setFxSpeedRatioQ8(uint16_t ratio_q8);
  static void setFxQuantizeStepMs(uint32_t step_ms);
  // Route only the selected Beat / Parts / Music buses through the shared
  // FX/deck processor. Unselected buses bypass FX and rejoin before the final
  // output limiter, so this does not duplicate Deck Buffer memory.
  static void setFxTargetMask(uint8_t mask);
  static uint8_t fxTargetMask(void);
  // PCM voice ownership is stable (Beat audio/pattern vs all musical Parts),
  // but it is assigned explicitly to keep the audio engine independent from
  // sampler_app's voice-number layout.
  static void setVoiceFxTarget(uint8_t voice, uint8_t target);

  // Final-mix tape stop. This runs after the normal mixer/limiter, so Audio Beat,
  // Pad voices and SAM2695 all slow and stop together without altering the
  // loop transport. The final WAV stream captures this processed result.
  static void setTapeStop(bool active);
  // The UI supplies a musical duration before pressing Tape Stop. It is
  // latched by the audio task at the press edge, so changing a grid cannot
  // alter a stop already in progress.
  static void setTapeStopDurationMs(uint32_t duration_ms);
  static bool tapeStopAvailable(void);
  // Tape Stop、Master Scratch、Master Repeat、Grid Delayが共有する最終ミックス履歴。FX画面中だけ
  // 書き込み、通常演奏中のPSRAM帯域を消費しない。
  static void setDeckBufferEnabled(bool enabled);
  static void setMasterScratch(bool active);
  static void setMasterScratchRateQ8(int16_t rate_q8);
  static bool masterScratchAvailable(void);
  // Beat Repeat captures the dry final mix from a musical grid boundary,
  // then loops that PCM while the underlying transport keeps advancing.
  // capture_frames may be larger than repeat_frames so changing 4 -> 2 -> 4
  // keeps the original start point without recapturing the performance.
  static void setMasterRepeatFrames(uint32_t repeat_frames, uint32_t capture_frames);
  static void setMasterRepeat(bool active);
  static bool masterRepeatAvailable(void);
  // Grid Delay shares the final-mix Deck Buffer. It feeds one stereo tap
  // back through the limiter, then lets the tail decay after button release.
  static void setMasterDelayFrames(uint32_t delay_frames);
  static void setMasterDelay(bool active);
  static bool masterDelayAvailable(void);

  // I2S入力(マイク/ライン)をPCM16 monoで呼び出し元バッファへ録音する。
  // buffer は stopRecording() まで有効であること。
  static bool startRecording(int16_t* buffer, uint32_t capacity_frames, uint32_t initial_frames = 0);
  static uint32_t stopRecording(void);
  static uint32_t recordingFrames(void);
  static bool isRecording(void);
  static bool recordingOverflowed(void);

  // CoreS3's internal microphone and the KANTAN base audio input share
  // GPIO14. M5.Mic routes it to I2S1, so return it to the base I2S0 RX path
  // after microphone capture ends.
  static void restoreInputRoute(void);

  // Capture the final mixed mono output for a one-shot WAV export. The audio
  // task owns the write cursor so the UI task can start/stop this safely.
  static bool startOutputCapture(int16_t* buffer, uint32_t capacity_frames);
  static uint32_t stopOutputCapture(void);
  static uint32_t outputCaptureFrames(void);

  // Performance capture keeps the final limiter-protected mix in a lock-free
  // ring. The SD writer drains it from a low-priority task, so I2S never waits
  // for filesystem access.
  static bool startOutputStreamCapture(int16_t* ring, uint32_t capacity_frames);
  static void stopOutputStreamCapture(void);
  static uint32_t readOutputStreamCapture(int16_t* dst, uint32_t max_frames);
  static bool outputStreamCaptureActive(void);
  static bool outputStreamCaptureOverflowed(void);

private:
  static void task_func(sampler_audio_t* me);
};

//-------------------------------------------------------------------------
} // namespace sampler_ns

#endif
