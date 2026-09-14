# PCM多重発音レスポンス 実機A/B比較手順

## 目的

コードの拍合わせ仕様は変えず、PCM描画の処理負荷を下げた効果を実機で比較します。

- `sampler_s3_latency_reference`: 従来のPCMミキサー
- `sampler_s3_latency_probe`: 最適化したPCMミキサー

両方とも、今回のキャッシュ、Recイベント処理、計測機能、`-O2`設定は同一です。そのため、この比較で主に分かるのはPCMミキサー変更の効果です。製品版全体との差ではありません。

Playでは最大24ms、Recでは最大グリッド半分のコード／ベース拍合わせを従来どおり維持しています。拍合わせによる一定の待ち時間と、発音数が増えたときの処理落ちを分けて評価してください。

## まず行う最短比較（約10分）

1. 同じProject、音色、音量、BPMを使います。
2. Reference版を書き込み、次の「テストA」と「テストB」を行います。
3. 各テスト後、全Padを離してLoopを停止し、Serialの`PERF`表示を保存します。
4. Probe版を書き込み、まったく同じ演奏を繰り返します。
5. `i2s-7plus-voices`のp95、`deadline-miss`、耳で感じるNote On／Off、画面反応を比較します。

### テストA: Chord単独

- 他パートを空またはMuteにする
- ChordをPlayで、短押し10回、1秒程度の長押し10回
- Note Onの立ち上がりと、Note Off後の離れ方を確認する
- 次にRecへ切り替え、同じ操作を行う
- 録音位置が演奏タイミングどおりか、発音だけが後ろへずれるかを確認する

### テストB: 多重発音

- Beatを再生する
- Bassを加える
- Melodyを加える
- 最後にChordを加え、7音以上が重なる状態を30秒ほど続ける
- コードを素早く切り替え、短い押下と離上を繰り返す
- 発音、離音、画面のPad反応、ページ切替の重さを確認する

最短判定は次のとおりです。

- Probe版の`i2s-7plus-voices` p95がReference版より小さい
- Probe版で`i2s-block` p95が1,000µs未満
- `deadline-miss=0`、`read-error=0`、`write-error=0`
- 音切れ、クリック、音程異常、鳴りっぱなしがない
- 多重発音中のNote On／Offと画面反応がReference版より悪化していない

## ファームウェアの準備

リポジトリ直下で実行します。PlatformIOの場所が異なる場合は、環境に合わせて読み替えてください。

接続ポートを確認します。

```sh
/Users/yuichi/.platformio/penv/bin/pio device list
```

以下の`/dev/cu.usbmodemXXXXXXXX`を、表示されたCoreS3のポートへ置き換えます。

### 1. Reference版

```sh
/Users/yuichi/.platformio/penv/bin/pio run -e sampler_s3_latency_reference -t upload --upload-port /dev/cu.usbmodemXXXXXXXX
```

### 2. Probe版

```sh
/Users/yuichi/.platformio/penv/bin/pio run -e sampler_s3_latency_probe -t upload --upload-port /dev/cu.usbmodemXXXXXXXX
```

Serial Monitorは115200bpsで開きます。

```sh
/Users/yuichi/.platformio/penv/bin/pio device monitor --baud 115200 --port /dev/cu.usbmodemXXXXXXXX
```

モニターを終了する場合は`Ctrl+C`です。書き込み直後にポート名が変わった場合は、もう一度`pio device list`で確認します。

## 比較時の共通ルール

- Reference版を先、Probe版を後に測る
- 両方で同じ保存済みProjectを読み込む
- 音色、各パート音量、Master音量、BPM、Loop長、FX、量子化設定を変えない
- 可能なら押すPadの順序と回数も揃える
- 主観評価では、Reference／Probeの名前を意識しすぎず、演奏の気持ちよさを優先する
- 計測値は累積されるため、シナリオごとに本体を再起動する
- 再起動後、音色の読み込みが終わってから演奏を始める
- シナリオ終了後はLoopを停止し、すべてのPad／鍵盤を離す
- Releaseの長い音色は音が完全に消えるまで待つ
- その後5秒ほど待ち、表示された`PERF`行をまとめて保存する

演奏中には計測ログを出さない設計です。停止後も音が残っている間は表示されません。

## 本確認シナリオ

### 1. Chord単独・拍合わせなし側の基準

最初に、Transport停止中など拍合わせ待ちが入らない状態で確認します。

- Chord以外をMuteまたは空にする
- 同じコードを短押し20回
- 同じコードを約1秒ずつ10回保持する
- コードを切り替えながら20回押す
- Note OnとNote Offの反応、Attack、Release、クリックの有無を記録する

ここで遅れが出る場合は、拍合わせではなく入力処理、ボイス確保、PCM処理、音色のAttackなどが候補です。

### 2. Chord単独・Play／Rec

- 同じBPM、同じ量子化設定でLoopを再生する
- Playで拍の直前、拍上、拍の直後を狙ってコードを押す
- Recでも同じ操作を行う
- Note Onだけでなく、短い押下のNote Offも確認する
- 録音後に再生し、記録位置が正しいかを確認する

Play最大24ms、Rec最大グリッド半分の待ちは仕様です。毎回ほぼ一定で拍へ吸い付く遅れは拍合わせ、発音数に応じて増えたり不規則に揺れたりする遅れは負荷の影響として記録します。

### 3. Beat + Bass + Melody + Chord

ユーザーが感じた状況を同じ順序で再現します。

1. Beatだけで10秒
2. Bassを加えて10秒
3. Melodyを加えて10秒
4. Chordを加えて30秒
5. 7音以上を重ね、コードを素早く切り替える

各段階で次を確認します。

- 押してから鳴るまで
- 離してからReleaseへ移るまで
- Pad表示とページ切替の追従
- Beatの乱れ、音切れ、クリック
- Bass／Melodyの発音がChord追加後に悪化しないか

### 4. Recイベント高密度

- 短いLoopにコードを多数記録する
- コードを素早く切り替え、Note On／Offを増やす
- 可能ならイベント上限に近いProjectでも試す
- 記録済みChordを再生しながら、新たにBass／Melodyを演奏する
- 録音中と録音後の画面反応を比較する

記録位置がReference版と一致し、Probe版でRec時の発音と画面反応が悪化していないことを確認します。

### 5. BLE MIDIとメモリ余裕

- BLE MIDI未接続でシナリオ3を行う
- BLE MIDIを接続し、同じ演奏を行う
- 一度切断して再接続する
- 接続後もChordを含む多重発音を行う
- 可能なら、その後Mic録音の開始／終了も確認する

再接続失敗、再起動、メモリ不足、音色消失、異常な発音遅延がないことを確認します。キャッシュ確保に失敗した場合はPSRAMからの再生へ安全に戻るため、音が出なくなるのは不合格です。

### 6. 従来の汎用PCM経路

次も短く確認します。

- Reverse
- Seek／Scratchに関係する操作
- Touchフィルター
- Beat／Chop
- Music再生
- Master FX

これらは通常PCMとは別の汎用描画経路を使うため、音質、位置、ループ、クリックに回帰がないことを聴きます。

## `PERF`表示の読み方

主な項目は次のとおりです。

| 表示 | 意味 |
|---|---|
| `input-on-queue` / `input-off-queue` | 物理入力からメイン処理が受け取るまで |
| `live-on-audio` / `live-off-audio` | ライブNote On／Offからミキサーが最初に処理するまで |
| `rec-on-audio` / `rec-off-audio` | Rec再生Note On／Offからミキサーが最初に処理するまで |
| `rec-store` | Recイベントを保存する処理時間 |
| `rec-batch` | 同時刻のRecイベント群を処理する時間 |
| `voice-alloc` | 発音ボイスを探して確保する時間 |
| `i2s-block` | PCM発音中の1ms音声ブロック全体の処理時間 |
| `i2s-1-2-voices` | 1〜2ボイス発音時のI2S処理時間 |
| `i2s-3-4-voices` | 3〜4ボイス発音時のI2S処理時間 |
| `i2s-5-6-voices` | 5〜6ボイス発音時のI2S処理時間 |
| `i2s-7plus-voices` | 7ボイス以上発音時のI2S処理時間 |

各行の`count`は件数、`avg`は平均、`p95`は95%の処理が収まる上限、`max`は最大値です。

- 入力／Noteイベントの時間幅は1ms
- CPU処理時間の時間幅は100µs
- `p95<900us`なら、p95は900µs未満
- `p95>=3200us`のような表示は範囲超過で、3,200µs以上
- `i2s-block`は無音時間を含めず、PCM発音中だけを集計

末尾のカウンターも確認します。

- `deadline-miss`: 1ms以上かかった音声ブロック数
- `read-error`: I2S読み込みエラーまたは短い転送
- `write-error`: I2S書き込みエラーまたは短い転送

これらは問題の兆候を示しますが、ハードウェア出力のunderrunを直接数えた値ではありません。

`live-on-audio`などはミキサーがイベントを受け取るまでの値です。DAC、DMA、スピーカー、音色のAttack／Releaseまでを含む「耳に聞こえる総遅延」ではありません。

## 合格基準

必須条件:

- Probe版の`i2s-block` p95が1,000µs未満
- `deadline-miss=0`
- `read-error=0`、`write-error=0`
- Reference版と録音位置、Pitch、Loop、Envelopeが一致する
- 音切れ、クリック、鳴りっぱなし、誤ったNote Offがない
- BLE MIDI再接続とMic開始後も安定する

改善判定:

- 特に`i2s-5-6-voices`と`i2s-7plus-voices`のp95／maxがReference版より下がる
- Chord追加時のNote On／Offが軽くなる、または少なくとも悪化しない
- 記録済みChord再生中のPad／ページ描画が滑らかになる
- 物理入力から聞こえるまでの目標p95 20msは、拍合わせ待ちと音色Attackを除いた条件で評価する

拍合わせ込みのChord Playは最大24ms、Recは最大グリッド半分の仕様があるため、それだけで20msを超えても処理落ちとは判定しません。

## 結果記録表

シナリオごとに本体を再起動し、次の表を複製して使います。

| 項目 | Reference | Probe | 備考 |
|---|---:|---:|---|
| Chord Play Note On体感 |  |  | 良い／普通／遅い |
| Chord Play Note Off体感 |  |  | 良い／普通／遅い |
| Chord Rec Note On体感 |  |  | 良い／普通／遅い |
| Chord Rec Note Off体感 |  |  | 良い／普通／遅い |
| 画面反応 |  |  | 良い／普通／遅い |
| `live-on-audio` p95 |  |  |  |
| `live-off-audio` p95 |  |  |  |
| `rec-on-audio` p95 |  |  |  |
| `rec-off-audio` p95 |  |  |  |
| `rec-store` p95 |  |  |  |
| `voice-alloc` p95 |  |  |  |
| `i2s-3-4-voices` p95 |  |  |  |
| `i2s-5-6-voices` p95 |  |  |  |
| `i2s-7plus-voices` p95 |  |  |  |
| `i2s-block` max |  |  |  |
| `deadline-miss` |  |  | 必須0 |
| `read-error` / `write-error` |  |  | 必須0 / 0 |
| 音切れ／クリック／鳴りっぱなし |  |  | 必須なし |

保存する情報:

- 使用Project名
- BPM、量子化設定、Loop長
- Bass／Melody／Chordの音色名
- BLE MIDI接続の有無
- Reference／Probeそれぞれの`PERF`全文
- 遅れを感じた操作と、そのときのおおよその発音数

問題が出た場合は、同じシナリオをもう一度再起動後に再現し、Referenceだけか、Probeだけか、両方かを記録してください。

取得済みの実機計測値は[PCM response on-device A/B results](pcm-response-device-results.md)へ保存します。
