# PCM polyphony response validation

## Scope

Optimize the existing SAM2695 + PCM release engine under Beat + Bass +
Melody + Chord. The reported symptom follows polyphony, not identical tone
selection. Preserve Play/Rec soft snap, quantized positions, sustain loops,
envelope timing, existing render sample rates, BLE MIDI and master FX.

No device flash, OTA publication, version change or commit is part of this
change. On-device latency and listening tests remain pending.

## Findings and implementation

The high-priority Core 1 I2S task formerly visited every active voice on every
output frame. Each visit handled generic scratch/seek/filter branches and
64-bit fixed-point cursor arithmetic, even for a plain sustained chord.
The new ordinary-forward renderer visits one voice for a 48-frame block,
keeps its state local, and uses a 32-bit integer cursor plus fractional carry.
The stereo Parts/Beat buses remain wide until the existing limiter. The
scratch, reverse, seek, filter and chopped-edge paths use the general renderer.

The working buffer adds 768 bytes of static internal RAM, with no new task,
allocation in audio processing, PCM copy, or additional output queue. The
existing four DMA descriptors and 1ms output block are unchanged. Note On is
published after all envelope and loop parameters are initialized, avoiding
I2S observing a half-initialized synth voice.

The old difference-first interpolation multiply could overflow int32 on a
steep PCM edge. Both renderers now use a bounded weighted sum. This is a
correctness fix; the comparison oracle uses a widened multiply for that one
old expression so overflow artifacts are not treated as intended audio.

Rec Note Off storage formerly normalized every stored Off by searching for
its On inside an interrupt-disabled critical section, then invalidated the
whole playback index. The new path normalizes only the new Off and appends
its final position to the snapshot. Global normalization remains for actual
global timeline changes. Playback masks now use native 32-bit words with
the same 2KiB total storage and the same Off/Bend/On ordering.

## Memory policy

As in the AMY work, keep bulk source PCM in PSRAM and use internal RAM only
for bounded optional working data. Allocate the actual attack/loop window
length, rather than the maximum 24KiB for every cache slot. The six-slot
cache budget is 48KiB total. Admission checks require at least 48KiB free
internal RAM after the requested allocation, plus a largest-block margin
of 16KiB. These are allocation-time checks, not a reservation against
allocations by other tasks. Failure simply retains direct source reads.

Prepare Chord's shared source cache before the single-note parts. Existing
BLE connection and microphone guards can still release unused entries;
active entries cannot be replaced or freed. Identical builtin PCM remains
reference-counted, but importing it into another part restores authored
metadata instead of inheriting the other part's Trim/Gain/Envelope edits.

## Host validation

Run from the repository root:

```sh
python3 tools/test_sampler_pcm_render.py
python3 tools/test_sampler_pcm_render.py --sanitize
python3 tools/test_sampler_rec_store.py
python3 tools/test_sampler_audio_memory.py
python3 tools/test_sampler_shared_synth.py
python3 tools/test_ktkit_format.py
```

These compile isolated harnesses from the actual production functions and
the baseline at `2418bc88`. They do not use a second reimplementation of the
optimized audio or storage algorithms.

- PCM: 720,000 output frames, 1–30 voices, separate source arrays, cache
  present/absent, On/Off, retrigger, pitch, envelope, loop wrap/crossfade,
  reverse, seek, chopped fades and filters; both output buses match.
- Rec: quantize enabled/disabled, all parts, collapsed gates, loop wrap,
  orphan Off and capacity 512; stored event timing matches the old path.
- Cache: exact allocation sizes, total budget, scarce/fragmented heap,
  zero-length attack and protection of active entries.
- Asset: real Alto Sax file, independent metadata, replacing with the same
  asset, failed CRC and release of the final reference.

AddressSanitizer also passed the PCM comparison. Final CoreS3 builds all
succeeded (the existing `esp-idf-size --ng` warning remains non-fatal):

| Environment | Static internal RAM | Application flash |
|---|---:|---:|
| `sampler_s3` | 152,080 bytes / 46.4% | 6,063,882 bytes / 92.5% |
| `sampler_s3_latency_probe` | 154,408 bytes / 47.1% | 6,065,694 bytes / 92.6% |
| `sampler_s3_latency_reference` | 154,408 bytes / 47.1% | 6,064,674 bytes / 92.5% |

Release static RAM increased by 528 bytes compared with the preceding local
implementation (151,552 bytes). Dynamic cache admission is governed by the
separate runtime policy above; static-RAM percentages do not measure BLE's
runtime heap. Generated distribution binaries were restored after building.

Observed host timing examples (Apple Silicon, optimized C++17): ordinary
1/4/7/12-voice rendering used about 56–65% of the previous CPU time; storing
an On/Off pair into a 512-event timeline dropped from about 33µs to 0.28µs.
These are algorithm comparisons on a PC, not CoreS3 wall-clock latency,
PSRAM bandwidth measurements, or measurements of perceived responsiveness.

## On-device A/B procedure

For an immediately usable Japanese checklist and result sheet, see
[PCM多重発音レスポンス 実機A/B比較手順](pcm-response-validation-ja.md).

Build `sampler_s3_latency_probe` for the new renderer and
`sampler_s3_latency_reference` for the original general mixer with the same
cache, event scheduler, instrumentation and `-O2` flags. Thus this A/B isolates
the renderer, not the entire difference from the release baseline.

Restart for each scenario because counters are cumulative. Use the same
saved project, routing, volume and playing sequence for both binaries:

1. Chord alone in Play, then in Rec, with short presses and releases.
2. Beat + Bass; add Melody; then add Chord, using different KANTAN tones.
3. Repeat at 7+ voices, near the Rec event limit, rapid chord changes and
   short loops; check that live notes and pad/page updates remain responsive.
4. Repeat with BLE MIDI connected, then reconnect BLE and start Mic recording.
5. Exercise reverse, Touch filtering, Beat/Chop, Music and master FX to cover
   the retained general renderer and wide-bus mixing.

Stop transport and release notes to obtain `PERF` lines. Input timing bins
are 1ms wide; CPU duration bins are 100µs wide. Overflow bins print a lower
bound, never a false upper bound. The I2S histogram includes only blocks
with active PCM voices and also reports 1–2/3–4/5–6/7+ groups, so idle periods
cannot hide polyphonic overload. Rec batch timing includes nonempty batches.

Check I2S p95 < 1ms, zero deadline misses and transfer errors, bounded On/Off
arrival, no stuck notes/clicks, and smooth screen response. Deadline/transfer
counters are diagnostics, not a direct hardware-underrun measurement.
Input-to-mixer stamps exclude DAC and DMA delay; soft-snap waiting and authored
Attack/Release must be distinguished from actual scheduling delay when
checking the intended physical-to-audible p95 target of 20ms.

Captured hardware measurements are recorded in
[PCM response on-device A/B results](pcm-response-device-results.md).
