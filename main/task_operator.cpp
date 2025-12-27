// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "common_define.hpp"

#include "task_operator.hpp"
#include "system_registry.hpp"
#include "file_manage.hpp"
#include "menu_data.hpp"

#if !defined (M5UNIFIED_PC_BUILD)
#include <nvs_flash.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------

static bool isSubButtonSlotSwap(void)
{ // 本体右上レバーを引いている間  サブボタンのスロット選択をシフトする
  return system_registry->runtime_info.getSubButtonSwap();
}

static uint32_t getColorByCommand(const def::command::command_param_t &command_param)
{
  uint32_t color = system_registry->color_setting.getButtonDefaultColor(); //0x555555u;
  switch (command_param.getCommand()) {
  default:
      break;
  case def::command::chord_degree:
    color = system_registry->color_setting.getButtonDegreeColor();
    break;
  case def::command::chord_modifier:
    color = system_registry->color_setting.getButtonModifierColor();
    break;
  case def::command::chord_minor_swap:
    color = system_registry->color_setting.getButtonMinorSwapColor();
    break;
  case def::command::autoplay_switch:
  case def::command::chord_semitone:
    color = system_registry->color_setting.getButtonSemitoneColor();
    break;
  case def::command::note_button:
    color = system_registry->color_setting.getButtonNoteColor();
    break;
  case def::command::drum_button:
    color = system_registry->color_setting.getButtonDrumColor();
    break;
  case def::command::part_on:
  case def::command::part_off:
    color = system_registry->color_setting.getButtonPartColor();
    if (!system_registry->current_slot->chord_part[command_param.getParam() - 1].part_info.getEnabled()) {
      color = (color >> 1) & 0x7F7F7F;
    }
    break;
  case def::command::part_edit:
    color = system_registry->color_setting.getArpeggioNoteBackColor();
    break;
  case def::command::menu_function:
    switch (command_param.getParam()) {
    case def::command::menu_function_t::mf_back:
    case def::command::menu_function_t::mf_exit:
      color = system_registry->color_setting.getButtonDrumColor();
      break;
    case def::command::menu_function_t::mf_enter:
      color = system_registry->color_setting.getButtonNoteColor();
      break;
    default:
      color = system_registry->color_setting.getButtonMenuNumberColor();
      break;
    }
    break;

  case def::command::slot_select:
    {
      auto play_mode = system_registry->song_data.slot[command_param.getParam() - 1].slot_info.getPlayMode();
      switch (play_mode) {
      case def::playmode::chord_mode:
        color = system_registry->color_setting.getButtonDegreeColor();
        break;
      case def::playmode::note_mode:
        color = system_registry->color_setting.getButtonNoteColor();
        break;
      case def::playmode::drum_mode:
        color = system_registry->color_setting.getButtonDrumColor();
        break;
      default:
        break;
      }
    }
    break;

  case def::command::play_mode_set:
    {
      auto play_mode = command_param.getParam();
      switch (play_mode) {
      case def::playmode::chord_mode:
        color = system_registry->color_setting.getButtonDegreeColor();
        break;
      case def::playmode::note_mode:
        color = system_registry->color_setting.getButtonNoteColor();
        break;
      case def::playmode::drum_mode:
        color = system_registry->color_setting.getButtonDrumColor();
        break;
      default:
        break;
      }
    }
    break;

  case def::command::edit_function:
    color = system_registry->color_setting.getButtonCursorColor();
    switch (command_param.getParam()) {
    case def::command::edit_function_t::copy:
    case def::command::edit_function_t::paste:
      color = system_registry->color_setting.getButtonSemitoneColor();
      break;

    case def::command::edit_function_t::ef_mute:
      color = system_registry->color_setting.getButtonDefaultColor();
      break;

    case def::command::edit_function_t::ef_on:
      color = 0xFF8800u; // オレンジ色
      break;

    case def::command::edit_function_t::ef_off:
      color = 0x7F4400u; // 暗いオレンジ色
      break;

    default:
      color = system_registry->color_setting.getButtonMenuNumberColor();
      break;
    }
    break;
  case def::command::edit_exit:
    switch (command_param.getParam()) {
    default:
      color = system_registry->color_setting.getButtonDrumColor();
      break;
    case def::command::edit_exit_t::save:
      color = system_registry->color_setting.getButtonNoteColor();
      break;
    }
    break;

  case def::command::edit_enc2_target:
    color = system_registry->color_setting.getButtonCursorColor();
    // color = 0x996688u;
    break;
  }

  return color;
}

void task_operator_t::start(void)
{
  // Modifierを押した順序の記録を初期化
  memset(_modifier_press_order, 0, sizeof(_modifier_press_order));
  // オンコードボタンを押した順序の記録を初期化
  memset(_bass_degree_press_order, 0, sizeof(_bass_degree_press_order));

#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "operator", this);
#else
  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "operator", 4096, this, def::system::task_priority_operator, &handle, def::system::task_cpu_operator);
  system_registry->operator_command.setNotifyTaskHandle(handle);
  system_registry->working_command.setNotifyTaskHandle(handle);
#endif
}

void task_operator_t::task_func(task_operator_t* me)
{
  uint32_t working_command_change_counter = 0;
  for (;;) {
    system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_OPERATOR);
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(1);
#else
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_OPERATOR);

    bool is_pressed;
    def::command::command_param_t command_param;
    while (system_registry->operator_command.getQueue(&me->_history_code, &command_param, &is_pressed))
    {
      me->commandProccessor(command_param, is_pressed);
#if !defined (M5UNIFIED_PC_BUILD)
      // commander側で待機中の処理があり得るためここでYIELD処理を行う
      taskYIELD();
#endif
    }

    auto tmp = system_registry->working_command.getChangeCounter();
    if (working_command_change_counter != tmp)
    {
      working_command_change_counter = tmp;
  
      me->syncButtonColor();
    }
  }
}

void task_operator_t::syncButtonColor(void)
{
  { // メインボタンの色設定
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto pair = system_registry->command_mapping_current.getCommandParamArray(i);
      uint32_t color = 0;
      bool hit = true;
      for (int j = 0; pair.array[j].command != def::command::none; ++j) {
        auto command_param = pair.array[j];
        color = getColorByCommand(command_param);
        hit &= system_registry->working_command.check(command_param);
      }

      if (!hit) {
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        r = (r * 3) >> 3;
        g = (g * 3) >> 3;
        b = (b * 3) >> 3;
        color = (r << 16) | (g << 8) | b;
      }
      system_registry->rgbled_control.setColor(i, color);
    }
  }

  { // サブボタンの色設定
    bool is_swap = isSubButtonSlotSwap();
    for (int i = 0; i < def::hw::max_sub_button*2; ++i) {
      auto pair = system_registry->sub_button.getCommandParamArray(i);
      auto command_param = pair.array[0];
      auto color = getColorByCommand(command_param);
      bool isWorking = system_registry->working_command.check(command_param);

      if (!isWorking) {
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        if (is_swap == (i < def::hw::max_sub_button)) {
          // RGB色を合成してグレー化する
          // gamma2.0 convert and ITU-R BT.601 RGB to Y convert
          uint32_t y = ( (r * r * 19749)    // R 0.299
                      + (g * g * 38771)    // G 0.587
                      + (b * b *  7530)    // B 0.114
                      ) >> 24;
          y = (y * 3) >> 3;
          color = y | (y << 8) | (y << 16);
        } else {
          int k = 7;
          r = (r * k) >> 4;
          g = (g * k) >> 4;
          b = (b * k) >> 4;
          color = (r << 16) | (g << 8) | b;
        }
      }
      if (is_swap == (i >= def::hw::max_sub_button)) {
        int sub_button_index = i % def::hw::max_sub_button;
        system_registry->rgbled_control.setColor(sub_button_index + def::hw::max_main_button, color);
// M5_LOGE("sub_button_index:%d color:%08x", sub_button_index, color);
      }
      system_registry->sub_button.setSubButtonColor(i, color);
    }
  }
}

void task_operator_t::commandProccessor(const def::command::command_param_t& command_param, bool is_pressed)
{
  const auto command = command_param.getCommand();
  const auto param = command_param.getParam();

  switch (command)
  {
  case def::command::sub_button:
    { // サブボタンの場合は、サブボタンのコマンドマッピングから取得してコマンドを変更する
      uint8_t slot_index = param - 1;
      if (isSubButtonSlotSwap()) {
        slot_index += def::hw::max_sub_button;
      }
      def::command::command_param_array_t pair = system_registry->sub_button.getCommandParamArray(slot_index);
      for (auto &cp : pair.array) {
        if (cp.command == def::command::none || cp.command == def::command::sub_button) { continue; }
        // 変更したコマンドで再入する
        commandProccessor(cp, is_pressed);
      }
    }
    break;

  case def::command::internal_button:
    {
      uint8_t button_index = param - 1;
      def::command::command_param_array_t pair = system_registry->command_mapping_current.getCommandParamArray(button_index);
      for (auto &cp : pair.array) {
        if (cp.command == def::command::none) { continue; }
        // 変更したコマンドで再入する
        commandProccessor(cp, is_pressed);
      }
    }
    break;

  case def::command::chord_bass_semitone:
  case def::command::mapping_switch:
  case def::command::chord_modifier:
    // 動作中コマンドの更新
    if (is_pressed) {
      system_registry->working_command.set(command_param);
    } else {
      system_registry->working_command.clear(command_param);
    }
    break;

  default: 
  // case def::command::slot_select_ud:
  // case def::command::slot_select:  // スロット選択操作に関してはスロット変更関数の中で処理する。
  // case def::command::chord_degree:  // Degree操作に関しては task_kanplay_t で処理する。
    break;
  }

  { // 編集パターンの全クリアボタンに関する特殊処理
    // Clearボタンを一回押すと消去確認モードに移行して画面上のパターンが輪郭表示に変更されて、続けて二回目を押すとパターンデータが消えるという仕様のため、
    // ここでは、一回目のClearボタンを押した後に別の操作をしたなら 消去確認モードを解除する
    if ((_command_history[1] == def::command::command_param_t( def::command::edit_function, def::command::edit_function_t::clear ))
      && (_command_history[1] != command_param)) {
      // M5_LOGV("reset confirm allclear");
      system_registry->chord_play.setConfirm_AllClear(false);
    }
    if ((_command_history[1] == def::command::command_param_t( def::command::edit_function, def::command::edit_function_t::paste ))
      && (_command_history[1] != command_param)) {
      // M5_LOGV("reset confirm paste");
      system_registry->chord_play.setConfirm_Paste(false);
    }
  }


  // コマンド履歴をひとつ後ろにずらす
  memmove(&_command_history[1], &_command_history[0], (max_command_history - 1) * sizeof(_command_history[0]));
  _command_history[0] = command_param;


// M5_LOGV("command:%d value:%d", command, cmd_value);
  switch (command) {
  default: break;
  case def::command::chord_beat:
    if (system_registry->runtime_info.getGuiAutoplayState() != def::play::auto_play_state_t::auto_play_beatmode) {
      switch (system_registry->runtime_info.getSequenceMode())
      {
      case def::seqmode::seq_free_play:
        system_registry->runtime_info.setSequenceMode(def::seqmode::seq_beat_play);
        break;
      default: break;
      }
      system_registry->player_command.addQueue( { def::command::autoplay_switch, def::command::autoplay_switch_t::autoplay_beat } );
    }
    [[fallthrough]]; // chord_beatは外部ビートでの演奏の開始と併せて以下の処理も実行する
  case def::command::set_velocity:
  case def::command::chord_degree:
  case def::command::note_button:
  case def::command::drum_button:
  case def::command::chord_step_reset_request:
  case def::command::autoplay_switch:
  case def::command::play_control:
    system_registry->player_command.addQueue(command_param, is_pressed);
    break;

  case def::command::chord_modifier:
    procChordModifier(command_param, is_pressed);
    break;

  case def::command::chord_minor_swap:
    procChordMinorSwap(command_param, is_pressed);
    break;

  case def::command::chord_semitone:
    procChordSemitone(command_param, is_pressed);
    break;

  case def::command::chord_bass_degree:
    procChordBassDegree(command_param, is_pressed);
    break;

  case def::command::chord_bass_semitone:
    procChordBassSemitone(command_param, is_pressed);
    break;

  // case def::command::swap_sub_button:
  //   updateSubButton();
  //   break;

  case def::command::slot_select:
    if (is_pressed) {
      // パターン編集モードの場合、スロット切替を許可しない
      if (system_registry->runtime_info.getPlayMode() != def::playmode::playmode_t::chord_edit_mode) {
        uint8_t slot_index = param - 1;
        setSlotIndex(slot_index);
        changeCommandMapping();
      }
    }
    break;

  case def::command::slot_select_ud:
// スロット選択ボタンの上下操作 , 現在のスロット番号から相対的に移動する
// InstaChord側からのスロット選択は、スロット番号を相対移動で行う。
    if (is_pressed) {
      // パターン編集モードの場合、スロット切替を許可しない
      if (system_registry->runtime_info.getPlayMode() != def::playmode::playmode_t::chord_edit_mode) {
        auto slot_index = (int)system_registry->runtime_info.getPlaySlot();
        switch (param) {
        case def::command::slot_select_ud_t::slot_next:  slot_index += 1; break;
        case def::command::slot_select_ud_t::slot_prev:  slot_index -= 1; break;
        default: break;
        }
        // スロット番号を範囲内に収まるようループさせる
        while (slot_index < 0) {
          slot_index += def::app::max_slot;
        }
        while (slot_index >= def::app::max_slot) {
          slot_index -= def::app::max_slot;
        }
        setSlotIndex(slot_index);
        changeCommandMapping();
      }
    }
    break;


  case def::command::mapping_switch:
    if (is_pressed) {
      system_registry->runtime_info.setButtonMappingSwitch(param);
      // system_registry->working_command.set(def::command::swap_sub_button);

      // コード演奏モードの場合、アルペジエータのステップを先頭に戻す
      if (system_registry->runtime_info.getCurrentMode() == def::playmode::playmode_t::chord_mode) {
        system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
      }

    } else {
#if 1  // レバーを離したタイミングで元のマッピングに戻す
      system_registry->runtime_info.setButtonMappingSwitch(0);
#else
      // レバーを離したタイミングで元のマッピングに戻すが、前回のコマンドと内容が異なる場合にのみ実施する。
      // これにより、レバー操作を二連続で行った場合はマッピングを維持したままにできる。
      if (command_param != _command_history[2])
      {
        system_registry->runtime_info.setButtonMappingSwitch(0);
      }
#endif
    }
// M5_LOGV("A:%04x  B:%04x  C:%04x  D:%04x", _command_history[0].raw, _command_history[1].raw, _command_history[2].raw, _command_history[3].raw);
// M5_LOGE("mapping_switch %d", system_registry->runtime_info.getButtonMappingSwitch());
    changeCommandMapping();
    break;

  case def::command::master_vol_ud:
    if (is_pressed) {
    // マスターボリューム上下変更操作
      int32_t tmp = system_registry->user_setting.getMasterVolume();
      int add = command_param.getParam();
      add = add < 0 ? -5 : 5;
      tmp += add;
      if (tmp < 0) { tmp = 0; }
      if (tmp > 100) { tmp = 100; }
      system_registry->user_setting.setMasterVolume(tmp);
      // system_registry->operator_command.addQueue( { def::command::master_vol_set, tmp } );
    }
    break;

  case def::command::master_vol_set:
    if (is_pressed) {
      system_registry->user_setting.setMasterVolume(param);
    }
    break;

  case def::command::master_key_ud:
    if (is_pressed) {
      // マスターキー上下変更操作
      int32_t tmp = system_registry->runtime_info.getMasterKey();
      tmp += param;
      while (tmp < 0) { tmp += 12; }
      while (tmp >= 12) { tmp -= 12; }
      system_registry->operator_command.addQueue( { def::command::master_key_set, tmp } );
    }
    break;

  case def::command::master_key_set:
    if (is_pressed) {
      M5_LOGV("master key %d", param);
      system_registry->runtime_info.setMasterKey(param);
    }
    break;

  case def::command::target_key_set:
    if (is_pressed) {
      // InstaChord側から目的のキーを設定するためのコマンド
      // ここでは、InstaChord側からのキー設定を受け取って、スロットの相対キーを相殺した値をマスターキーに設定する。
      // これにより、InstaChord側から設定されたキーが実際の演奏キーになる。
      M5_LOGV("target key %d", param);
      int slot_key = (int8_t)system_registry->current_slot->slot_info.getKeyOffset();
      int master_key = param - slot_key;
      while (master_key < 0) { master_key += 12; }
      while (master_key >= 12) { master_key -= 12; }
      system_registry->runtime_info.setMasterKey(master_key);
    }
    break;

  case def::command::note_scale_ud:
    if (is_pressed) {
      // ノートスケール上下動作
      int32_t tmp = system_registry->current_slot->slot_info.getNoteScale();
      tmp += param;
      while (tmp < 0) { tmp += def::play::note::max_note_scale; }
      while (tmp >= def::play::note::max_note_scale) { tmp -= def::play::note::max_note_scale; }
      system_registry->operator_command.addQueue( { def::command::note_scale_set, tmp + 1 } );
    }
    break;

  case def::command::note_scale_set:
    if (is_pressed) {
      int scale_index = param - 1;
      system_registry->current_slot->slot_info.setNoteScale(scale_index);
      M5_LOGV("note scale %d %s", scale_index, def::play::note::note_scale_name_table[scale_index]);
    }
    break;

  case def::command::file_load_notify:
    if (is_pressed) {

      // file_managerがファイルをメモリ上に展開し終えた後にload_memoryコマンドが実行される
      auto mem = file_manage.getMemoryInfoByIndex(param);
      if (mem != nullptr) {
        switch (mem->dir_type)
        {
        default:
          break;
        case def::app::data_type_t::data_song_preset:
        case def::app::data_type_t::data_song_extra:
        case def::app::data_type_t::data_song_users:
          {
  uint32_t msec = M5.millis();
            bool result = system_registry->backup_song_data.loadSongJSON(mem->data, mem->size);
  msec = M5.millis() - msec;
  M5_LOGD("load time %d", msec);
            if (!result) {
              result = system_registry->backup_song_data.loadText(mem->data, mem->size);
            }
            if (result) {
              system_registry->song_data.assign(system_registry->backup_song_data);
              system_registry->backup_song_data.reset();
              system_registry->unchanged_song_crc32 = system_registry->song_data.crc32();
              
              const auto seqmode = system_registry->runtime_info.getSequenceMode();
              if (system_registry->song_data.sequence.info.getLength() > 0) {
                // シーケンスデータが存在する場合は、フリープレイモードからガイドプレイモードに変更する
                if (seqmode == def::seqmode::seq_free_play || seqmode == def::seqmode::seq_beat_play) {
                  system_registry->operator_command.addQueue( { def::command::sequence_mode_set, def::seqmode::seq_guide_play } );
                }
              } else {
                // シーケンスデータが存在しない場合は、ガイドプレイモードからフリープレイモードに変更する
                if (seqmode == def::seqmode::seq_guide_play || seqmode == def::seqmode::seq_auto_song) {
                  system_registry->operator_command.addQueue( { def::command::sequence_mode_set, def::seqmode::seq_free_play } );
                }
              }
            }
            system_registry->operator_command.addQueue( { def::command::slot_select, 1 } );
            system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
          }
          break;
        }
        system_registry->checkSongModified();
        // メモリを解放しておく
        mem->release();
      }
      changeCommandMapping();
    }
    break;

  case def::command::part_off:
  case def::command::part_on:
  case def::command::part_edit:
    if (is_pressed) {
      uint8_t part_index = param - 1;
      // パートオンまたは編集の場合は当該パートを有効化する
      bool en = def::command::part_off != command;
      system_registry->current_slot->chord_part[part_index].part_info.setEnabled(en);

      auto seqmode = system_registry->runtime_info.getGuiSequenceMode();
      if (seqmode == def::seqmode::seq_free_play) {
        if (def::command::part_edit == command)
        { // 編集に入る前にバックアップする
          system_registry->backup_song_data.assign(system_registry->song_data);
          system_registry->chord_play.setEditTargetPart(part_index);
          system_registry->operator_command.addQueue( { def::command::play_mode_set, def::playmode::playmode_t::chord_edit_mode } );

          // 編集に入る際にオートプレイは無効にする
          system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_none);

          // 編集に入る際に、カーソル位置を左下原点に移動させる
          system_registry->operator_command.addQueue( { def::command::edit_function, def::command::edit_function_t::backhome } );
        }
      }
    }
    break;

  case def::command::play_mode_set:
    if (is_pressed) {
      auto play_mode = (def::playmode::playmode_t)param;
      system_registry->runtime_info.setPlayMode(play_mode);
      if (def::playmode::playmode_t::chord_mode <= play_mode && play_mode <= def::playmode::playmode_t::drum_mode) {
        system_registry->current_slot->slot_info.setPlayMode(play_mode);
        auto slot_index = system_registry->runtime_info.getPlaySlot();
        system_registry->song_data.slot[slot_index].slot_info.setPlayMode(play_mode);
      }

      int mapping_switch = 0;
      if (system_registry->working_command.check( { def::command::mapping_switch, 1 } )) { mapping_switch = 1; }
      if (system_registry->working_command.check( { def::command::mapping_switch, 2 } )) { mapping_switch = 2; }
      if (system_registry->working_command.check( { def::command::mapping_switch, 3 } )) { mapping_switch = 3; }
      system_registry->runtime_info.setButtonMappingSwitch(mapping_switch);
      changeCommandMapping();
    }
    break;

  case def::command::sequence_mode_set:
    if (is_pressed) {
      auto seq_mode = (def::seqmode::seqmode_t)param;
      system_registry->runtime_info.setSequenceStepIndex(0);
      system_registry->runtime_info.setSequenceMode(seq_mode);
      system_registry->player_command.addQueue({ def::command::autoplay_switch, def::command::autoplay_switch_t::autoplay_stop });
    }
    break;

  case def::command::sequence_step_ud:
    if (is_pressed) {
      int current_step = system_registry->runtime_info.getSequenceStepIndex();

      if (param > 0) {
        auto desc = system_registry->current_sequence->getStepDescriptor(current_step);
        if (!desc.empty()) {
          // スロットを反映
          auto slot_index = desc.getSlotIndex();
          if (slot_index != system_registry->runtime_info.getPlaySlot()) {
            setSlotIndex(slot_index);
          }

          // パート有効/無効を反映
          for (int part = 0; part < def::app::max_chord_part; ++part) {
            bool en = desc.getPartEnable(part);
            system_registry->current_slot->chord_part[part].part_info.setEnabled(en);
          }
        }
      }
      system_registry->player_command.addQueue( { def::command::sequence_step_ud, param } );
    }
    break;

  case def::command::system_control:
    if (is_pressed) {
      switch (param) {
      default: break;
      case def::command::system_control_t::sc_power_off:
        system_registry->save();
        system_registry->runtime_info.setPowerOff(def::command::system_control_t::sc_power_off);
        break;

      case def::command::system_control_t::sc_reset:
        system_registry->save();
        system_registry->runtime_info.setPowerOff(def::command::system_control_t::sc_reset);
        break;

      case def::command::system_control_t::sc_save:
        system_registry->save();
        break;

      case def::command::system_control_t::sc_boot:
        {
          // 最後に扱ったソングデータのファイル名とデータタイプを取得
          auto filename = file_manage.getLatestFileName();
          auto datatype = file_manage.getLatestDataType();
          M5_LOGV("file:%s  type:%d", filename.c_str(), (uint8_t)datatype);

          system_registry->file_command.setUpdateList(datatype);
          auto dm = file_manage.getDirManage(datatype);
          if (dm != nullptr) {
            // 前回使用していたファイルを特定する (ファイル名からindex特定)
            for (int retry = 16; dm->isEmpty() && retry; --retry) {
              M5.delay(16);
            }
            auto index = dm->search(filename.c_str());
            if (index >= 0) {
              def::app::file_command_info_t songinfo;
              songinfo.file_index = index;
              songinfo.dir_type = datatype;
              system_registry->file_command.setCurrentSongInfo(songinfo);
            }
          }
          system_registry->syncParams();
          system_registry->updateCRC32();

          // 演奏の強制停止処理を入れておく
          system_registry->player_command.addQueueW( { def::command::play_control, def::command::play_control_t::pc_panic_stop } );
        }
        break;

      case def::command::system_control_t::sc_erase_nvs:
#if !defined (M5UNIFIED_PC_BUILD)
        nvs_flash_erase();
#endif
        break;
      }
    }
    break;

  case def::command::edit_function:
    if (is_pressed) {
      procEditFunction(command_param);
      system_registry->checkSongModified();
    }
    break;

  case def::command::edit_enc2_target:
    {
      int8_t target = system_registry->chord_play.getEditEnc2Target();
      if (is_pressed) {
        target = param;
        system_registry->chord_play.setEditEnc2Target(target);
      }
    }
    break;

  case def::command::edit_enc2_ud:
    if (is_pressed) {
      auto part_index = system_registry->chord_play.getEditTargetPart();
      auto part = &system_registry->current_slot->chord_part[part_index];
      auto part_info = &part->part_info;
      auto enc2_switch = (def::command::edit_enc2_target_t)(system_registry->chord_play.getEditEnc2Target());
      switch (enc2_switch) {
      case def::command::edit_enc2_target_t::part_vol:
        {
          int part_vol = part_info->getVolume();
          part_vol += 5 * param;
          if (part_vol < 0) { part_vol = 0; }
          if (part_vol > 100) { part_vol = 100; }
          part_info->setVolume(part_vol);
        }
        break;

      case def::command::edit_enc2_target_t::position:
        {
          int position = part_info->getPosition();
          position += 4 * param;
          if (position < def::app::min_position) { position = def::app::min_position; }
          if (position > def::app::max_position) { position = def::app::max_position; }
          part_info->setPosition(position);
        }
        break;

      case def::command::edit_enc2_target_t::voicing:
        {
          int voicing = part_info->getVoicing();
          voicing += param;
          if (voicing < 0) { voicing = 0; }
          if (voicing >= KANTANMusic_MAX_VOICING) { voicing = KANTANMusic_MAX_VOICING - 1; }
          part_info->setVoicing(voicing);
        }
        break;

      case def::command::edit_enc2_target_t::velocity:
        {
          int velo = system_registry->runtime_info.getEditVelocity();
          int add = 5 * param;
          velo += add;
          if (add < 0) {
            if (velo < 5) { velo = - 5; }
          } else {
            if (velo < 5) { velo = 5; }
            if (velo > 100) { velo = 100; }
          }
          system_registry->runtime_info.setEditVelocity(velo);
        }
        break;

      case def::command::edit_enc2_target_t::program:
        {
          int program = part_info->getTone();
          program += param;
          if (program < 0) { program = 0; }
          if (program > def::app::max_program_number - 1) { program = def::app::max_program_number - 1; }
          part_info->setTone(program);
        }
        break;

      case def::command::edit_enc2_target_t::endpoint:
        {
          int endpoint = part_info->getLoopStep();
          endpoint += 2 * param;
          if (endpoint < 1) { endpoint = 1; }
          if (endpoint > def::app::max_arpeggio_step - 1) { endpoint = def::app::max_arpeggio_step - 1; }
          part_info->setLoopStep(endpoint);
        }
        break;

      case def::command::edit_enc2_target_t::banlift:
        {
          int step = part_info->getAnchorStep();
          step += 2 * param;
          if (step < 0) { step = 0; }
          if (step > def::app::max_arpeggio_step - 2) { step = def::app::max_arpeggio_step - 2; }
          part_info->setAnchorStep(step);
        }
        break;

      case def::command::edit_enc2_target_t::displacement:
        {
          int msec = part_info->getStrokeSpeed();
          msec += 5 * param;
          if (msec < 5) { msec = 5; }
          if (msec > 50) { msec = 50; }
          part_info->setStrokeSpeed(msec);
          system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::testplay } );
        }
        break;
      }
      system_registry->checkSongModified();
    }
    break;

  case def::command::edit_exit:
    if (is_pressed) {
      auto flag = (def::command::edit_exit_t)param;
      switch (flag) {
      case def::command::edit_exit_t::discard:
        { // データ破棄する場合、編集前の状態を反映する
          system_registry->song_data.assign(system_registry->backup_song_data);
        }
        break;

      default:
      case def::command::edit_exit_t::save:
        { // 確定操作は特になにもしなくてよい。
        }
        break;
      }
      // 編集の終了時にレジューム情報も更新しておく
      system_registry->operator_command.addQueue( { def::command::system_control, def::command::sc_save } );

      // コード演奏モードに戻す
      system_registry->operator_command.addQueue( { def::command::play_mode_set, def::playmode::playmode_t::chord_mode } );
      // 演奏ステップを先頭に戻す
      system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
      system_registry->checkSongModified();
    }
    break;

  // メニューを新規に開く操作
  case def::command::menu_open:
    if (is_pressed) {
      menu_control.openMenu( static_cast<def::menu_category_t>(param) );

      changeCommandMapping();
    }
    break;

  case def::command::menu_function:
    if (is_pressed) {
      switch (param) {
      case def::command::mf_down:
      case def::command::mf_up:
        menu_control.inputUpDown(param == def::command::mf_down ? -1 : 1);
        break;

      case def::command::mf_enter: // 子階層に入る / アイテムの実行
        menu_control.enter();
        break;

      case def::command::mf_back: // 親階層に戻る
        if (!menu_control.exit()) {
          afterMenuClose();
        }
        break;

      case def::command::mf_exit: // メニューをすべて閉じる
        while (menu_control.exit()) { M5.delay(1); };
        afterMenuClose();
        break;

      default: // mf_0 ～ mf_9
        { // 番号指定
          uint8_t number = param - def::command::mf_0;
          if (number < 10) {
            menu_control.inputNumber(number);
          }
        }
        break;
      }
      system_registry->checkSongModified();
    }
    break;
  }
}

// 最上位メニューから抜けた時の処理
void task_operator_t::afterMenuClose(void)
{
  system_registry->runtime_info.setMenuVisible( false );
  changeCommandMapping();

 // メニューから抜ける時はオートプレイは無効にする
  system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_none);

  // 設定を保存しておく
  system_registry->operator_command.addQueue( { def::command::system_control, def::command::sc_save } );
}

void task_operator_t::procEditFunction(const def::command::command_param_t& command_param)
{
  auto part_index = system_registry->chord_play.getEditTargetPart();
  auto part = &system_registry->current_slot->chord_part[part_index];
  int cursor_x = system_registry->chord_play.getPartStep(part_index);
  if (cursor_x < 0) {
    cursor_x = 0;
  }
  int cursor_y = system_registry->chord_play.getCursorY();
  int new_x = cursor_x;
  int new_y = cursor_y;

  auto param = command_param.getParam();
  switch (param) {
  case def::command::edit_function_t::left:       new_x -= 1; break;
  case def::command::edit_function_t::right:      new_x += 1; break;
  case def::command::edit_function_t::edit_down:  new_y += 1; break;
  case def::command::edit_function_t::edit_up:    new_y -= 1; break;
  case def::command::edit_function_t::page_left:  new_x -= 8; break;
  case def::command::edit_function_t::page_right: new_x += 8; break;
  case def::command::edit_function_t::backhome:
    new_x = 0;
    new_y = 6;
    break;

  case def::command::edit_function_t::ef_on:   // ピッチのベロシティプロット ON
  case def::command::edit_function_t::ef_off:  // ピッチのベロシティプロット OFF
  case def::command::edit_function_t::ef_mute: // ピッチのベロシティプロット Mute
    {
      int pitch = system_registry->chord_play.getCursorY();
      if (pitch < 0) {
        pitch = 0;
      }
      if (pitch == 6 && !part->part_info.isDrumPart())
      { // ドラムパート以外の場合は奏法(スタイル)を変更する (同時発音・アップストローク・ダウンストローク・ミュート)
        auto style = part->arpeggio.getStyle(cursor_x);
        auto tmp = style + 1;
        if (param == def::command::edit_function_t::ef_mute) {
          tmp = def::play::arpeggio_style_t::mute;
        } else if (param == def::command::edit_function_t::ef_off) {
          tmp = def::play::arpeggio_style_t::same_time;
        }
        if (tmp >= def::play::arpeggio_style_t::arpeggio_style_max) {
          tmp = 0;
        }
        style = (def::play::arpeggio_style_t)tmp;
        part->arpeggio.setStyle(cursor_x, style);
        system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::single } );
      } else {
        int new_velocity = 0;
        if (param == def::command::edit_function_t::ef_mute) {
          new_velocity = -5;
        } else if (param == def::command::edit_function_t::ef_on) {
          new_velocity = system_registry->runtime_info.getEditVelocity();
        }
        part->arpeggio.setVelocity(cursor_x, pitch, new_velocity);
        // ドットを置いた時にプレビュー音を鳴らす
        system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::single } );
      }
    }
    break;

  case def::command::edit_function_t::onoff: // ピッチのベロシティプロットのON/OFFを切り替える
    {
      int pitch = system_registry->chord_play.getCursorY();
      if (pitch < 0) {
        pitch = 0;
      }
      if (pitch == 6 && !part->part_info.isDrumPart()) {
        auto style = part->arpeggio.getStyle(cursor_x);
        auto tmp = style + 1;
        if (tmp >= def::play::arpeggio_style_t::arpeggio_style_max) {
          tmp = 0;
        }
        style = (def::play::arpeggio_style_t)tmp;
        part->arpeggio.setStyle(cursor_x, style);
        system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::single } );
      } else {
        int new_velocity = system_registry->runtime_info.getEditVelocity();
        int prev_velocity = part->arpeggio.getVelocity(cursor_x, pitch);
        if (prev_velocity != new_velocity) {
          prev_velocity = new_velocity;
          // ドットを置いた時にプレビュー音を鳴らす
          system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::single } );
        } else {
          prev_velocity = 0;
        }
        part->arpeggio.setVelocity(cursor_x, pitch, prev_velocity);
      }
    }
    break;

  case def::command::edit_function_t::clear:
  // クリア操作は2回連続して行うことにより実施される
    if (command_param == _command_history[2]) {
      part->arpeggio.reset();
    } else {
// M5_LOGV("set confirm");
      system_registry->chord_play.setConfirm_AllClear(true);
    }
    break;

  case def::command::edit_function_t::copy:
    {
      int left = part->part_info.getAnchorStep();
      int right = part->part_info.getLoopStep();
      if (left > right) {
        std::swap(left, right);
      }
      int w = right - left + 1;
      system_registry->chord_play.setRangeWidth(w);
      system_registry->clipboard_arpeggio.copyFrom(0, part->arpeggio, left, w);
    }
    break;

  case def::command::edit_function_t::paste:
    // ペースト操作は2回連続して行うことにより実施される
    if (command_param == _command_history[2]) {
      int w = system_registry->chord_play.getRangeWidth();
      if (w > def::app::max_arpeggio_step - cursor_x) {
        w = def::app::max_arpeggio_step - cursor_x;
      }
      part->arpeggio.copyFrom(cursor_x, system_registry->clipboard_arpeggio, 0, w);
    }
    else
    {
      system_registry->chord_play.setConfirm_Paste(true);
    }
    break;
  }

  // 移動した場合はプレビュー音の再生などを行う
  if (cursor_x != new_x || cursor_y != new_y) {
    if (cursor_y != new_y) {
      if (new_y < 0) { new_y = 0; }
      if (new_y > def::app::max_cursor_y - 1) { new_y = def::app::max_cursor_y - 1; }
      system_registry->chord_play.setCursorY(new_y);
    }
    if (cursor_x != new_x) {
      int end_point = 1 + part->part_info.getLoopStep();
      while (new_x < 0) { new_x += end_point; }
      while (new_x >= end_point) { new_x -= end_point; }
      // 横座標が先頭にある場合は全てのパートを先頭に戻させる
      if (new_x == 0) {
        system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
      }
    }
    system_registry->chord_play.setPartStep(part_index, new_x);
    system_registry->player_command.addQueue( { def::command::sound_effect, def::command::sound_effect_t::single } );
  }
}

void task_operator_t::procChordModifier(const def::command::command_param_t& command_param, const bool is_pressed)
{
  auto modifier = (KANTANMusic_Modifier)command_param.getParam();
  for (int i = 0; i < sizeof(_modifier_press_order)-1; ++i) {
    if (_modifier_press_order[i] == modifier) {
      memmove(&_modifier_press_order[i], &_modifier_press_order[i + 1], sizeof(_modifier_press_order) - i);
      _modifier_press_order[sizeof(_modifier_press_order) - 1] = KANTANMusic_Modifier::KANTANMusic_Modifier_None;
    }
  }
  if (is_pressed) {
    memmove(&_modifier_press_order[1], _modifier_press_order, sizeof(_modifier_press_order) - 1);
    _modifier_press_order[0] = modifier;
  }
  modifier = static_cast<KANTANMusic_Modifier>(_modifier_press_order[0]);
#if defined ( KANTAN_USE_MODIFIER_6_AS_7M7 )
  if (modifier == KANTANMusic_Modifier_7
  ||  modifier == KANTANMusic_Modifier_M7) {
    uint32_t bitmask = 0;
    // 現在押されているModifierのビットマスクを作成
    for (int i = 0; i < sizeof(_modifier_press_order)-1; ++i) {
      auto mod = _modifier_press_order[i];
      if (!mod) break;
      bitmask |= 1 << mod;
    }
    uint32_t mask = (1 << KANTANMusic_Modifier_7) | (1 << KANTANMusic_Modifier_M7);
    if ((bitmask & mask) == mask) { // _7とM7の同時操作は_6として扱う
      modifier = KANTANMusic_Modifier_6;
    }
  }
#endif
  int8_t prev_modifier = system_registry->chord_play.getChordModifier();
  if (prev_modifier != modifier)
  {
    system_registry->working_command.clear( { def::command::chord_modifier, prev_modifier } );
    system_registry->chord_play.setChordModifier(modifier);
    // M5_LOGV("modifier %d", (int)modifier);
  }
#if defined ( KANTAN_USE_MODIFIER_6_AS_7M7 )
  if (prev_modifier == KANTANMusic_Modifier_6) {
    system_registry->working_command.clear( { def::command::chord_modifier, KANTANMusic_Modifier_7 } );
    system_registry->working_command.clear( { def::command::chord_modifier, KANTANMusic_Modifier_M7 } );
  }
#endif
  system_registry->working_command.set( { def::command::chord_modifier, modifier } );
#if defined ( KANTAN_USE_MODIFIER_6_AS_7M7 )
  if (modifier == KANTANMusic_Modifier_6) {
    system_registry->working_command.set( { def::command::chord_modifier, KANTANMusic_Modifier_7 } );
    system_registry->working_command.set( { def::command::chord_modifier, KANTANMusic_Modifier_M7 } );
  }
#endif
}

void task_operator_t::procChordMinorSwap(const def::command::command_param_t& command_param, const bool is_pressed)
{
  system_registry->runtime_info.addChordMinorSwapPressCount( is_pressed ? 3 : -4);

  bool prev_value = system_registry->chord_play.getChordMinorSwap();
  bool value = system_registry->runtime_info.getChordMinorSwapPressCount() > 0;
  if (prev_value != value)
  {
    system_registry->chord_play.setChordMinorSwap(value);
    if (value) {
      system_registry->working_command.set(  { def::command::chord_minor_swap, 1 } );
    } else {
      system_registry->working_command.clear({ def::command::chord_minor_swap, 1 } );
    }
  }

  // system_registry->chord_play.setChordMinorSwap(system_registry->working_command.check( { def::command::chord_minor_swap, 1 } ));
  // system_registry->chord_play.setChordMinorSwap(is_pressed);
  // メジャー・マイナースワップボタンを押したタイミングでコード演奏のアルペジオパターン先頭にステップを戻す
  // system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
}

void task_operator_t::procChordSemitone(const def::command::command_param_t& command_param, const bool is_pressed)
{
  switch (command_param.getParam()) {
  case semitone_flat:  system_registry->runtime_info.addChordSemitoneFlatPressCount( is_pressed ? 3 : -4); break;
  case semitone_sharp: system_registry->runtime_info.addChordSemitoneSharpPressCount(is_pressed ? 3 : -4); break;
  default: break;
  }
  int value = system_registry->runtime_info.getChordSemitoneShift();
  int prev_value = system_registry->chord_play.getChordSemitoneShift();

  if (prev_value != value)
  {
    system_registry->chord_play.setChordSemitoneShift(value);
    switch (value) {
    case -1:
      system_registry->working_command.set(  { def::command::chord_semitone, semitone_flat } );
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_sharp } );
      break;
    case 1:
      system_registry->working_command.set(  { def::command::chord_semitone, semitone_sharp } );
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_flat } );
      break;
    default:
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_flat } );
      system_registry->working_command.clear({ def::command::chord_semitone, semitone_sharp } );
      break;
    }
  }

  // if (system_registry->working_command.check( { def::command::chord_semitone, 1 } )) { --value; }
  // if (system_registry->working_command.check( { def::command::chord_semitone, 2 } )) { ++value; }
  // system_registry->chord_play.setChordSemitone(value);
  // 半音変更ボタンを操作したタイミングでコード演奏のアルペジオパターン先頭にステップを戻す
  // system_registry->player_command.addQueue( { def::command::chord_step_reset_request, 1 } );
}

void task_operator_t::procChordBassDegree(const def::command::command_param_t& command_param, const bool is_pressed)
{
  // M5_LOGV("chord_bass_degree %d %d", command_param.getParam(), is_pressed);
  // TODO: 暫定実装。
  // Modifierボタンと同じように、同時押しされた場合には最後に押されたボタンの値を優先し、
  // 離された時点でも別のボタンが押されていればそちらに変更するなどの処理が必要になる。
  auto bass_degree = command_param.getParam();
  for (int i = 0; i < sizeof(_bass_degree_press_order)-1; ++i) {
    if (_bass_degree_press_order[i] == bass_degree) {
      memmove(&_bass_degree_press_order[i], &_bass_degree_press_order[i + 1], sizeof(_bass_degree_press_order) - i);
      _bass_degree_press_order[sizeof(_bass_degree_press_order) - 1] = 0;
    }
  }
  if (is_pressed) {
    memmove(&_bass_degree_press_order[1], _bass_degree_press_order, sizeof(_bass_degree_press_order) - 1);
    _bass_degree_press_order[0] = bass_degree;
  }
  bass_degree = _bass_degree_press_order[0];

  system_registry->chord_play.setChordBassDegree(bass_degree);
}

void task_operator_t::procChordBassSemitone(const def::command::command_param_t& command_param, const bool is_pressed)
{
  // M5_LOGV("chord_bass_semitone %d %d", command_param.getParam(), is_pressed);
  int value = 0;
  if (system_registry->working_command.check( { def::command::chord_bass_semitone, 1 } )) { --value; }
  if (system_registry->working_command.check( { def::command::chord_bass_semitone, 2 } )) { ++value; }
  system_registry->chord_play.setChordBassSemitoneShift(value);
}

// スロット番号設定操作
void task_operator_t::setSlotIndex(uint8_t slot_index)
{
  while (slot_index >= def::app::max_slot) { slot_index -= def::app::max_slot; }
  M5_LOGV("slot set %d", slot_index);

  int8_t prev_slot = system_registry->runtime_info.getPlaySlot();
  if (prev_slot != slot_index)
  {
    system_registry->working_command.clear( { def::command::slot_select, 1 + prev_slot } );
  }
  system_registry->runtime_info.setPlaySlot(slot_index);
  system_registry->working_command.set( { def::command::slot_select, 1 + slot_index } );

  system_registry->runtime_info.setPlayMode(system_registry->current_slot->slot_info.getPlayMode());
}

// 現在の状態に合わせてボタンのコマンドマッピングを変更する
void task_operator_t::changeCommandMapping(void)
{
  auto mode = system_registry->runtime_info.getCurrentMode();
  auto map_index = system_registry->runtime_info.getButtonMappingSwitch();
  auto sub_map = def::command::command_mapping_sub_button_normal_table;
  auto main_map = def::command::command_mapping_chord_play_table;
  bool custom_map = false;

  switch (mode) {
  default:
  case def::playmode::playmode_t::chord_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_chord_play_table,
        def::command::command_mapping_chord_alt1_table,
        def::command::command_mapping_chord_alt2_table,
        def::command::command_mapping_chord_alt3_table,
      };
      main_map = tbl[map_index];
      custom_map = (map_index == 0);
    }
    break;

  case def::playmode::playmode_t::note_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_note_play_table,
        def::command::command_mapping_note_alt1_table,
        def::command::command_mapping_note_alt1_table,
        def::command::command_mapping_note_alt1_table,
      };
      main_map = tbl[map_index];
    }
    break;

  case def::playmode::playmode_t::drum_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_drum_play_table,
        def::command::command_mapping_drum_alt1_table,
        def::command::command_mapping_drum_alt1_table,
        def::command::command_mapping_drum_alt1_table,
      };
      main_map = tbl[map_index];
    }
    break;

  case def::playmode::playmode_t::chord_edit_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_chord_edit_table,
        def::command::command_mapping_edit_alt_table,
        def::command::command_mapping_edit_alt_table,
        def::command::command_mapping_edit_alt_table,
      };
      main_map = tbl[map_index];
      custom_map = (map_index != 0);
    }
    sub_map = def::command::command_mapping_sub_button_edit_table;
    break;

  case def::playmode::playmode_t::seq_edit_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_sequence_edit_table,
        def::command::command_mapping_chord_alt1_table,
        def::command::command_mapping_chord_alt2_table,
        def::command::command_mapping_chord_alt3_table,
      };
      main_map = tbl[map_index];
      custom_map = (map_index == 0);
    }
    break;

  case def::playmode::playmode_t::seq_play_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_sequence_play_table,
        def::command::command_mapping_chord_alt1_table,
        def::command::command_mapping_chord_alt2_table,
        def::command::command_mapping_chord_alt3_table,
      };
      main_map = tbl[map_index];
      custom_map = (map_index == 0);
    }
    break;

  case def::playmode::playmode_t::menu_mode:
    {
      static constexpr const def::command::command_param_array_t* tbl[] = {
        def::command::command_mapping_menu_table,
        def::command::command_mapping_menu_alt1_table,
        def::command::command_mapping_menu_alt2_table,
        def::command::command_mapping_menu_alt3_table,
      };
      main_map = tbl[map_index];
      sub_map = def::command::command_mapping_sub_button_normal_table;
    }
    break;
  }

  for (int i = 0; i < def::hw::max_button_mask; ++i) {
    auto pair = main_map[i];
    system_registry->command_mapping_current.setCommandParamArray(i, pair);
  }
  if (custom_map) {
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto pair = system_registry->command_mapping_internal->getCommandParamArray(i);
      system_registry->command_mapping_current.setCommandParamArray(i, pair);
    }
  }

 // サブボタンのコマンドマッピングを変更する
  for (int i = 0; i < def::hw::max_sub_button*2; ++i) {
    system_registry->sub_button.setCommandParamArray(i, sub_map[i]);
  }

  syncButtonColor();
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
