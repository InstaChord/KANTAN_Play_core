# 音楽Loopを作る

ビートを土台にして、サンプラー、Bass、Melody、Chordを少しずつ重ねます。最後にMuteやFXで変化を付けると、短いLoopから演奏の展開を作れます。

## 最小構成から始める

最初は次の3つだけで作ります。

1. ビート
2. サンプラーの1〜2音
3. Bassの1音

音を増やす前に、少ない音で気持ちよく繰り返す状態を作るのがコツです。

## 1. ビートを決める

内蔵Patternから、雰囲気に近いビートを選びます。

[ビートを選ぶ](../tutorials/select-beat.md){ .md-button .md-button--primary }

最初はTempoやSwingを細かく調整せず、そのままの速さで試します。

## 2. サンプラーを重ねる

1. パートを`SAMPLER`にします
2. `REC`モードへ切り替えます
3. ビートを1周聴きます
4. サンプルを1音だけRecします
5. 次の周回で、必要ならもう1音加えます

[演奏をRecする](../tutorials/record-loop.md){ .md-button }

## 3. Bassを加える

1. Loopを再生したままパートを`BASS`へ切り替えます
2. `PLAY`でパッドを試し、合いそうな音を探します
3. `REC`へ切り替え、1音か2音だけ記録します

Bassは低い音でLoopの中心を作ります。すべての隙間を埋めず、長めの音を少なく置くとまとまりやすくなります。

## 4. MelodyまたはChordを加える

まだ物足りない場合だけ、MelodyまたはChordを1パート加えます。

- `MELODY`：短いフレーズや合いの手
- `CHORD`：和音で雰囲気を加える

KANTAN Samplerは共通のKey / Scaleに沿って音を並べるため、音楽理論が分からなくても外れにくい演奏ができます。

## 5. Muteで展開を作る

`Fn2`でRecしたパートをMuteし、音を減らした状態と全パートが鳴る状態を切り替えます。

例：

1. ビートだけ
2. ビート + Bass
3. すべてのパート
4. サンプラーをMuteしてビート + Bass

## 6. FXを加える

最後にRepeat、Filter、Delayなどを短く使います。

[FXで変化させる](../tutorials/use-fx.md){ .md-button }

FXを常にかけ続けるより、切り替わりの直前だけ使うと変化が伝わりやすくなります。

## 7. Projectを保存する

同じLoopをあとで続きから使う場合はProjectを保存します。

Projectには、サンプラー、ビート、Rec、Key / Scale、シンセ音色、FX、Mixerなど、演奏状態全体が保存されます。

サンプルの音色セットだけを別の演奏でも使いたい場合は、サンプルキットとして保存します。

[Projectとサンプルキットの保存](../reference/save-and-restore.md){ .md-button .md-button--primary }
[演奏全体をWAVへ録音する](../tutorials/performance-recording.md){ .md-button }
