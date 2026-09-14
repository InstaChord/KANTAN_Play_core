# WEBファイラー内 KANTANシンセ作成・SD登録 設計

## 目的と境界

- `.sf2` / `.wav` / `.mp3` は `File.arrayBuffer()` でブラウザ内だけに読み込み、元ファイルはネットワークへ送らない。
- Wi-Fiで本体へ送るのは、SF2から選択した1〜2サウンド、または単一のWAV/MP3から生成・再検証済みの最大2 MiBの `.ktsynth` だけとする。
- 変換コアはDOMや本体APIへ依存しないES moduleに分離する。既存Standalone Converterの `sf2.js` / `audio.js` / `ktsynth.js` を移植元とし、`docs/sampler-ui/converter/` をWEBファイラー版の共有元にする。Standalone版はこのモジュールを取り込むか、ビルド時に同期する。コピーした実装を2系統で手修正しない。
- WEBファイラー固有コードは画面状態、Web Audio試聴、`/api/sampler/files/samples/Synth/*` への保存だけを担当する。

## 画面フローと設計判断

KANTANシンセ作成は Sample / Beat / Kit / Project / Music に続く独立した `Synth` タブに置く。WEBファイラーの表示文言は英語で統一し、最初に `Create from SoundFont (.sf2)`、`Create from WAV / MP3`、`Edit a KANTAN Synth File` から選ぶ。変換開始後も入力方法に戻れるようにし、スマートフォンではタブ列を横スクロール可能にして主操作が切れない一列レイアウトにする。

既存のKTS2 v2.1 `.ktsynth` は1〜2レイヤーのPCMと各レイヤー設定を検証後に読み込み、SF2変換後と同じレイヤーカードで音量、サンプルレート、root note、pitch correction、Delay、Attack、Hold、Decay、Sustain、Release、Loop crossfadeを変更して再保存できる。サンプルレート変更時は再生範囲とLoop範囲も同じ比率で変換する。旧v2.0互換出力は行わない。元のSF2にあったプリセット構造、generator、modulator、フィルター、LFO、エフェクトなどはKTS2に含まれないため復元対象外とする。

SF2の簡易設定には SoundFont、音色、使用するサウンド、試聴、音量、音色名、主操作「SDカードに保存」を常時表示する。音量はSF2のinitial attenuationから求めた値を初期値として0〜200%で調整し、試聴と `.ktsynth` の `defaultGainQ8`（0〜512）へ同じ値を反映する。100%を標準とし、100%超では音割れの可能性を案内する。

音色と基準音・代表Velocityに該当する候補が1件なら自動選択するが、サウンド名は表示する。2件以上なら未選択状態にし、最大2件をLayer 1 / Layer 2として選べる候補カードを簡易設定に表示する。

> Choose one or two sounds. The second selection becomes Layer 2. Unsupported SoundFont features are ignored.

各カードはサンプル名と楽器名を主表示し、音域・強さは補足表示に留め、個別の試聴ボタンとチェックボックスを持つ。候補を選ぶまで生成・保存はできない。詳細設定の基準音または代表Velocityで候補が変わった場合、カードを即時再描画し、同一候補が残らない限り選択を解除する。音量、サンプルレート、Loop crossfade、Delay、Attack、Hold、Decay、Sustain、Release、音程補正はLayerごとに独立させ、2層の音量合計は200%以下とする。Layer 2には `Same waveform as Layer 1` / `Different waveform` を表示する。同一Region波形の場合は前者を初期値とし、`pcmSourceLayer=0`でLayer 0のdataを共有してKT2Dを出力しない。別波形は`pcmSourceLayer=1`としてKT2Dへ格納する。

SF2 generatorにKANTANシンセが再現しないフィルター、LFO、エフェクトなどが含まれる場合も変換を止めない。対象名を「変換時に無視される非対応機能」として警告し、対応している波形、root key/tuning、Loop、Envelope、音量だけで保存する。

詳細設定には基準音、代表Velocity、サンプルレート、Loop crossfade、DAHDSR Envelope、音程補正、推定出力サイズを置く。SF2のpreset global/localとinstrument global/localを合成し、timecentsをミリ秒、sustainVolEnvをQ15へ変換する。generator 33/35/36/37/38を実値として出力し、generator 50はendloopAddrsCoarseOffsetとしてLoop終端へ加算する。完了画面には `/sampler/samples/Synth/<name>.ktsynth` と、本体のMelody／Chord／Bassから選べることを示す。PC保存は補助操作とする。

### WAV／MP3の音程と容量

- WAV/MP3は1ファイルから単層KTS2を作る。複数ファイルのレイヤー機能は設けない。
- Web Audio APIでブラウザ内PCMデコードし、モノラルPCM16へ変換する。MVPは全長を使い `sustainMode=off`、Loop値は0とする。
- 20秒超過は自動トリミングせず停止し、短い素材を案内する。2 MiB超過は保存せず、短い素材または低い出力サンプルレートを案内する。MP3はデコード後のPCMを格納するため、元MP3より大きくなることを推定サイズと共に示す。
- 基準ノート候補は、信頼できるWAV `smpl` unity note、ファイル名の明確な音名、音声解析の順で求める。選ばれた出典とconfidenceをUIに示す。
- 音声解析は無音・過渡を避けた複数窓の自己相関を用い、各窓の明瞭度、推定値のばらつき、有効窓数からconfidenceを出す。単一音・安定区間を想定し、打楽器、コード、声、ノイズ、極端に短い素材は低confidenceまたは判定不能とする。
- UI上は「元の音程」と呼び、候補があっても `自動検出：C4（確認してください）` と表示する。音名・オクターブの変更、または「この音程で使う」で初めて確定する。低confidence/判定不能は `音程を判定できませんでした。元の音程を選んでください。` と示し、「C4として使う」を明示的に選べる。
- cent補正は詳細設定に置く。SF2は元のroot key/tuning metadataを正とし、この音声解析を適用しない。

## 保存と障害回復

1. ブラウザで生成後に同じ共通モジュールでRIFF、KNTN、CRC、Loop、範囲、2 MiB制限を再検証する。
2. `Synth` フォルダーが無ければ既存フォルダーAPIで安全に作成する。
3. 初回PUTは上書き禁止で行う。同名なら名前変更または明示的な上書きを選べる状態へ戻す。
4. 本体は `.upload` 一時ファイルへ受信し、ファイル全体を `parse_ktsynth` で検証してから正式名へ原子的に置換する。
5. 通信切断、SD書き込み失敗/容量不足、形式不正は一時ファイルを除去し、具体的なHTTPエラーを返す。UIはHTTP status/messageを「同名」「容量不足」「通信切断」「非対応形式」「検証失敗」の回復可能な案内に変換する。

正式保存前の実機試聴では、同じ変換・再検証済み `.ktsynth` を専用APIへPUTする。本体は `/sampler/samples/Synth/.web-preview.ktsynth` へ検証付きで一時保存し、既存の音声プレビュー経路で再生する。このファイルは通常一覧に表示せず、次回試聴時に置換する。`Audition Converted Sound on Device` はブラウザ内試聴の直下に置き、正式な `Save to SD Card` と区別する。

Preview/demoでは実ファイル解析も利用でき、SD保存だけを模擬して完了画面まで確認できる。

## 検証

- 変換コア: SF2の複数候補、明示選択、WAV unity note/ファイル名解析、複数窓pitch confidence、リサンプル、KTSYNTH round-trip、CRC破損、20秒/2 MiB超過。
- UI: 必須項目、複数候補警告、候補別試聴、上書き回復、完了案内、キーボード操作を静的/ブラウザテストで確認する。
- 本体: 既存 `parse_ktsynth` のホストテストとSamplerビルドで受信後検証を確認する。
- 回帰: 既存のSample一覧、通常Upload、Pad Assign、他タブを変更せずに通す。
