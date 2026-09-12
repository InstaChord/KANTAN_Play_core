# 曲をChopして遊ぶ

Chopは、長いサンプルを複数の短い音へ分け、P1から順に並べる機能です。曲の一部分やリズムフレーズをばらばらの順番で鳴らせるようになります。

最初は、切り分けやすい短いドラムフレーズを使い、8 Sliceに分ける方法がおすすめです。

!!! warning
    Chopを実行すると、作られたSliceの数だけP1から順にサンプルが置き換わります。残したいKitは先に保存してください。

## 1. 元になるサンプルを用意する

1. 曲やフレーズのWAV / MP3をサンプラーパッドへ追加します
2. `SAMPLER`の`SOUND`モードで、そのパッドを2回短く押してEditへ入ります
3. `Start`と`End`で、Chopしたい範囲だけを残します

長すぎる曲全体よりも、最初は数秒程度のフレーズを選ぶと結果を理解しやすくなります。

[サンプルを追加する](../tutorials/add-sample.md){ .md-button }
[Start / Endを編集する](../tutorials/edit-sample.md#start-end){ .md-button }

<!-- IMAGE CHOP-01: Sample Edit画面。P1のChopと、波形上のStart / End範囲が分かる状態。 -->

## 2. Chop画面を開く

Edit画面でP1 `Chop`を押します。Chop画面では、分割後の境界が波形上へ表示されます。

<!-- IMAGE CHOP-02: Chop画面。FIT、KEEP、TAP CUT、4 / 8 / 12 / MANUALと波形の分割線が読める状態。 -->

## 3. 速度の扱いを選ぶ

| パッド | 設定 | 結果 |
|---|---|---|
| P1 | `FIT` | 現在のビートまたは確定済みLoopの長さに合わせる。速度と音程は一緒に変わる |
| P2 | `KEEP` | 元のサンプルの速度と音程を保つ |

`FIT`には、基準になるビートまたは確定済みLoopが必要です。何もない状態では`NOTHING TO FIT`と表示されます。

!!! tip
    元の曲らしさを残したい場合は`KEEP`、今鳴っているビートへ合わせたい場合は`FIT`から試します。

## 4. 分け方を選ぶ

| パッド | 分け方 |
|---|---|
| P5 | 4 Slice |
| P6 | 8 Slice |
| P7 | 12 Slice |
| P8 | `MANUAL`。下のダイヤルで4〜12 Sliceを選ぶ |

選んだあとに`Fn1`を押すと、分割予定の音をP1相当から順番にPreviewできます。分け方を変えた場合、Previewは最初のSliceへ戻ります。

### 聴きながら切る：TAP CUT

等分では合わない場合はP4 `TAP CUT`を使います。

1. P4を押すと、選択した範囲の再生が始まります
2. 次の音の始まりに合わせてP4を押し、切断位置を追加します
3. 最後まで再生すると自動で確定します。途中で終える場合は`Fn1`を押します

最大12 Sliceまで作れます。最初のSliceは、再生を開始した位置から自動で作られます。

## 5. Chopを実行する

1. `Fn1`で分割予定の音を確認します
2. `Fn2 CHOP`を押します
3. `CHOPPING`などの処理表示が消えるまで待ちます

完了すると`SAMPLER`パートの`PLAY`画面へ戻り、P1から順にSliceが配置されます。Slice同士は、次のSliceを鳴らすと前の音が止まるように設定されます。

<!-- IMAGE CHOP-03: Chop完了後のSampler PLAY画面と、CHOP COMPLETE / HOLD: MAKE LOOPの案内。 -->

## 6. そのままLoopにする

Chop完了後は次のどちらかを選べます。

- 短く操作して`DONE`：Sliceだけを作り、自分でパッドを演奏する
- `Fn2`を長押しして`MAKE LOOP`：先頭4個または8個のSliceを元の順番でRecへ並べる

`MAKE LOOP`のあとも、RECモードで別の順番を重ねたり、[Undo](../tutorials/record-loop.md#undo)したりできます。

## ChopしたSliceの注意点

ChopしたSliceはリズム位置を保つ専用サンプルです。Start / End、Pitch、Reverse、再Chop、サンプルシンセへの割り当ては使用できません。音量の調整と演奏はできます。
