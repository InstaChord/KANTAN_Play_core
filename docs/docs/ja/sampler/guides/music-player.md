# 曲を流しながら演奏する

Music Playerでは、SDカードのWAV / MP3を曲全体として再生できます。曲を聴くだけでなく、サンプラーパッドで効果音や声、合いの手を重ねられます。

ここで再生する曲は`Music`です。繰り返し再生するリズム素材の`Beat`や、Recした演奏データの`Loop`とは別に扱います。

## 1. MusicファイルをSDカードへ入れる

File Editorの`Music`タブを開き、WAVまたはMP3をUploadします。MusicファイルはSDカードの`/sampler/music/`に保存されます。

[File Editorの使い方](../reference/file-editor.md){ .md-button }

## 2. 曲を読み込む

1. 本体で右側の下のダイヤルを押し、メニューを開きます
2. `Music` → `Music Track` → `Load Music`へ進みます
3. 再生したいWAV / MP3を選びます
4. 読み込みと解析が終わるまで待ちます

読み込み後はPLAYモードの`MUSIC`パートへ移動し、曲名、再生状態、時間、音量が表示されます。

<!-- IMAGE MUSIC-01: Load Musicのファイル一覧。曲名とPreview / Back / OKが分かる状態。 -->

## 3. Musicを操作する

MUSICパートのPLAYモードでは、中央の4つのパッドで再生を操作します。

| パッド | 操作 |
|---|---|
| P5 | 10秒戻る |
| P6 | Stop。曲の先頭へ戻して停止する |
| P7 | Play / Pause |
| P8 | 10秒進む |

<!-- IMAGE MUSIC-02: MUSIC + PLAY画面。P5〜P8の4つの再生アイコンと曲情報が見える状態。 -->

メニューの`Music Track`からも、同じPlay / Pause、10秒戻る・進む、Stopを操作できます。

## 4. 曲にサンプルを重ねる

1. P7でMusicを再生します
2. 下のダイヤルを回して`SAMPLER`パートへ移動します
3. PLAYモードで好きなパッドを鳴らします

Musicは止まらずに再生を続けます。曲へ短い声、効果音、パーカッションなどを重ねてみてください。

!!! tip
    Musicとサンプルの音量差が大きい場合は、`Music Track`の`Track Volume`を調整します。サンプル側はパッドのVolumeまたはサンプラーパートのVolumeを調整します。

## 5. FXで2つの音を使い分ける

Musicを読み込んだ状態でFXモードへ入ると、FXの対象は次の2つになります。

- P9 `LIVE`：ビート、サンプラー、Bass、Melody、Chord
- P10 `MUSIC`：Music Playerの曲

たとえば`MUSIC`だけにFilterをかけ、`LIVE`の合いの手はそのまま鳴らせます。両方を選ぶこともできます。

[FXの使い方](../tutorials/use-fx.md){ .md-button .md-button--primary }

## 曲を入れ替える・外す

- 別の曲へ替える：`Music Track` → `Load Music`
- 読み込んだ曲を外す：`Music Track` → `Remove Music`

`Remove Music`はSDカード上のファイルを削除しません。現在の演奏からMusicを外す操作です。

!!! note
    Music Playerの再生中は、演奏全体をWAVへ残すPerformance Recordingを同時には使えません。
