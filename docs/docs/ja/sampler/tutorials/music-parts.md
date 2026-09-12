# Bass・Melody・Chordを演奏する

サンプラーに音楽的なパートを加えると、ビートとサンプルだけの演奏へ低音、旋律、和音を重ねられます。音楽理論が分からなくても、共通のKeyとScaleに合う音がパッドへ並びます。

## まず3つの役割を知る

| パート | 役割 | 演奏の特徴 |
|---|---|---|
| `BASS` | 曲の低音と土台 | 12音。新しい音を押すと前のBass音と入れ替わる |
| `MELODY` | メロディーや合いの手 | Key / Scaleに合う12音 |
| `CHORD` | 和音と曲の雰囲気 | 7つのコードと5つのModifier |

最初はビートを鳴らし、Bassを1音ずつ試してからMelody、Chordへ進みます。

## BassとMelodyを鳴らす

1. `Fn1`でビートまたはLoopを再生します
2. 下のダイヤルを回して`BASS`を選びます
3. PLAYモードでパッドを1つずつ押します
4. 同じ手順で`MELODY`へ移動します

画面のパッドには現在のKeyとScaleに応じた音名が表示されます。どのパッドを押しても設定中のScale内の音です。

<!-- IMAGE MUSIC-PART-01: BASSまたはMELODY + PLAY。12 Padの音名が読める。 -->

## Chordを鳴らす

CHORDの7つのコードパッドは次の位置です。

- P1、P2、P3：1、2、3番目のコード
- P5、P6、P7：4、5、6番目のコード
- P9：7番目のコード

空いている位置はChordの種類を変えるModifierです。

| パッド | Modifier |
|---|---|
| P4 | `SWAP`。MajorとMinorの性格を入れ替える |
| P8 | `7th` |
| P10 | `sus4` |
| P11 | `9th` |
| P12 | `M7` |

Modifierを押したままコードパッドを押します。Modifierだけでは音は鳴りません。

<!-- IMAGE MUSIC-PART-02: CHORD + PLAY。7つのコード名と5つのModifierが読める。 -->

## KeyとScaleを変える

1. 下のダイヤルを押してメニューを開きます
2. `Music` → `Key / Scale`へ進みます
3. `Key`で曲の中心音を選びます
4. `Scale`で使う音の並びを選びます

主なScaleの目安です。

| Scale | 向いている雰囲気 |
|---|---|
| `Pentatonic` | 最初におすすめ。音を外した感じが出にくい |
| `Major` | 明るい曲 |
| `Minor` / `Pentatonic Min` | 暗め、落ち着いた曲 |
| `Blues` | ブルース、Hip Hop風 |
| `Japanese` | 和風の響き |
| `Chromatic` | 12音すべて。自由度は高いが音を選ぶ必要がある |
| `Dorian` / `Mixolydian` | モード感のある発展的な演奏 |

Key / ScaleはBass、Melody、Chordで共有されます。

### Tuning

同じ画面の`Tuning`ではA=425〜455Hzを選べます。通常は初期値のA=440Hzのまま使います。別の楽器や古い録音とわずかに音程が合わないときだけ調整します。

## 音色、Octave、Volumeを変える

Bass、Melody、Chordの各パートを表示してメニューを開くと、そのパートの設定が先頭に表示されます。

- `Sound Source` → `Synth Sound`：内蔵シンセの音色を選ぶ
- `Sound Source` → `Pad Sample`：サンプラーパッドの音を使う
- `Base Note`：`Pad Sample`選択時だけ表示し、サンプルの基準音程を調整する
- `Octave`：-2〜+2オクターブ
- `Volume`：0〜100%

サンプラーパッドを楽器音源として使う場合は、[サンプルシンセ](../guides/sample-synth.md)も参照してください。

## Pitch Bendを使う

BassまたはMelodyでは、右側面レバーで音程を滑らかに上下できます。パートメニューの`Pitch Bend`で幅を選びます。

- `1 Semitone`：細かな表情。最初はこちら
- `1 Octave`：大きな音程変化

RECモードで行ったPitch BendはLoopへ記録されます。

## TOUCHで演奏する

BassまたはMelodyのPLAYモードで`Fn3 TOUCH`を押している間、画面タッチまたは本体の傾きで演奏できます。

- 画面の縦方向：Scale内の音程
- 画面の横方向：音色変化
- 画面に触れていないとき：本体の傾きで演奏

TOUCH中は現在パートの記録済み演奏が一時Muteされます。TOUCH演奏そのものはRecへ記録されません。

## Loopへ重ねる

演奏方法が分かったらRECモードへ切り替えます。サンプラーと同じように、パートごとに演奏を追加し、Undo、Mute、個別Deleteができます。

[演奏をRecする](record-loop.md){ .md-button .md-button--primary }
