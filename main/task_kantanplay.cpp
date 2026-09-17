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
  system_registry->player_command.setNotifyTaskHandle(handle);
#endif
}

void task_kantanplay_t::task_func(task_kantanplay_t* me)
{
  for (;;) {
    uint32_t next_usec;
    do {
      me->sustainProc();
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
        system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_KANTANPLAY);
        ulTaskNotifyTake(pdTRUE, next_msec);
        system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_KANTANPLAY);
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
  if (false == system_registry->player_command.getQueue(&_player_command_history_code, &command_param, &is_pressed))
  { return false; }
// printf("commandProccessor: %d, %d, isPressed %d\n", command_param.getCommand(), command_param.getParam(), is_pressed);
  switch (command_param.getCommand()) {
  default:
    break;
  case def::command::set_velocity:
    if (is_pressed)
    {
      const auto param = command_param.getParam();
      system_registry->runtime_info.setPressVelocity(param);
      _press_velocity = param;
    }
    break;
  case def::command::sound_effect:
    procSoundEffect(command_param, is_pressed);
    break;
  case def::command::menu_cursor_sound:
    if (is_pressed) {
      // 0=シ, 1=ド, 2=レ, 3=ミ, 4=ファ, 5=ソ, 6=ラ, 7=シ, 8=ド', 9=レ'
      static constexpr uint8_t digit_scale[] = { 11, 12, 14, 16, 17, 19, 21, 23, 24, 26 };
      static constexpr uint8_t menu_cursor_ch      = 1; // ch2 (0-indexed)
      static constexpr uint8_t menu_cursor_program = 4;
      static constexpr uint8_t menu_cursor_base    = 48; // C3 基準
      static constexpr uint8_t se_part             = def::app::max_chord_part;
      static constexpr int32_t note_duration_usec  = 1000 * 70;
      static constexpr int32_t note_interval_usec  = 1000 * 80;
      system_registry->midi_out_control.setProgramChange(menu_cursor_ch, menu_cursor_program);
      system_registry->midi_out_control.setChannelVolume(menu_cursor_ch, 100);
      system_registry->midi_out_control.setChannelPan(menu_cursor_ch, 64);
      // 送信側で200以上は/10済み、100〜199は3桁、99以下は1〜2桁
      uint8_t pos = (uint8_t)command_param.getParam();
      uint8_t hundreds = pos / 100;
      uint8_t tens     = (pos % 100) / 10;
      uint8_t ones     = pos % 10;
      if (hundreds > 0) {
        // 3桁：百の位→十の位→一の位
        setPitchManage(se_part, 2, menu_cursor_ch, menu_cursor_base + digit_scale[hundreds], 100, 0,                    note_duration_usec);
        setPitchManage(se_part, 1, menu_cursor_ch, menu_cursor_base + digit_scale[tens],     100, note_interval_usec,     note_interval_usec + note_duration_usec);
        setPitchManage(se_part, 0, menu_cursor_ch, menu_cursor_base + digit_scale[ones],     100, note_interval_usec * 2, note_interval_usec * 2 + note_duration_usec);
      } else if (tens > 0) {
        // 2桁：十の位→一の位
        setPitchManage(se_part, 1, menu_cursor_ch, menu_cursor_base + digit_scale[tens], 100, 0,                note_duration_usec);
        setPitchManage(se_part, 0, menu_cursor_ch, menu_cursor_base + digit_scale[ones], 100, note_interval_usec, note_interval_usec + note_duration_usec);
      } else {
        // 1桁：一の位のみ
        setPitchManage(se_part, 0, menu_cursor_ch, menu_cursor_base + digit_scale[ones], 100, 0, note_duration_usec);
      }
    }
    break;
  case def::command::menu_navigate_sound:
    if (is_pressed) {
      // param = (navigate_type << 4) | level
      // navigate_type: 0=open, 1=enter(下層), 2=back(上層), 3=exit(閉じる)
      // level: 移動後の階層数（enterはlevel回、back/exit/openは1回鳴らす）
      static constexpr uint8_t nav_drum_ch   = 9;  // ch10 (0-indexed) GM ドラム
      static constexpr uint8_t se_part       = def::app::max_chord_part;
      static constexpr int32_t drum_dur_usec = 1000 * 80;
      static constexpr int32_t drum_int_usec = 1000 * 90;
      // メニュー種別→ドラムノート（open/enter用）
      static constexpr uint8_t note_enter = 37; // サイドスティック
      static constexpr uint8_t note_back  = 42; // クローズハイハット
      static constexpr uint8_t note_exit  = 36; // バスドラム
      auto p = (uint8_t)command_param.getParam();
      uint8_t nav_type = (p >> 4) & 0x0F;
      uint8_t level    = p & 0x0F;
      system_registry->midi_out_control.setChannelVolume(nav_drum_ch, 100);
      if (nav_type == 1) {
        // enter: 階層数分だけ連打
        uint8_t count = (level < 1) ? 1 : (level > 6) ? 6 : level;
        for (uint8_t i = 0; i < count; ++i) {
          int32_t t = drum_int_usec * i;
          setPitchManage(se_part, 4 + i, nav_drum_ch, note_enter, 100, t, t + drum_dur_usec);
        }
      } else if (nav_type == 2) {
        // back: 階層数分だけ連打
        uint8_t count = (level < 1) ? 1 : (level > 6) ? 6 : level;
        for (uint8_t i = 0; i < count; ++i) {
          int32_t t = drum_int_usec * i;
          setPitchManage(se_part, 4 + i, nav_drum_ch, note_back, 100, t, t + drum_dur_usec);
        }
      } else if (nav_type == 3) {
        // exit（メニューを閉じる）: 1発
        setPitchManage(se_part, 4, nav_drum_ch, note_exit, 100, 0, drum_dur_usec);
      } else {
        // open: 1発
        setPitchManage(se_part, 4, nav_drum_ch, note_enter, 100, 0, drum_dur_usec);
      }
    }
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
  case def::command::play_control:
    procPlayEffect(command_param, is_pressed);
    break;
  case def::command::progression_pos_ud:
    procProgressionPosUd(command_param, is_pressed);
    break;
  case def::command::melody_preview:
    procMelodyPreview(command_param, is_pressed);
    break;

  case def::command::preview_switch:
    if (is_pressed) {
      // オートプレイ停止指示
      system_registry->player_command.addQueue( { def::command::autoplay_switch, def::command::autoplay_switch_t::autoplay_stop } );
      const auto param = command_param.getParam();
      bool is_play = (param != def::command::preview_switch_t::preview_stop);
      system_registry->runtime_info.setPreviewPlay(is_play);
      if (is_play) {
        system_registry->player_command.addQueue( { def::command::autoplay_switch, def::command::autoplay_switch_t::autoplay_start } );
      }
      resetStep();
      // 録音モード中はシーケンス位置を維持する（メニューから戻った時に位置が先頭に戻らないよう）
      if (!system_registry->runtime_info.getGuiFlag_SongRecording()) {
        system_registry->runtime_info.setProgressionPosition(0);
      }
    }
    break;

  case def::command::autoplay_switch:
    { // 自動演奏モードのオン・オフのトグル
      auto seq_mode = system_registry->currentPlayMode();
      auto autoplay_state = system_registry->runtime_info.getAutoplayState();
      auto prev_autoplay_state = autoplay_state;

      if (is_pressed) {
        // OTA実行中はオートプレイ禁止
        if (system_registry->runtime_info.getWiFiOtaProgress() != 0) {
          autoplay_state = def::play::auto_play_state_t::auto_play_none;
        } else {
          bool no_working = (prev_autoplay_state != def::play::auto_play_state_t::auto_play_running)
                         && (prev_autoplay_state != def::play::auto_play_state_t::auto_play_beatmode);

          switch (command_param.getParam()) {
          case def::command::autoplay_switch_t::autoplay_beat:
            // ビート自動演奏モード(ExtBeat)に移行する
            // AutoSongは完全自動制御のためbeatmodeに遷移しない
            if (seq_mode != def::playmode::pm_auto_song) {
              autoplay_state = def::play::auto_play_state_t::auto_play_beatmode;
            }
            break;

          case def::command::autoplay_switch_t::autoplay_toggle:
            if (autoplay_state == def::play::auto_play_state_t::auto_play_none) {
              autoplay_state = def::play::auto_play_state_t::auto_play_waiting;
            } else {
              autoplay_state = def::play::auto_play_state_t::auto_play_none;
            }
            break;

          case def::command::autoplay_switch_t::autoplay_start:
            if (autoplay_state != def::play::auto_play_state_t::auto_play_running) {
              autoplay_state = def::play::auto_play_state_t::auto_play_waiting;
              system_registry->runtime_info.setAutoplayState(autoplay_state);
              system_registry->player_command.addQueueW( { def::command::chord_degree, 1 } );
            }
            break;

          case def::command::autoplay_switch_t::autoplay_stop:
            autoplay_state = def::play::auto_play_state_t::auto_play_none;
            break;

          case def::command::autoplay_switch_t::autoplay_pause:
            if (autoplay_state == def::play::auto_play_state_t::auto_play_running
              || autoplay_state == def::play::auto_play_state_t::auto_play_beatmode) {
              // オートプレイ実行中/ビートモードの場合は、一時停止に移行する
              autoplay_state = def::play::auto_play_state_t::auto_play_paused;
            } else {
              if (autoplay_state == def::play::auto_play_state_t::auto_play_waiting) {
                // オートプレイ待機中は、終了する
                autoplay_state = def::play::auto_play_state_t::auto_play_none;
              }
            }
            break;
          }

          // beatmode（ExtBeat）はシーケンス系モードでも有効にする

          if (no_working && (
            autoplay_state == def::play::auto_play_state_t::auto_play_none
            || autoplay_state == def::play::auto_play_state_t::auto_play_paused
          )) {
            // ソング記録モード以外の場合、オートプレイ停止/ポーズ時にシーケンス位置を先頭に戻す
            if (!system_registry->runtime_info.getGuiFlag_SongRecording()) {
              system_registry->runtime_info.setProgressionPosition(0);
            }
          }
          // GuidePlay/Free+Guide でインスタコードリンクのパッド演奏時は beatmode(ExtBeat)に移行
          if (autoplay_state == def::play::auto_play_state_t::auto_play_running
           && (seq_mode == def::playmode::pm_guide_play || seq_mode == def::playmode::pm_free_guide)) {
            auto iclink_port = system_registry->midi_port_setting.getInstaChordLinkPort();
            auto iclink_style = system_registry->midi_port_setting.getInstaChordLinkStyle();
            if ((iclink_port != def::command::instachord_link_port_t::iclp_off)
            && (iclink_style == def::command::instachord_link_style_t::icls_pad))
            {
              autoplay_state = def::play::auto_play_state_t::auto_play_beatmode;
            }
          }
        }
        if (autoplay_state == def::play::auto_play_state_t::auto_play_none) {
          resetStep();
        }
      } else {
        // リリース時の処理
        if (command_param.getParam() == def::command::autoplay_switch_t::autoplay_pause) {
          // オートプレイ一時停止の場合は、待機状態に移行する
          if (autoplay_state == def::play::auto_play_state_t::auto_play_paused) {
            autoplay_state = def::play::auto_play_state_t::auto_play_waiting;
          }
        }
      }
      if (prev_autoplay_state != autoplay_state)
      {
        // M5_LOGV("autoplay change %d -> %d", (int)prev_autoplay_state, (int)autoplay_state);
        system_registry->runtime_info.setAutoplayState(autoplay_state);
      }
    }
    break;
  }

  return true;
}

void task_kantanplay_t::setSustain(bool sustain_on)
{
  system_registry->runtime_info.setSustainState(sustain_on ? def::play::sustain_state_t::sustain_on : def::play::sustain_state_t::sustain_off);
  sustainProc();
}

void task_kantanplay_t::sustainProc(void)
{
  auto sustain_state = system_registry->runtime_info.getSustainState();
  if (_sustain_state == sustain_state) {
    // サステインの状態が変わっていない場合は何もしない
    return;
  }
  _sustain_state = sustain_state;

  int32_t release_usec = 0; 
  if (sustain_state == def::play::sustain_state_t::sustain_on)
  {
    // ノートオフの予定時間を最大値(約2000秒)にすることで実質的にノートオフを無効化する
    release_usec = INT32_MAX; 
  } else {
    // 動作中のコードボタンの表示を解除
    system_registry->working_command.clear( { def::command::chord_degree, _current_option.main_degree } );
  }

  {
    // サステイン状態変更 (ノートオフタイミングを上書きして実現)
    for (int part = 0; part < def::app::max_chord_part; ++part) {
      for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
        auto manage = &_midi_pitch_manage[part][pitch][max_manage_history - 1];
        if (manage->release_usec >= 0) {
          manage->release_usec = release_usec;
        }
      }
    }
    // ノートオフ設定する対象がなかった場合、念のためすべてのパートの音を停止する
    if (sustain_state != def::play::sustain_state_t::sustain_on) {
      allPartsNoteOff();
    }
  }
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
      if (system_registry->runtime_info.getGuiAutoplayState() == def::play::auto_play_paused) {
        // 自動演奏の一時停止時は処理を保留にする (2msec後に再設定する)
        remain_usec = 2048;
      } else {
        const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
        if (_current_beat_index < step_per_beat - 1) {
          remain_usec += _auto_play_offbeat_cycle_usec[1 & _current_beat_index];
        }
        // オフビートの演奏を行う
        chordBeat(false);
      }
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
      auto autoplay_state = system_registry->runtime_info.getGuiAutoplayState();
      if (autoplay_state == def::play::auto_play_state_t::auto_play_running)
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

        switch (system_registry->currentPlayMode()) {
        // オートソング(シーケンス演奏)の場合はステップを進める
        case def::playmode::pm_auto_song:
          system_registry->operator_command.addQueue( { def::command::progression_pos_ud, 1 } );
          break;

        default:
        // オンビートの演奏を行う
          updateNextOptions();
          chordBeat(true);
          addProgression();
        }

      } else if (autoplay_state == def::play::auto_play_paused) {
        // 自動演奏の一時停止時は処理を保留にする (2msec後に再設定する)
        remain_usec = 2048;
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

  if (_melody_preview_release_usec >= 0) {
    _melody_preview_release_usec -= progress_usec;
    if (_melody_preview_release_usec < 0) {
      melodyNoteOff(true);
    } else if (next_event_timing > (uint32_t)_melody_preview_release_usec) {
      next_event_timing = _melody_preview_release_usec;
    }
  }

  if (_melody_preview_next_remain_usec >= 0) {
    _melody_preview_next_remain_usec -= progress_usec;
    if (_melody_preview_next_remain_usec < 0) {
      const uint16_t step = _melody_preview_next_step;
      _melody_preview_next_remain_usec = -1;
      // 2ステップ目は実際のイベントだけを適用する。空なら1ステップ目の音を保持する。
      playMelodyPreviewStep(step, false);
      // 次のPlayまで実演奏と同様に保持する。操作が止まった場合だけNote Offする。
      if (_melody_preview_note < 128) {
        _melody_preview_release_usec = std::max<int32_t>(
          1500 * 1000, getOnbeatCycle() * 2);
      }
    } else if (next_event_timing > (uint32_t)_melody_preview_next_remain_usec) {
      next_event_timing = _melody_preview_next_remain_usec;
    }
  }

  if (_melody_preview_chord_next_remain_usec >= 0) {
    int32_t remain_usec = _melody_preview_chord_next_remain_usec - progress_usec;
    while (remain_usec < 0 && _melody_preview_chord_steps_remaining) {
      uint16_t melody_step = _melody_preview_chord_source_step;
      // 奇数メロディステップ（裏拍）から開始した場合だけ、拍の中央で
      // 次のコード進行ステップへまたぐ。
      if ((_melody_preview_chord_source_step & 1)
       && _melody_preview_chord_elapsed_usec * 2 >= _melody_preview_beat_usec
       && melody_step + 1 < def::app::max_progression_length) {
        ++melody_step;
      }
      playMelodyChordPreview(melody_step, _melody_preview_chord_next_step);
      --_melody_preview_chord_steps_remaining;
      if (_melody_preview_chord_steps_remaining) {
        int32_t delay_usec = getMelodyPreviewPatternDelayUsec(
          _melody_preview_chord_next_step, _melody_preview_chord_step_per_beat);
        ++_melody_preview_chord_next_step;
        _melody_preview_chord_elapsed_usec += delay_usec;
        remain_usec += delay_usec;
      }
    }
    if (_melody_preview_chord_steps_remaining) {
      _melody_preview_chord_next_remain_usec = remain_usec;
      if (next_event_timing > (uint32_t)remain_usec) {
        next_event_timing = remain_usec;
      }
    } else {
      _melody_preview_chord_next_remain_usec = -1;
    }
  }

  for (int part = 0; part <= def::app::max_chord_part; ++part) { // +1 はSE専用スロット
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
              // system_registry->midi_out_control.setNoteVelocity(midi_ch, note_number, 0);
              system_registry->midi_out_control.setNoteVelocity(midi_ch, note_number, velocity);
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
              system_registry->midi_out_control.setNoteVelocity(manage->midi_ch, manage->note_number, 0);
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
      system_registry->runtime_info.hitPartEffect(part);
    }
  }

  // パターン編集モードでない場合 && 自動演奏の一時停止モードでない場合
  if (!system_registry->runtime_info.getGuiFlag_PartEdit()
  &&  system_registry->runtime_info.getGuiAutoplayState() != def::play::auto_play_paused)
  {
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
  auto autoplay_state = system_registry->runtime_info.getAutoplayState();
  system_registry->runtime_info.setPerformanceActive(
      _arpeggio_reset_remain_usec >= 0
   || autoplay_state == def::play::auto_play_state_t::auto_play_running
   || autoplay_state == def::play::auto_play_state_t::auto_play_beatmode
  );

  return next_event_timing;
}

// Degree(度数)ボタン操作時の処理
void task_kantanplay_t::procChordDegree(const def::command::command_param_t& command_param, const bool is_pressed)
{
  const auto degree = make_degree(command_param.getParam());

  const auto autoplay_state = system_registry->runtime_info.getGuiAutoplayState();
  const bool is_auto = autoplay_state == def::play::auto_play_state_t::auto_play_running;

  // AutoSongモード演奏中: ユーザー操作による演奏を無効化
  if (is_auto && system_registry->currentPlayMode() == def::playmode::pm_auto_song) {
    return;
  }

  // 自動演奏の開始待ち受け状態または一時停止状態の場合はこのタイミングで自動演奏を開始
  if (is_pressed && (autoplay_state == def::play::auto_play_state_t::auto_play_waiting
                  || autoplay_state == def::play::auto_play_state_t::auto_play_paused)) {
    if (system_registry->currentPlayMode() == def::playmode::pm_auto_song) {
      _auto_play_onbeat_remain_usec = 0;
      system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_running);
      return;
    }
  }

  if (is_pressed) { // Degreeボタンを押したタイミングで次のオモテ拍での演奏オプションをセットしておく
    _pressed_option.main_degree = degree;
    _pressed_option.bass_degree = system_registry->chord_play.getChordBassDegree();
    updateNextOptions();
  }

  auto current_degree = system_registry->chord_play.getChordDegree();
  // 現在のDegreeと異なる場合
  if (current_degree != degree) {
    // 別のDegreeのボタンを離した場合は何もしない
    if (!is_pressed) {
      return;
    }
    system_registry->chord_play.setChordDegree(degree);

    if (is_auto && _auto_play_input_tolerating_remain_usec > 0) {
      auto step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
      // 自動演奏でオモテ拍の直後にDegreeボタンが押された場合 (オフビートが鳴る前に押された場合)
      // ユーザーの演奏タイミングが遅れたものと見做してオモテ拍の処理を強制的に行うことで、
      // 演奏サイクルが乱れないようにする。
      _current_beat_index = step_per_beat - 1;

      chordStepAdvance(true);
    }
  }

  // オンビート・オフビートそれぞれの自動化判定
  bool auto_on_beat = is_auto;
  bool auto_off_beat = is_auto || (system_registry->user_setting.getOffbeatStyle() == def::play::offbeat_style_t::offbeat_auto);

  bool playflag = ((is_pressed && (!auto_on_beat))  // オモテ拍手動
               || (!is_pressed && !auto_off_beat)); // ウラ拍手動

  // beatmode(ExtBeat)時: Degree操作ではオンビートを発行しない（ExtBeatに委ねる）
  if (system_registry->runtime_info.getAutoplayState() == def::play::auto_play_state_t::auto_play_beatmode
   && system_registry->isGuideActiveMode()) {
    return;
  }

  if (playflag)
  {
    if (autoplay_state == def::play::auto_play_state_t::auto_play_none) {
      // 手動演奏の場合はここでステップ進行コマンドを発行する
      // オモテ拍・ウラ拍の区別は is_pressedフラグで行う
      system_registry->player_command.addQueue( { def::command::chord_beat, 0 }, is_pressed );
    } else if (autoplay_state == def::play::auto_play_state_t::auto_play_waiting
            || autoplay_state == def::play::auto_play_state_t::auto_play_paused) {
      // 自動演奏の開始待ち受け状態または一時停止状態の場合はこのタイミングで自動演奏の開始
      _auto_play_onbeat_remain_usec = 0;
      system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_running);
    }
  }
}

void task_kantanplay_t::updateNextOptions(void)
{
  _pressed_option.setModifier(system_registry->chord_play.getChordModifier());
  _pressed_option.setSlotIndex(system_registry->runtime_info.getPlaySlot());
  for (int i = 0; i < def::app::max_chord_part; ++i) {
    // _pressed_option.setPartEnable(i, system_registry->chord_play.getPartEnable(i));
    auto part = &system_registry->current_slot->chord_part[i];
    _pressed_option.setPartEnable(i, part->part_info.getEnabled());
  }

  _next_option = _pressed_option;
  _next_option.bass_degree = system_registry->chord_play.getChordBassDegree();
  auto minor_swap = system_registry->chord_play.getChordMinorSwap();
  if (minor_swap) {
    _next_option.main_degree.setMinorSwap(minor_swap);
  }
  auto semitone_shift = system_registry->chord_play.getChordSemitoneShift();
  if (semitone_shift) {
    semitone_shift += _next_option.getSemitoneShift();
    _next_option.setSemitoneShift(semitone_shift);
  }
  auto bass_semitone_shift = system_registry->chord_play.getChordBassSemitoneShift();
  if (bass_semitone_shift) {
    bass_semitone_shift += _next_option.getBassSemitoneShift();
    _next_option.setBassSemitoneShift(bass_semitone_shift);
  }
}

void task_kantanplay_t::procChordBeat(const def::command::command_param_t& command_param, const bool is_pressed)
{
  // is_pressed==trueをオンビート、 falseをオフビート扱いとする
  const auto on_beat = is_pressed;

// この関数が呼ばれるのはユーザーによるDegreeボタン操作時や外部からのパルスがトリガー。
// 自動演奏によるトリガーは含まれない。

  // AutoSongモード: 演奏制御は完全にオート。外部オンビートは開始トリガーのみ。
  if (system_registry->currentPlayMode() == def::playmode::pm_auto_song) {
    auto state = system_registry->runtime_info.getAutoplayState();
    if (state == def::play::auto_play_state_t::auto_play_running
     || state == def::play::auto_play_state_t::auto_play_beatmode) {
      // 演奏中（Auto/ExtBeat問わず）: ユーザー操作による干渉を防止
      return;
    }
    if (on_beat && (state == def::play::auto_play_state_t::auto_play_waiting
                 || state == def::play::auto_play_state_t::auto_play_paused)) {
      // 開始トリガー
      _auto_play_onbeat_remain_usec = 0;
      system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_running);
      return;
    }
  }

  bool offbeat_auto = (system_registry->user_setting.getOffbeatStyle() == def::play::offbeat_style_t::offbeat_auto);

  if (!offbeat_auto) {
    auto iclink_port = system_registry->midi_port_setting.getInstaChordLinkPort();
    auto iclink_style = system_registry->midi_port_setting.getInstaChordLinkStyle();
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

  auto playmode = system_registry->currentPlayMode();
  if (playmode == def::playmode::pm_free_play
   || playmode == def::playmode::pm_beat_play
   || system_registry->runtime_info.getGuiFlag_SongRecording()
  ) {
    // 押されているボタンに基づいて次回オンビート時の演奏オプションを設定する
    updateNextOptions();

    // Modifierは即時変更を反映しておく（これによりアルペジエータ先頭戻し判定の影響を回避する）
    _current_option.setModifier(_next_option.getModifier());
  }
  else if (playmode == def::playmode::pm_guide_play) {
    if (on_beat) {
      auto stepindex = system_registry->runtime_info.getProgressionPosition();
      if (stepindex < system_registry->current_progression->info.getLength()) {
        auto desc = system_registry->current_progression->getStepDescriptor(stepindex);
        if (_pressed_option.main_degree.getDegree() == desc.main_degree.getDegree()) {
          _next_option = _pressed_option;
          system_registry->operator_command.addQueue( { def::command::progression_pos_ud, 1 } );
        }
      }
      return;
    }
  }
  else if (playmode == def::playmode::pm_free_guide) {
    if (on_beat) {
      // フリーガイド: 操作内容に関わらずステップを進める。演奏はprocProgressionPosUd側で行う
      _next_option = _pressed_option;
      system_registry->operator_command.addQueue( { def::command::progression_pos_ud, 1 } );
      return;
    }
  }

  chordBeat(on_beat);

  if (on_beat) {
    addProgression();
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
      const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
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

  // サステイン解除
  setSustain(false);

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

  auto playmode = system_registry->currentPlayMode();
  if (playmode == def::playmode::pm_free_play) {
    // 一定時間経過後にアルペジエータを先頭に戻す時間を更新する
    _arpeggio_reset_remain_usec = song_tempo * def::app::arpeggio_reset_timeout_beats;
  } else {
    // フリープレイモードの場合はアルペジエータの先頭戻しを無効にする
    _arpeggio_reset_remain_usec = -1;
  }
  auto autoplay_state = system_registry->runtime_info.getAutoplayState();
  system_registry->runtime_info.setPerformanceActive(
      _arpeggio_reset_remain_usec >= 0
   || autoplay_state == def::play::auto_play_state_t::auto_play_running
   || autoplay_state == def::play::auto_play_state_t::auto_play_beatmode
  );

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
  auto tempo = system_registry->song_data.song_info.getTempo();
  if (tempo < def::app::tempo_bpm_min) { tempo = def::app::tempo_bpm_default; }

  // テンポ値からオンビートの時間間隔を求める
  return (60 * 1000 * 1000 + (tempo >> 1)) / tempo;
}

void task_kantanplay_t::updateOffbeatTiming(void)
{
  // オモテ拍の間隔に基づいてウラ拍のタイミングを計算し更新する
  int32_t onbeat_cycle_usec = getOnbeatCycle();
  const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();

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
  uint_fast8_t swing = system_registry->song_data.song_info.getSwing();
  if (swing > def::app::swing_percent_max) { swing = def::app::swing_percent_max; }

  // swing 0のとき on:off 比率 1:1、swing 100のとき on:off 比率 = 2:1 となるようにする
  return (swing * 100 / 3); // 元の値 [0 ～ 100] に対し、得られる値 [0 ～ 3333]
}

// 何ステップ進むか調べる
int32_t task_kantanplay_t::calcStepAdvance(const bool on_beat)
{
  const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
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
  const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
  if (step_per_beat == 0) {
    // データが準備できていない場合は何もせずに返す
    return;
  }

  auto chord_play = &system_registry->chord_play;

  bool on_beat = (++_current_beat_index % step_per_beat) == 0;

  {
    system_registry->working_command.clear( { def::command::chord_degree, _current_option.main_degree } );
    auto degree = _current_option.main_degree.getDegree();
    if (_current_option.main_degree.raw != degree) {
      system_registry->working_command.clear( { def::command::chord_degree, degree } );
      system_registry->working_command.clear({ def::command::chord_minor_swap, 1 } );
      system_registry->working_command.clear( { def::command::chord_semitone, semitone_flat } );
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_sharp } );
    }
  }

  // printf("DEBUG 1 : %d \n", on_beat);
  if (on_beat)
  { // オンビート (オモテ拍) の場合、アルペジエータを先頭に戻す判定を実施
    _current_beat_index = 0;

    // 先頭に戻すフラグ (強制的に戻す)
    bool force_reset = false;

    // 先頭に戻すフラグ (但しアンカーステップが効く)
    bool normal_reset = false;

    // _current_option と _next_option が違う場合
    if (_current_option != _next_option) {
      if (_current_option.main_degree != _next_option.main_degree
       || _current_option.bass_degree != _next_option.bass_degree) {
        // メイン度数が変わる場合は先頭に戻す (アンカーステップは有効)
        normal_reset = true;
      }
      if (_current_option.slot_index != _next_option.slot_index) {
        // スロットが変わる場合は強制的に先頭に戻す(アンカーステップは無効)
        force_reset = true;
      }
      _current_option = _next_option;
    }

    // ステップのリセット要求があれば先頭に戻す
    if (_step_reset_request) {
      _step_reset_request = false;
      force_reset = true;
      setOnbeatCycle(-1);
    }

    // コードが選ばれていない場合は先頭に戻す
    if (_current_option.main_degree.getDegree() == 0)
    {
      force_reset = true;
    } else {
      // 動作中のコードボタンの表示反映
      system_registry->working_command.set( { def::command::chord_degree, _current_option.main_degree } );
      system_registry->working_command.set( { def::command::chord_degree, _current_option.main_degree.getDegree() } );
    }

    uint_fast8_t enabledCounter = 0;
    uint_fast8_t firstStepCounter = 0;
// printf("DEBUG 2 : %d \n", step_reset);

    int8_t step_list[def::app::max_chord_part];
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      int_fast8_t current_step = chord_play->getPartStep(i);
      step_list[i] = current_step;

      // 強制的に先頭に戻さない場合は条件を確認する
      auto part = &system_registry->current_slot->chord_part[i];
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
        bool next_enable;
        if (system_registry->isGuideActiveMode() && system_registry->runtime_info.getSongPartOperation() == def::play::song_part_auto) {
          // Auto: コード進行データのパート有効/無効を反映
          next_enable = _current_option.getPartEnable(i);
        } else {
          // Manual または非シーケンスモード: ユーザー操作（part_info）を反映
          next_enable = system_registry->current_slot->chord_part[i].part_info.getEnabled();
        }
        // パートが現在有効かどうかと、次回パートを有効にする指示があるかどうかを比較
        if (current_enable != next_enable) {
          if (flgFirstStep || current_enable) {
            current_enable = next_enable;
            system_registry->chord_play.setPartEnable(i, current_enable);
            chordNoteOff(i);
          }
        }
      }
      // パートが無効化している場合はステップを-1に設定しておく
      if (current_enable == false) {
        current_step = -1;
      }
      system_registry->chord_play.setPartStep(i, current_step);
    }
  }
  else
  { // オフビート (ウラ拍) の場合
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      int_fast8_t current_step = chord_play->getPartStep(i);
      if (chord_play->getPartEnable(i)) {
        current_step = ((current_step / step_per_beat) * step_per_beat) + _current_beat_index;
      }
      system_registry->chord_play.setPartStep(i, current_step);
    }
  }

  { // マイナースワップとセミトーンの動作中表示反映 (画面とLED用)
    auto minor_swap = system_registry->chord_play.getChordMinorSwap();
    auto semitone_shift = system_registry->chord_play.getChordSemitoneShift();
    if (on_beat) {
      minor_swap |= _current_option.main_degree.getMinorSwap();
      semitone_shift += _current_option.main_degree.getSemitoneShift();
    }
    if (minor_swap) {
      system_registry->working_command.set(  { def::command::chord_minor_swap, 1 } );
    } else {
      system_registry->working_command.clear({ def::command::chord_minor_swap, 1 } );
    }
    if (semitone_shift < 0) {
      system_registry->working_command.set(  { def::command::chord_semitone, semitone_flat } );
    } else {
      system_registry->working_command.clear( { def::command::chord_semitone, semitone_flat } );
    }
    if (semitone_shift > 0) {
      system_registry->working_command.set(  { def::command::chord_semitone, semitone_sharp } );
    } else {
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_sharp } );
    }
  }
}

void task_kantanplay_t::chordStepPlay(void)
{
  if (!_suppress_melody_step) {
    auto melody_length = system_registry->song_data.melody.info.getLength();
    uint32_t progression_steps = (uint32_t)system_registry->current_progression->info.getLength()
                               * system_registry->current_slot->slot_info.getStepPerBeat();
    if (progression_steps > def::app::max_progression_length) {
      progression_steps = def::app::max_progression_length;
    }
    if (melody_length < progression_steps) { melody_length = progression_steps; }
    if (melody_length) {
      if (_melody_step >= melody_length) {
        if (system_registry->runtime_info.getSongAutoRepeat()) {
          _melody_step = 0;
        } else {
          melodyNoteOff(false);
        }
      }
      if (_melody_step < melody_length) {
        playMelodyStep(_melody_step++);
      }
    }
  }
  auto degree = _current_option.main_degree;  //system_registry->chord_play.getChordDegree();
  if (degree.getDegree() == 0) {
    // コードが選ばれていない場合は終了
    return;
  }
  int master_key = system_registry->runtime_info.getMasterKey();
  int slot_key = master_key + (int8_t)system_registry->current_slot->slot_info.getKeyOffset();
  while (slot_key < 0) { slot_key += 12; }
  while (slot_key >= 12) { slot_key -= 12; }

  KANTANMusic_GetMidiNoteNumberOptions options;
  KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
  options.minor_swap          = _current_option.getMinorSwap();
  options.semitone_shift      = _current_option.getSemitoneShift();
  options.modifier            = _current_option.getModifier();
  options.bass_degree         = _current_option.getBassDegree();
  options.bass_semitone_shift = _current_option.getBassSemitoneShift();

// M5_LOGE("key: %d, minor_swap: %d, modifier: %d, semitone: %d", key, minor_swap, (int)modifier, semitone);
  for (int part = 0; part < def::app::max_chord_part; ++part) {
    bool part_en = system_registry->chord_play.getPartEnable(part);
    uint8_t midi_ch = part;
    auto chord_part = &system_registry->current_slot->chord_part[part];
    auto part_info = &chord_part->part_info;
    options.position = part_info->getPosition();
    options.voicing = part_info->getVoicing();

    int displacement_usec = 1000 * part_info->getStrokeSpeed();
    int autorelease_usec = 1000 * def::app::autorelease_msec;
    int32_t press_usec = 0;
    int step = system_registry->chord_play.getPartStep(part);
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
        note = system_registry->current_slot->chord_part_drum[part].getDrumNoteNumber(pitch_index);
      } else {
        if (pitch_index >= 6) { continue; }
        note = KANTANMusic_GetMidiNoteNumber(
          6 - pitch_index
          , degree.getDegree()
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
      uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
      uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
      if (chvolume > 127) { chvolume = 127; }
      system_registry->midi_out_control.setProgramChange(midi_ch, program);
      system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);
      system_registry->midi_out_control.setChannelPan(midi_ch, part_info->getPanCC());
    }
  }
}

void task_kantanplay_t::addProgression(void)
{
  auto mode = system_registry->runtime_info.getGuiMode();
  auto stepindex = system_registry->runtime_info.getProgressionPosition();
  if (stepindex < def::app::max_progression_length) {
    if (mode == def::gui_mode_t::gm_song_recording) {
      system_registry->current_progression->setStepDescriptor(stepindex, _current_option);
      ++stepindex;
      system_registry->runtime_info.setProgressionPosition(stepindex);
    }
  }
}

void task_kantanplay_t::procProgressionPosUd(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }

  int current_step = system_registry->runtime_info.getProgressionPosition();
  int param = command_param.getParam();

  if (param > 0) {
    auto desc = system_registry->current_progression->getStepDescriptor(current_step);
    if (!desc.empty()) {
      auto playmode = system_registry->currentPlayMode();
      if (playmode == def::playmode::pm_free_guide) {
        // フリーガイドモード時: 度数・モディファイアはユーザー操作を維持し、
        // スロット・パートはコード進行データを反映する
        _next_option.setPartBits(desc.getPartBits());
        _next_option.slot_index = desc.slot_index;
      } else {
        _next_option = desc;
        // Manualモード時: パート有効/無効とスロットは手動操作を維持
        if (system_registry->runtime_info.getSongPartOperation() == def::play::song_part_manual) {
          _next_option.setPartBits(_current_option.getPartBits());
          _next_option.slot_index = _current_option.slot_index;
        }
      }

      const auto current_usec = _current_usec;
      {
        // 短期間に連打されるのを防止する
        static uint32_t last_step_change_usec = 0;
        if (current_usec - last_step_change_usec > 16384) {
          chordBeat(true);
        }
        last_step_change_usec = current_usec;
      }

      // 自動演奏のサイクルを更新する
      setOnbeatCycle(current_usec - _reactive_onbeat_usec);
      _reactive_onbeat_usec = current_usec;

      // ここでオフビートのタイミングを更新する
      updateOffbeatTiming();
    }
  } else {
    // ステップ戻りの場合はアルペジエータを先頭に戻す
    chordStepReset();
  }

  current_step += param;
  int seq_length = system_registry->current_progression->info.getLength();
  // リピート有効かつ、レコーディングモードでない場合、末尾(length)に留まらず先頭に戻る
  if (current_step >= seq_length
   && !system_registry->runtime_info.getGuiFlag_SongRecording()
   && system_registry->runtime_info.getSongAutoRepeat()) {
    current_step = 0;
  }
  if (current_step >= 0 && current_step <= seq_length) {
    system_registry->runtime_info.setProgressionPosition(current_step);
  } else {
    auto mode = system_registry->currentPlayMode();
    if (mode == def::playmode::pm_auto_song) {
      system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_waiting);
    }
  }
}

void task_kantanplay_t::melodyNoteOff(bool preview)
{
  auto& note = preview ? _melody_preview_note : _melody_note;
  if (note < 128) {
    system_registry->midi_out_control.setNoteVelocity(def::midi::channel_7, note, 0);
  }
  note = 0xFF;
  if (preview) { _melody_preview_release_usec = -1; }
}

void task_kantanplay_t::playMelodyEvent(system_registry_t::melody_event_t event, bool preview)
{
  if (event.isEmpty()) { return; } // 空ステップは直前音を継続
  melodyNoteOff(preview);
  if (event.isMute()) { return; }

  uint8_t melody_program = system_registry->song_data.melody.info.getTone();
  uint16_t volume = system_registry->song_data.melody.info.getVolume()
                  * system_registry->runtime_info.getMIDIChannelVolumeMax() / 100;
  if (volume > 127) { volume = 127; }
  system_registry->midi_out_control.setProgramChange(def::midi::channel_7, melody_program);
  system_registry->midi_out_control.setChannelVolume(def::midi::channel_7, volume);
  system_registry->midi_out_control.setChannelPan(def::midi::channel_7, 64);
  auto& active_note = preview ? _melody_preview_note : _melody_note;
  active_note = event.getNote();
  system_registry->midi_out_control.setNoteVelocity(def::midi::channel_7, active_note, 0x80 | 96);
}

void task_kantanplay_t::playMelodyStep(uint16_t step)
{
  playMelodyEvent(system_registry->song_data.melody.getEvent(step), false);
}

void task_kantanplay_t::playMelodyChordPreview(uint16_t melody_step, uint16_t arranger_step)
{
  auto progression_length = system_registry->current_progression->info.getLength();
  if (!progression_length) { return; }
  // メロディ画面の1ステップは8分音符、コード進行の1ステップは4分音符。
  uint16_t progression_step = melody_step / def::app::melody_steps_per_beat;
  if (progression_step >= progression_length) { progression_step = progression_length - 1; }
  auto desc = system_registry->current_progression->getStepDescriptor(progression_step);
  if (desc.empty()) { return; }

  auto saved_option = _current_option;
  auto saved_slot = system_registry->current_slot;
  int8_t saved_steps[def::app::max_chord_part];
  bool saved_enabled[def::app::max_chord_part];
  for (int i = 0; i < def::app::max_chord_part; ++i) {
    saved_steps[i] = system_registry->chord_play.getPartStep(i);
    saved_enabled[i] = system_registry->chord_play.getPartEnable(i);
  }
  if (desc.getSlotIndex() < system_registry->song_data.song_info.getNumSlot()) {
    system_registry->current_slot = &system_registry->song_data.slot[desc.getSlotIndex()];
  }
  // 各パート固有のループ長で折り返し、指定された伴奏ステップだけを鳴らす。
  for (int i = 0; i < def::app::max_chord_part; ++i) {
    auto& part = system_registry->current_slot->chord_part[i];
    uint16_t loop_length = (uint16_t)part.part_info.getLoopStep() + 1;
    if (loop_length < 1 || loop_length > def::app::max_arpeggio_step) {
      loop_length = def::app::max_arpeggio_step;
    }
    system_registry->chord_play.setPartStep(i, arranger_step % loop_length);
    system_registry->chord_play.setPartEnable(i, desc.getPartEnable(i));
  }
  _current_option = desc;
  _suppress_melody_step = true;
  chordStepPlay();
  _suppress_melody_step = false;
  _current_option = saved_option;
  system_registry->current_slot = saved_slot;
  for (int i = 0; i < def::app::max_chord_part; ++i) {
    system_registry->chord_play.setPartStep(i, saved_steps[i]);
    system_registry->chord_play.setPartEnable(i, saved_enabled[i]);
  }
}

void task_kantanplay_t::playMelodyPreviewStep(uint16_t step, bool resolve_sustain)
{
  auto event = system_registry->song_data.melody.getEvent(step);
  if (event.isMute()) {
    melodyNoteOff(true);
  } else if (event.isNote()) {
    playMelodyEvent(event, true);
  } else if (resolve_sustain) {
    // 任意位置からプレビューを始める場合は、その位置で持続中の音を解決する。
    int active_note = system_registry->song_data.melody.timeline.getActiveNote(step);
    if (active_note >= 0) {
      // 連続再生中なら同じ持続音を再アタックしない。停止後や位置を飛ばした
      // 場合は、タイムライン上で有効な音だけを改めて発音する。
      if (_melody_preview_note != active_note) {
        playMelodyEvent(system_registry_t::melody_event_t::note((uint8_t)active_note), true);
      }
    } else {
      melodyNoteOff(true);
    }
  }
}

uint8_t task_kantanplay_t::getMelodyPreviewStepPerBeat(uint16_t step)
{
  uint8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
  auto progression_length = system_registry->current_progression->info.getLength();
  if (progression_length) {
    uint16_t progression_step = step / def::app::melody_steps_per_beat;
    if (progression_step >= progression_length) { progression_step = progression_length - 1; }
    auto desc = system_registry->current_progression->getStepDescriptor(progression_step);
    if (!desc.empty() && desc.getSlotIndex() < system_registry->song_data.song_info.getNumSlot()) {
      step_per_beat = system_registry->song_data.slot[desc.getSlotIndex()].slot_info.getStepPerBeat();
    }
  }
  if (step_per_beat < 1) { step_per_beat = 1; }
  return step_per_beat;
}

int32_t task_kantanplay_t::getMelodyPreviewPatternDelayUsec(
  uint16_t arranger_step, uint8_t step_per_beat)
{
  if (step_per_beat < 1) { step_per_beat = 1; }
  int32_t step_usec = getOnbeatCycle() / step_per_beat;
  // updateOffbeatTiming()と同じ長短の組で、偶数分割のSwingを再現する。
  if ((step_per_beat & 1) == 0) {
    int32_t swing_usec = step_usec * calcSwing_x100() / 10000;
    step_usec += (arranger_step & 1) ? -swing_usec : swing_usec;
  }
  return std::max<int32_t>(1024, step_usec);
}

void task_kantanplay_t::procMelodyPreview(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }
  auto type = (def::command::melody_preview_t)command_param.getParam();
  _melody_preview_next_remain_usec = -1;
  _melody_preview_chord_next_remain_usec = -1;
  _melody_preview_chord_steps_remaining = 0;
  if (type == def::command::melody_preview_stop) {
    melodyNoteOff(true);
    return;
  }
  if (type == def::command::melody_preview_pitch) {
    melodyNoteOff(true);
    playMelodyEvent(system_registry_t::melody_event_t::note(
      system_registry->runtime_info.getMelodyCursorPitch()), true);
    _melody_preview_release_usec = 180 * 1000;
    return;
  }
  if (type == def::command::melody_preview_volume) {
    uint16_t volume = system_registry->song_data.melody.info.getVolume()
                    * system_registry->runtime_info.getMIDIChannelVolumeMax() / 100;
    system_registry->midi_out_control.setChannelVolume(
      def::midi::channel_7, std::min<uint16_t>(127, volume));
    return;
  }
  if (type == def::command::melody_preview_step) {
    uint16_t step = system_registry->runtime_info.getMelodyCursorStep();
    playMelodyPreviewStep(step, true);

    // Play 1回は常に1拍。伴奏は、その位置のSlotのStep per beatに従って
    // 1拍分の全サブステップを実演奏と同じSwing間隔で鳴らす。
    uint8_t step_per_beat = getMelodyPreviewStepPerBeat(step);
    uint16_t arranger_step = ((uint32_t)step * step_per_beat)
                           / def::app::melody_steps_per_beat;
    playMelodyChordPreview(step, arranger_step);
    if (step_per_beat > 1) {
      int32_t delay_usec = getMelodyPreviewPatternDelayUsec(arranger_step, step_per_beat);
      _melody_preview_chord_next_remain_usec = delay_usec;
      _melody_preview_chord_elapsed_usec = delay_usec;
      _melody_preview_beat_usec = getOnbeatCycle();
      _melody_preview_chord_source_step = step;
      _melody_preview_chord_next_step = arranger_step + 1;
      _melody_preview_chord_steps_remaining = step_per_beat - 1;
      _melody_preview_chord_step_per_beat = step_per_beat;
    }

    // Playを続けている間は音を持続させる。2拍以上（最低1.5秒）操作が
    // なければセッションを終了し、次回はカーソル位置から状態を再構築する。
    if (_melody_preview_note < 128) {
      _melody_preview_release_usec = std::max<int32_t>(
        1500 * 1000, getOnbeatCycle() * 2);
    }

    const uint16_t next_step = step + 1;
    if (next_step < def::app::max_progression_length) {
      _melody_preview_next_step = next_step;
      _melody_preview_next_remain_usec = std::max<int32_t>(1024, getOnbeatCycle() / 2);
    }

    // Playは実演奏の表・裏を1組として2ステップ進む。矢印操作は1ステップのまま。
    uint16_t cursor_step = std::min<uint32_t>(
      def::app::max_progression_length - 1, (uint32_t)step + 2);
    if (cursor_step != step) {
      system_registry->runtime_info.setMelodyCursorStep(cursor_step);
    }
  }
}

void task_kantanplay_t::procSoundEffect(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }

  auto effect_param = (uint8_t)command_param.getParam();
  if (effect_param & def::command::sound_effect_t::drum_note_preview_flag) {
    uint8_t note = effect_param & ~def::command::sound_effect_t::drum_note_preview_flag;
    static constexpr uint8_t drum_ch = def::midi::channel_10;
    system_registry->midi_out_control.setChannelVolume(drum_ch, 100);
    setPitchManage(def::app::max_chord_part, 3, drum_ch, note, 100, 0, 1000 * 300);
    return;
  }
  if (effect_param & (def::command::sound_effect_t::guide_part_on | def::command::sound_effect_t::guide_part_off)) {
    uint8_t part_index = effect_param & def::command::sound_effect_t::guide_part_index_mask;
    bool enabled = effect_param & def::command::sound_effect_t::guide_part_on;
    bool empty = effect_param & def::command::sound_effect_t::guide_part_empty;
    procPartSwitchGuideSound(part_index, enabled, empty);
    return;
  }

  auto effect_type = (def::command::sound_effect_t)effect_param;
  int master_key = system_registry->runtime_info.getMasterKey();
  int slot_key = master_key + (int8_t)system_registry->current_slot->slot_info.getKeyOffset();
  while (slot_key < 0) { slot_key += 12; }
  while (slot_key >= 12) { slot_key -= 12; }
  int degree = system_registry->chord_play.getChordDegree();
  if (degree < 1) { degree = 1; }
  else if (degree > 7) { degree = 7; }

  auto part_index = system_registry->chord_play.getEditTargetPart();
  auto chord_part = &system_registry->current_slot->chord_part[part_index];
  auto part_info = &chord_part->part_info;
  int pitch_index = system_registry->chord_play.getCursorY();
  int step = system_registry->chord_play.getPartStep(part_index);
  if (step < 0) { step = 0; }

  int pitch_last = pitch_index + 1;
  uint8_t midi_ch = part_index;

  if (effect_type != def::command::sound_effect_t::tone_preview
   && !part_info->isDrumPart() && pitch_index >= def::app::max_pitch_without_drum) {
    effect_type = def::command::sound_effect_t::testplay;
  }

  KANTANMusic_GetMidiNoteNumberOptions options;
  KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
  options.minor_swap = system_registry->chord_play.getChordMinorSwap();
  options.semitone_shift = system_registry->chord_play.getChordSemitoneShift();
  options.modifier = system_registry->chord_play.getChordModifier();
  options.bass_degree = system_registry->chord_play.getChordBassDegree();
  options.bass_semitone_shift = system_registry->chord_play.getChordBassSemitoneShift();
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

  case def::command::sound_effect_t::tone_preview:
    playTonePreviewNote(system_registry->chord_play.getTonePreviewProgram(), 1000 * 300);
    return;

  case def::command::sound_effect_t::testplay:
    {
      int step = system_registry->chord_play.getPartStep(part_index);
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
      note = system_registry->current_slot->chord_part_drum[part_index].getDrumNoteNumber(pitch_index);
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
    uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }
    system_registry->midi_out_control.setProgramChange(midi_ch, program);
    system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);
    system_registry->midi_out_control.setChannelPan(midi_ch, part_info->getPanCC());
  }
}

void task_kantanplay_t::playPartPreviewNote(uint8_t part_index, uint8_t note, int32_t release_usec)
{
  auto part_info = &system_registry->current_slot->chord_part[part_index].part_info;
  uint8_t midi_ch = part_info->isDrumPart() ? def::midi::channel_10 : part_index;

  // ドラム以外はオクターブ設定を反映
  if (midi_ch != def::midi::channel_10) {
    int n = (int)note + part_info->getPosition();
    if (n < 0) { n = 0; } else if (n > 127) { n = 127; }
    note = (uint8_t)n;
  }

  uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
  uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
  if (chvolume > 127) { chvolume = 127; }
  system_registry->midi_out_control.setProgramChange(midi_ch, part_info->getTone());
  system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);
  system_registry->midi_out_control.setChannelPan(midi_ch, part_info->getPanCC());
  setPitchManage(part_index, 0, midi_ch, note, 96, 0, release_usec);
}

void task_kantanplay_t::playTonePreviewNote(uint8_t program, int32_t release_usec)
{
  auto part_index = system_registry->chord_play.getEditTargetPart();
  if (part_index >= def::app::max_chord_part) { return; }

  static constexpr uint8_t preview_part = def::app::max_chord_part;
  static constexpr uint8_t preview_pitch = def::app::max_pitch_with_drum - 1;
  auto part_info = &system_registry->current_slot->chord_part[part_index].part_info;
  bool is_drum_preview = program >= def::app::max_program_number - 1;
  uint8_t midi_ch = is_drum_preview ? def::midi::channel_10 : def::midi::channel_7;
  uint8_t note = 60;

  if (is_drum_preview) {
    uint8_t pitch = system_registry->chord_play.getCursorY();
    if (pitch >= def::app::max_pitch_with_drum) { pitch = 0; }
    note = system_registry->current_slot->chord_part_drum[part_index].getDrumNoteNumber(pitch);
  } else {
    int n = (int)note + part_info->getPosition();
    if (n < 0) { n = 0; } else if (n > 127) { n = 127; }
    note = (uint8_t)n;
  }

  if (_tone_preview_midi_ch < def::midi::channel_max && _tone_preview_note < 128) {
    system_registry->midi_out_control.setNoteVelocity(_tone_preview_midi_ch, _tone_preview_note, 0);
  }
  for (int m = 0; m < max_manage_history; ++m) {
    auto manage = &_midi_pitch_manage[preview_part][preview_pitch][m];
    if (manage->note_number < def::midi::max_note && manage->midi_ch < def::midi::channel_max) {
      system_registry->midi_out_control.setNoteVelocity(manage->midi_ch, manage->note_number, 0);
    }
    manage->press_usec = -1;
    manage->release_usec = -1;
    manage->note_number = 0xFF;
    manage->velocity = 0;
  }
  _tone_preview_midi_ch = midi_ch;
  _tone_preview_note = note;

  uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
  uint16_t chvolume = part_info->getVolume() * max_chvol / 100;
  if (chvolume > 127) { chvolume = 127; }

  if (!is_drum_preview) {
    system_registry->midi_out_control.setProgramChange(midi_ch, program);
  }
  system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);
  system_registry->midi_out_control.setChannelPan(midi_ch, part_info->getPanCC());
  setPitchManage(preview_part, preview_pitch, midi_ch, note, 96, 0, release_usec);
}

void task_kantanplay_t::procPartSwitchGuideSound(uint8_t part_index, bool enabled, bool empty)
{
  if (part_index >= def::app::max_chord_part) { return; }

  auto part_info = &system_registry->current_slot->chord_part[part_index].part_info;
  bool is_drum = part_info->isDrumPart();

  uint8_t note = enabled ? 60 : 55;
  if (empty) {
    note -= 24;
  } else if (is_drum) {
    note -= 12;
  }

  playPartPreviewNote(part_index, note, 1000 * (enabled ? 300 : 200));
}

void task_kantanplay_t::procChordStepResetRequest(const def::command::command_param_t& command_param, const bool is_pressed)
{
  if (!is_pressed) { return; }
  chordStepReset();
}

void task_kantanplay_t::procPlayEffect(const def::command::command_param_t& command_param, const bool is_pressed)
{
  switch (command_param.getParam())
  {
  case def::command::play_control_t::pc_panic_stop:
    if (is_pressed) {
      resetStep();
      allPartsNoteOff();
    }
    break;

  case def::command::play_control_t::pc_sustain:
    setSustain(is_pressed);
    break;

  case def::command::play_control_t::pc_reset_arpeggio:
    if (is_pressed) {
      resetStep();
    }
  }
}

void task_kantanplay_t::chordStepReset(void)
{
  _step_reset_request = true;

  const uint_fast8_t step_per_beat = system_registry->current_slot->slot_info.getStepPerBeat();
  if (_current_beat_index || step_per_beat < 2) {
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      system_registry->chord_play.setPartStep(i, -1);
    }
  }
}

void task_kantanplay_t::resetStepAndMute(void)
{
  resetStep();
  allPartsNoteOff();
}

void task_kantanplay_t::resetStep(void)
{
// 各種自動処理のタイミングをリセットする
  _reactive_onbeat_cycle_usec = -1;
  _auto_play_onbeat_remain_usec = -1;
  _auto_play_offbeat_remain_usec = -1;
  _auto_play_input_tolerating_remain_usec = -1;

  // アルペジエータを先頭に戻す
  _step_reset_request = true;
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index) {
    system_registry->chord_play.setPartStep(part_index, -1);
  }

  _arpeggio_reset_remain_usec = 1024;
  _melody_step = 0;
  _melody_preview_next_remain_usec = -1;
  _melody_preview_chord_next_remain_usec = -1;
  _melody_preview_chord_steps_remaining = 0;
  melodyNoteOff(false);
  melodyNoteOff(true);

  // 動作中のコードボタンの表示を解除
  system_registry->working_command.clear( { def::command::chord_degree, _current_option.main_degree } );
}

void task_kantanplay_t::allPartsNoteOff(void)
{
  melodyNoteOff(false);
  melodyNoteOff(true);
  // 各パートの音を停止する
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index) {
    chordNoteOff(part_index);
  }
  for (int i = 0; i < 16; ++i) { // CC#120はすべてのMIDI音を停止する
    system_registry->midi_out_control.setControlChange(i, 120, 0);
  }
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
  // auto chord_part = &system_registry->current_slot->chord_part[part];
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
          system_registry->midi_out_control.setNoteVelocity(midi_ch, note, 0);
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
        system_registry->midi_out_control.setNoteVelocity(midi_ch, note_number, 0);
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
    system_registry->midi_out_control.setNoteVelocity(midi_ch, note, 0);
    manage->note_number = 0xFF;
  }
  if (!on_beat) {
    return;
  }

  int32_t master_key = (int8_t)system_registry->runtime_info.getMasterKey();
  uint8_t note_scale = system_registry->runtime_info.getNoteScale();
  int32_t offset_key = (int8_t)system_registry->current_slot->slot_info.getKeyOffset();
  note = def::play::note::note_scale_note_table[note_scale][button_index];

  note += master_key + offset_key;
  if (note < 128) {
    // TODO:ボリュームの設定をスロット情報から取得する
    uint8_t volume = 100;
    uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = volume * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }

    uint8_t program = system_registry->current_slot->slot_info.getNoteProgram();
    midi_ch = def::midi::channel_7;
    manage->midi_ch = midi_ch;
    manage->note_number = note;
    system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);
    system_registry->midi_out_control.setProgramChange(midi_ch, program);
    system_registry->midi_out_control.setNoteVelocity(midi_ch, note, 0);
    uint8_t velocity = 0x80 | (_press_velocity > 127 ? 127 : _press_velocity);
    system_registry->midi_out_control.setNoteVelocity(midi_ch, note, velocity);
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
    system_registry->midi_out_control.setNoteVelocity(midi_ch, note, 0);
    manage->note_number = 0xFF;
  }
  if (!on_beat) {
    return;
  }

  note = system_registry->drum_mapping.get8(button_index);

  if (note < 128) {
    // TODO:ボリュームの設定をスロット情報から取得する
    uint8_t volume = 100;
    uint8_t max_chvol = system_registry->runtime_info.getMIDIChannelVolumeMax();
    uint16_t chvolume = volume * max_chvol / 100;
    if (chvolume > 127) { chvolume = 127; }
    midi_ch = def::midi::channel_10;
    manage->midi_ch = midi_ch;
    manage->note_number = note;
    system_registry->midi_out_control.setChannelVolume(midi_ch, chvolume);

    uint8_t velocity = 0x80 | (_press_velocity > 127 ? 127 : _press_velocity);
    system_registry->midi_out_control.setNoteVelocity(def::midi::channel_10, note, velocity);
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
