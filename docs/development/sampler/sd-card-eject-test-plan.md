# SDカード安全取り外し 受け入れテスト

## 状態遷移

1. 起動直後の`READY`からEjectを一度押し、確認表示だけで状態が変わらないこと
2. 二度目で`SAFE TO REMOVE`となり、SD上の一覧を再度開けないこと
3. 同じカードと別カードをそれぞれ挿し、`Load SD Card`で`READY`へ戻ること
4. カードなし起動で`NOT INSERTED`となり、後挿し後のLoadで認識すること
5. 未対応または壊れたカードで`READY`にせず、失敗表示が一度だけ出ること
6. Eject / Loadを10回繰り返し、古い一覧、Musicハンドル、プレビューが残らないこと

## 演奏継続

Eject前にSample、Audio Beat、KANTAN Synth、RecイベントをRAMへ読み込みます。Eject後もそれぞれが
演奏できることを確認します。Musicは停止し、Load / Save、File Editor、Performance Recordingは
案内を一度表示して開始しないことを確認します。Load後も現在Project / Kitが自動で変わらないことを確認します。

## BUSYと書込み保護

- Music再生中: EjectでMusicを停止・closeし、安全に取り外せること
- Performance Recording中、終了flush中、保存確認中: `BUSY`で拒否すること
- Project / Kit保存中: 完了までEject操作へ遷移しないこと
- File EditorでUpload / Rename / Delete中: 本体でEject画面へ入れず、セッション終了後に実行できること
- File Editorのブラウザ更新が`SAFE TO REMOVE` / `ERROR`状態を再マウントしないこと

## 予期しない抜去

Music読込中、Music再生中、Performance Recording書込み中にカードを抜き、SD状態が`ERROR`へ遷移し、
Music / 録音 / File Editorが停止することを確認します。同じ通知がループ表示されず、明示LoadまでSDアクセスを
再試行しないことを確認します。書込み中の物理抜去で途中ファイルが破損し得る点は製品制約として扱います。

## ビルドと実機

- `sampler_native_m1mac`をビルドし、メニュー、状態表示、世代不一致拒否を確認する
- `sampler_s3`をビルドする
- 実機でSPI共有中の表示、実カードのflush / unmount、抜去時のSdFatエラー値、各種カードとの互換性を確認する
- CoreS3のカード検出信号は、回路と利用可能GPIOが確認できるまで試験対象へ追加しない
