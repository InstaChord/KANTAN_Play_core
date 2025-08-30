// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "common_define.hpp"

#include "task_commander.hpp"
#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------

class commander_t {
  // チャタリング防止のための前回ボタンを押したタイミングの記録
  uint32_t _chattering_prev_msec[def::hw::max_button_mask];

  // ボタンを押した時に発動したコマンドを記憶するためのキャッシュ
  def::command::command_param_array_t _pressed_command_cache[def::hw::max_button_mask];
  uint32_t _prev_bitmask;

  // チャタリング防止処理を行う対象のボタンのビットマスク
  const uint32_t _chattering_target_bitmask;
  const uint16_t _mask_all;
  const uint16_t _mask_single;
  const uint16_t _thresh_press;
  const uint16_t _thresh_release;
  const uint8_t _bitlength = 1; // ボタンひとつあたりのビット数 (通常は 1。 アナログ扱いの PortB は 8bitとする) 
  const bool _use_internal_imu = false;

public:

  commander_t(uint8_t bitlen, bool use_internal_imu, uint32_t chattering_bitmask)
  : _chattering_target_bitmask { chattering_bitmask }
  , _mask_all       { (uint16_t)((1 << bitlen)- 1 ) }
  , _mask_single    { (uint16_t)( 1 <<(bitlen - 1)) }
  , _thresh_press   { (uint16_t)( _mask_all ^ (_mask_all >> 2) ) }
  , _thresh_release { (uint16_t)( _mask_all - _thresh_press ) }
  , _bitlength { bitlen }
  , _use_internal_imu { use_internal_imu }
  {}

  void start(void)
  {
    memset(_chattering_prev_msec, 0, sizeof(_chattering_prev_msec));
    memset(_pressed_command_cache, 0, sizeof(_pressed_command_cache));
    _prev_bitmask = 0;
  }

  uint32_t update(uint32_t raw_value, uint32_t msec, system_registry_t::reg_command_mapping_t* command_mapping)
  {
    uint32_t delay_msec = 0xffffffffUL;
    if (_prev_bitmask == raw_value) { return delay_msec; }

    uint8_t chattering_threshold_msec = system_registry.user_setting.getChatteringThreshold();
    bool imu_requested = false;
    uint8_t imu_level = system_registry.user_setting.getImuVelocityLevel();
    static constexpr const uint8_t imu_ratio_table[] = { 0, 127, 255 };
    static constexpr const uint8_t imu_base_table[] = { 127, 63, 0 };
    if (imu_level >= sizeof(imu_ratio_table) / sizeof(imu_ratio_table[0])) { imu_level = 0; }
    uint8_t imu_ratio = imu_ratio_table[imu_level];
    uint8_t imu_base = imu_base_table[imu_level];

    // 前回のボタン状態との差分を取得
    uint32_t xor_bitmask = _prev_bitmask ^ raw_value;
    uint32_t shift_value = raw_value;
    uint32_t prev_value = _prev_bitmask;

    _prev_bitmask = raw_value;

    for (int i = 0; xor_bitmask; ++i, xor_bitmask >>= _bitlength, shift_value >>= _bitlength, prev_value >>= _bitlength) {
      // 変化がない箇所はスキップ
      if (0 == (xor_bitmask & _mask_all)) { continue; }

      uint32_t value = shift_value & _mask_all;
      bool pressed  = value >= _thresh_press;
      bool released = value <= _thresh_release;
      if (pressed == released) { continue; }

      if (_bitlength > 1) {
        uint32_t prev = prev_value & _mask_all;
        bool prev_pressed  = prev >= _thresh_press;
        bool prev_released = prev <= _thresh_release;
        if (pressed == prev_pressed && released == prev_released) { continue; }
      }

      // チャタリング防止判定
      if (_chattering_target_bitmask & (1 << i)) {
        if (pressed) {
          // 押したタイミングを記録しておく
          _chattering_prev_msec[i] = msec;
        } else {
          int32_t diff = msec - _chattering_prev_msec[i];
          diff = chattering_threshold_msec - diff;

          if (0 < diff) {
            // 押してから離すまでの時間が短すぎる場合はチャタリングの可能性を考慮して、まだ押したままという扱いにする
            _prev_bitmask |= _mask_all << (i * _bitlength);
            // 次回のdelay終了タイミングがチャタリング判定期間の終了タイミングになるよう設定
            if (delay_msec > diff) {
              delay_msec = diff;
            }
            continue;
          }
        }
      }

      def::command::command_param_array_t command_param_array;
      if (pressed) { // ボタンを押したときのコマンドセットを記憶しておく
        command_param_array = command_mapping->getCommandParamArray(i);
        _pressed_command_cache[i] = command_param_array;
        if (!imu_requested) {
          // 演奏に関連するボタンを押したときIMUベロシティを反映する
          switch (command_param_array.array[0].command) {
          default: break;
          case def::command::chord_degree:
          case def::command::note_button:
          case def::command::drum_button:
            imu_requested = true;
            int velocity = imu_base;
            if (_use_internal_imu && imu_ratio) {
              uint32_t imu_sd = system_registry.internal_imu.getImuStandardDeviation();
              velocity += sqrtf(sqrtf(imu_sd)) * imu_ratio / 100;
              if (velocity < 1) { velocity = 1; }
              if (velocity > 255) { velocity = 255; }
            }
            system_registry.operator_command.addQueue( { def::command::set_velocity, velocity } );
            break;
          }
        }
      } else {
        // 離した時は前回押したときのコマンドセットを取得する。
        // こうする理由はコマンドマッピングが変更されても、押した時と離したときの整合性が取れるようにするため
        command_param_array = _pressed_command_cache[i];
      }
      for (auto command_param : command_param_array.array) {
// M5_LOGV("command_param:%04x", command_param.raw);
        uint8_t command = command_param.getCommand();
        if (command == 0) { continue; }
        system_registry.operator_command.addQueue(command_param, pressed);
      }
    }
    return delay_msec;
  }
#if 0
  uint32_t update_bitmask(uint32_t btn_bitmask, uint32_t msec, system_registry_t::reg_command_mapping_t* command_mapping)
  {
    uint32_t delay_msec = 0xffffffffUL;
    if (_prev_bitmask == btn_bitmask) { return delay_msec; }

    uint8_t chattering_threshold_msec = system_registry.user_setting.getChatteringThreshold();
    bool imu_requested = false;
    uint8_t imu_level = system_registry.user_setting.getImuVelocityLevel();
    static constexpr const uint8_t imu_ratio_table[] = { 0, 127, 255 };
    static constexpr const uint8_t imu_base_table[] = { 127, 63, 0 };
    if (imu_level >= sizeof(imu_ratio_table) / sizeof(imu_ratio_table[0])) { imu_level = 0; }
    uint8_t imu_ratio = imu_ratio_table[imu_level];
    uint8_t imu_base = imu_base_table[imu_level];

    // 前回のボタン状態との差分を取得
    uint32_t xor_bitmask = _prev_bitmask ^ btn_bitmask;
    _prev_bitmask = btn_bitmask;

    uint32_t shift_mask = 1;
    for (int i = 0; xor_bitmask; ++i, shift_mask <<= 1) {
      bool modified = xor_bitmask & 1;
      bool pressed = btn_bitmask & shift_mask;
      xor_bitmask >>= 1;
      // 変化がない箇所はスキップ
      if (!modified) { continue; }
        // チャタリング防止判定
      if (shift_mask & _chattering_target_bitmask) {
        if (pressed) {
          // 押したタイミングを記録しておく
          _chattering_prev_msec[i] = msec;
        } else {
          int32_t diff = msec - _chattering_prev_msec[i];
          diff = chattering_threshold_msec - diff;

          if (0 < diff) {
            // 押してから離すまでの時間が短すぎる場合はチャタリングの可能性を考慮して、まだ押したままという扱いにする
            _prev_bitmask |= shift_mask;
            // 次回のdelay終了タイミングがチャタリング判定期間の終了タイミングになるよう設定
            if (delay_msec > diff) {
              delay_msec = diff;
            }
            continue;
          }
        }
      }

      def::command::command_param_array_t command_param_array;
      if (pressed) { // ボタンを押したときのコマンドセットを記憶しておく
        command_param_array = command_mapping->getCommandParamArray(i);
        _pressed_command_cache[i] = command_param_array;
        if (!imu_requested) {
          // 演奏に関連するボタンを押したときIMUベロシティを反映する
          switch (command_param_array.array[0].command) {
          default: break;
          case def::command::chord_degree:
          case def::command::note_button:
          case def::command::drum_button:
            imu_requested = true;
            int velocity = imu_base;
            if (_use_internal_imu && imu_ratio) {
              uint32_t imu_sd = system_registry.internal_imu.getImuStandardDeviation();
              velocity += sqrtf(sqrtf(imu_sd)) * imu_ratio / 100;
              if (velocity < 1) { velocity = 1; }
              if (velocity > 255) { velocity = 255; }
            }
            system_registry.operator_command.addQueue( { def::command::set_velocity, velocity } );
            break;
          }
        }
      } else {
        // 離した時は前回押したときのコマンドセットを取得する。
        // こうする理由はコマンドマッピングが変更されても、押した時と離したときの整合性が取れるようにするため
        command_param_array = _pressed_command_cache[i];
      }
      for (auto command_param : command_param_array.array) {
// M5_LOGV("command_param:%04x", command_param.raw);
        uint8_t command = command_param.getCommand();
        if (command == 0) { continue; }
        system_registry.operator_command.addQueue(command_param, pressed);
      }
    }
    return delay_msec;
  }


  uint32_t update_analog(uint32_t analog_values, uint32_t msec, system_registry_t::reg_command_mapping_t* command_mapping)
  {
    uint32_t delay_msec = 0xffffffffUL;
    if (_prev_bitmask == analog_values) { return delay_msec; }

    // 前回のボタン状態との差分を取得
    uint32_t xor_bitmask = _prev_bitmask ^ analog_values;
    _prev_bitmask = analog_values;

    for (int i = 0; xor_bitmask; ++i, xor_bitmask >>= 8) {
      uint8_t velocity = analog_values;
      analog_values >>= 8;

      bool modified = xor_bitmask & 0xFF;
      // 変化がない箇所はスキップ
      if (!modified) { continue; }
      bool pressed = velocity >= 64;

      // コマンドマッピング表にあるコマンドを発動する
      // (コマンドは1Byte+データ1Byteの計2Byteだが１ボタンに最大2個のコマンドが登録でき4Byteとなる)
      def::command::command_param_array_t command_param_array;
      if (pressed) { // ボタンを押したときのコマンドセットを記憶しておく
        command_param_array = command_mapping->getCommandParamArray(i);
        _pressed_command_cache[i] = command_param_array;
        {
          // 演奏に関連するボタンを押したときIMUベロシティを反映する
          switch (command_param_array.array[0].command) {
          default: break;
          case def::command::chord_degree:
          case def::command::note_button:
          case def::command::drum_button:
            system_registry.operator_command.addQueue( { def::command::set_velocity, velocity } );
            break;
          }
        }
      } else {
        // 離した時は前回押したときのコマンドセットを取得する。
        // こうする理由はコマンドマッピングが変更されても、押した時と離したときの整合性が取れるようにするため
        command_param_array = _pressed_command_cache[i];
      }
      for (auto command_param : command_param_array.array) {
// M5_LOGV("command_param:%04x", command_param.raw);
        uint8_t command = command_param.getCommand();
        if (command == 0) { continue; }
        system_registry.operator_command.addQueue(command_param, pressed);
      }
    }
    return delay_msec;
  }
#endif
};

static commander_t commander_internal { 1, true , (1 << def::hw::max_rgb_led) - 1};
static commander_t commander_port_a   { 1, false, ~0u };
static commander_t commander_port_b   { 8, false, ~0u };

void task_commander_t::start(void)
{
  // ボタンのマスク状態をクリア
  system_registry.internal_input.setButtonBitmask(0x00);

  commander_internal.start();
  commander_port_a.start();
  commander_port_b.start();

#if defined (M5UNIFIED_PC_BUILD)
  // 
  static constexpr const SDL_KeyCode keymap[] = {
    SDL_KeyCode::SDLK_z, SDL_KeyCode::SDLK_x, SDL_KeyCode::SDLK_c, SDL_KeyCode::SDLK_v, SDL_KeyCode::SDLK_b,
    SDL_KeyCode::SDLK_a, SDL_KeyCode::SDLK_s, SDL_KeyCode::SDLK_d, SDL_KeyCode::SDLK_f, SDL_KeyCode::SDLK_g,
    SDL_KeyCode::SDLK_q, SDL_KeyCode::SDLK_w, SDL_KeyCode::SDLK_e, SDL_KeyCode::SDLK_r, SDL_KeyCode::SDLK_t,
    SDL_KeyCode::SDLK_1, SDL_KeyCode::SDLK_2, SDL_KeyCode::SDLK_3, SDL_KeyCode::SDLK_4,
    SDL_KeyCode::SDLK_5, SDL_KeyCode::SDLK_6,
    SDL_KeyCode::SDLK_7, SDL_KeyCode::SDLK_8, SDL_KeyCode::SDLK_9,
    SDL_KeyCode::SDLK_y, SDL_KeyCode::SDLK_i, SDL_KeyCode::SDLK_u,
    SDL_KeyCode::SDLK_h, SDL_KeyCode::SDLK_k, SDL_KeyCode::SDLK_j,
    SDL_KeyCode::SDLK_n, SDL_KeyCode::SDLK_m,
  };
  for (int i = 0; i < sizeof(keymap) / sizeof(keymap[0]); ++i) {
    m5gfx::Panel_sdl::addKeyCodeMapping(keymap[i], i);
  }
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "command", this);
#else
  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "command", 1024 * 3, this, def::system::task_priority_commander, &handle, def::system::task_cpu_commander);
  system_registry.internal_input.setNotifyTaskHandle(handle);
  system_registry.external_input.setNotifyTaskHandle(handle);
#endif
}

void task_commander_t::task_func(task_commander_t* me)
{
  uint32_t delay_msec = 0;

  system_registry.popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_URL_MANUAL);

#if defined (M5UNIFIED_PC_BUILD)
  M5.delay(2000);
#else
  ulTaskNotifyTake(pdTRUE, 16384);
#endif
  // 起動直後に表示されているQRコードについて、何か操作があったらQRコードを消す
  system_registry.popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);

  for (;;) {
    system_registry.task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_COMMANDER);
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(delay_msec);
    uint32_t btn_mask = 0;
    for (int i = 0; i < 32; ++i) {
      btn_mask |= (m5gfx::gpio_in(i) ? 0 : 1) << i;
    }
    system_registry.internal_input.setButtonBitmask(btn_mask);
    system_registry.runtime_info.setPressVelocity(100);
    delay_msec = 1;
#else
    ulTaskNotifyTake(pdTRUE, delay_msec);
    delay_msec = portMAX_DELAY;
#endif
    system_registry.task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_COMMANDER);

    bool hit = 0;

    const registry_t::history_t* history = nullptr;
    while (nullptr != (history = system_registry.internal_input.getHistory(me->_internal_input_history_code)))
    {
      hit = true;
      if (history->index == system_registry_t::reg_internal_input_t::BUTTON_BITMASK) {
        auto result = commander_internal.update(history->value, M5.millis(), &system_registry.command_mapping_current);
        if (delay_msec > result) {
          delay_msec = result;
        }
      }
    }
    if (hit == 0) { // 履歴がない場合は読み取って処理を行う (チャタリング回避のための遅延処理があり得るため)
      auto result = commander_internal.update(system_registry.internal_input.getButtonBitmask(), M5.millis(), &system_registry.command_mapping_current);
      if (delay_msec > result) {
        delay_msec = result;
      }
    }

    bool hit_a = false, hit_b = false;
    while (nullptr != (history = system_registry.external_input.getHistory(me->_external_input_history_code)))
    {
      if (history->index == system_registry_t::reg_external_input_t::PORTA_BITMASK_BYTE0) {
        hit_a = true;
        auto result = commander_port_a.update(history->value, M5.millis(), &system_registry.command_mapping_external);
        if (delay_msec > result) {
          delay_msec = result;
        }
      } else
      if (history->index == system_registry_t::reg_external_input_t::PORTB_BITMASK_BYTE0) {
        hit_b = true;
        auto result = commander_port_b.update(history->value, M5.millis(), &system_registry.command_mapping_port_b);
        if (delay_msec > result) {
          delay_msec = result;
        }
      }
    }
    if (hit_a == false) { // 履歴がない場合は読み取って処理を行う (チャタリング回避のための遅延処理があり得るため)
      auto result = commander_port_a.update(system_registry.external_input.getPortAButtonBitmask(), M5.millis(), &system_registry.command_mapping_external);
      if (delay_msec > result) {
        delay_msec = result;
      }
    }
    if (hit_b == false) {
      auto result = commander_port_b.update(system_registry.external_input.getPortBButtonBitmask(), M5.millis(), &system_registry.command_mapping_port_b);
      if (delay_msec > result) {
        delay_msec = result;
      }
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
