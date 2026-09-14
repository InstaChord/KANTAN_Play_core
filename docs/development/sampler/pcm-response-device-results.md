# PCM response on-device A/B results

## Reference renderer

Final cumulative snapshot captured on 2026-09-14 from
`sampler_s3_latency_reference` on `/dev/cu.usbmodem111401`.

| Metric | Count | Average | p95 | Maximum |
|---|---:|---:|---:|---:|
| `input-on-queue` | 95 | 56,757us | >=32,000us | 1,370,000us |
| `input-off-queue` | 95 | 83,305us | >=32,000us | 1,297,000us |
| `live-on-audio` | 41 | 162,115us | >=32,000us | 1,650,211us |
| `live-off-audio` | 93 | 105,588us | >=32,000us | 1,313,147us |
| `rec-on-audio` | 380 | 1,121us | <4,000us | 45,088us |
| `rec-off-audio` | 315 | 302us | <1,000us | 10,023us |
| `rec-store` | 152 | 9,533us | >=3,200us | 197,947us |
| `rec-batch` | 940 | 2,852us | >=3,200us | 217,934us |
| `voice-alloc` | 712 | 269us | <100us | 45,880us |
| `i2s-block` | 113,889 | 630us | <900us | 1,166us |
| `i2s-1-2-voices` | 10,340 | 391us | <500us | 644us |
| `i2s-3-4-voices` | 34,908 | 508us | <600us | 815us |
| `i2s-5-6-voices` | 31,299 | 637us | <800us | 999us |
| `i2s-7plus-voices` | 37,342 | 806us | <1,000us | 1,166us |

- `deadline-miss=779`
- `read-error=0`
- `write-error=0`

The counters are cumulative across the user's complete Reference performance
session. The very large input/live and Rec processing maxima are retained as
observations; the Probe run is needed before assigning them specifically to
the renderer rather than UI scheduling, intentional snap, or another shared
path.

## Optimized renderer

Final cumulative snapshot captured on 2026-09-14 from
`sampler_s3_latency_probe` on `/dev/cu.usbmodem111401`.

The user noted that the performance content was not identical to the Reference
run. The values therefore establish a successful stressed Probe run and show
the scaling by active-voice group, but they are not a controlled timing ratio.

| Metric | Count | Average | p95 | Maximum |
|---|---:|---:|---:|---:|
| `input-on-queue` | 107 | 2,457us | <7,000us | 13,000us |
| `input-off-queue` | 107 | 3,327us | <13,000us | 19,000us |
| `live-on-audio` | 42 | 5,087us | <13,000us | 18,292us |
| `live-off-audio` | 106 | 4,439us | <14,000us | 20,290us |
| `rec-on-audio` | 398 | 432us | <1,000us | 1,626us |
| `rec-off-audio` | 325 | 459us | <1,000us | 1,386us |
| `rec-store` | 192 | 1,028us | >=3,200us | 11,870us |
| `rec-batch` | 833 | 246us | <800us | 1,076us |
| `voice-alloc` | 791 | 15us | <100us | 3,646us |
| `i2s-block` | 88,410 | 402us | <600us | 773us |
| `i2s-1-2-voices` | 9,637 | 297us | <400us | 509us |
| `i2s-3-4-voices` | 25,029 | 352us | <500us | 542us |
| `i2s-5-6-voices` | 26,969 | 403us | <500us | 635us |
| `i2s-7plus-voices` | 26,775 | 486us | <600us | 773us |

- `deadline-miss=0`
- `read-error=0`
- `write-error=0`

## Indicative comparison

Although the performances differed, both sessions contain tens of thousands
of blocks in each important polyphony group. Average I2S block duration was
lower in the Probe run in every group:

| Active PCM voices | Reference average | Probe average | Indicative change |
|---|---:|---:|---:|
| 1–2 | 391us | 297us | -24% |
| 3–4 | 508us | 352us | -31% |
| 5–6 | 637us | 403us | -37% |
| 7+ | 806us | 486us | -40% |
| All active blocks | 630us | 402us | -36% |

The Reference session recorded 779 blocks at or above the 1ms deadline and a
1,166us maximum. The Probe session recorded no deadline misses and a 773us
maximum. This strongly supports the renderer optimization under the two
sessions tested, but an identical saved performance should still be used if a
strict A/B percentage is required.
