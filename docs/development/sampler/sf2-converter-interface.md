# SoundFont Converter Interface

KANTAN PlayはSoundFont全体を本体で解釈しない。SoundFont 2を制作用入力とし、PC側Converterが最大2つのRegionを1つの **KANTAN Synth tone** (`.ktsynth`) に変換する。

KANTAN Synthは未公開段階でKTS2へ統合したため、KTS1との互換分岐は設けない。単層音色も`layerCount = 1`のKTS2として扱う。

## RegionとLayer

- Preset、基準MIDI Note、代表Velocityを入力する
- Preset / InstrumentのGlobal ZoneとLocal Zoneを合成する
- Key RangeとVelocity Rangeが入力値を含むRegionを候補にする
- 1つまたは2つのRegionをLayer 0 / 1として選択できる
- 各LayerはPCM、基準音、Tune、Gain、Loop、Attack、Releaseを独立して持つ
- Modulator、LFO、Filter、Chorus、Reverb、3層以上は対象外とする
- 2層の`defaultGainQ8`合計は512以下にする

## ファイル構造

```text
RIFF / WAVE
|- fmt   Layer 0 PCM format
|- smpl  Layer 0 standard loop mirror (optional)
|- KNTN  KTS2 metadata (required)
|- data  Layer 0, 16-bit mono PCM
`- KT2D  Layer 1, 16-bit mono PCM (layerCount=2のみ)
```

- PCMはLinear PCM、16-bit、mono、little endian
- 各LayerのSample Rateは8–48kHz（推奨32kHz）
- 各Layerは最大20秒
- ヘッダーを含むファイル全体は最大2MiB
- 座標は各LayerのPCM frame単位、Endはexclusive
- RIFF payloadが奇数byteならpaddingを1byte加える

Layer 0は通常のWAVプレイヤーでも試聴できる。`fmt `はLayer 0だけを表し、Layer 1のSample Rateとframe数はKNTN descriptorを正とする。

## KNTN v2.0

`KNTN`は128-byte固定headerと最大63-byteのUTF-8 nameで構成する。数値はlittle endian。

| Offset | Type | Field |
|---:|---|---|
| 0 | char[4] | `KTS2` |
| 4 | u16 | versionMajor = 2 |
| 6 | u16 | versionMinor = 0 |
| 8 | u16 | headerBytes = 128 |
| 10 | u16 | nameBytes |
| 12 | u32 | flags = 0 |
| 16 | u32 | contentCrc32 |
| 20 | u8 | layerCount = 1..2 |
| 21 | u8[3] | reserved = 0 |
| 24 | byte[48] | Layer 0 descriptor |
| 72 | byte[48] | Layer 1 descriptor（未使用時0） |
| 120 | u8[8] | reserved = 0 |
| 128 | byte[] | UTF-8 name |

各48-byte Layer descriptor:

| 相対Offset | Type | Field |
|---:|---|---|
| 0 | u32 | sampleRate |
| 4 | u32 | frameCount |
| 8 | u32 | startFrame |
| 12 | u32 | endFrameExclusive |
| 16 | u32 | loopStartFrame |
| 20 | u32 | loopEndFrameExclusive |
| 24 | u32 | loopCrossfadeFrames |
| 28 | u16 | attackMs |
| 30 | u16 | releaseMs |
| 32 | i16 | tuneCents |
| 34 | u16 | defaultGainQ8（256 = 100%） |
| 36 | u8 | rootNote |
| 37 | u8 | sustainMode（0=Off、1=Loop） |
| 38 | u8 | pcmChunk（Layer 0=0/data、Layer 1=1/KT2D） |
| 39 | u8[9] | reserved = 0 |

## CRCと検証

CRCはCRC-32/ISO-HDLC。`KNTN` payloadの16–19 byteを0として全payloadを処理し、Layer順に`data`、`KT2D` payloadを続ける。RIFF header、chunk header、paddingは含めない。

本体は読み込み前に次を検証する。

- `KTS2`、versionMajor=2、headerBytes>=128、layerCount 1..2
- Layer 0 descriptorが`fmt ` / `data`と一致する
- Layer 1がある場合だけ`KT2D`が1つ存在する
- `frameCount == pcmBytes / 2`、`0 <= start < end <= frameCount`
- Loop時は`start <= loopStart < loopEnd <= end`
- Crossfadeは65535以下かつLoop長の1/4以下
- rootNote 0..127、tuneCents -100..100
- attack 0..5000ms、release 10..2000ms、各gain 0..512、gain合計0..512
- CRC一致、ファイル全体2MiB以下

未知のmajor versionと壊れた`.ktsynth`は通常WAVへフォールバックせず拒否する。

## 本体のメモリ・再生方針

- PCM本体は選択時にPSRAMへ展開し、演奏中はSDを読まない
- Layer 1は3パートだけが持つ小型descriptorを使い、Pad用の名前・パス・波形表示cacheを重複させない
- 8つの論理ピッチボイスは維持し、Layer 1は専用物理バンクで同じNote On/Off、Pitch Bend、Filterに追従する
- Layer 1の同時発音は最大6。内部パートをBLE MIDIより優先し、上限時もLayer 0は必ず鳴る
- Attack/Loopの内部RAM cacheは9 descriptorへ拡張するが、実データ総量48KiB、空き48KiB、連続空き16KiBのガードは維持する
- cacheを確保できない場合はPSRAMから直接再生し、BLE/Mic開始時は未使用cacheを解放する
- Project保存では2層KTS2をそのまま複製し、再読込後もLayer 1を失わない

## Converter要件

- KTS2 `.ktsynth`を唯一の標準出力にする
- WAV + JSONは任意のデバッグ出力としてよい
- 出力後に再度開き、全descriptor、CRC、Loop、合成Gain、試聴を検証する
- Built-in化では同じKTS2を直接バイナリ埋め込みする
- Web File Editorは`/sampler/samples/Synth`への保存を推奨し、MIMEを`application/vnd.instachord.ktsynth`とする

旧い開発用KTS1資産は`tools/migrate_ktsynth_v2.py --in-place <files>`で一度だけ移行する。このツールは公開互換機能ではなく、リポジトリ資産の移行専用である。

Converter開発前の確認用には`tools/build_ktsynth.py manifest.json output.ktsynth`を使える。manifest例:

```json
{
  "name": "Piano + Bell",
  "layers": [
    { "wav": "piano.wav", "rootNote": 60, "defaultGainQ8": 224,
      "sustainMode": "loop", "loopStartFrame": 12000, "loopEndFrame": 28000 },
    { "wav": "bell.wav", "rootNote": 72, "defaultGainQ8": 128,
      "tuneCents": 4, "attackMs": 3, "releaseMs": 240 }
  ]
}
```
