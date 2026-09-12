# 外部コントローラーを使う

MIDIコントローラー、PC、USB Keyboard、USB Gamepad、BLE MIDI機器などから、パッド演奏や本体操作ができます。

## Input Sourceを選ぶ

`External Device` → `Input Source`から1つ選びます。

| 選択肢 | 主な接続先 |
|---|---|
| `Off` | 外部入力を使わない |
| `USB MIDI Controller` | Class Compliant USB MIDI鍵盤・パッド |
| `USB MIDI Computer` | PCやタブレットからのUSB MIDI |
| `USB Keyboard` | USBキーボード |
| `BLE MIDI` | Bluetooth MIDI機器 |
| `USB Gamepad` | デジタルボタンと方向入力 |

USB Hostへ切り替える場合など、設定後に再起動が必要になることがあります。画面の案内に従ってください。

Port C MIDIとPort A拡張ボタンは、この択一設定とは別に受信できます。

## BLE MIDIを接続する

1. `Input Source`を`BLE MIDI`にします
2. `External Device` → `BLE MIDI Connection` → `Scan & Connect`へ進みます
3. 一覧から機器を選びます
4. `Allow Connection`を確認します

接続先は保存されます。

- 別の機器へ変更：`Forget Device`のあと再Scan
- 接続が不安定：`Reset BLE Connection`
- 状態確認：`Device Info`

<!-- IMAGE EXT-01: BLE Devices一覧またはAllow Connection画面。 -->

## MIDI Noteの動作を選ぶ

`External Device` → `MIDI Note Action`で選びます。

| 設定 | 動作 |
|---|---|
| `Auto` | Assign済みNoteは操作、未Assign Noteは演奏 |
| `Play` | Assignを使わず、すべて演奏に使う |
| `Control` | MIDI演奏をせず、Assignした操作だけを実行 |

初めは`Auto`がおすすめです。

## Input Assignで操作を覚えさせる

1. `External Device` → `Input Assign` → `Learn`を選びます
2. 本体で割り当て先を押します
3. 画面が`Press MIDI or EXT button`になったら、外部機器の割り当てたいキーやボタンを押します
4. 登録完了表示を確認します

本体側で選べる割り当て先は、12個のパッド、Fn1〜Fn3、SOUND / PLAY / REC / FX、上のダイヤル押し込みによる全停止です。

<!-- IMAGE EXT-02: LEARNのSelect targetとPress MIDI or EXT buttonの2状態。 -->

Learnの最初の割り当て先選択は約5秒でTime Outします。時間切れになった場合は、もう一度Learnを開きます。

## Assignを確認・削除する

- `Assign List`：登録済み割り当てを確認し、個別削除
- `Clear All`：すべての割り当てを削除

同じ外部入力を別の割り当て先へ登録すると、新しい割り当てが優先されます。

## 対応する入力

- MIDI Note / CC
- BLE MIDI
- USB MIDI Controller / Computer
- USB Keyboardのキー
- USB Gamepadのボタンと方向入力
- Port A拡張ボタン、最大32入力

USB GamepadのAnalog Stickの連続値は使いません。方向入力をデジタルボタンとして扱います。
