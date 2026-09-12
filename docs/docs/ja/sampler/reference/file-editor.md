# File Editor

File Editorは、PCやスマートフォンからKANTAN SamplerのSDカード内ファイルを管理する画面です。

サンプルタブではファイル管理に加えて、内蔵PresetまたはSD上のサンプルをパッドへAssignできます。サンプルのStart / End、Pitch、Volumeなどは本体で編集します。

## File Editorでできること

- サンプル、ビート、Kit、Project、Musicファイルの表示
- SDカードへのUpload
- PCやスマートフォンへのDownload
- Rename、Delete
- SDカード内のフォルダー作成と表示先の切り替え
- サンプルのPreviewとパッドへのAssign

ビートのLoadや音量変更、Rec設定、ProjectのSave / Loadなど、本体の演奏状態を変える操作はFile Editorにはありません。

## 開く前の準備

1. KANTAN SamplerをWi-Fiへ接続します
2. 本体メニューから`File Editor`を選びます
3. 画面に表示されたQRコードまたはアドレスを、同じWi-Fiに接続したPCやスマートフォンで開きます

File Editorを開いている間は、ファイル保護のため本体の演奏機能が制限されます。

## サンプルをAssignする { #assign-sample }

1. `Sample`タブを開きます
2. `Assignment target`で割り当て先パッドを選びます
3. `Location`を選びます
4. 使いたいサンプルの`Play`で音を確認します
5. `Assign`を押します

<!-- IMAGE FILE-01: Sampleタブ全体。Assignment targetとLocationが同時に見える画面。 -->

### Locationの意味

| 表示例 | 内容 |
|---|---|
| `Device Preset` | KANTAN Sampler本体に内蔵されたサンプル |
| `SD / Samples` | SDカードのサンプル用ルートフォルダー |
| `SD / Samples / Drum` | SDカード内のDrumフォルダー |
| `SD / Samples / Song1` | SDカード内のSong1フォルダー |

選択した場所の音だけが一覧に表示されます。Device PresetとSDファイルが同じ一覧へ混ざることはありません。

## ファイルをUploadする

1. 保存先の`Location`を選びます
2. ファイルをUpload領域へドラッグ＆ドロップするか、領域を押して選びます
3. `Upload files`を押します
4. 進捗が完了するまで待ちます

複数ファイルを選んだ場合も、SDカード保護のため1件ずつ保存されます。

## タブと対応形式

| タブ | 主なファイル |
|---|---|
| サンプル（`Sample`） | WAV、MP3 |
| ビート（`Beat`） | WAV、MP3、MID、MIDI |
| Kit | JSON |
| Project | JSON |
| Music | WAV、MP3 |

!!! warning
    Upload、Rename、Deleteの処理中は、本体の電源を切ったりSDカードを抜いたりしないでください。

## File Editorを終了する

本体で別の操作を行うとFile Editorを終了します。ファイル処理が進行中でないことを確認してから終了してください。
