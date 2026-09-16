# KANTAN Sampler Product Specification

- 最終同期日: 2026-09-16
- 同期確認バージョン: 0.8.8

この文書は、KANTAN Samplerのマニュアル、広告、Webサイト、製品紹介で使用する
**製品仕様の正本**です。ユーザーが触れる名称、操作、対応形式、制限はこの文書を優先します。

実装内部のデータ構造や処理方式は[プログラム仕様](./program-spec.md)、ビルドと書き込みは
[開発ガイド](./development.md)を参照してください。現在のバージョン番号は文書へ固定せず、
[`main/sampler/sampler_version.hpp`](../../../main/sampler/sampler_version.hpp)を参照してください。

## 文書利用ルール

- 本文に記載された機能は、原則として現行ファームウェアへ実装済みです。
- 「今後の候補」にある内容を、現行製品の機能として広告しないでください。
- 表示名は、原則として実機画面の英語表記を使用します。
- 製品説明では内部用語のAudio/Patternを必要以上に強調せず、どちらも`Beat`として説明します。
- `Rec`はループへ演奏を記録する機能、`Sample Recording`はPadへの録音、
  `Performance Recording`は最終演奏をWAVへ保存する機能として区別します。
- `BGM`は旧称とし、現行の実機UI、File Editor、マニュアル、Webサイトでは使用しません。
- この文書と実機動作に差が見つかった場合は、推測で補わず仕様書を更新してください。

## 用語の正本

Webサイトとマニュアルは、次の意味で用語を統一します。

| 用語 | 意味 |
|---|---|
| `Beat` | リズムの土台。Pattern Beat、または繰り返し再生するWAV / MP3のAudio Beat |
| `Rec` | Pad演奏を記録し、Loop再生する機能 |
| `Loop` | Recで記録した演奏データとその繰り返し再生。Beat素材の名称には使わない |
| `Music` | Music Playerで再生する曲全体のWAV / MP3。Beatとは別の音源 |
| `Sample Repeat` | 1つのSample PadをWhole SampleまたはGrid単位で反復する再生方式 |
| `Sustain Loop` | Sample Synthで波形の一部を循環再生し、音を伸ばす機能 |

SDカードの`/sampler/loops/`や旧Projectの`loop.background`など、互換性のために残る
内部パス・JSONキーはこの表記ルールの対象外です。これらをユーザー向け名称として表示しないでください。

## 製品コンセプト

KANTAN Samplerは、DAWを小さくした機械ではなく、音を録り、鳴らし、重ね、変化させながら
音楽が生まれる過程を楽しむ小型電子楽器です。

中心となる価値は次のとおりです。

- 楽器や音楽制作の知識がなくても、Beatに合わせて演奏がまとまる
- マイク、SDカード、内蔵音源、外部MIDI機器から音楽を始められる
- Sampler、Bass、Melody、Chord、Beatを1台で重ねられる
- 停止して細かく編集するより、演奏しながら録音、Mute、FXで展開を作る
- BPMを常に意識せず、曲を64分割したGridを基準に操作できる
- 初心者向けの分かりやすさを保ちながら、上級者のライブ演奏にも応える

想定ユーザーは、サンプラー未経験者、若いクリエイター、Lo-fiやBeat cultureに興味がある人、
楽器経験がなくても音楽を演奏してみたい人です。

## ハードウェア

- M5Stack CoreS3 SE / ESP32-S3
- 12個のメインPad（4列x3段）
- 3個のFnボタン
- SOUND / PLAY / REC / FXの4モードボタン
- ロータリーエンコーダーと押し込み操作
- ジョグダイヤル
- 右側面レバー
- タッチパネル
- 加速度センサー
- 内蔵マイク
- 外部音声入力対応端子
- スピーカー、ヘッドホン出力
- microSDカード
- USB、BLE MIDI、Grove端子
- Pad LED

## 画面と操作の全体構造

本機には、音を重ねる**5つのパート**と、操作目的を切り替える**4つのモード**があります。

### 5つのパート

ジョグダイヤル、エンコーダー、左右サイドボタンで切り替えます。

1. `BEAT`
2. `SAMPLER`
3. `BASS`
4. `MELODY`
5. `CHORD`

パート選択ウィンドウは5パートを縦に表示し、選択候補を大きな反転表示で示します。
候補表示中に演奏操作を行うとウィンドウを閉じ、演奏を優先します。

### 4つのモード

1. `SOUND`: 音源の選択、録音、編集
2. `PLAY`: 自由演奏
3. `REC`: ループへの演奏記録
4. `FX`: リアルタイムエフェクトとMixer

パートとモードは独立しています。たとえば`BASS + SOUND`ではBass音源を選び、
`BASS + PLAY`ではBassを演奏します。パートを切り替えた場合は、誤編集を防ぐためPLAYへ戻ります。

### 共通操作

- Enc1回転: マスター音量
- Enc1押し込み: 全音停止
- ジョグダイヤル / Enc2 / Enc3: 通常はパート選択、メニュー中は項目操作、FX/MixerのPad保持中はその値変更
- 左右サイドボタン: パート移動。端では循環しません
- モードボタン: SOUND / PLAY / REC / FX切替
- メニュー表示中は、画面上のモード切替タッチを無効にします

## パート仕様

### BEAT

曲のリズムとループの長さを決めるパートです。ユーザーには1つのBeatとして見せますが、
内部では次のどちらか一方を使用します。

- Audio Beat: WAVまたはMP3のリズム音源
- Pattern Beat: 12個の短いドラム音源と演奏パターン

Audio BeatとPattern Beatは排他的で、同時に2レイヤーとして扱いません。新しいBeatを選ぶと、
必要に応じて既存Recを新しい長さへ追随、現在のTempoを維持、またはRecを消去できます。

#### Pattern Beat

- 内蔵パターン: Disco / Pop / Rock / House / Hip Hop / Break / Funk / Reggae
- 内蔵Kit: Acoustic / Dance / Chiptune
- SDからStandard MIDI File（`.mid` / `.midi`）を読み込み可能
- Pad演奏で新しいPatternを作成可能
- PatternはNoteごとのVelocity `1〜127`を保持。未指定および本体Pad入力は110、127をAccentとして使用
- Tap Tempoと0.5 BPM単位の微調整
- Swing: 0 / 25 / 50 / 75 / 100%。100%は3連符相当
- Half Speed / Double Speed
- Half Time / Double Time
- Beat Repeat: 1 / 2 / 4

#### Audio Beat

- 対応: PCM WAV、MP3、mono / stereo
- 読み込み時に48kHz / mono / PCM16へ変換
- 最大8秒
- Beat Repeatを含む長さをRecの基準に使用
- Half Speed / Double Speedに対応。速度とともに音程も変化
- Sampler Padの現在のStart / End / Reverse範囲からBeatを作成可能
- 音程情報が十分な素材ではKeyを推定し、Bass / Melody / Chordへ反映
- ドラムのみなど判定が不確かな素材では、現在のKeyを変更しない

Beat選択画面では候補を試聴できます。Patternは元のTempoで1周、Audioは短いプレビューを再生し、
現在のProjectやBeat Kitを置き換える前に確認できます。

### SAMPLER

12個のPadへ音声を割り当て、演奏する中心パートです。

- 内蔵Sample、SD上のWAV / MP3、マイク録音に対応
- 最大20秒のLong Sampleに対応
- 12 Pad全体で約5MBのPCMメモリ予算を共有
- 長いChop元素材と短い効果音を、固定スロット長ではなく共有メモリで管理
- PadごとにStart、End、Volume、Pitch、Reverse、Hold、Repeatを保持
- パートVolume: 0～100%、5%単位。上下限で循環しない
- サンプル削除時はSynth、Repeat、Chokeなど関連パラメーターも初期化
- Pad移動、トリミングコピー、Mixに対応

長尺素材をChopしたSliceは、共有PCMを参照してメモリを節約します。元Padを削除してもSliceが
残っている間は必要なPCMを保持し、最後の参照がなくなった時点で解放します。

### BASS

- Key / Scaleに沿った12音を演奏
- Melodyより1オクターブ低い位置をOctave 0として扱う
- モノフォニック。新しい音を押すと前のBass音を置き換える
- General MIDIまたは通常Sampleを音源に選択可能
- Octave: -2 / -1 / 0 / +1 / +2
- Volume: 0～100%、5%単位。上下限で循環しない
- `Pitch Bend`: `1 Semitone / 1 Octave`。初期値は`1 Semitone`
- レバーを倒すと設定幅まで滑らかに変化し、中央復帰で原音へ戻る
- PLAY中のTOUCH操作に対応

### MELODY

- Key / Scaleに沿った12音を演奏
- General MIDIまたは通常Sampleを音源に選択可能
- Octave: -2 / -1 / 0 / +1 / +2
- Volume: 0～100%、5%単位。上下限で循環しない
- `Pitch Bend`: `1 Semitone / 1 Octave`。初期値は`1 Semitone`
- レバーを倒すと設定幅まで滑らかに変化し、中央復帰で原音へ戻る
- PLAY中のTOUCH操作に対応

### CHORD

- 7つの度数とModifierを組み合わせて4音のClose Voicingを演奏
- ルートPadを押した瞬間に、押されているModifierを判定
- Modifierだけを押しても発音、Note On、Rec開始は行わない
- General MIDIまたは通常Sampleを音源に選択可能
- Octave: -2 / -1 / 0 / +1 / +2
- Volume: 0～100%、5%単位。上下限で循環しない
- Scaleに応じてMajor、Minor、dim、Dominant 7などの基本コードセットを自動選択
- Swap、7th、sus4、9th、M7を利用可能
- Pad上に現在のKeyに応じたコード記号を表示

### Key / Scale / Tuning

Bass、Melody、Chordは共通のKeyとScaleを使用します。Key / Scale変更時は、Pad配色、
コード名、TOUCH画面、画面キャッシュをまとめて更新します。

- Key: 12キー
- Scale: Pentatonic、Major、Chromatic、Blues、Minor Pentatonic、Hirajoshi、
  Dorian、Mixolydianなど
- Tuning: A=425～455 Hz、1Hz単位
- 内部のFine Tuningは0.1 cent単位でProjectへ保存
- ChopやAudio Beatの速度変更で生じる半音未満のずれを、自動的にFine Tuningへ反映
- General MIDIとSampleベースのBass / Melody / Chordへ同じ調律を適用
- Pitch Bendとは別パラメーターとして保持
- Clear ProjectとReset AllではA=440 Hzへ戻す

録音音声そのもののFine Tuning推定は行いません。自動Fine Tuningは、既知の速度変更比から
正確に計算できる場合だけ適用します。

## SOUNDモード

現在のパートの音源を選択、確認、編集するモードです。

### 共通表示

- 選択中Padを白枠で表示
- 発音中はパート色の枠を優先し、再生終了後に白枠へ戻す
- Fn1: 選択音のPreview / Stop
- Fn2: 編集確定時のOK
- Fn3: SamplerではDelete、編集画面ではExit
- SOUND中は複数の試聴音を重ねず、新しい試聴時に前の音を停止

### 空Padへの追加

- SOUNDでは空Padを選び、案内に従ってImportまたはRecordへ進む
- PLAYでは誤操作を避けるため、空Padの長押し完了後に追加操作へ進む
- ImportとRecordの選択待機中は、案内を自動消去せず操作完了まで表示
- 録音準備は選択待機中に開始し、押下開始付近から取り込めるようプリロールを使用
- 録音開始・終了の物理クリックを避けるため、自動Cropと短い端処理を適用
- 録音後は32kHz入力を48kHz再生用PCMとして正しく変換

### Sampler編集

Samplerのサンプル入りPadは、選択、再タップ、同時押しを使い分けます。

- 1回目: 選択とPreview
- 同じPadをもう一度短く押す: Edit
- Padを保持しながら別Padを押す: Move / Copy
- Fn3を押しながらPadを選択: Delete確認。選択枠と波形も対象Padへ移す

Editでは次を使用できます。

- Preview / Stop
- Start / End
- Volume
- Pitch
- Hold
- Repeat: None / Whole Sample / 8 / 4 / 2 / 1 / 0.5 Grid
- Reverse
- Choke Group
- Synth
- Chop
- Melody / Bass / Chordへの割り当て、解除
- Delete

Start / End編集中にPreviewを再生し、目的位置でStart / End Padを押すと現在位置を設定できます。
Start / Endを長押しすると、その境界を解除します。確定後の処理中は待機画面を表示します。

### Sample Synth

通常SampleをBass / Melody / Chordの音源として使用できます。

- Pad Base Noteを自動推定または手動設定
- Attack後の安定区間を解析し、適性がある素材だけSustain Loopを使用
- Attack、Loop In / Out、Release、サンプル固有のTuneを編集可能
- Release: 10 / 50 / 100 / 200 / 500 / 750 / 1000 / 1250 / 1500 ms
- Loop点はゼロクロスだけでなく、前後波形の類似性も使って選択
- 音程変化、音色変化、減衰が大きい素材は無理にLoopしない

Chop Sliceは拍頭AnchorとChoke処理を持つリズム素材のため、Synth音源には割り当てません。
SliceのStart / End、Pitch、Reverse、再Chopも無効にし、タイミング情報を保護します。

### Chop

選択したStart / End範囲を分割し、Sampler Padへ配置します。

- 固定数: 4 / 8 / 12
- Manual Count: 4～12をエンコーダーで選択
- Tap Cut: 再生中にタップした位置で最大12個へ分割
- FIT: 現在のBeat / Loopに速度を合わせる
- KEEP SPEED: 元の速度を保つ
- 分割線を波形上へ表示
- 通常分割では全Sliceの拍間隔を均等にし、最後だけ余りを許容
- Start付近の拍頭を小範囲で補正し、最後の余白や不正確なEndを吸収
- Sliceの前後に短い重なりとフェードを作り、拍頭Anchorを基準にRecへ配置
- Chop元Sampleと生成Sliceを同じChoke Groupへ入れる
- Chop完了後に、終了またはMake Loopを選択
- Make Loopは先頭4個または8個を原曲順にRecへ配置し、PLAYへ戻る

Chop素材を現在のBeatへ追随させる場合、PCMを再変換せず再生倍率を保存します。速度変化に応じて
KeyとFine Tuningも追随します。元のLong Assetが残っている場合は、後のTempo変更でもSliceを
再構成できます。

### Beat SOUND

Pattern Beatの12音について、Sampler SOUNDと同じ選択、Preview、Import、Record、Move、Copy、
Mix、Delete操作を使用します。Beat Padは最大2秒です。

次の機能はBeat Padには適用しません。

- Synth設定
- Chop
- Melody / Bass / Chord音源への割り当て

### Bass / Melody / Chord SOUND

Samplerの12音から音源候補を選びます。

- 1回目のタップ: 選択、単音Preview、確認メッセージ
- 2回目のタップ: 現在パートへ割り当て
- 編集へ入った場合はSamplerと共通の波形編集画面を使用
- 空PadからImport / Recordも可能
- Chop Sliceは暗く表示し、選択不可

## PLAYモード

- Recへ追加せず、現在パートを自由演奏
- ループ再生中も生演奏を重ねられる
- Fn1: Loop Play / Stop。ProjectメニューでPerformance RecordingをOnにした場合は録音開始 / 停止
- Fn2: 現在パートの記録済みシーケンスMute
- Melody / BassのFn3: TOUCH
- Mute中も生演奏は鳴るため、記録演奏と生演奏を切り替えられる
- Samplerのライブ波形は発音を優先しつつ更新

### TOUCH

Melody / Bassをタッチ面または本体の傾きで演奏します。

- 縦方向: Scale内の音程
- 横方向: 音色変化。中央を原音とし、左右で効果を強める
- タッチ中は傾き入力を無視
- タッチしていない状態で意図的に本体を動かすと傾き演奏へ移行
- TOUCH中はEnc2 / Enc3によるパート切替を無効化
- Enc1の音量と全停止、レバーPitch Bendは有効
- TOUCH中は現在パートの記録済みシーケンスを一時Muteし、終了時に元へ戻す
- TOUCH演奏はRecへ記録しない

## RECモード

各パートの演奏をLoopへ記録します。BPMを直接指定しなくても、Loop全体をGridへ分割して
タイミングを整えられます。

- Beatがある場合はBeatの長さをLoop基準にする
- BeatもRecも空の場合は、最初の演奏からFn1 `END`までを新しいLoop長にする
- Fn1: END / STOP / PLAY
- Fn2: パートまたはPadの記録シーケンスMute
- Fn3: Delete / Undo
- Quantize: On / Off
- Note Grid: 8 / 16 / 32 / 64 / 128
- Note OffはNote Gridの半分の間隔へ自動設定（内部値16 / 32 / 64 / 128 / 256、UI非表示）
- Swing: 0 / 25 / 50 / 75 / 100%
- Loop長に応じて、約8分音符感を基準にGridを自動選択
- 発音と記録は同じ確定タイミングを共有し、演奏時と再生結果の差を抑える
- レバーRepeatの1 Grid／0.5 Grid演奏は、発音済みの位置を再量子化せずそのまま記録する
- Swing時の0.5 Gridは、選択中Note Gridの長区間／短区間を各々2分割して追随する
- わずかな早押しは次のGridで発音し、遅れた入力は即時発音
- Chop Sliceは拍頭Anchorを使って先行部分を補い、実演奏を記録済みSliceより優先
- Melody / BassのPitch BendもRecへ記録
- パート内の全イベントがなくなっても、他パートのためLoop再生を継続

Delete履歴はパートごとに保持し、ページを移動した時に破棄します。Fn3を短く押して離すとUndo、
長押しすると現在パートの記録を全消去します。個別Pad Deleteを行った場合はUndoを実行しません。

`Clear Rec`は記録シーケンスだけを消し、現在のBeat音源、Pattern Kit、Tempo、Beat Repeatを維持します。

## FXモード

最終ミックスへリアルタイム効果を加えます。FX操作自体はRecイベントへ保存しませんが、
Performance Recordingには結果を含めます。

### FX Pad

- Pad 1～4: Repeat 4 / 2 / 1 / 0.5 Grid
- Pad 5: Filter
- Pad 6: Gater
- Pad 7: Crusher
- Pad 8: Delay
- Musicなし: Pad 9 = Target `BEAT`、Pad 10 = Target `PARTS`
- Music読み込み済み: Pad 9 = Target `LIVE`（Beat＋Parts）、Pad 10 = Target `MUSIC`
- Pad 11: Tempo
- Pad 12: Tape Stop

Targetは左右のどちらか、または両方を選択できます。Music読み込み中は、
本機の演奏全体を`LIVE`、SDの楽曲を`MUSIC`とする2デッキの考え方で操作します。
Musicの再生・停止では表示を変えず、ファイルが読み込まれている間は`LIVE / MUSIC`を維持します。
Target PadはFXトリガと異なる紫系ランプ表現を使い、現在の対象を常時表示します。

FX Padを押していない時のEnc2 / Enc3は、他モードと同じパート選択です。
パラメータ値は対応するFX Padを押している間だけ変更できます。
待機画面はパラメータ一覧を表示せず、FXと現在のTargetだけを表示します。

### エフェクト

- Repeat: 押した位置から最終ミックスをGrid単位で反復
- Filter: 下でLow Pass、上でHigh Pass。中央は効果が分かる中程度のLow Pass
- Gater: 下端は原音、中央からGrid同期のゲートが明確にかかり、上ほど細かくなる
- Crusher: 下端は原音、中央で明確な8bit風、上ほどビット低減と間引きを強くする
- Tempo: テープ速度のように滑らかに加速、減速し、解除後は約1秒以内で本来位置へ復帰
- Tape Stop: 急ブレーキのように速度と音程を落として停止
- Delay: 4 / 2 / 1 / 0.5 Grid、WetとFeedbackは演奏向け固定値
- Scratch: 右側面レバーで最終ミックスの履歴を前後操作

Scratch、Repeat、Tape Stop、Delayは共通Deck Bufferを排他的に利用し、メモリを節約します。
エフェクト中も本来のLoop時計は進み、解除時は現在位置へ滑らかに戻ります。

Filter、Gater、Crusher、Delayを押している間は、画面の上下位置、Enc2 / Enc3、または本体の上下傾斜で値を変更できます。
画面タッチ中は姿勢入力を無視し、エンコーダー操作後もその押下中の姿勢入力を無効にします。
Tempoは専用メーターと`TURN DIAL`を表示し、Enc2 / Enc3だけで操作します。
途中でエンコーダーを操作した場合は、誤操作を避けるため姿勢入力を一時無効にします。

### Mixer

FX中にFn3でMixerへ切り替えます。もう一度Fn3を押すまで維持し、両手で操作できます。

- Pad 1～3: BEAT / SAMPLER / BASS
- Pad 5～7: MELODY / CHORD / MUSIC
- フェーダー表示は横3等分とし、上段にMELODY / CHORD / MUSIC、下段にBEAT / SAMPLER / BASSを配置
- Part Padを押していない時のEnc2 / Enc3: パート選択
- Part Pad短押し: 記録済みシーケンスMute
- Part Padを保持してEnc2 / Enc3: 5%単位のVolume
- Part Padを保持して本体を左右に振る: 3度ごと5%
- Mute中に上方向へVolume操作: 0%からFade In
- Pad 9～12: Mix 1～4の保存、呼び出し
- Loop停止時はMixer Volumeを全パート100%へ戻す

Mixer Muteは音源全体を無音にせず、記録済みシーケンスだけを止めます。Padや外部機器からの
生演奏は鳴るため、記録演奏からライブ演奏へ即時に切り替えられます。

## Performance Recording

Projectメニューで`Performance Recording`をOnにすると録音待機状態になり、PLAYまたはFXのFn1で
Loop再生と最終ミックス録音を開始します。もう一度Fn1を押して停止します。

- SDカードが必要。Music PlayerでMusicを読み込んでいる間はOnにできない
- 待機中は録音待機マーク、録音中は赤い録音マークをステータスバーへ表示
- Beat、Sampler、Bass、Melody、Chord、Scratch、Repeat、Filter、Delayなど最終出力をWAVへ保存
- 停止後は確認状態に入り、短押しで保存、長押しで一時録音を削除
- 保存先: `/sampler/recordings/Performance_NNN.wav`
- 起動時は必ず通常Playへ戻る

## ファイルと保存

### Sample Kit

12個のSampler音源とPad編集設定のセットです。

- Load Sample Kit
- Save Sample Kit: Copy / New / Save as Default
- New Kit
- Reset Kit
- Default Kit: `/sampler/kits/Default/Default_Kit.ktkit`（旧JSONは読込互換）

Sample KitはBeat、Rec、FX、Mixer、Key / Scaleを置き換えません。音色セットだけを交換できます。

### Project

楽曲全体を保存する単位です。

- Sampler波形と編集設定
- Beat形式、AudioまたはPattern音源
- RecシーケンスとLoop長
- Key / Scale / Fine Tuning
- Bass / Melody / Chordの音源、Octave、Volume
- FX値とTarget
- MixerとMix 1～4

Input Assign、Wi-Fi、画面輝度などの本体設定はProjectへ含めません。

- Load
- Save
- File Editor
- Clear Project
- 保存先: `/sampler/projects/`

Loadの先頭に内蔵Project `DISCO Beat` を表示します。Clear Projectは確認後に音楽データを空にし、Acoustic Kitだけを読み込みます。Wi-Fi、外部入力、
Input Assign、本体設定、SD上の保存済みファイルは消しません。

### Resume

電源を切っても直前状態を復元するため、内部メモリとSDのセッション領域を使用します。
録音SampleはSD装着時に一時保存し、Projectを明示保存していなくても次回起動時に復元します。
SDがない場合、未保存録音は電源断後に復元できません。

### 対応ファイル

- Sample: WAV / MP3
- Audio Beat: WAV / MP3
- Pattern Beat: MID / MIDI
- Project: JSONと対応する`_assets`フォルダ
- Sample Kit / Beat Kit: 設定とユーザーPCMを含む単一`.ktkit`パッケージ。内蔵音源はID参照
- Music Player: WAV / MP3（SDからストリーミング再生）
- 内部PCM: mono / PCM16
- 再生出力: 48kHz

## Music Player

SDカードの`/sampler/music/`に保存したWAVまたはMP3を、曲全体をRAMへ展開せずに再生します。

- Track Volume: 0～100%、5%単位（初期値80%）
- Load Music
- Play / Pause
- Rewind 10 sec / Forward 10 sec
- Stop
- File Editor

WAV / MP3のmono / stereoを受け付け、48kHz stereoへ変換して出力します。MP3のシーク位置は
ビットレートから求める近似位置です。Music PlayerとPerformance Recordingの同時使用は行いません。

Music出力は高音圧の楽曲にヘッドルームを作るため、Track VolumeとMixerがともに
100%でも元データの約60%とします。Track VolumeはProjectに保存される基本音量、
MixerのMUSICは演奏中の一時的な相対音量です。

Music読み込み時に既存のBeat / Recがある場合は、Musicの検出周期にそのまま
ループ長を縮めず、検出周期の2倍・4倍または1/2・1/4を候補にして、
現在の演奏テンポに最も近い組み合わせを選びます。現在のRecが短い場合は、
Beat Repeatを2倍・4倍に増やし、Beatだけでなく全パートの記録を繰り返します。
最後に残った小さな差だけを伸縮し、既存のリズムを大きく速めたり遅くしたりしません。

## SDカードの安全な取り外しと再読込

`System > SD Card`を、`Recording Input` / `Menu Sound`の後、`Display`の前に置きます。
画面には`READY` / `BUSY` / `NOT INSERTED` / `SAFE TO REMOVE` / `ERROR`を表示し、
装着中の主操作は`Eject SD Card`、それ以外は`Load SD Card`とします。

`Eject SD Card`は二度押しで確認します。MusicのSDストリームとメニュープレビューを閉じ、
ファイル一覧を無効化してアンマウントした後、`SD CARD / SAFE TO REMOVE`を大きく表示します。
Performance Recording中または録音結果の保存待ち、Project / Kit等のファイル処理中、
File Editor使用中は、処理理由を添えた`BUSY`表示でEjectを拒否します。

Eject後も、すでにRAM / PSRAMへ読み込まれたSample、Audio Beat、KANTAN Synth、Recイベントは
演奏できます。Music、SDからのLoad / Save、File Editor、Performance Recordingは利用できません。
ファイルパスはProject状態として保持できますが、古いファイルハンドルと一覧は再利用しません。

カードを挿し直した後は`Load SD Card`を明示的に実行します。古い媒体状態を破棄してマウントし、
標準フォルダーを確認した後、一覧を次回表示時に作り直します。同じカードでも別カードでも、
現在RAM上で演奏しているProject / Kitを自動的に差し替えません。起動時にカードがない場合も、
後から挿してこの操作で認識できます。

予期しないI/O失敗では新しいSD処理を停止し、MusicとPerformance Recordingを安全側へ止め、
同じエラーの連続表示や自動再マウントを行いません。復旧は`System > SD Card > Load SD Card`から行います。
これは任意のタイミングのホットスワップを保証する機能ではありません。書込み中の物理抜去による
ファイル破損を完全には防げないため、必ずEject完了表示を待ってから抜くよう案内します。

## File Editor

各ファイル一覧では、PCから複数ファイルを選択するか、アップロード領域へまとめてドラッグ＆ドロップできます。
ファイルはSDカード保護のため1件ずつ順番に保存し、画面には全体の進捗と処理中のファイル名を表示します。

本体をWi-Fiファイルサーバーとして動作させ、スマートフォンやPCからファイルを管理します。
File Editorはファイル管理を中心とし、演奏状態やRec設定を操作するWebコントローラーにはしません。

- Sample、Beat、Kit、Project、Musicの一覧、Upload、Download、Rename、Delete
- Sampleの参照元を `Device Preset`、`SD / Samples`、すべてのSD下位フォルダーから1つ選び、選択中の場所だけを一覧表示
- 選択したPadへのSample割り当てとSampleのPreview
- Start / End、Pitch、Volume、RepeatなどのSample編集は本体で行う
- BeatのLoad / Clear / Volume、Rec設定、ProjectのSave / Load / Clearなど、演奏状態を変える操作は本体で行う
- `.ktkit`は単一ファイルとしてUpload / Download / Rename / Deleteする
- 旧JSON Kit / Project削除時は対応する`_assets`フォルダも削除し、Rename時はJSON内のAsset参照も更新する
- Upload中は操作付近に進行表示を出し、完了まで二重操作を防止
- スマートフォン向けレスポンシブレイアウト
- 実機がない場合はGitHub Pages上のDemo UIでレイアウトを確認可能

将来Webから演奏状態を操作する場合は、File Editorへ混在させず、別のWeb Controllerとして設計します。

File Editor中は本体画面描画と演奏処理を抑えます。Enc1の音量と全停止は有効、Enc2 / Enc3は
無視します。その他の本体操作を行うとFile Editorを終了します。SD操作を開始する場合は、
再生音を停止してファイル整合性を優先します。

## Wi-Fi

Wi-Fiは演奏中の常用機能ではなく、UpdateとFile Editorに使用します。

- Wi-Fi Setup: Use Smartphone / WPS
- Update
- File Editor
- Auto Update
- Wi-Fi Info

### Smartphone Setup

1. 本体が`kanplay-ap`を起動
2. 画面全体へ接続用QR、SSID、Password、案内を表示
3. スマートフォン接続後、設定サーバー準備中画面を表示
4. サーバー準備完了後に設定ページQRへ切替
5. SSIDとPasswordを送信
6. Wi-Fi接続とインターネット接続を分けて結果表示
7. 完了または中断時に通常画面を完全再描画

### Update

- 起動後のアイドル時に更新の有無だけを確認
- 更新がある場合はステータスバーへ黄色の`UP!`
- Update実行時だけWi-Fi接続、カタログ取得、ダウンロード、書き込みを行う
- ダウンロード開始前など安全な段階では、本体操作で中止可能
- 書き込み開始後は操作を拒否
- Wi-Fi接続失敗とCatalog取得失敗を区別して表示
- Wi-Fi処理中は画面キャッシュを解放してPSRAMを確保し、終了後に再構築

## 外部入力

### Input Source

択一で選択します。USB Host系は必要な場合だけ再起動します。

- Off
- USB MIDI Controller
- USB MIDI Computer
- USB Keyboard
- BLE MIDI
- USB Gamepad

Port C MIDIとPort A拡張ボタンは、選択式Input Sourceとは別に受信します。

### MIDI Note Action

- Auto: Assign済みNoteは操作、未Assign Noteは演奏
- Play: Assignを無視して演奏
- Control: MIDI演奏を行わずAssign操作だけを実行

### Input Assign

- MIDI Note / CC
- BLE MIDI
- USB MIDI Controller / Computer
- USB Keyboard
- USB Gamepadのデジタルボタン
- Port A拡張ボタン、最大32入力
- Pad、Fn、モード、停止などへLearn
- Assign Listから個別削除
- Learn待機中はBack / Exitを使用可能

同じ入力を別Targetへ割り当てた場合は新しいTargetを優先します。USB GamepadのAnalog値は無視し、
ボタンと方向入力のON / OFFだけを扱います。

### BLE MIDI

- 周辺機器をScanし、一覧から接続先を選択
- 接続先名とアドレスを保存
- Scan & Connect / Forget Device / Reset BLE Connection
- Bondingを要求するBLE MIDI機器に対応
- Device Infoで接続方式と取得できた機器名を表示

### USB MIDI Host

一般的なClass Compliant USB MIDI機器に加え、DescriptorやEndpoint構成が異なる一部の
YAMAHA、M-AUDIO機器へ対応する互換処理を持ちます。通常の電源投入ではUSB Hostを解除して
PC接続と充電を優先し、メニュー変更による再起動時だけ選択したHost設定を引き継ぎます。

## 音声と性能

- 出力: 48kHz
- マイク録音: 32kHz、処理後に48kHz再生PCMへ変換
- 最大30ボイス相当を用途別に管理
- 出力直前にピークリミッターを適用
- 製品出力ゲインは175%固定
- 演奏、録音、FX、Wi-Fi処理中は240MHz
- 無操作、無発音が5秒続くと160MHzへ下げ、入力時に即時復帰
- Pad押下直後は発音を描画より優先
- ピアノロール背景、Pad波形、PLAY画面、TOUCH画面をPSRAMへキャッシュ
- Wi-FiとBLE使用時は画面キャッシュを必要に応じて解放、再生成
- Loop再生イベントは画面描画とは別の高優先処理で発火
- SampleとBeatの読み込み、録音変換、Chop、Mix中は全画面待機表示を使用

## 初期状態

- Beat: 内蔵Pop Pattern
- Beat Kit: Acoustic
- Beat Volume: 100%
- Beat Repeat: 2
- Quantize: On
- Note Grid: 32
- Note Off Grid（内部）: 64
- Swing: 0%
- Melody: GM 82 Lead 2 (sawtooth)、Octave 0、Volume 80%
- Chord: GM 91 Pad 3 (polysynth)、Octave 0、Volume 60%
- Bass: GM 39 Synth Bass 1、Octave 0（Melodyより1オクターブ低い）、Volume 80%
- Key: C
- Scale: Pentatonic
- Tuning: A=440 Hz
- Sampler Pad 9～12: 空
- 外部入力: Off
- 製品出力ゲイン: 175%

## Reset

### Clear Project

Sampler、Beat、Rec、Key / Scale / Tuning、シンセ設定、FX、Mixerを初期状態へ戻します。
Wi-Fi、外部入力、Input Assign、本体設定、SD上の保存済みProjectは維持します。

### Reset Kit

SD上にDefault Kitがあれば読み込み、なければ消去できない内蔵Kitへ戻します。Defaultを読み込んだ
場合も現在Kit名は`NEW_KIT`として扱い、誤上書きを防ぎます。

### Reset All

- Clear Rec
- Reset Kit
- Beat、FX、Mixer、Key / Scale / Tuningを初期化
- Default KitをSDから削除
- ファイル選択フォルダを親階層へ戻す
- 外部入力をOffへ戻す
- Wi-Fi設定は保持
- 消去できない内蔵音源と内蔵Kitは維持

## 広告・マニュアルでの表現

推奨する短い説明:

> 音を録って、刻んで、重ねて、壊す。Beat、Sampler、Bass、Melody、Chordを直感的に演奏できる、小さな音遊び楽器。

現行仕様として説明できる主な訴求:

- 好きな音をマイクで録音し、最大12個へChopして演奏できる
- Beatに合わせてChop、Rec、Key、Fine Tuningが追随する
- SampleをBass、Melody、Chord音源として演奏できる
- AudioとPatternを意識せずBeatとして扱える
- Scratch、Repeat、Filter、Tape Stop、Delayをリアルタイム操作できる
- 5パートMixerと4つのMix Sceneでライブ展開を作れる
- 最終演奏をWAVへ録音できる
- USB、BLE MIDI、Keyboard、Gamepad、外部ボタンを割り当てられる
- スマートフォンからWi-Fi設定、ファイル管理、ファームウェア更新ができる

避ける表現:

- DAWと同等の編集機能
- 音程を保ったTime Stretch
- 無制限のサンプル長、ポリフォニー、ファイル容量
- 自動解析が常に正しいという保証
- FX操作をRecシーケンスへ記録できるという説明
- 将来予定のハードウェアや未実装FXを現行機能として紹介すること

## 今後の候補

以下は現行機能として広告しません。

- SAM2695廃止後の標準内蔵Sample Synth音源セット
- 外部Flash / PSRAMを追加した次期ハードウェア
- OEMごとの内蔵音源差し替え
- Reverbなど追加FX
- 加速度センサーへ割り当てる操作の追加
- FX Targetのさらに細かなパート分離
- 複数Sequence Patternを使った曲展開
