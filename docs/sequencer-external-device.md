# Sequencer / Sampler: External Device の対応

## 共通にした操作

- ルートの **External Device** → **Input Source** から入力元を選ぶ。
- BLE MIDI を選んだときだけ **BLE MIDI Connection** を表示する。
- **Scan & Connect** → 検索結果 → **Cancel / Allow Connection**。初期選択は Cancel。
- 機器名、アドレス末尾、受信強度を検索結果に表示。選んだ接続先を本体に保存する。
- 検索中・接続中・接続完了・機器なし・失敗を表示し、失敗時は再試行できる。
- 確認画面で「戻る」は検索結果へ、検索画面で「戻る」は検索を中止してメニューへ戻る。
- **Forget Device** は保存済み接続先を解除。**Reset BLE Connection** は接続先を保ったまま再ペアリングする。
- **Device Info** で BLE 状態、保存先、MIDI受信数、USB状態を確認する。
- Wi-Fi は BLE コントローラーの停止完了後、さらに1秒待ってから起動する。25秒以内に停止できなければ Wi-Fi の起動を中止する。
- Wi-Fi 使用後、BLE はその起動中は再開しない。Input Source の BLE MIDI を再選択、または Reset BLE Connection で再起動する。
- 通常起動時は USB ホストを解除して PC接続・充電を優先。入力選択による意図した再起動時だけホストを開始する。USB給電はスタック準備完了後とし、外部給電中は出力しない。

## Sequencer 固有の差分

- 対応入力は Off / USB MIDI Controller / USB MIDI Computer / BLE MIDI。Sampler の USB Keyboard / USB Gamepad の入力割り当て機能は未移植。
- **Input Assign** は既存の **Control Mapping** に接続する。本体・ソングの二層の割り当てを維持する。Sampler の Learn、CC割り当て、MIDI Note Action は移植していない。
- PortC MIDI と InstaChord Link は Sequencer の機能として残す。InstaChord Link は選択済みの同じ入力経路でのみ有効にできる。
- 入力方式の変更は保存成功後に再起動して適用する。Sampler は一部の変更を再起動なしで適用するが、Sequencer では安全側に統一した。
- 入力方式を選んだだけでは再起動しない。変更先と「再起動が必要です」を示し、初期選択が **Cancel** の確認画面で **Apply & Restart** を選んだ場合だけ実行する。
- 適用後は全画面で変更内容、設定保存完了、安全な再起動中であること、電源を切らない旨を表示する。BLE接続リセットにも同じ案内を使う。
- ファームウェア更新完了など、Sequencer内のほかの再起動要求も同じ全画面案内を経由する。
- BLE 接続リセットも再起動して実行する。Sampler の接続リセットは同じ起動中で再接続する。
- Wi-Fi の File Editor は既存の Song Manager を開く。Sampler のファイル形式・音源処理には変更しない。
- Wi-Fi Setup のAP接続後は、iPhoneでも確実に開ける固定アドレス `http://192.168.4.1` を画面とQRに表示する。`kanplay.local` はmDNSが有効な通常LAN上のFile Editorだけで使う。

## 実装上の注意

Sequencer の BLE 検索・接続・停止は12KBスタックの MIDI 管理タスクに集約した。3KBの受信タスクでは検索・接続処理を実行しない。Wi-Fi開始判定は「未接続」表示ではなく、管理タスクからの停止完了通知を用いる。InstaChord Link もこの停止に従う。

接続先は小さな専用ファイル `/sequencer_ble.bin` にチェックサム付きで保存する。低レベルファイル操作で一時ファイルへの書き込み・検証後に置換し、接続中にソング全体をシリアライズしない。Sampler の保存ファイルには触れない。

## 実機確認項目（未確認）

1. BLE MIDI を選択し再起動。検索結果に機器が現れ、Cancel / 戻るでは新しい接続を開始しないこと。
2. Allow Connection で接続し、外部機器の操作に応じて MIDI packets が増加し、割り当てた演奏操作が動くこと。
3. 本体再起動後に保存済み機器へ再接続できること。相手機器がない場合に再起動を繰り返さないこと。
4. Forget Device 後は自動再接続しないこと。Reset BLE Connection で保存済み機器へ再ペアリングできること。
5. BLE接続中および検索中に Wi-Fi Setup を起動し、BLE停止 → 準備中 → 接続用QRの順になること。スマートフォンから接続・設定できること。
6. Wi-Fi終了後に BLE が Off (Wi-Fi) と表示され、再起動後に再び使えること。
7. USB Controller と Computer の切替、通常電源起動時のホスト解除、PCから給電中にUSB出力を有効化しないこと。

ホスト上の状態遷移テストとビルド成功は、実際の電波状況、相手機器とのペアリング、実機の長時間動作の確認を代替しない。

## 今回の検証記録

- Sequencer実機版、macOSシミュレーター、Sampler実機版のビルド成功。
- BLE操作・無線切替の状態遷移テスト、Sequencer互換ガード、SamplerのWi-Fiライフサイクル・音声メモリ・PCM描画・録音・共有音源テスト、KTKIT形式・SMF出力テストが成功。
- 接続中のCoreS3（`94:A9:90:C5:F7:60`）へ書き込み、書き込みデータの照合と再起動指示が成功。実際のBLE・Wi-Fi操作は上記項目で別途確認する。
- 書き込んだファームウェアのSHA-256: `92f5da890b1f4abaa7cd5ba8f1517041136b767ba9806ad5fb128447735eb35f`。
