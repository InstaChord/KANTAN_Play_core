# KANTAN Sampler プリセット・Project構成指示書

## 1. 文書の目的

本文書は、ChatGPT WorkなどでKANTAN Samplerのプリセットを企画・整理する際に、音源ファイルとProjectの関係を正しく理解するための指示書です。

プリセットは、WAVファイルを個別に並べるだけでは完成しません。

次の要素を音楽的に組み合わせ、読み込んだ直後から演奏できる状態にしたものが`Project`です。

- Sample Kit（Samplerの12 Pad）
- Beat Kit（Pattern Beatを鳴らす12音）
- Beat Pattern（リズムの演奏データ）
- Bass音色
- Melody音色
- Chord音色
- 共通のKey
- 共通のScale
- 必要に応じたRecシーケンス、FX、Mixer設定

`preset-library-production-guide.md`、`product-spec.md`、`program-spec.md`と一緒に参照してください。Beat Patternを制作する場合は、`beat-pattern-production-guide.md`も参照してください。

## 2. 全体構造

```text
Project
├── Sample Kit
│   ├── Sampler Pad 1〜12の波形
│   └── Start / End / Volume / Pitch / Hold / Repeat / Reverse / Choke / Synth設定
├── Beat
│   ├── Beat Kit
│   │   └── Beat Pad 1〜12のドラム・パーカッション音源
│   └── Beat Pattern
│       └── Note On位置 / Loop長 / Tempo / Swing / Repeat
├── Synth Parts
│   ├── Bass
│   │   └── Sound Source / ToneまたはPad Sound / Base Note / Octave / Volume
│   ├── Melody
│   │   └── Sound Source / ToneまたはPad Sound / Base Note / Octave / Volume
│   └── Chord
│       └── Sound Source / ToneまたはPad Sound / Base Note / Octave / Volume
├── Harmony
│   ├── Key
│   ├── Scale
│   └── Fine Tuning
├── Rec
│   ├── Samplerシーケンス
│   ├── Beatシーケンス
│   ├── Bassシーケンス
│   ├── Melodyシーケンス
│   └── Chordシーケンス
├── FX
└── Mixer / Mix 1〜4
```

## 3. 用語と役割

### 3.1 Sample Kit

製品UIでの正式名称は`Sample Kit`です。「Sampler Kit」という説明も意味は同じですが、ファイル名や仕様書では`Sample Kit`に統一してください。

Sample Kitは、Samplerパートの12個のPadに割り当てる音と、それぞれの編集設定のセットです。

含まれる主な情報：

- Pad番号
- WAVデータ
- 表示名
- Start / End
- Volume / Pitch
- Base Note
- Hold / Repeat / Reverse / Choke
- Beat AnchorとChop情報
- Synth Attack / Loop In / Out / Crossfade / Release / Tune

Sample Kitを単体で読み込んだ場合、Beat、Rec、FX、Mixer、Key / Scaleは変更しません。

### 3.2 Beat Kit

Beat Kitは、Pattern Beatが発音する12個のドラム・パーカッション音源のセットです。

例：

- Kick
- Snare
- Side Stick / Rim
- Clap
- Tom Low / Mid / High
- HiHat Closed / Open
- Shaker / Tambourine
- Crash
- Ride

Beat Kitは音色であり、リズムの並び方ではありません。同じBeat Patternでも、Beat Kitを変えると別のジャンルや雰囲気にできます。

現行フォーマットでは、Beat Kitは独立したユーザー保存JSONではなく、Project内の`beat.pads`と対応Assetとして保存されます。

### 3.3 Beat Pattern

Beat Patternは、Beat KitのどのPadを、どのタイミングで鳴らすかを示すシーケンスです。

含まれる主な情報：

- Pad番号
- Note Onの位置
- Velocity（1〜127、未指定および本体手入力は110）
- Patternの長さ
- Tempo
- Swing
- Beat Repeat
- Note Grid

Built-in Patternはプログラム内に保持できます。SDカードからはMID / MIDI形式のPatternも読み込めます。Project保存時は、実際に使用中のPatternがVelocityを含むRecイベントとしてProjectに固定されます。

### 3.4 Bass / Melody / Chord音色

3つのSynthパートは、それぞれ独立した音色設定を持ちます。

各パートで必要な情報：

- Sound Source: General MIDI、Sample (Pad / File)、KANTAN Synth
- General MIDIの場合: Program Numberと音色名
- Sample > Padの場合: 参照するSampler Pad
- Sample > Fileの場合: パート専用のWAV/MP3とBase Note
- KANTAN Synthの場合: `.ktsynth`または調整済み内蔵音色
- Base Note
- Octave
- Volume
- Sampleベースの場合: Loop In / Out / Crossfade / Attack / Release / Tuning

`File`と`KANTAN Synth`はSampler Padとは別の専用スロットに保持します。Project保存時はPCMと音色パラメーターをProject assetsへ固定し、元のSDファイルに依存しない形で復元します。

プリセットProjectでは、Bass、Melody、Chordが互いに聞き分けられ、BeatやSamplerと同時に鳴らしても役割が重複しない組み合わせを選んでください。

### 3.5 Key / Scale / Fine Tuning

KeyとScaleは、Bass、Melody、Chordで共通の音楽設定です。

- Key: 12キー
- Scale: Pentatonic、Major、Chromatic、Blues、Minor Pentatonic、Hirajoshiなど
- Fine Tuning: A=440Hzを基準とした微調整

プリセット作成時は、シンセ音色だけでなく、そのProjectのBeatやChop素材に合うKey / Scaleを必ず選びます。

Key / Scaleは各音色に個別保存するものではなく、Project全体に1組だけ保存します。

## 4. Projectとは何か

Projectは、上記のパートや設定をまとめた「楽曲・セッション全体の保存単位」です。

プリセットProjectは、次の状態を目標に作ります。

- 読み込んだ直後からBeatを再生できる
- Sampler Padを押すとすぐ楽しい
- Bass、Melody、Chordが同じKey / Scaleで演奏できる
- パート同士の音域と音量のバランスが取れている
- 必要であればRecデータが用意され、ミュート解除や生演奏で展開できる
- FXとMixerが音割れや過度な音量を起こさない

Projectは、Sample Kit JSON、Beat Kit JSON、Patternファイルへの単なるリンク集ではありません。

保存時に、現在選ばれている波形、Beat Kit、Patternイベント、Synth設定、Key / Scaleなどを、そのProjectの完成状態として保存します。

## 5. 実際のファイル構成

SDカード内の基本構成：

```text
/sampler/
├── samples/                  # 単体Sample、Synth素材
├── loops/                    # Audio Beat、MIDI Pattern（互換性のためフォルダ名はloops）
├── kits/                     # Sample Kit
│   ├── Example_Kit.json
│   └── Example_Kit_assets/
│       ├── pad01.wav
│       ├── pad02.wav
│       └── ...
└── projects/                 # 完成Project
    ├── Example_Project.json
    └── Example_Project_assets/
        ├── pad01.wav         # Sample Kit内容の実体
        ├── pad02.wav
        ├── ...
        ├── beatPad01.wav     # Beat Kit内容の実体
        ├── beatPad02.wav
        ├── ...
        └── background.wav    # Audio Beat使用時のみ
```

Project JSONと`_assets`フォルダは必ずペアで扱ってください。

- Rename時はJSON、Assetフォルダ、JSON内のAsset参照を同時に更新する
- Delete時はJSONと対応するAssetフォルダの両方を削除する
- JSONだけ、または`_assets`だけを移動しない
- 音源を差し替えるだけでも、必ず本体または対応する作成ツールでProjectを再保存する

## 6. Project JSONの論理構造

現行Projectの論理構造は次の通りです。フォーマットバージョンは将来変更されるため、Workが推測だけでJSONを生成しないでください。

```text
Project JSON
├── version / kind / assets
├── samples[]                 # Sample Kitの12 Padと編集値
├── sampler.volume
├── beat
│   ├── format                 # pattern / audio / none
│   ├── drumKit
│   ├── name / volume / repeats
│   ├── tempo情報
│   └── pads[]                 # Pattern Beat用Beat Kit
├── loop
│   ├── length / quantize / grid / swing
│   ├── background             # Audio Beat使用時
│   └── events[]               # Beatと各パートのRecイベント
├── synth
│   ├── key / scale / tuning
│   ├── bass
│   ├── melody
│   └── chord
├── fx
└── mixer
```

Input Assign、Wi-Fi設定、画面輝度、外部機器の接続設定などは製品本体の環境設定であり、プリセットProjectには含めません。

## 7. プリセット制作の単位

Official Library用の制作物は、次の単位で整理してください。

### 7.1 単体素材

- Sample WAV
- Synth Source WAV
- Beat One Shot WAV
- Audio Beat WAV / MP3
- Beat Pattern MID / MIDI

これらはユーザーが自由に組み合わせる素材です。

### 7.2 Sample Kit

12 Padの音色セットです。これだけを差し替えて、現在のBeatや演奏データを残したい場合に使います。

### 7.3 Beat素材

- Beat Kitの12音
- Beat Pattern
- Patternの推奨Tempo / Swing / Repeat

Beat KitとBeat Patternは別に評価します。一つのBeat Kitに対して複数のPatternを用意できます。

### 7.4 完成Project

初心者向けの中心プリセットです。必ず次をひとまとめで設計してください。

1. Project名とコンセプト
2. Sample Kit
3. Beat Kit
4. Beat Pattern
5. Bass音色
6. Melody音色
7. Chord音色
8. Key
9. Scale
10. 各パートのOctaveとVolume
11. 推奨FX Targetと初期FX値
12. Mixerバランス
13. 必要に応じたRecシーケンス

## 8. プリセットProjectの企画表

Workは、ファイルを作る前にProjectごとに次の表を作成してください。

| 項目 | 内容 |
|---|---|
| Project名 | 英数字を基本とする |
| 初心者が感じるジャンル | Pop / House / Hip-Hop / Chiptuneなど |
| 楽しみ方 | 連打、Chop、コード、スクラッチなど |
| Sample Kit | Pad 1〜12の役割とファイル |
| Beat Kit | Pad 1〜12の役割とファイル |
| Beat Pattern | Pattern名、長さ、推奨Tempo、Swing |
| Bass | 音色、Base Note、Octave、Volume |
| Melody | 音色、Base Note、Octave、Volume |
| Chord | 音色、Base Note、Octave、Volume |
| Harmony | Key / Scale / Fine Tuning |
| Rec | プリセットとして事前配置する内容 |
| FX | Target、初期値 |
| Mixer | 5パートの音量バランス |
| 必要容量 | WAV合計、JSON、推定展開容量 |
| 配布先 | Built-in / Official Web Library |

## 9. 音楽的な組み合わせのルール

- Beat KitとBeat Patternのジャンル感を合わせる
- Sample Kitには、Beatと役割が重なりすぎない音を入れる
- Bassは低域でBeatのKickと衝突しない音色と音量にする
- Melodyは初心者が1音押すだけでキャラクターを感じられる音にする
- Chordは3〜4音同時発音で破綻しない音にする
- Bass / Melody / Chordは共通のKey / Scaleで気持ちよく鳴ることを確認する
- SampleベースのSynth SourceはBase NoteとLoop設定を確定する
- プリセットの初期状態でマスターが過大にならないよう、全パート同時再生で確認する
- 一部のパートをMuteしても、生演奏で展開を作れる構成にする

## 10. Workへの作業指示

最初からProject JSONやWAVを大量生成せず、次の順序で作業してください。

### Phase 1: 構造の確認

- Sample Kit、Beat Kit、Beat Patternの違いを説明する
- Bass / Melody / Chordの役割を説明する
- Key / Scaleが3つのSynthパートで共通であることを確認する
- Projectがそれらの完成状態であることを確認する

### Phase 2: Project企画

- 初心者がすぐ遊べるProjectテーマを提案する
- Projectごとに必要な音の役割を先に決める
- 既存音源を共用できるProjectを明示する
- Built-in向けとOfficial Web Library向けを分ける

### Phase 3: 構成表の作成

- 各ProjectのSample Kit、Beat Kit、Beat Patternを表にする
- Bass / Melody / Chordの音色、Octave、Volumeを表にする
- Key / Scaleを明記する
- 使用するWAVの予想容量を集計する

### Phase 4: 承認

ユーザーがProject構成表を承認するまで、大量の音源生成やProject JSONの手作業を始めないでください。

### Phase 5: 制作と検証

- 承認された音源だけを制作する
- 本体または対応ツールでProjectを保存する
- JSONと`_assets`をペアで確認する
- Projectを新規読み込みし、ファイル欠落がないことを確認する
- Beat再生、Sampler、Bass、Melody、Chordを全て鳴らす
- Key / Scale、Tempo、Swing、Loop長を確認する
- 全パート同時再生で音量、音域、処理負荷を確認する

## 11. Workの最初の回答に必要な内容

この文書を参照したWorkは、最初の回答で次を提示してください。

1. Sample Kit、Beat Kit、Beat Patternの違い
2. Projectに含める要素の一覧
3. Bass / Melody / ChordとKey / Scaleの関係
4. Built-in用のプリセットProject案
5. Official Web Library用のプリセットProject案
6. 各Projectの音源共用案
7. 各Projectの概算容量
8. 初心者がProjectを読み込んだ直後に行う遊び方
9. 不足している仕様や確認が必要な項目

プリセットの評価は個別の音の良さだけではなく、Project全体を読み込んだときに、Beat、Sampler、Bass、Melody、Chordが一つの楽器として気持ちよく組み合わさるかで判断してください。
