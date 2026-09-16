# Beat Part Test Plan

Beat統合後の実機確認を、原因を切り分けやすい順番で行うためのチェックリストです。
一度に複数項目を確認せず、各項目の結果を記録してから次へ進みます。

## 1. 起動と旧KIT互換

- 旧BGM入りKITを読み込み、Beatが `Audio` と表示される
- BGMなし、Drumイベントありの旧KITを読み込み、Beatが `Pattern` と表示される
- Sampler/Bass/Melody/Chordの音源とイベントが変化していない

## 2. ページとモード

- パート順が `BEAT / SAMPLER / BASS / MELODY / CHORD` になっている
- 各パートでSAMPLEを押してもSAMPLERへ強制移動しない
- FX中にパートを切り替えた時だけPLAYへ戻る

## 3. パート別SAMPLE

- BEAT + SAMPLE: Audio Beatでは全体波形、Patternでは12個の波形Padを表示する
- Pattern BeatのPadを押すと、そのBeat音だけを試聴できる
- BASS/MELODY/CHORD + SAMPLE: Sampler Padを押すとPad Soundへ切り替わり、基準音を試聴できる
- 試聴中にページまたはモードを切り替えても音が残らない

## 4. Beat選択

- 組み込み `DISCO / POP / ROCK / HOUSE / HIP HOP / BREAK / FUNK / REGGAE` を選べる
- 各Patternの速度とリズムが明確に異なり、1周の先頭で不自然な間や二重発音がない
- `New Pattern` はイベントなしで始まり、最初の演奏からLoop長を決められる
- WAV/MP3を選ぶとAudio Beatになり、Pattern音源が解放される
- MID/MIDIを選ぶとPattern Beatになり、Audio Beatが解放される

## 5. Pattern演奏

- 12 Padが遅延なく発音する
- Closed/Open HiHatが互いをChokeする
- 同時打鍵と高速連打で発音漏れがない
- RECで記録し、先頭イベントを含めて同じリズムで再生される

## 6. Mute

- Pattern BeatのPart Mute中も生演奏は鳴り、記録イベントだけ止まる
- Pattern BeatのPad Muteは対象Padの記録イベントだけ止める
- Audio BeatのPart MuteはAudioを停止する
- Mute解除後にLoop位置へ正しく復帰する

## 7. MixerとFX

- Mixerが5パート `BEAT / SAMPLER / BASS / MELODY / CHORD` になっている
- Beat VolumeとMixer Beat VolumeがAudio/Patternの両方へ反映される
- Repeat/Scratch/Delay/Tape Stop後もBeatとLoopの位相が崩れない

## 8. KIT v8とResume

- Audio Beat入りKITを保存・再読込できる
- Pattern Beat入りKITを保存・再読込できる
- Pattern音源のVolume/Pitch/Choke設定とBeat Patternイベントが戻る
- 電源再投入後も組み込みPattern Beatが復元される

## 9. File Editor

- SampleのLocationに `Device Preset`、`SD / Samples`、すべてのSD下位フォルダーがフルパスで並ぶ
- Locationを切り替えると、選択したDevice PresetまたはSDフォルダーのSampleだけが並ぶ
- Device PresetとSD上のWAV/MP3を選択中のPadへ割り当てられる
- SampleのStart / End、Pitch、Volumeなどの編集UIを表示しない
- Beat / Rec / Projectなど本体の演奏状態を変える設定UIを表示しない
- MIDIファイルをアップロードできる
- 各タブでSDファイルをUpload / Download / Rename / Deleteできる

## 10. 負荷とメモリ

- Pattern Beat、Sampler、Pad Synthを重ねても音切れや操作遅延が増えていない
- Audio BeatとPattern Beatを繰り返し切り替えてもPSRAM使用量が増え続けない
- KIT読込、File Editor、Wi-Fi Update後にBeatを再生できる
