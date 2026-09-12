# DJのように演奏する

RecしたLoop、Music、Mute、Mixer、FXを組み合わせると、音を足したり引いたりしながらリアルタイムに展開を作れます。

## 1. 演奏の土台を用意する

次のどちらかから始めます。

- ビートとRecしたLoopを使う
- Music Playerで曲を読み込み、サンプラーの合いの手を加える

[音楽Loopを作る](loop-making.md){ .md-button }
[Music Playerを使う](music-player.md){ .md-button }

## 2. Muteで音を足し引きする

PLAYまたはRECモードの`Fn2`で、現在パートの記録済み演奏をMute / Unmuteします。

基本的な展開例です。

1. ビートだけを鳴らす
2. BassをUnmuteする
3. サンプラーとChordを加える
4. いったんBass以外をMuteする
5. 全パートを戻す

Mute中も生演奏は鳴るため、記録演奏を止めて自分のパッド演奏へ置き換えられます。

## 3. Mixerの状態を4つ作る

FXモードで`Fn3 MIX`を押します。各パートのパッドを短く押すとMute、押したまま下のダイヤルを回すと音量を変えられます。

P9〜P12には、4つのMix状態を保存できます。

例：

- Mix 1：ビートだけ
- Mix 2：ビート + Bass
- Mix 3：全パート
- Mix 4：Musicを小さく、LIVEを大きく

Mix Slotを使うと、複数パートを一度に切り替えられます。Loopを停止すると一時的なMixer音量は100%へ戻りますが、保存したMix Slotは残ります。

## 4. FXを短く使う

Mixerから`Fn3`でFX画面へ戻ります。

- 切り替え直前：P1〜P4 Repeat
- 音をこもらせてから戻す：P5 Filter
- リズムを細かく切る：P6 Gater
- 粗い音へ変える：P7 Crusher
- 余韻を残す：P8 Delay
- 速度を落として止める：P12 Tape Stop

右側面レバーを動かすと、最終ミックスを前後へ動かすScratchとして使えます。

[FXの詳しい操作](../tutorials/use-fx.md){ .md-button .md-button--primary }

## 5. MusicとLIVEを分けて加工する

Musicを読み込んでいるときは、FX Targetが次の2つになります。

- P9 `LIVE`：ビート、サンプラー、Bass、Melody、Chord
- P10 `MUSIC`：Music Playerの曲

たとえばMUSICだけをFilterで細くし、その上でLIVEのサンプルを鳴らせます。両方のTargetを選ぶと、全体へFXをかけられます。

## 6. 演奏を残す

Musicを読み込んでいない演奏は、Performance RecordingでFXとScratchを含む最終出力をWAVへ保存できます。

[演奏全体をWAVへ録音する](../tutorials/performance-recording.md){ .md-button }

!!! tip
    変化を増やしすぎず、「Muteで減らす → 1つのFXを使う → 全パートを戻す」の3段階から始めると、展開が分かりやすくなります。
