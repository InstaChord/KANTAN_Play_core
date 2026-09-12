# Projectとサンプルキット

KANTAN Samplerには、音色だけを保存する`Sample Kit`と、曲全体を保存する`Project`があります。

## どちらを保存するか

| 保存形式 | 保存する内容 | 向いている用途 |
|---|---|---|
| サンプルキット | 12個のサンプル、パッド編集設定、サンプラーパートの音量 | 効果音セット、ドラムセット、別の曲でも使う音色セット |
| Project | サンプルキットの内容、ビート、Rec、Loop長、Key / Scale、Bass / Melody / Chord、FX、Mixer | 制作中の曲やライブセット全体 |

迷った場合はProjectを保存します。

## Projectを保存する

1. SDカードを挿入します
2. 下のダイヤルを押してメニューを開きます
3. `Project` → `Save`へ進みます
4. 保存方法を選びます

表示される候補は現在の状態によって変わります。

- `Update`：現在のProjectへ上書き
- `Copy`：現在名をもとに複製
- `New`：日時または連番の新しいProjectとして保存

初めて保存する場合は`New`を選びます。

<!-- IMAGE SAVE-01: Save Project画面。Update / Copy / Newの候補が分かる。 -->

## Projectを読み込む

1. `Project` → `Load`へ進みます
2. 読み込みたいProjectを選びます
3. `OK`で確定し、処理が終わるまで待ちます

内蔵Projectの`DISCO Beat`も一覧の先頭から選べます。

!!! warning
    Projectを読み込むと、現在のサンプル、ビート、Recなどが置き換わります。必要な状態は先に保存してください。

## サンプルキットを保存する

1. `SAMPLER`パートを選びます
2. メニューの`Sample` → `Save Sample Kit`へ進みます
3. 保存方法を選びます

- `Update`：現在のKitへ上書き
- `Copy`：現在のKitを複製
- `New`：新しいKitとして保存
- `Save as Default`：起動時やReset Kitで使う自分用のDefault Kitとして保存

サンプルキットを保存しても、ビート、Rec、Key / Scale、FXは含まれません。

## サンプルキットを読み込む

`Sample` → `Load Sample Kit`から選びます。現在の12個のパッドと編集設定が置き換わりますが、ビートとRecは残ります。

## New KitとReset Kit

- `New Kit`：新しい空のKitから始める
- `Reset Kit`：保存済みDefault Kitがあれば読み込み、なければ内蔵Kitへ戻す

`Reset Kit`でDefault Kitを読み込んだ場合も、誤ってDefaultへ上書きしないよう現在名は`NEW_KIT`になります。

## 自動復元について

本体は直前の状態を自動保存し、次回起動時に復元します。マイク録音したサンプルも、SDカード装着時は一時保存されます。

!!! warning
    SDカードがない状態で録音したサンプルは、電源を切ったあと復元できません。大切な録音はSDカードを入れ、Projectまたはサンプルキットとして保存してください。

## ファイルをPCへバックアップする

File EditorではProject、Kitと、それぞれに対応する音声ファイルをDownloadできます。RenameやDeleteでは関連する音声フォルダーも一緒に処理されます。

[File Editorの使い方](file-editor.md){ .md-button }
