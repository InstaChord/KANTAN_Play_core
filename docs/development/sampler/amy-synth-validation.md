# AMY Synth Feasibility Validation

## Purpose

This experiment evaluates AMY as a future replacement for the discontinued
SAM2695 GM chip. AMY is initially limited to the Melody, Bass, and Chord parts.
Beat, Sampler, recording, Music playback, and the existing master FX remain in
the KANTAN Sampler audio engine.

The release firmware is not changed by this experiment. AMY is linked only by
the dedicated `sampler_s3_amy_probe` and `sampler_s3_amy_integration`
PlatformIO environments.

## Release decision

The public `sampler_s3` and `sampler_s3_debug` environments explicitly use the
SAM2695 + PCM backend. AMY remains available only in the probe and integration
environments until its mixed-workload latency meets the product's performance
target. `KANPLAY_RELEASE_SYNTH_SAM_PCM` and `KANPLAY_AMY_INTEGRATION` are
compile-time mutually exclusive so an AMY experiment cannot accidentally be
published as the standard firmware.

Memory-lifecycle improvements discovered during AMY validation remain in the
shared sampler code where they also benefit the release build: bounded MIDI
queues, deferred BLE connection, radio hand-off, recording-buffer release,
retained UI cache management, and allocation-free realtime audio paths. AMY's
renderer, event state, ring buffer, and dependency tables are not linked into
the public build.

## Pinned dependency

- AMY Synthesizer `1.2.108`
- Upstream commit `223723765c84e6f0148c0a5f6a01674d9e828dd4`
- Repository: <https://github.com/shorepine/amy>

The exact commit is used so performance and memory results remain comparable.

AMY also contains `src/usb.h`, which collides with Arduino-ESP32's `USB.h` on
the default case-insensitive macOS filesystem. `amy_probe_build.py` gives the
Arduino core header priority for project-wide angle-bracket includes; AMY's
own quoted include continues to resolve beside its source file. A production
fork should rename the AMY header instead of retaining this build workaround.

## Current constraints

| Item | KANTAN Sampler | AMY 1.2.108 |
|---|---:|---:|
| Sample rate | 48,000 Hz | 44,100 Hz |
| Audio block | 48 stereo frames / 1 ms | 256 stereo frames / 5.80 ms |
| Main audio task | Core 1 | Configurable |
| Output ownership | Existing ES8388 I2S path | Disabled for probe |

AMY must not initialize I2S. Its default ESP multicore/multithread tasks are
also disabled because they would compete with the existing Core 1 I2S task.

For a production integration, AMY should render at 48 kHz through a maintained
sample-rate configuration patch. A permanent 44.1-to-48 kHz resampler would
add latency and continuous CPU cost, so it is acceptable only for an audible
prototype.

## Baseline before AMY

Measured from `sampler_s3` at commit `3ca956f`:

- Internal static RAM: 149,536 / 327,680 bytes (45.6%)
- Application flash: 5,959,834 / 6,553,600 bytes (90.9%)
- Remaining application partition: approximately 594 KB
- Existing audio processing telemetry: average and peak fraction of the 1 ms
  I2S deadline via `sampler_audio_t::processingLoadQ8()` and
  `processingPeakQ8()`

Flash headroom is already narrow. Runtime feature switches reduce AMY RAM and
CPU use, but do not necessarily remove all built-in patch and PCM tables from
the linked image. The probe build measures the real linker result.

## Probe configuration

- Audio interface: none
- MIDI interface: host-owned stubs (KANTAN Play retains MIDI ownership)
- Multicore and multithread: off
- Reverb, echo, chorus, partials, custom oscillators, audio input: off
- Maximum oscillators: 40
- Maximum voices: 9 (Melody 4, Bass 1, Chord 4)
- Synth slots: 3
- Sequencer tags: 32
- Memory patches: 4
- Events, synth state, samples, SysEx, and delay allocations: PSRAM
- Render blocks and feedback buffers: internal RAM
- Benchmark: 16, 24, 32, and 40 alternating sine/saw oscillators, 128
  measured AMY blocks at each load after warm-up

At startup the probe prints:

```text
AMY_PROBE begin ...
AMY_PROBE heap ...
AMY_PROBE oscs=... avg=... p95=... max=... budget=...
AMY_PROBE stopped
AMY_PROBE heap ...
```

Build with:

```bash
pio run -e sampler_s3_amy_probe
```

The probe initializes, benchmarks, and stops AMY before the normal sampler
audio path starts. It produces no audio and cannot disturb I2S.

## Static build result

Measured against the unchanged `sampler_s3` release build at commit `3ca956f`:

| Build | Static RAM | Application flash | Partition remaining |
|---|---:|---:|---:|
| Release (`sampler_s3`) | 149,536 bytes (45.6%) | 5,959,834 bytes (90.9%) | 593,766 bytes |
| Probe (`sampler_s3_amy_probe`) | 163,480 bytes (49.9%) | 6,332,686 bytes (96.6%) | 220,914 bytes |
| Instrumented AMY probe delta | +13,944 bytes | +372,852 bytes | -372,852 bytes |

The upstream library fits in the current application partition, but the
remaining flash margin is too small for production use and future firmware
growth. Runtime feature switches do not remove several large linked tables:

- Built-in PCM table: approximately 102 KB
- Interpolated-piano harmonic tables: approximately 39 KB
- General built-in patch command strings: approximately 125 KB

Those three groups account for roughly 266 KB. A production fork should make
PCM, interpolated piano, and the general patch bank compile-time options, while
retaining only the oscillator types and a small KANTAN-specific preset bank.
This is expected to recover most of the flash increase without weakening the
planned dance-oriented synth.

The probe includes its USB telemetry code, so a production fork will be
slightly smaller. Static RAM passes the preliminary budget.

## CoreS3 runtime result

Measured on CoreS3 at 240 MHz with AMY 44.1 kHz / 256-frame blocks. One block
has a 5,804 us deadline.

| Active oscillators | Average | p95 | Maximum | p95 load |
|---:|---:|---:|---:|---:|
| 16 | 1,565 us | 1,569 us | 1,570 us | 27% |
| 24 | 2,303 us | 2,310 us | 2,313 us | 39% |
| 32 | 3,126 us | 3,148 us | 3,151 us | 54% |
| 40 | 4,038 us | 4,165 us | 4,177 us | 71% |

AMY startup consumed 40,140 bytes of free internal heap and 43,400 bytes of
PSRAM. After startup, 111,668 bytes of internal heap remained, with a largest
contiguous block of 49,140 bytes. After `amy_stop()`, the residual difference
from the pre-start snapshot was 1,804 internal bytes and 140 PSRAM bytes.

The first run exposed an upstream integration issue: ESP32 single-thread mode
polls the AMY MIDI UART from every `amy_update()` call even when MIDI is set to
`NONE`. KANTAN Play now builds the probe with `AMY_HOST_MIDI` and supplies
no-op device hooks because the existing firmware owns MIDI. This removed the
UART error loop and reduced the 16-oscillator render result from roughly 5 ms
to 1.57 ms.

The 40-oscillator result narrowly exceeds the 70% p95 target. The practical
starting point is therefore 24 oscillators, with 32 as a candidate upper limit
after measuring the combined Sampler/Beat/Music/FX workload. The standalone
AMY test does not yet prove mixed-engine stability.

## Acceptance targets

- No allocation from the existing 1 ms I2S callback
- No AMY-created task on Core 1
- 9 musical voices with no dropped audio in the later mixed prototype
- AMY render p95 below 70% of its block duration
- AMY render maximum below 85% of its block duration
- Zero audio underruns in a 30-minute worst-case session
- At least 25% internal-RAM headroom after final integration

## Integration plan

1. Fork AMY and compile out unused patch, PCM, and interpolated-piano content;
   the unmodified upstream build fits but leaves only about 222 KB.
2. Make AMY's sample rate explicitly configurable and validate 48 kHz output.
3. Add a Core 0 producer and a small stereo ring buffer. The existing Core 1
   I2S task only consumes ready samples and never calls AMY.
4. Route Melody, Bass, and Chord Note On/Off events to an allocation-free event
   queue while retaining SAM2695 as an A/B option.
5. Start at 24 oscillators, then compare 24 and 32 under the complete audio
   workload using existing processing-load telemetry.
6. Add three lightweight dance-oriented presets and run latency, polyphony,
   Music/Beat/FX, BLE MIDI, Wi-Fi, and 30-minute endurance tests.
7. Expand the preset set only after the timing and memory budgets pass.

## Audible integration prototype

Build `sampler_s3_amy_integration` to replace only the General MIDI backends
of Melody, Chord, and Bass with AMY. Pad-sourced synths and every other audio
path remain unchanged. AMY renders at 48 kHz on a priority-4 Core 0 producer;
the existing Core 1 I2S task consumes an allocation-free 512-frame stereo
ring and mixes it into the Parts bus.

The initial allocation is:

| Part | Polyphony | Osc/voice | Maximum Osc |
|---|---:|---:|---:|
| Melody | 4 | 2 | 8 |
| Bass | 1 | 2 | 2 |
| Chord | 4 | 1 | 4 |
| Reserved headroom | - | - | 10 |

The prototype uses a bright detuned lead, octave-sub bass, and restrained
triangle chord sound. Program Change is intentionally ignored; it cannot
replace these three test instruments with AMY's large built-in banks. Channel
volume, fine tuning, and per-part pitch-bend range are translated at the AMY
boundary. This also avoids upstream AMY 1.2.108's global pitch-bend behavior.

AMY 1.2.108 hard-codes 44.1 kHz on ESP32. `amy_probe_build.py` changes only
the integration environment's downloaded dependency copy to 48 kHz. A
production fork must replace this temporary patch with a supported compile-time
sample-rate option.

The first integration build uses 163,512 bytes of static RAM (49.9%) and
6,331,162 bytes of application flash (96.6%). It fits, but confirms that the
unused AMY patch/PCM tables must be compiled out before merging into release.

## Known architectural risk

`send_sam_midi()` is a useful event boundary, but it also carries menu sounds,
external MIDI through, program changes, tuning, and pitch bend. Production code
should route only the three pitched part channels through a synth-backend
interface instead of globally replacing this function.
