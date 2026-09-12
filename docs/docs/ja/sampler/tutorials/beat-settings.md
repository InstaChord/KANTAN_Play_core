# ビートとタイミングを調整する

ビートを選んだあと、速さ、跳ね方、Recのタイミング、Patternの密度を調整できます。最初は`Tap Tempo`と`Quantize`だけ覚えれば十分です。

## ビートのテンポを変える { #change-tempo }

1. 下のダイヤルを押してメニューを開きます
2. `Music` → `Tempo` → `Change Tempo` → `Tap Tempo`へ進みます
3. 聞きたい速さに合わせてパッドを数回押します
4. `OK`で確定します

Tap Tempo画面ではビートを聞きながら調整できます。`Back`で戻ると、画面を開く前のTempoへ戻ります。

<!-- IMAGE BEAT-SET-01: Tap Tempo画面。タップ対象とOK / Backが分かる。 -->

### Tempo Half / Double

同じ`Change Tempo`メニューにあります。

- `Tempo Half`：再生速度を半分にする
- `Tempo Double`：再生速度を2倍にする

オーディオビートでは速度と一緒に音程も変わります。音程を保つTime Stretchではありません。

## Swingを加える

`Music` → `Tempo` → `Swing`で、0 / 25 / 50 / 75 / 100%から選びます。

- 0%：均等なタイミング
- 値を上げる：後ろ側の音が遅れ、跳ねたリズムになる
- 100%：3連符に近い跳ね方

最初は25%から試し、ビートとRecした演奏を聞きながら増やします。

## Recの細かさを決める

### Quantize

`Rec` → `Quantize`をONにすると、少しずれたパッド入力を近いGridへ合わせます。初心者はONがおすすめです。

### Note Grid

`Music` → `Tempo` → `Note Grid`で8 / 16 / 32 / 64 / 128から選びます。

- 小さい値：大きな区切りで、タイミングがまとまりやすい
- 大きい値：細かい演奏を記録しやすい

迷った場合は初期値の32を使います。

## パターンビートを編集する

パターンビートを選んでいるときは、`BEAT + REC`で12個のドラムパッドを演奏し、新しいリズムを重ねられます。

- 間違えた直後：`Fn3`を短く押してUndo
- 特定のパッドだけ消す：`Fn3`を押しながら対象パッドを押す
- Pattern全体を消す：`Beat` → `Pattern` → `Clear Pattern`

`Beat` → `Select Kit`では、Patternを変えずにAcoustic / Dance / Chiptuneの音色を切り替えられます。

### Pattern Half / Double

`Beat` → `Pattern`から選びます。

- `Pattern Half`：Tempoを変えず、Patternの動きを半分の密度にする
- `Pattern Double`：Tempoを変えず、Patternの動きを2倍の密度にする

Tempo Half / Doubleとは結果が違います。

## 今の演奏を新しいビートとして保存する

ビートとRecした各パートを1つの音声へまとめる場合は、`Rec` → `Save as Beat`を使います。

1. Loop長が確定していることを確認します
2. `Save as Beat`を選びます
3. `BOUNCING`が終わるまで待ちます

WAVはSDカードへ`Beat_001.wav`のような名前で保存されます。現在のビートとRecデータはそのまま残ります。保存したWAVは、あとからオーディオビートとして選べます。

!!! warning
    Bouncing中は電源を切ったりSDカードを抜いたりしないでください。
