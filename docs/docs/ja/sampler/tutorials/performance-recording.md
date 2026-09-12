# 演奏全体をWAVへ録音する

Performance Recordingは、ビート、サンプラー、Bass、Melody、Chord、FX、Scratchを含む最終的な演奏音を1本のWAVへ保存します。

パッド演奏をLoopへ覚えさせる`Rec`とは目的が違います。

| 機能 | 保存するもの |
|---|---|
| Rec | パッドを押したタイミング。あとからLoopとして再生・編集できる |
| Performance Recording | スピーカーから聞こえる演奏全体。WAVとして聞ける |

## 準備

- SDカードを挿入します
- Music Playerで曲を読み込んでいる場合は`Remove Music`で外します
- 保存したいビート、Rec Loop、FX Targetを準備します

Music PlayerとPerformance Recordingは同時に使えません。

## 1. 録音を待機状態にする

1. 下のダイヤルを押してメニューを開きます
2. `Project` → `Performance Recording`を`On`にします
3. メニューを閉じます

ヘッダーに録音待機のマークが表示され、PLAYまたはFXモードのFn1が録音開始ボタンになります。

<!-- IMAGE PERF-REC-01: Performance RecordingをOnにした待機状態。ヘッダーの録音マークとFn1が見える。 -->

## 2. 録音を開始する

`Fn1`を押すとLoop再生とPerformance Recordingが始まります。録音中はヘッダーの赤い録音マークを確認できます。

録音中も通常どおり操作できます。

- パッドを演奏する
- パートを切り替える
- MuteやMixerで展開を変える
- Repeat、Filter、Delay、Tape Stop、Scratchなどを使う

## 3. 録音を止める

もう一度`Fn1`を押してLoopを停止します。WAVの書き込みが終わるまで待ちます。

完了すると`TAP: SAVE / HOLD: DELETE`と表示されます。

<!-- IMAGE PERF-REC-02: 録音終了後のTAP: SAVE / HOLD: DELETE表示。 -->

## 4. 保存または削除する

- 保存する：`Fn1`を短く押して離す
- 保存しない：`Fn1`を長押しし、削除ゲージが完了してから離す

保存したファイルはSDカードの`/sampler/recordings/`へ`Performance_001.wav`のような名前で入ります。File EditorからDownload、Rename、Deleteできます。

## 録音できない場合

- `NO SD FOR RECORDING`：SDカードを挿入し、認識を確認します
- `REMOVE MUSIC FIRST`：Music Playerの`Remove Music`を実行します
- `RECORDING UNAVAILABLE`：SDカードの空き容量と状態を確認します
- `RECORDING FAILED`：本体を再起動し、別のSDカードでも確認します
