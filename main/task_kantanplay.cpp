// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "common_define.hpp"

#include "task_kantanplay.hpp"
#include "system_registry.hpp"

#include "kantan-music/include/KANTANMusic.h"

namespace kanplay_ns {
//-------------------------------------------------------------------------

void task_kantanplay_t::start(void)
{
  memset(_midi_pitch_manage, 0xFF, sizeof(_midi_pitch_manage));

  _current_usec = M5.micros();

#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "kanplay", this);
#else
  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "kanplay", 1024*3, this, def::system::task_priority_kantanplay, &handle, def::system::task_cpu_kantanplay);
  system_registry.player_command.setNotifyTaskHandle(handle);
#endif
}

void task_kantanplay_t::task_func(task_kantanplay_t* me)
{
  for (;;) {
    uint32_t next_usec;
    do {
      me->_prev_usec = me->_current_usec;
      me->_current_usec = M5.micros();
      auto next1 = me->autoProc();
      auto next2 = me->chordProc();
      next_usec = next1 < next2 ? next1 : next2;
    } while (me->commandProccessor());

#if !defined (M5UNIFIED_PC_BUILD)
    if (ulTaskNotifyTake(pdTRUE, 0) == false)
#endif
    {
#if defined (M5UNIFIED_PC_BUILD)
      M5.delay(1);
#else
      int next_msec = (next_usec + 128) >> 10;
      if (next_msec)
      {
        system_registry.task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_KANTANPLAY);
        ulTaskNotifyTake(pdTRUE, next_msec);
        system_registry.task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_KANTANPLAY);
      } else {
        taskYIELD();
      }
      #endif
    }
  }
}

bool task_kantanplay_t::commandProccessor(void)
{
  def::command::command_param_t command_param;
  bool is_pressed;
  if (false == system_registry.player_command.getQueue(&_player_command_history_code, &command_param, &is_pressed))
  { return false; }
// printf("commandProccessor: %d, %d, isPressed %d\n", command_param.getCommand(), command_param.getParam(), is_pressed);
  switch (command_param.getCommand()) {
  default:
    break;
  case def::command::set_velocity:
    if (is_pressed)
    {
      const auto param = command_param.getParam();
      system_registry.runtime_info.setPressVelocity(param);
      _press_velocity = param;
    }
    break;
  case def::command::sound_effect:
    procSoundEffect(command_param, is_pressed);
    break;
  case def::command::chord_degree:
    procChordDegree(command_param, is_pressed);
    break;
  case def::command::note_button:
    procNoteButton(command_param, is_pressed);
    break;
  case def::command::drum_button:
    procDrumButton(command_param, is_pressed);
    break;
  case def::command::chord_beat:
    procChordBeat(command_param, is_pressed);
    break;
  case def::command::chord_step_reset_request:
    procChordStepResetRequest(command_param, is_pressed);
    break;
  case def::command::panic_stop:
    resetStepAndMute();
    break;

  case def::command::autoplay_switch:
    if (is_pressed)
    { // 自動演奏モードのオン・オフのトグル
      auto autoplay_state = system_registry.runtime_info.getChordAutoplayState();

      // OTA実行中はオートプレイ禁止
      if (system_registry.runtime_info.getWiFiOtaProgress() != 0) {
        autoplay_state = def::play::auto_play_mode_t::auto_play_none;
      } else {
        auto iclink_port = system_registry.midi_port_setting.getInstaChordLinkPort();
        auto iclink_style = system_registry.midi_port_setting.getInstaChordLinkStyle();

        // インスタコードリンクのパッド演奏時はビート自動演奏モードに固定扱いとする
        if ((iclink_port != def::command::instachord_link_port_t::iclp_off)
        && (iclink_style == def::command::instachord_link_style_t::icls_pad))
        {
          autoplay_state = def::play::auto_play_mode_t::auto_play_beatmode;
        } else {
          switch (command_param.getParam()) {
            case def::command::autoplay_switch_t::autoplay_toggle:
              if (autoplay_state == def::play::auto_play_mode_t::auto_play_none) {
                autoplay_state = def::play::auto_play_mode_t::auto_play_waiting;
              } else {
                autoplay_state = def::play::auto_play_mode_t::auto_play_none;
              }
              break;
    
            case def::command::autoplay_switch_t::autoplay_start:
              if (autoplay_state != def::play::auto_play_mode_t::auto_play_running) {
                autoplay_state = def::play::auto_play_mode_t::auto_play_waiting;
                system_registry.runtime_info.setChordAutoplayState(autoplay_state);
                system_registry.player_command.addQueue( { def::command::chord_degree, 1 } );
              }
              break;
    
            case def::command::autoplay_switch_t::autoplay_stop:
              autoplay_state = def::play::auto_play_mode_t::auto_play_none;
              break;
          }
        }
      }
      // M5_LOGV("autoplay %d", (int)autoplay);
      system_registry.runtime_info.setChordAutoplayState(autoplay_state);
      if (autoplay_state == def::play::auto_play_mode_t::auto_play_none) {
        resetStepAndMute();
      }
    }
    break;
  }

  return true;
}

// オートプレイ処理
uint32_t task_kantanplay_t::autoProc(void)
{
  uint32_t next_event_timing = INT32_MAX;
  const int progress_usec = (int32_t)(_current_usec - _prev_usec);

  // 入力遅延の許容時間を更新
  _auto_play_input_tolerating_remain_usec -= progress_usec;

  // 自動演奏 (ウラ拍) タイミング判定
  if (_auto_play_offbeat_remain_usec >= 0) {
    int remain_usec = _auto_play_offbeat_remain_usec - progress_usec;
    if (remain_usec < 0) {
      const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
      if (_current_beat_index < step_per_beat - 1) {
        remain_usec += _auto_play_offbeat_cycle_usec[1 & _current_beat_index];
      }
      // オフビートの演奏を行う
      chordBeat(false);
    }
    _auto_play_offbeat_remain_usec = remain_usec;
    if (next_event_timing > remain_usec) {
      next_event_timing = remain_usec;
    }
  }

  // 自動演奏 (オモテ拍) タイミング判定
  if (_auto_play_onbeat_remain_usec >= 0) {
    int remain_usec = _auto_play_onbeat_remain_usec - progress_usec;
    if (remain_usec < 0) {
      _auto_play_input_tolerating_remain_usec = def::app::input_tolerating_msec * 1000 + remain_usec;
      auto autoplay_state = system_registry.runtime_info.getChordAutoplayState();
      if (autoplay_state == def::play::auto_play_mode_t::auto_play_running)
      {
        auto onbeat_cycle_usec = getOnbeatCycleBySongTempo();

        // 曲のテンポ情報に基づいてオンビートのサイクルを更新
        setOnbeatCycle(onbeat_cycle_usec);

        updateOffbeatTiming();

        // 次回オフビートのタイミングを今回のオンビートのズレ分を加味して調整する
        _auto_play_offbeat_remain_usec += remain_usec;

        // 次回オフビートのタイミングを次回イベントのタイミングに反映する
        // (これを忘れると運次第でオフビートのタイミングがずれる)
        if (next_event_timing > _auto_play_offbeat_remain_usec) {
          next_event_timing = _auto_play_offbeat_remain_usec;
        }
    
        // 次回のオンビート自動演奏までの時間を更新する
        remain_usec += onbeat_cycle_usec;

        // オンビートの演奏を行う
        chordBeat(true);
      }
    }
    _auto_play_onbeat_remain_usec = remain_usec;
    if (next_event_timing > remain_usec) {
      next_event_timing = remain_usec;
    }
  }

  return next_event_timing;
}

uint32_t task_kantanplay_t::chordProc(void)
{
  uint32_t next_event_timing = INT32_MAX;
  const int progress_usec = (int32_t)(_current_usec - _prev_usec);

  for (int part = 0; part < def::app::max_chord_part; ++part) {
    bool hit_flg = false;
    for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
      for (int m = 0; m < max_manage_history; ++m) {
        auto manage = &_midi_pitch_manage[part][pitch][m];

        int press_usec = manage->press_usec;
        if (press_usec >= 0) {
          press_usec -= progress_usec;
          if (press_usec < 0) {
            auto note_number = manage->note_number;
            auto midi_ch = manage->midi_ch;
            auto velocity = manage->velocity;
            if (velocity) {
              velocity |= 0x80;
              // system_registry.midi_out_control.setNoteVelocity(midi_ch, note_number, 0);
              system_registry.midi_out_control.setNoteVelocity(midi_ch, note_number, velocity);
              hit_flg = true;
            }
          } else if (next_event_timing > press_usec) {
            next_event_timing = press_usec;
          }
          manage->press_usec = press_usec;
        }

        int release_usec = manage->release_usec;
        if (release_usec >= 0) {
          release_usec -= progress_usec;
          if (release_usec < 0) {
            auto note_number = manage->note_number;
            auto midi_ch = manage->midi_ch;
            // 同じノートナンバーの音が他のピッチで鳴っていない場合は音を停止する
            if (0 == checkOtherPitchNote(part, pitch, midi_ch, note_number)) {
              system_registry.midi_out_control.setNoteVelocity(manage->midi_ch, manage->note_number, 0);
            }
            manage->note_number = 0xFF;
            manage->velocity = 0;
          } else if (next_event_timing > release_usec) {
            next_event_timing = release_usec;
          }
          manage->release_usec = release_usec;
        }
      }
    }
    if (hit_flg) {
      system_registry.runtime_info.hitPartEffect(part);
    }
  }

  // パターン編集モードでない場合
  if (system_registry.runtime_info.getPlayMode() != def::playmode::playmode_t::chord_edit_mode) {
    // アルペジエータのタイムアウト判定 (一定時間たつと強制的に先頭に戻す)
    if (_arpeggio_reset_remain_usec >= 0) {
      _arpeggio_reset_remain_usec -= progress_usec;
      if (_arpeggio_reset_remain_usec < 0) {
        chordStepReset();
      } else {
        if (next_event_timing > _arpeggio_reset_remain_usec) {
          next_event_timing = _arpeggio_reset_remain_usec;
        }
      }
    }
  }

  return next_event_timing;
}

// Degree(度数)ボタン操作時の処理
void task_kantanplay_t::procChordDegree(const def::command::command_param_t& command_param, const bool is_pressed)
{
  const uint8_t degree = command_param.getParam();

  if (is_pressed) { // Degreeボタンを押したタイミングで次のオモテ拍での演奏オプションをセットしておく
    _next_option.degree = degree;
    _next_option.bass_degree = system_registry.chord_play.getChordBassDegree();
  }

  const auto autoplay_state = system_registry.runtime_info.getChordAutoplayState();
  const bool is_auto = autoplay_state == def::play::auto_play_mode_t::auto_play_running;

  int current_degree = system_registry.chord_play.getChordDegree();
  // 現在のDegreeと異なる場合
  if (current_degree != degree) {
    // 別のDegreeのボタンを離した場合は何もしない
    if (!is_pressed) {
      return;
    }
    system_registry.chord_play.setChordDegree(degree);

    if (is_auto && _auto_play_input_tolerating_remain_usec > 0) {
      auto step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
      // 自動演奏でオモテ拍の直後にDegreeボタンが押された場合 (オフビートが鳴る前に押された場合)
      // ユーザーの演奏タイミングが遅れたものと見做してオモテ拍の処理を強制的に行うことで、
      // 演奏サイクルが乱れないようにする。
      _current_beat_index = step_per_beat - 1;
//*
      chordStepAdvance(true);
/*/
      chordStepAdvance();
      chordStepPlay();
//*/
    }
  }

  // オンビート・オフビートそれぞれの自動化判定
  bool auto_on_beat = is_auto;
  bool auto_off_beat = is_auto || (system_registry.user_setting.getOffbeatStyle() == def::play::offbeat_style_t::offbeat_auto);

  bool playflag = ((is_pressed && (!auto_on_beat))  // オモテ拍手動
               || (!is_pressed && !auto_off_beat)); // ウラ拍手動

  if (playflag)
  {
    if (autoplay_state == def::play::auto_play_mode_t::auto_play_none) {
      // 手動演奏の場合はここでステップ進行コマンドを発行する
      // オモテ拍・ウラ拍の区別は is_pressedフラグで行う
      system_registry.player_command.addQueue( { def::command::chord_beat, 0 }, is_pressed );
    } else if (autoplay_state == def::play::auto_play_mode_t::auto_play_waiting) {
      // 自動演奏の開始待ち受け状態の場合はこのタイミングで自動演奏の開始
      _auto_play_onbeat_remain_usec = 0;
      system_registry.runtime_info.setChordAutoplayState(def::play::auto_play_mode_t::auto_play_running);
    }
  }
}

void task_kantanplay_t::procChordBeat(const def::command::command_param_t& command_param, const bool is_pressed)
{
  // is_pressed==trueをオンビート、 falseをオフビート扱いとする
  const auto on_beat = is_pressed;

// この関数が呼ばれるのはユーザーによるDegreeボタン操作時や外部からのパルスがトリガー。
// 自動演奏によるトリガーは含まれない。

  bool offbeat_auto = (system_registry.user_setting.getOffbeatStyle() == def::play::offbeat_style_t::offbeat_auto);

  if (!offbeat_auto) {
    auto iclink_port = system_registry.midi_port_setting.getInstaChordLinkPort();
    auto iclink_style = system_registry.midi_port_setting.getInstaChordLinkStyle();
    // インスタコードリンクのパッド演奏時はウラ拍自動演奏として扱う
    offbeat_auto = (iclink_port != def::command::instachord_link_port_t::iclp_off)
                && (iclink_style == def::command::instachord_link_style_t::icls_pad);
  }

  // オフビートかつウラ拍自動の場合、オフビートコマンドに対する処理は行わず終了する
  if (!on_beat && offbeat_auto) {
    return;
  }

  // 次回の自動演奏タイミングをキャンセルしておく
  _auto_play_onbeat_remain_usec = -1;
  _auto_play_offbeat_remain_usec = -1;

  chordBeat(on_beat);

  if (on_beat) {
    // 自動演奏のサイクルを更新する
    setOnbeatCycle(_current_usec - _reactive_onbeat_usec);
    _reactive_onbeat_usec = _current_usec;

    // オフビートがオートの場合は、ここでオフビートのタイミングを更新する
    if (offbeat_auto) {
      updateOffbeatTiming();
    }
  } else {
    // オフビートがオートでない場合
    if (!offbeat_auto) {
      // 手動だが step per beat が 3以上の場合は、後続のウラ拍を自動演奏にする
      const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
      if (step_per_beat >= 3) {
        auto offbeat_cycle_usec = _current_usec - _reactive_onbeat_usec;
        // ウラ拍のタイミングを更新する
        // _auto_play_offbeat_cycle_usecの配列0と1はスウィングを考慮したオフビートの時間間隔を保持する
        uint32_t step_cycle_usec = offbeat_cycle_usec;
        _auto_play_offbeat_remain_usec = step_cycle_usec;
        _auto_play_offbeat_cycle_usec[0] = step_cycle_usec;
        _auto_play_offbeat_cycle_usec[1] = step_cycle_usec;
      }
    }
  }
}

// ビート処理
void task_kantanplay_t::chordBeat(const bool on_beat)
{
  // 何ステップ進むか調べる (ウラを飛ばしてオモテが連打されるケースに対応するため)
  int advance = calcStepAdvance(on_beat);
  if (advance == 0) return;

  do {
    // アルペジエータのステップを進める
    chordStepAdvance();

    // 現在位置の演奏
    chordStepPlay();
  } while (--advance);
}

void task_kantanplay_t::setOnbeatCycle(int32_t usec)
{
  uint32_t song_tempo = getOnbeatCycleBySongTempo();

  // 一定時間経過後にアルペジエータを先頭に戻す時間を更新する
  _arpeggio_reset_remain_usec = song_tempo * def::app::arpeggio_reset_timeout_beats;

  // 無効値の場合はソングデータのテンポに基づいた値に変更する。
  //  - ステップが強制リセットされた後
  //  - ソングデータのオンビートサイクルの2倍以上の値になっている場合
  //  - チャタリングなどで極端に小さい値の場合
  if ((usec < 16384) || (usec > (int32_t)(song_tempo * def::app::auto_beat_reset_timeout_beats))) {
    usec = song_tempo;
    _reactive_onbeat_usec = _current_usec;
  }
  _reactive_onbeat_cycle_usec = usec;
}

// オンビート演奏の間隔を取得する
int32_t task_kantanplay_t::getOnbeatCycle(void)
{
  if (_reactive_onbeat_cycle_usec >= 16384) { return _reactive_onbeat_cycle_usec;}
  return getOnbeatCycleBySongTempo();
}

// オンビート演奏の間隔を取得する (曲のテンポから計算する)
int32_t task_kantanplay_t::getOnbeatCycleBySongTempo(void)
{
  auto tempo = system_registry.song_data.song_info.getTempo();
  if (tempo < def::app::tempo_bpm_min) { tempo = def::app::tempo_bpm_default; }

  // テンポ値からオンビートの時間間隔を求める
  return (60 * 1000 * 1000 + (tempo >> 1)) / tempo;
}

void task_kantanplay_t::updateOffbeatTiming(void)
{
  // オモテ拍の間隔に基づいてウラ拍のタイミングを計算し更新する
  int32_t onbeat_cycle_usec = getOnbeatCycle();
  const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();

  uint32_t step_cycle_usec = onbeat_cycle_usec / step_per_beat;
  uint32_t swing_0_usec = step_cycle_usec;
  uint32_t swing_1_usec = step_cycle_usec;
  if ((step_per_beat & 1) == 0) { // step_per_beatが2や4の場合はスイングさせる
    // スイングパラメータを確認する
    uint_fast16_t swing = calcSwing_x100();
    swing_0_usec = step_cycle_usec + (step_cycle_usec * swing / 10000);
    swing_1_usec = step_cycle_usec * 2 - swing_0_usec;
  }
  // オフビートの間隔を求める (スイングに対応させる)
  _auto_play_offbeat_remain_usec = swing_0_usec;
  _auto_play_offbeat_cycle_usec[0] = swing_1_usec;
  _auto_play_offbeat_cycle_usec[1] = swing_0_usec;
}

// スイングの計算 (スイングのパラメータに基づいて、オモテ拍側の比率増加分を計算する)
// 得られる値 0 - 3333 (オモテ拍の時間間隔にこの値を掛けて / 10000 するとスイング比率が反映された値になる)
int32_t task_kantanplay_t::calcSwing_x100(void)
{
  uint_fast8_t swing = system_registry.song_data.song_info.getSwing();
  if (swing > def::app::swing_percent_max) { swing = def::app::swing_percent_max; }

  // swing 0のとき on:off 比率 1:1、swing 100のとき on:off 比率 = 2:1 となるようにする
  return (swing * 100 / 3); // 元の値 [0 ～ 100] に対し、得られる値 [0 ～ 3333]
}

// 何ステップ進むか調べる
int32_t task_kantanplay_t::calcStepAdvance(const bool on_beat)
{
  const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
  if (step_per_beat < 1) {
    // データが準備できていない場合は 0 で返す (起動直後の未初期化状態への対策)
    return 0;
  }

  int advance = 1;
  if (on_beat) {
    if (step_per_beat > 1) {
      // オンビートの場合は現在位置から次のオモテ拍の位置までのステップ数を得る
      advance = (((int)step_per_beat - _current_beat_index - 1) % step_per_beat) + 1;
    }
  } else {
    // オフビートの場合、進行先がオンビートのステップに到達してしまう時は進まないように調整する
    if (_current_beat_index >= step_per_beat - 1) {
      _current_beat_index = step_per_beat - 1;
      advance = 0;
    }
  }
  return advance;
}

// アルペジエータのステップを進める
void task_kantanplay_t::chordStepAdvance(bool disable_note_off)
{
  const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
  if (step_per_beat < 1) {
    // データが準備できていない場合は何もせずに返す
    return;
  }

  auto chord_play = &system_registry.chord_play;

  bool on_beat = (++_current_beat_index % step_per_beat) == 0;

  system_registry.working_command.clear( { def::command::chord_degree, _current_option.degree } );
  // printf("DEBUG 1 : %d \n", on_beat);
  if (on_beat)
  { // オンビート (オモテ拍) の場合、アルペジエータを先頭に戻す判定を実施
    _current_beat_index = 0;

    // 先頭に戻すフラグ (強制的に戻す)
    bool force_reset = false;

    // 先頭に戻すフラグ (但しアンカーステップが効く)
    bool normal_reset = false;

    // _current_option と _next_option が違う場合は先頭に戻す (アンカーステップは効く)
    if (_current_option != _next_option) {
      _current_option = _next_option;
      normal_reset = true;
    }
    auto semitone_shift = system_registry.chord_play.getChordSemitone();
    auto bass_semitone_shift = system_registry.chord_play.getChordBassSemitone();
    auto minor_swap = system_registry.chord_play.getChordMinorSwap();
    if (_semitone_shift != semitone_shift
     || _bass_semitone_shift != bass_semitone_shift
     || _minor_swap != minor_swap) {
      _semitone_shift = semitone_shift;
      _bass_semitone_shift = bass_semitone_shift;
      _minor_swap = minor_swap;
      normal_reset = true;
    }


    // ステップのリセット要求があれば先頭に戻す
    if (_step_reset_request) {
      _step_reset_request = false;
      force_reset = true;
      setOnbeatCycle(-1);
    }

    // コードが選ばれていない場合は先頭に戻す
    if (_current_option.degree < 1 || 7 < _current_option.degree)
    {
      force_reset = true;
    } else {
      // 動作中のコードボタンの表示反映
      system_registry.working_command.set( { def::command::chord_degree, _current_option.degree } );
    }

    // スロットが変更になっている場合は先頭に戻す (アンカーステップ無効、強制的に戻す)
    auto slot_index = system_registry.runtime_info.getPlaySlot();
    if (_current_slot_index != slot_index) {
      _current_slot_index = slot_index;
      force_reset = true;
    }
    uint_fast8_t enabledCounter = 0;
    uint_fast8_t firstStepCounter = 0;
// printf("DEBUG 2 : %d \n", step_reset);

    int8_t step_list[def::app::max_chord_part];
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      int_fast8_t current_step = chord_play->getPartStep(i);
      step_list[i] = current_step;

      // 強制的に先頭に戻さない場合は条件を確認する
      auto part = &system_registry.current_slot->chord_part[i];
      auto part_info = &part->part_info;

      const int loop_step = part_info->getLoopStep();

      bool note_off_flag = false;

      if (force_reset)
      { // 強制的に戻す場合
        note_off_flag = true;
        current_step = 0;
      } else {
        int anchor_step = part_info->getAnchorStep();
        // 先頭に戻す(アンカーステップが効く)場合は現在位置を比較
        if (normal_reset && current_step >= anchor_step) {
          // アンカーステップより先に進んでいれば先頭に戻す
          note_off_flag = true;
          current_step = 0;
        } else {
          // 現在位置より先のオモテ拍の位置を求める
          current_step = ((current_step + step_per_beat) / step_per_beat) * step_per_beat;
          // 終端に達していたら先頭に戻す
          if (current_step > loop_step) {
            current_step = 0;
          }
        }
      }

      if (loop_step > 2) {
        if (chord_play->getPartEnable(i)) {
          ++enabledCounter;
          firstStepCounter += (bool)(current_step <= 0);
        }
      }
      if (note_off_flag && !disable_note_off) {
        chordNoteOff(i);
      }
      step_list[i] = current_step;
    }

    bool flgFirstStep = (enabledCounter == 0 || ((firstStepCounter << 1) > enabledCounter));

    for (int i = 0; i < def::app::max_chord_part; ++i) {
      int_fast8_t current_step = step_list[i];

      bool current_enable = chord_play->getPartEnable(i);
      if (flgFirstStep || current_step <= 0) {
        auto part = &system_registry.current_slot->chord_part[i];
        bool next_enable = part->part_info.getEnabled();
        // パートが現在有効かどうかと、次回パートを有効にする指示があるかどうかを比較
        if (current_enable != next_enable) {
          if (flgFirstStep || current_enable) {
            current_enable = next_enable;
            system_registry.chord_play.setPartEnable(i, current_enable);
            chordNoteOff(i);
          }
        }
      }
      // パートが無効化している場合はステップを-1に設定しておく
      if (current_enable == false) {
        current_step = -1;
      }
      system_registry.chord_play.setPartStep(i, current_step);
    }
  }
  else
  { // オフビート (ウラ拍) の場合
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      int_fast8_t current_step = chord_play->getPartStep(i);
      if (chord_play->getPartEnable(i)) {
        current_step = ((current_step / step_per_beat) * step_per_beat) + _current_beat_index;
      }
      system_registry.chord_play.setPartStep(i, current_step);
    }
  }
}

void task_kantanplay_t::chordStepPlay(void)
{
  int degree = _current_option.degree;  //system_registry.chord_play.getChordDegree();
  if (degree < 1 || 7 < degree) {
    // コードが選ばれていない場合は終了
    return;
  }
  int master_key = system_registry.runtime_info.getMasterKey();
  int slot_key = master_key + (int8_t)system_registry.current_slot->slot_info.getKeyOffset();
  while (slot_key < 0) { slot_key += 12; }
  while (slot_key >= 12) { slot_key -= 12; }

  KANTANMusic_GetMidiNoteNumberOptions options;
  KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
  options.minor_swap = _minor_swap;
  options.semitone_shift = _semitone_shift;
  options.modifier = system_registry.chord_play.getChordModifier();
  options.bass_degree =  _current_option.bass_degree;
  options.bass_semitone_shift =  _bass_semitone_shift;

// M5_LOGE("key: %d, minor_swap: %d, modifier: %d, semitone: %d", key, minor_swap, (int)modifier, semitone);
  for (int part = 0; part < def::app::max_chord_part; ++part) {
    bool part_en = system_registry.chord_play.getPartEnable(part);
    uint8_t midi_ch = part;
    auto chord_part = &system_registry.current_slot->chord_part[part];
    auto part_info = &chord_part->part_info;
    options.position = part_info->getPosition();
    options.voicing = part_info->getVoicing();

    int displacement_usec = 1000 * part_info->getStrokeSpeed();
    int autorelease_usec = 1000 * def::app::autorelease_msec;
    int32_t press_usec = 0;
    int step = system_registry.chord_play.getPartStep(part);
    if (step < 0) {
      continue;
    }
    bool flg_use = false;

    int pitch_flow = 1;
    int pitch_index = 0;
    int pitch_last = def::app::max_pitch_with_drum;

    // ドラムパートの場合の処理分岐
    bool is_drum = (part_info->isDrumPart());
    bool mute = false;
    if (is_drum) {
      displacement_usec = 0;
      midi_ch = def::midi::channel_10;
    } else {
      switch (chord_part->arpeggio.getStyle(step))
      {
      default:
      case def::play::arpeggio_style_t::same_time:
        displacement_usec = 0;
        break;

      case def::play::arpeggio_style_t::high_to_low:
        pitch_flow = 1;
        pitch_index = 0;
        pitch_last = def::app::max_pitch_with_drum;
        break;

      case def::play::arpeggio_style_t::low_to_high:
        pitch_flow = -1;
        pitch_index = def::app::max_pitch_with_drum - 1;
        pitch_last = -1;
        break;

      case def::play::arpeggio_style_t::mute:
        mute = true;
        // ミュート処理は同時発音ではなく高速ダウンストロークとして扱う

        // ミュート処理の時はリリースまでの時間はストロークスピードの 2倍とする
        autorelease_usec = displacement_usec * 2;

        // ミュート処理の時はストロークスピードは 1/4 とする。
        displacement_usec >>= 2;
        pitch_flow = -1;
        pitch_index = def::app::max_pitch_with_drum - 1;
        pitch_last = -1;
        break;
      }
    }

    for (; pitch_index != pitch_last; pitch_index += pitch_flow) {
      int velocity = 0;
      if (part_en) { velocity = chord_part->arpeggio.getVelocity(step, pitch_index); }
      if (velocity) {
        if (0 < velocity) {
          velocity = velocity * _press_velocity / 100;
          if (velocity > 127) { velocity = 127; }
          if (velocity < 1) { velocity = 1; }
        }
      } else {
        if (!mute) {
          continue;
        }
        // if (manage->velocity == 0
        //  && manage->midi_ch == midi_ch) {
        //     continue;
        //  }
      }

      uint32_t note = 0;
      if (is_drum) {
        note = system_registry.song_data.chord_part_drum[part].getDrumNoteNumber(pitch_index);
      } else {
        if (pitch_index >= 6) { continue; }
        // M5_LOGV("degree: %d, slot_key: %d, semitone: %d, base_degree:%d, base_semitone:%d", degree, slot_key, options.semitone_shift, options.bass_degree, options.bass_semitone_shift);
        note = KANTANMusic_GetMidiNoteNumber(
          6 - pitch_index
          , degree
          , slot_key
          , &options
        );
      }
      setPitchManage(part, pitch_index, midi_ch, note, velocity, press_usec, press_usec + autorelease_usec);
      press_usec += displacement_usec;
      flg_use = true;
    }
    if (flg_use) {
      uint8_t program = part_info->getTone();
      uint8_t max_chvol = system_registry.runtime_info.getMIDIChannelVolumeMax();
      uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
      if (chvolume > 127) { chvolume = 127; }
      system_registry.midi_out_control.setProgramChange(midi_ch, program);
      system_registry.midi_out_control.setChannelVolume(midi_ch, chvolume);
    }
  }
}








void task_kantanplay_t::procSoundEffect(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }

  auto effect_type = (def::command::sound_effect_t)command_param.getParam();
  int master_key = system_registry.runtime_info.getMasterKey();
  int slot_key = master_key + (int8_t)system_registry.current_slot->slot_info.getKeyOffset();
  while (slot_key < 0) { slot_key += 12; }
  while (slot_key >= 12) { slot_key -= 12; }
  int degree = system_registry.chord_play.getChordDegree();
  if (degree < 1) { degree = 1; }
  else if (degree > 7) { degree = 7; }

  auto part_index = system_registry.chord_play.getEditTargetPart();
  auto chord_part = &system_registry.current_slot->chord_part[part_index];
  auto part_info = &chord_part->part_info;
  int pitch_index = system_registry.chord_play.getCursorY();
  int step = system_registry.chord_play.getPartStep(part_index);
  if (step < 0) { step = 0; }

  int pitch_last = pitch_index + 1;
  uint8_t midi_ch = part_index;

  if (!part_info->isDrumPart() && pitch_index >= def::app::max_pitch_without_drum) {
    effect_type = def::command::sound_effect_t::testplay;
  }

  KANTANMusic_GetMidiNoteNumberOptions options;
  KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
  options.minor_swap = system_registry.chord_play.getChordMinorSwap();
  options.semitone_shift = system_registry.chord_play.getChordSemitone();
  options.modifier = system_registry.chord_play.getChordModifier();
  options.voicing = part_info->getVoicing();
  options.position = part_info->getPosition();

  // プレビュー再生時の音は 500msecとする
  int autorelease_usec = 1000 * 500;

  int displacement_usec = 1000 * part_info->getStrokeSpeed();
  int pitch_flow = 1;
  bool mute = false;

  switch (effect_type) {
  default:
    break;

  case def::command::sound_effect_t::testplay:
    {
      int step = system_registry.chord_play.getPartStep(part_index);
      if (step < 0) { step = 0; }
      switch (chord_part->arpeggio.getStyle(step))
      {
      default: break;
      case def::play::arpeggio_style_t::same_time:
        pitch_flow = 1;
        pitch_index = 0;
        pitch_last = def::app::max_pitch_with_drum;
        displacement_usec = 0;
        break;

      case def::play::arpeggio_style_t::high_to_low:
        pitch_flow = 1;
        pitch_index = 0;
        pitch_last = def::app::max_pitch_with_drum;
        break;

      case def::play::arpeggio_style_t::low_to_high:
        pitch_flow = -1;
        pitch_index = def::app::max_pitch_with_drum - 1;
        pitch_last = -1;
        break;

      case def::play::arpeggio_style_t::mute:
        mute = true;
        // ミュート処理は同時発音ではなく高速ダウンストロークとして扱う

        // ミュート処理の時はリリースまでの時間はストロークスピードの 2倍とする
        autorelease_usec = displacement_usec * 2;

        // ミュート処理の時はストロークスピードは 1/4 とする。
        displacement_usec >>= 2;

        pitch_flow = -1;
        pitch_index = def::app::max_pitch_with_drum - 1;
        pitch_last = -1;
        break;
      }
    }
    break;
  }

  uint32_t note = 0;
  uint32_t press_usec = 0;
  for (; pitch_index != pitch_last; pitch_index += pitch_flow) {
    int velocity = chord_part->arpeggio.getVelocity(step, pitch_index);
    if (effect_type == def::command::sound_effect_t::testplay) {
      if (velocity == 0 && !mute) {
        continue;
      }
    } else {
      if (velocity == 0)
      { // OFFドットのプレビュー音は ベロシティ 64 を 100msecとする
        velocity = 64;
        autorelease_usec = 1000 * 100;
      } else if (velocity < 0)
      { // ミュートドットのプレビュー音は ベロシティ 32 を 50msecとする
        velocity = 32;
        autorelease_usec = 1000 * 50;
      }
    }

    if (part_info->isDrumPart()) {
      note = system_registry.song_data.chord_part_drum[part_index].getDrumNoteNumber(pitch_index);
      midi_ch = def::midi::channel_10;
    } else {
      if (pitch_index < 0 || pitch_index > 5) { continue; }

      note = KANTANMusic_GetMidiNoteNumber(
          6 - pitch_index
        , degree
        , slot_key
        , &options
      );
    }
    setPitchManage(part_index, pitch_index, midi_ch, note, velocity, press_usec, press_usec + autorelease_usec);
    press_usec += displacement_usec;
    uint8_t program = part_info->getTone();
    uint8_t max_chvol = system_registry.runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }
    system_registry.midi_out_control.setProgramChange(midi_ch, program);
    system_registry.midi_out_control.setChannelVolume(midi_ch, chvolume);
  }
}

void task_kantanplay_t::procChordStepResetRequest(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }
  chordStepReset();
}

void task_kantanplay_t::chordStepReset(void)
{
  _step_reset_request = true;

  const uint_fast8_t step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
  if (_current_beat_index || step_per_beat < 2) {
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      system_registry.chord_play.setPartStep(i, -1);
    }
  }
}

void task_kantanplay_t::resetStepAndMute(void)
{
// 各種自動処理のタイミングをリセットする
  _reactive_onbeat_cycle_usec = -1;
  _auto_play_onbeat_remain_usec = -1;
  _auto_play_offbeat_remain_usec = -1;
  _auto_play_input_tolerating_remain_usec = -1;

  // アルペジエータも先頭に戻す
  _step_reset_request = true;
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index) {
    system_registry.chord_play.setPartStep(part_index, -1);
    // 各パートの音を停止する
    chordNoteOff(part_index);
  }

  for (int i = 0; i < 16; ++i) { // CC#120はすべてのMIDI音を停止する
    system_registry.midi_out_control.setControlChange(i, 120, 0);
  }
  _arpeggio_reset_remain_usec = 1024;
}






// 指定したノートナンバーの音が他のピッチでも鳴っている数を調べる関数
int32_t task_kantanplay_t::checkOtherPitchNote(int part, int pitch, int midi_ch, int note_number)
{
  int count = 0;
  for (int p = 0; p < def::app::max_pitch_with_drum; ++p) {
    if (p == pitch) { continue; }
    for (int m = 0; m < max_manage_history; ++m) {
      auto manage = &_midi_pitch_manage[part][p][m];
      // まだ鳴っていない音や鳴り終わった音は除外する
      if (manage->press_usec >= 0 || manage->release_usec < 0) { continue; }
      if (manage->note_number != note_number || manage->midi_ch != midi_ch) { continue; }
      if (manage->velocity == 0) { continue; }
      ++count;
    }
  }
  // if (count) {
  //   M5_LOGV("checkOtherPitchNote: %d", count);
  // }
  return count;
}

void task_kantanplay_t::chordNoteOff(int part)
{
  // auto chord_part = &system_registry.current_slot->chord_part[part];
  for (int pitch_index = 0; pitch_index < def::app::max_pitch_with_drum; ++pitch_index) {
    for (int m = 0; m < max_manage_history; ++m) {
      auto manage = &_midi_pitch_manage[part][pitch_index][m];
      if (manage->press_usec >= 0 || manage->release_usec >= 0) {
        auto note = manage->note_number;
        manage->velocity = 0;
        manage->press_usec = -1;
        manage->release_usec = -1;
        manage->note_number = 0xFF;
        auto midi_ch = manage->midi_ch;
        if (note < def::midi::max_note && midi_ch < def::midi::channel_max) {
          system_registry.midi_out_control.setNoteVelocity(midi_ch, note, 0);
        }
      }
    }
  }
}

void task_kantanplay_t::setPitchManage(uint8_t part, uint8_t pitch, uint8_t midi_ch, uint8_t note_number, int8_t velocity, int32_t press_usec, int32_t release_usec)
{
  auto manage = &_midi_pitch_manage[part][pitch][0];
  { // 履歴末尾のデータが消失する前に、管理している音を停止する
    if (manage[0].press_usec < 0 && manage[0].release_usec >= 0)
    {
      manage[0].release_usec = -1;
      manage[0].press_usec = -1;

      // 同じノートナンバーの音が他のピッチで鳴っていない場合は音を停止する
      if (0 == checkOtherPitchNote(part, pitch, midi_ch, note_number)) {
// M5_LOGV("stop note: %d, pitch: %d, midi_ch: %d, note_number: %d, velocity: %d, press_usec: %d, release_usec: %d", part, pitch, midi_ch, note_number, velocity, press_usec, release_usec);
        system_registry.midi_out_control.setNoteVelocity(midi_ch, note_number, 0);
      }
    }
  }

  // 履歴をずらす
  memmove(&(_midi_pitch_manage[part][pitch][0]), &(_midi_pitch_manage[part][pitch][1]), sizeof(midi_pitch_manage_t) * (max_manage_history - 1));

  // 今回指定された音よりも後のタイミングで処理される予定だった音を探し、予定をキャンセルしたり早めたりする
  for (int m = 0; m < max_manage_history - 1; ++m) {
    if (manage[m].press_usec >= press_usec) {
      manage[m].press_usec = -1;
      manage[m].release_usec = -1;
    } else
    if (manage[m].release_usec > press_usec) {
//M5_LOGV("short note_number: %d, press_usec: %d, release_usec: %d", note_number, press_usec, manage[m].release_usec);
      manage[m].release_usec = press_usec;
    }
  }

  if (velocity < 0 || note_number == 0)
  { // マイナスベロシティやノートナンバー0 は停止処理に変換する
    velocity = 0;
    release_usec = press_usec;
    press_usec = -1;
  }

  // if ((velocity > 127) { velocity = 127; }

  {
  // M5_LOGV("part: %d, pitch: %d, midi_ch: %d, note_number: %d, velocity: %d, press_usec: %d, release_usec: %d", part, pitch, midi_ch, note_number, velocity, press_usec, release_usec);
    manage[max_manage_history - 1].note_number = note_number;
    manage[max_manage_history - 1].midi_ch = midi_ch;
    manage[max_manage_history - 1].velocity = velocity;
    manage[max_manage_history - 1].release_usec = release_usec;
    manage[max_manage_history - 1].press_usec = press_usec;
  }
}


void task_kantanplay_t::procNoteButton(const def::command::command_param_t& command_param, const bool is_pressed)
{
  const uint8_t button_index = command_param.getParam() - 1;
  if (button_index >= def::hw::max_main_button) { return; }

  const bool on_beat = is_pressed;

  // 押した時と離した時でボタンのマッピングが変化している可能性がある。
  // 押した時点の情報を_midi_note_manageに記憶しておき、次回おなじボタンが操作されたらまず前回のボタンをノートオフする
  auto manage = &_midi_note_manage[button_index];
  auto midi_ch = manage->midi_ch;
  auto note = manage->note_number;
  if (note < 127 && midi_ch < def::midi::channel_max) {
    system_registry.midi_out_control.setNoteVelocity(midi_ch, note, 0);
    manage->note_number = 0xFF;
  }
  if (!on_beat) {
    return;
  }

  int32_t master_key = (int8_t)system_registry.runtime_info.getMasterKey();
  uint8_t note_scale = system_registry.current_slot->slot_info.getNoteScale();
  int32_t offset_key = (int8_t)system_registry.current_slot->slot_info.getKeyOffset();
  note = def::play::note::note_scale_note_table[note_scale][button_index];

  note += master_key + offset_key;
  if (note < 128) {
    // TODO:ボリュームの設定をスロット情報から取得する
    uint8_t volume = 100;
    uint8_t max_chvol = system_registry.runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = volume * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }

    uint8_t program = system_registry.current_slot->slot_info.getNoteProgram();
    midi_ch = def::midi::channel_7;
    manage->midi_ch = midi_ch;
    manage->note_number = note;
    system_registry.midi_out_control.setChannelVolume(midi_ch, chvolume);
    system_registry.midi_out_control.setProgramChange(midi_ch, program);
    system_registry.midi_out_control.setNoteVelocity(midi_ch, note, 0);
    uint8_t velocity = 0x80 | (_press_velocity > 127 ? 127 : _press_velocity);
    system_registry.midi_out_control.setNoteVelocity(midi_ch, note, velocity);
  }
}

void task_kantanplay_t::procDrumButton(const def::command::command_param_t& command_param, const bool is_pressed)
{
  const uint8_t button_index = command_param.getParam() - 1;
  if (button_index >= def::hw::max_main_button) { return; }

  const bool on_beat = is_pressed;
  auto manage = &_midi_note_manage[button_index];

  auto midi_ch = manage->midi_ch;
  auto note = manage->note_number;

  if (note < 127 && midi_ch < def::midi::channel_max) {
    system_registry.midi_out_control.setNoteVelocity(midi_ch, note, 0);
    manage->note_number = 0xFF;
  }
  if (!on_beat) {
    return;
  }

  note = system_registry.drum_mapping.get8(button_index);

  if (note < 128) {
    // TODO:ボリュームの設定をスロット情報から取得する
    uint8_t volume = 100;
    uint8_t max_chvol = system_registry.runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = volume * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }
    midi_ch = def::midi::channel_10;
    manage->midi_ch = midi_ch;
    manage->note_number = note;
    system_registry.midi_out_control.setChannelVolume(midi_ch, chvolume);

    uint8_t velocity = 0x80 | (_press_velocity > 127 ? 127 : _press_velocity);
    system_registry.midi_out_control.setNoteVelocity(def::midi::channel_10, note, velocity);
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
