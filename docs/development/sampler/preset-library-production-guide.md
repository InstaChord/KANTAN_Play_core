# KANTAN Sampler プリセット音源ライブラリ選定・制作指示書

## 1. 文書の目的

本文書は、KANTAN Samplerのプリセット音源を選定・制作する際の基準です。

ChatGPT Workなどを使って音源ライブラリを作る場合は、次の仕様書と一緒に参照してください。

- `product-spec.md`
  - 製品コンセプト
  - 初心者向けの操作体験
  - Sampler、Beat、Synthの役割
- `program-spec.md`
  - 音声形式
  - サンプル長
  - PSRAMおよびFlash容量
  - 内蔵音源の技術的制約
- `preset-project-structure-guide.md`
  - Sample Kit、Beat Kit、Beat Patternの違い
  - Bass / Melody / Chord音色とKey / Scaleの関係
  - プリセットProjectの完成構成
  - Project JSONと`_assets`フォルダの扱い
- `beat-pattern-production-guide.md`
  - Beat PatternのMIDI Note配列
  - Velocity、Pattern長、Grid、Swingの制作基準
  - WorkでのPattern企画、生成、検証手順

提案や音源生成を始める前に、仕様書から音源制作に関係する制約を抽出して整理してください。

仕様書、本指示書、実際のファーム容量に矛盾がある場合は、勝手に判断せず、矛盾点と推奨する解決方法を報告してください。

## 2. 今回の前提

現在ファームウェアへ組み込まれているWAVファイルは、すべて削除して新しいライブラリへ置き換えます。

したがって、現在の内蔵WAVとの差し引きではなく、新しいBuilt-inライブラリ全体の容量をゼロから設計してください。

現行の組み込みWAVは次の規模です。

- 61ファイル
- 合計2,133,451 bytes
- 約2.03MiB

これらはすべて置き換え対象です。

ただし、WAV以外のプログラム、画面データ、Pattern、通信機能などは残るため、Flashの空き容量をすべて音源へ使用してはいけません。

## 3. 配布区分

音源は次の2種類に分類します。

### 3.1 Built-in

ファームウェアへ直接内蔵する音源です。

インターネット、Webファイラー、SDカードを使わなくても利用できます。

Built-inはさらに次の2種類に分けます。

#### Factory Kit

- 起動時またはKit Reset時にPadへ配置される音源
- 初心者がすぐに演奏を始められる
- 音の役割が分かりやすい
- 少数精鋭
- 製品の第一印象を決める

#### Built-in Library

- 本体のファイル選択メニューから選べる追加音源
- Factory Kitには入らないが、オフラインで用意する価値が高い
- Bass、Lead、Chord素材、電子音、効果音などを含む

### 3.2 Official Web Library

Webファイラー上で公式提供する音源ライブラリです。

ユーザーはWebファイラーから好きな音源を選び、SDカードへ保存して使用します。

次のすべてをOfficial Web Libraryへ含められます。

- 追加Sample
- 追加Synth Source
- 追加Drum Kit
- ジャンル別Kit
- Audio Beat
- Chop素材
- 長いフレーズ
- Voice
- 特殊効果音
- テーマ別、アーティスト別、OEM向け音源
- Built-in音源のロング版、高音質版、別バリエーション

Web OptionとSD Packは別区分にしません。

Official Web Libraryから取得したファイルを、ユーザーがSDカードへ保存して使う一つの仕組みとして扱ってください。

## 4. Built-inとOfficial Web Libraryの選別基準

### Built-inへ入れる音

- 初心者がすぐ楽しめる
- 使用頻度が高い
- 複数ジャンルで使える
- 短く、小容量
- 小型スピーカーでも特徴が分かる
- 他の音と役割が重複しない
- 製品の代表音として価値がある
- FX、Pitch、Repeatで変化させて楽しい
- オフラインでも必要
- 短いPCMとLoop設定で広く使える

### Official Web Libraryへ回す音

- Audio Beat
- 特定ジャンル専用
- 長い音
- 長い残響やReleaseを含む
- 似た音のバリエーション
- Voiceや特殊FX
- Chop用の長尺素材
- 高音質版
- 使用頻度が限られる音
- Built-inへ入れるには容量効率が悪い音

本体に入る容量だから採用するのではなく、本体に入れる価値が特に高い音だけをBuilt-inにしてください。

## 5. Audio Beatの方針

オーディ形式のBeatは`Audio Beat`と呼び、原則としてBuilt-inへ入れません。

- Audio BeatはOfficial Web Libraryで提供する
- ユーザーが選択してSDカードへ保存する
- 本体には容量の小さいPattern Beatを用意する
- Built-in容量はOne Shot、Drum Kit、Synth Source、効果音へ優先的に使う
- 例外的な内蔵Loopを提案する場合は、容量に見合う明確な理由を示す

## 6. Acoustic Drum Kit

Acoustic Drum KitはBuilt-inへ含めます。

Acoustic Drumの音源は、今回のChatGPT Workによる生成対象にはしません。別途、自前で用意する予定です。

ただし、その容量はBuilt-inライブラリ全体の計算へ必ず含めてください。

実ファイルが完成するまで、次の暫定枠を確保してください。

- Acoustic Drum Kit予約容量：約400KB
- 12 Pad相当
- Kick
- Snare
- Side StickまたはRim
- Clapまたは補助Percussion
- Tom Low
- Tom Mid
- Tom High
- HiHat Closed
- HiHat Open
- ShakerまたはTambourine
- Crash
- Ride

実ファイルが提供されたら、予約容量を実測値へ置き換えて全体を再計算してください。

Acoustic Drumの音を今回新たに生成する必要はありません。

## 7. 今回提案・制作する音源

Acoustic Drum以外を対象に、Built-in候補を整理してください。

### 7.1 Electronic Drum

- Electronic Kick
- Electronic Snare
- Clap
- Closed Hat
- Open Hat
- Electronic Tom
- Rim
- Cowbell
- Electronic Percussion
- Chiptune系
- House、Hip-Hop、Electroなどで使いやすい音

生ドラムの模倣ではなく、電子音として分かりやすく楽しい音を優先してください。

### 7.2 Bass

- Sub Bass
- Saw Bass
- Punchy Bass
- 短くてもLoopで自然に伸ばせる音
- 低い音域でも輪郭が分かる音
- 小型スピーカーでも存在を感じられる音

### 7.3 Melody / Lead

- Square Lead
- Sync Lead
- Pluck
- Bell
- Mallet
- 短い波形でも音程演奏しやすい音
- 他パートに埋もれにくい音

### 7.4 Chord / Pad / Keys

- Warm Pad
- Airy Pad
- Organ
- Electric Keys
- 短い素材をLoopして伸ばせる音
- 3〜4音を重ねても破綻しにくい音

### 7.5 Effect / Fun Sound

初心者が押した瞬間に楽しさを感じられる音を優先してください。

例：

- Riser
- Fall
- Laser
- Air Horn
- Impact
- Glitch
- Jump
- Short Voice
- Game Sound
- Transition FX
- コミカルな効果音

似た用途の音を多数用意せず、性格の違う代表音を選んでください。

## 8. 音声仕様

- 内部形式：PCM16 / mono
- 再生出力：48kHz
- 読み込み時に48kHz / PCM16 / monoへ変換可能
- Sampler Pad：12 Padで約5MBのPSRAMを共有
- Long Sample：最大20秒
- Pattern Beat音源：12音、合計約1.5MB
- Pattern Beatの1音：最大2秒
- Audio Beat：最大8秒
- Audio Beatは原則Built-inにしない

## 9. Built-inのFlash容量予算

現在のファーム領域：

- 最大：6,553,600 bytes
- 現行ファーム使用量：5,938,654 bytes
- 現行組み込みWAV：2,186,831 bytes（72ファイル、実時間合計27.319秒）

現行WAVをすべて削除した場合の概算：

- WAVを除くファーム：約3,751,823 bytes
- 理論上の残り：約2,801,777 bytes

ただし、将来の機能追加、コード増加、アラインメント、メタデータの余裕を残す必要があります。

新しいBuilt-in WAV全体の容量は次を基準にしてください。

- 推奨範囲：2.1〜2.3MB
- 暫定上限：2.4MB
- Acoustic Drum Kitを含む
- 最低でも約450〜600KB程度は将来のファーム更新用として残す
- 最終容量は必ず実ファームへ組み込んだビルド結果で判断する

2,186,831 bytesをPCM16 monoの時間へ換算すると、48kHzで約22.8秒、32kHzで約34.2秒、24kHzで約45.6秒、18kHzで約60.7秒です。短いLoop Sourceを18〜32kHz中心で作り、必要な音だけ48kHzにする場合、Built-in全体で約35〜60秒を現実的な制作目標とします。

ここで示す容量はFlash上のBuilt-in Libraryです。演奏時に選択音を展開する5MBのSampler PSRAM Poolとは別予算であり、Built-in全体を同時にPSRAMへ展開しません。

暫定配分：

- Acoustic Drum Kit：約400KB
- Electronic Drum：約300〜450KB
- Bass / Lead / Chord / Synth Source：約800KB〜1.0MB
- Effect / Fun Sound：約350〜500KB
- 調整用余白：約100〜200KB

この配分は固定ではありません。音源の価値と実容量を比較して調整してください。

## 10. 容量を抑える制作方針

### 10.1 One Shot

- 不要な先頭無音を削除
- 不要な末尾を削除
- 短いFade Outでクリックを防止
- Built-inには長い残響を入れない
- 小型スピーカーで聞こえない帯域へ容量を使わない
- 音の特徴が残る最短の長さを探す
- 長いバージョンはOfficial Web Libraryへ回す

### 10.2 Synth Source

- アタック部分だけを必要十分な長さで保持
- Sustain部分は短いLoopで構成
- Releaseは長く録音せず、本体のRelease処理を利用
- Base Noteを明記
- Loop In / Loop Outを明記
- Loop Crossfadeを明記
- Attackを明記
- Release設定を明記
- Base Noteに対する微調律が必要な場合はTuneをcent単位で明記
- FM風、Sync、Metallic、Voice、Lo-Fiなど、短くても個性が出る音を優先

## 11. Built-in候補の評価

各候補を100点満点で評価してください。

- 初心者が押して楽しい：25点
- 複数ジャンルで使える：15点
- 小型スピーカーで明瞭：15点
- 他の音と役割が重複しない：15点
- ファイル容量が小さい：15点
- FXやPitchで変化させて楽しい：10点
- 製品らしい個性がある：5点

Built-in候補は原則70点以上とします。

似た役割の候補が複数ある場合は、評価が高く容量が小さいものを残してください。

残った候補はOfficial Web Libraryへ移せるか検討してください。

## 12. 作業手順

### Phase 1：仕様整理

仕様書から音源制作と容量に関係する条件を抽出してください。

この段階では音源を生成しないでください。

### Phase 2：必要な役割の整理

現在の内蔵WAVはすべて置き換える前提で、製品に必要な音の役割を整理してください。

音色名を大量に並べる前に、必要な役割を決めてください。

### Phase 3：Acoustic Drum容量の確保

Acoustic Drum Kit用に暫定400KBを予約してください。

実ファイル提供後に、実容量へ置き換えてください。

### Phase 4：Built-in候補の選定

候補を次へ分類してください。

- Factory Kit
- Built-in Library
- Official Web Library
- 不採用

### Phase 5：容量シミュレーション

最低3案を作ってください。

#### Minimal

- 必要最小限
- 将来のファーム余裕を大きく残す

#### Recommended

- 製品体験と容量のバランスを優先
- Built-in WAV合計2.1〜2.3MBを目安とする

#### Maximum

- Built-in WAV上限2.4MB以内
- 初心者が楽しめる音を可能な限り収録
- 将来更新用のFlash余白を必ず残す

各案について、次を表示してください。

- Acoustic Drum予約容量
- Electronic Drum容量
- Synth Source容量
- Effect容量
- Built-in WAV合計
- 推定ファーム使用量
- 推定残容量
- ファイル数
- Factory Kitの内容
- Built-in Libraryの内容
- Official Web Libraryへ回す内容

### Phase 6：承認

候補一覧、評価、役割、容量を提示し、ユーザーの承認を得てください。

承認前に大量の音源生成を始めないでください。

### Phase 7：音源制作

承認された音源だけを制作してください。

各音源について次を記録してください。

- ファイル名
- 本体表示名
- Category
- 用途
- 配布区分
- Factory Kit採用の有無
- Sample Rate
- Bit Depth
- Channels
- 長さ
- ファイル容量
- Normalize基準
- Base Note
- Loop In
- Loop Out
- Loop Crossfade
- Release
- 推奨パート

## 13. 最初の回答に含める内容

最初の回答では音源を生成せず、次だけを提示してください。

1. 仕様書から読み取った容量制約
2. Built-inとOfficial Web Libraryの分類ルール
3. 現行WAVをすべて置き換えることの確認
4. Acoustic Drumを別途用意し、容量計算には含めることの確認
5. Built-inに必要な音の役割一覧
6. Factory KitとBuilt-in Libraryの違い
7. Minimal / Recommended / Maximumの容量案
8. 最初に制作すべき音源の優先順位
9. 判断に必要な不足情報

Built-in容量を先に埋めるのではなく、必要な役割を決めてから、その役割に最も適した音を容量内で選んでください。
