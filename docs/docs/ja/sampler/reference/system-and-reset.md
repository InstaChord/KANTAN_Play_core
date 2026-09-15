# System設定とReset

Systemメニューには、録音入力、画面、LEDなど本体全体の設定があります。Reset操作は消える範囲を確認してから実行してください。

## System設定

下のダイヤルを押し、`System`を開きます。

| 項目 | 内容 |
|---|---|
| `Recording Input` | `Auto` / `Internal` / `External`からサンプル録音の入力を選ぶ |
| `Menu Sound` | メニュー操作音のON / OFF |
| `SD Card` | SDカードの状態確認、安全な取り外し、挿し直したカードの読込 |
| `Display` | 画面の明るさ、1〜5 |
| `LED` | パッドLEDの明るさ、1〜5 |
| `Language` | `EN` / `JP` |
| `Info` | 本体情報とファームウェア情報 |
| `Reset All` | サンプラー設定を初期化する |

### Recording Input

- `Auto`：接続状態に応じて選ぶ
- `Internal`：本体内蔵マイク
- `External`：外部音声入力端子

録音画面には`MIC INPUT`または`LINE INPUT`が表示されます。意図した入力と違う場合はここを確認します。

### SD Card

SDカードをPCなどへ移すときは、電源を切る代わりに次の手順で安全に取り外せます。

1. `System` → `SD Card`を開き、状態が`READY`であることを確認します
2. `Eject SD Card`を選び、確認のためもう一度押します
3. `SD CARD / SAFE TO REMOVE`が表示されてからカードを抜きます
4. PCなどでファイルを変更し、カードを本体へ戻します
5. `System` → `SD Card` → `Load SD Card`を実行します

`Load SD Card`は、起動時にカードを入れていなかった場合にも使えます。別のカードを挿した場合も、
現在演奏中のProjectやサンプルキットは勝手に切り替わりません。必要なファイルを各Loadメニューから選びます。

| 状態 | 意味 |
|---|---|
| `READY` | SDカードを使用できます |
| `BUSY` | 録音、保存、File EditorなどがSDカードを使用中です |
| `NOT INSERTED` | カードがないか、まだ読み込まれていません |
| `SAFE TO REMOVE` | 安全に取り外せます |
| `ERROR` | 読み書きに失敗しました。カードを確認して`Load SD Card`を実行します |

Eject後も、すでに読み込んだサンプル、ビート、KANTAN Synth、Recは演奏できます。
Music、保存、SDからの読込、File Editor、Performance Recordingは`Load SD Card`が成功するまで使えません。

!!! danger
    録音、保存、Upload、Rename、Delete中にカードを物理的に抜くと、ファイルが破損することがあります。
    この機能はどのタイミングでも抜ける「ホットスワップ」ではありません。必ず`SAFE TO REMOVE`を待ってください。

## 3種類のReset

### Clear Project

`Project` → `Clear Project`から実行します。

サンプル、ビート、Rec、Key / Scale / Tuning、Bass / Melody / Chord、FX、Mixerを初期状態へ戻します。Wi-Fi、外部入力、Input Assign、本体設定、SD上の保存済みファイルは残ります。

### Reset Kit

`Sample` → `Reset Kit`から実行します。

保存済みDefault Kitがあればそれを読み込み、なければ内蔵Kitへ戻します。ビートやRecは消しません。

### Reset All

`System` → `Reset All`から実行します。

- Recをすべて消す
- サンプルキット、ビート、FX、Mixer、Key / Scale / Tuningを初期化
- SD上のDefault Kitを削除
- 外部入力をOffへ戻す
- ファイル選択位置を初期化

Wi-Fi設定、消去できない内蔵サンプル、SD上の通常ProjectやKitは維持します。

!!! danger
    Reset Allは現在の演奏状態とDefault Kitを失います。必要な内容をProjectまたはサンプルキットへ保存してから実行してください。

## 音だけをすぐ止める

Resetする必要はありません。右側の上のダイヤルを押すと、ビート、Loop、サンプル、Musicを含むすべての音を停止できます。
