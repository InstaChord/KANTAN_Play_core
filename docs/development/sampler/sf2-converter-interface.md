# SoundFont Converter Interface

KANTAN PlayはSoundFont全体を本体で解釈しない。SoundFont 2を制作用の入力形式とし、PC側のConverterが指定した1つの発音Regionを小容量の **KANTAN Synth tone** (`.ktsynth`) へ変換する。

## UI上の分類

シンセパートのSound Sourceは次の構成とする。

1. `General MIDI`
2. `Sample`
   - `Pad`
   - `File`
3. `KANTAN Synth`

`KANTAN Synth` はPCMという内部方式をユーザーに意識させず、基準音、Loop、Envelopeまで調整済みの楽器音色を示す名称とする。通常のPad素材やWAV/MP3と区別するため、専用拡張子を `.ktsynth` とする。

## Region選択

- Preset、基準MIDI Note、代表Velocityを入力する
- Preset / InstrumentのGlobal ZoneとLocal Zoneを合成する
- Key RangeとVelocity Rangeが入力値を含むSample Regionを候補にする
- 複数候補は自動で隠さず、Sample名とRangeを表示して試聴可能にする
- Modulator、LFO、Filter、Chorus、Reverb、複数Layerの同時合成は対象外とする

## ファイル構造

`.ktsynth` はRIFF/WAVE互換の単一ファイルである。音声と設定を分離せず、次のchunkを持つ。

```text
RIFF / WAVE
|- fmt   PCM format
|- smpl  Standard loop mirror (optional)
|- KNTN  KANTAN Synth metadata (required)
`- data  16-bit mono PCM
```

音声仕様:

- Linear PCM、16-bit、mono、little endian
- Sample Rate: 8–48kHz。Converter標準は18 / 24 / 32 / 48kHz、推奨32kHz
- 最大20秒、最大2MiB
- 座標はPCM frame単位、EndとLoop Endはexclusive

## KNTN v1.0

`KNTN` chunkは64-byte固定headerと、最大63-byteのUTF-8 nameで構成する。数値はlittle endian。

| Offset | Type | Field |
|---:|---|---|
| 0 | char[4] | `KTS1` |
| 4 | u16 | versionMajor = 1 |
| 6 | u16 | versionMinor = 0 |
| 8 | u16 | headerBytes = 64 |
| 10 | u16 | nameBytes (0..63、NULなし) |
| 12 | u32 | flags = 0 |
| 16 | u32 | sampleRate |
| 20 | u32 | frameCount |
| 24 | u32 | startFrame |
| 28 | u32 | endFrameExclusive |
| 32 | u32 | loopStartFrame |
| 36 | u32 | loopEndFrameExclusive |
| 40 | u32 | loopCrossfadeFrames |
| 44 | u32 | contentCrc32 |
| 48 | u16 | attackMs |
| 50 | u16 | releaseMs |
| 52 | i16 | tuneCents |
| 54 | u16 | defaultGainQ8 (256 = 100%) |
| 56 | u8 | rootNote |
| 57 | u8 | sustainMode (0 = Off, 1 = Loop) |
| 58 | u8[6] | reserved = 0 |
| 64 | byte[] | UTF-8 name |

RIFF規則に従い、chunk payloadが奇数byteならpaddingを1byte加える。

## CRCと検証

CRCはCRC-32/ISO-HDLCを使う。`KNTN` payloadの44-47 byteを0として全payloadを処理し、続けて`data` payloadを処理する。RIFF paddingは含めない。

本体は読み込み前に次を検証する。

- `KTS1`、versionMajor=1、headerBytes>=64
- `sampleRate`が`fmt `と一致し、`frameCount == dataBytes / 2`
- `0 <= start < end <= frameCount`
- Loop時は`start <= loopStart < loopEnd <= end`
- Crossfadeは65535以下かつLoop長の1/4以下
- rootNote 0..127、tuneCents -100..100
- attack 0..5000ms、release 10..2000ms、gain 0..512
- CRC一致

壊れた`.ktsynth`を通常WAVとして黙って読み込まず、エラーとして拒否する。未知のmajor versionも拒否する。同一majorの将来minor versionは、`headerBytes`以降の追加fieldを無視できる設計とする。

`smpl` chunkは他ソフトとの相互運用用であり、値が異なる場合は`KNTN`を正とする。`smpl`のLoop Endはinclusiveなので、`loopEndFrameExclusive - 1`を保存する。

## 本体側の対応

| `.ktsynth` | `sample_slot_t` |
|---|---|
| `rootNote` | `base_note` |
| `tuneCents` | `synth_tune_cents` |
| `loopStartFrame` | `synth_loop_start` |
| `loopEndFrameExclusive` | `synth_loop_end` |
| `loopCrossfadeFrames` | `synth_loop_crossfade` |
| `sustainMode` | `synth_sustain_mode` |
| `attackMs` | `synth_attack_ms` |
| `releaseMs` | `synth_release_ms` |
| `defaultGainQ8` | `volume_q8` |

本体は選択時に検証し、演奏用PCMをPSRAMへ展開する。演奏中はSDカードを読まない。Converterで確定した音量、基準音、Loop、Envelopeを守るため、自動正規化、基準音推定、サステイン推定を適用しない。

SoundFontの波形先頭からLoop Inまでは一度だけ再生する。ノート保持中はLoop In / Outを繰り返し、Note Off後は現在のLoop波形にRelease Envelopeを適用する。SoundFontのDecay、Sustain Attenuation、Release Tailはv1の対象外とする。

## Converter要件

- `.ktsynth`を標準出力にする
- WAV + JSONは任意のデバッグ出力として残せる
- 出力後にファイルを再度開き、metadata、CRC、Loop、試聴を検証する
- Built-in化では同じ`.ktsynth`を直接バイナリ埋め込みできる
- Web File Editorは`/sampler/samples/Synth`への保存を推奨し、MIMEを`application/vnd.instachord.ktsynth`とする
