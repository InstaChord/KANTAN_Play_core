# 主な仕様

## 本体と操作

- 対応ハードウェア：M5Stack CoreS3 SE / ESP32-S3
- 演奏パッド：12個
- Fnボタン：3個
- モード：SOUND / PLAY / REC / FX
- 内蔵マイク、タッチパネル、加速度センサー
- スピーカー、ヘッドホン出力、外部音声入力
- microSD、USB、BLE MIDI、Grove端子

## 音声

- 再生出力：48kHz
- マイク録音：32kHz。処理後に48kHz再生用データへ変換
- サンプル：最大20秒
- オーディオビート：最大8秒
- 12個のパッドで共有するPCMメモリ：約5MB
- 内部音声：mono / PCM16
- 出力直前にピークリミッターを適用

実際に読み込める合計時間は、サンプルの長さや変換後サイズによって変わります。

## 対応ファイル

| 用途 | 形式 |
|---|---|
| サンプル | WAV、MP3 |
| オーディオビート | WAV、MP3 |
| パターンビート | MID、MIDI |
| Music Player | WAV、MP3 |
| サンプルキット / Project | JSONと対応する音声フォルダー |
| Performance Recording | WAV |

WAV / MP3はmono / stereoを受け付けます。本体内部では用途に合わせて変換されます。

## 音楽設定

- Key：12キー
- Scale：Pentatonic、Major、Chromatic、Blues、Japanese、Minor、Pentatonic Min、Dorian、Mixolydian
- Tuning：A=425〜455Hz、1Hz単位
- Octave：Bass / Melody / Chordで-2〜+2
- Pitch Bend：Bass / Melodyで1 Semitoneまたは1 Octave
- ビート Repeat：1 / 2 / 4
- Note Grid：8 / 16 / 32 / 64 / 128
- Swing：0 / 25 / 50 / 75 / 100%

## 主な保存先

| 内容 | SDカード上の場所 |
|---|---|
| サンプル | `/sampler/samples/` |
| ビート | `/sampler/loops/` |
| サンプルキット | `/sampler/kits/` |
| Project | `/sampler/projects/` |
| Music | `/sampler/music/` |
| Performance Recording | `/sampler/recordings/` |

通常はフォルダーを直接操作せず、File Editorまたは本体メニューを使います。
