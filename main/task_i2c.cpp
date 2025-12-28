// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_i2c.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"
#include "gui.hpp"

#if !defined (M5UNIFIED_PC_BUILD)

#include "in_i2c/internal_kanplay.hpp"
static kanplay_ns::interface_internal_kanplay_t* internal_kanplay = nullptr;

// デバッグのため暫定で追加したもの (i2s_controlでI2Sピン動作を変更するために使用)
#if __has_include(<soc/io_mux_reg.h>)
#include <soc/io_mux_reg.h>
#endif

#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------

bool task_i2c_t::start(void)
{
#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "i2c", this);
#else
  internal_kanplay = new kanplay_ns::internal_kanplay_t();
  // STM32と通信できるまで待機させる
  while (!internal_kanplay->init()) {
    M5.delay( 16 );
  }

  if (internal_kanplay->checkUpdate()) { // || M5.BtnPWR.wasClicked()) {
    if (internal_kanplay->execFirmwareUpdate()) {
      internal_kanplay->init();
    }
  }

  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "i2c", 1024*3, this, def::system::task_priority_i2c, nullptr, def::system::task_cpu_i2c);
#endif
  return true;
}

void task_i2c_t::task_func(task_i2c_t* me)
{
#if !defined ( M5UNIFIED_PC_BUILD )
  internal_kanplay->setupInterrupt();
#endif
  uint8_t bat_check_counter = -1;
  for (;;) {
#if defined ( M5UNIFIED_PC_BUILD )
    M5.delay(1);
#else
    if (internal_kanplay->update())
#endif

    {
      if (++bat_check_counter == 0) {
#if defined ( M5UNIFIED_PC_BUILD )
        bool is_charging = M5.Power.isCharging();
        system_registry->runtime_info.getBatteryCharging();
        int bat_level = 1 + system_registry->runtime_info.getBatteryLevel();
        
        if (bat_level > 100) { bat_level = 0; is_charging = !is_charging; }
        system_registry->runtime_info.setBatteryLevel(bat_level);
        system_registry->runtime_info.setBatteryCharging(is_charging);
#else
        auto bat_level = M5.Power.getBatteryLevel();
        system_registry->runtime_info.setBatteryLevel(bat_level);
        auto is_charging = M5.Power.isCharging();
        system_registry->runtime_info.setBatteryCharging(is_charging);
#endif
      } else {
/*
TODO:CoreS3でのSDカード挿抜状態判定を追加する
*/
        M5.update();
        if (M5.Touch.getCount()) {
          auto td = M5.Touch.getDetail();
          system_registry->internal_input.setTouchValue(td.x, td.y, td.isPressed());
          gui.procTouchControl(td);
        }
        if (M5.BtnPWR.wasHold()) {
          system_registry->operator_command.addQueue( { def::command::system_control, def::command::system_control_t::sc_power_off } );
        }

        if (M5.BtnPWR.wasClicked() && M5.BtnPWR.getClickCount() == 8) {
if (system_registry->runtime_info.getDeveloperMode()) {
system_registry->operator_command.addQueue( { def::command::system_control, def::command::system_control_t::sc_erase_nvs } );
}


          system_registry->runtime_info.setDeveloperMode(true);
          system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_DEVELOPER_MODE);
        }

#if 0 // for DEBUG
        // これはデバッグ目的で、電源ボタンのクリックに割り当てる特殊操作
        // 謎のピー音放出バグが発生したときにGPIO設定を変更することで音が止まるかを確認するためのコード
        if (M5.BtnPWR.wasClicked())
        {
          static bool toggle;
          toggle = !toggle;
          if (toggle) {
            m5gfx::gpio_lo(def::hw::pin::i2s_out);
            m5gfx::pinMode(def::hw::pin::i2s_out, m5gfx::pin_mode_t::output);
          } else {
      #if defined (CONFIG_IDF_TARGET_ESP32S3)
            esp_rom_gpio_connect_out_signal(def::hw::pin::i2s_out, I2S1O_SD_OUT_IDX, 0, 0);
      #elif defined (CONFIG_IDF_TARGET_ESP32)
            esp_rom_gpio_connect_out_signal(def::hw::pin::i2s_out, I2S1O_DATA_OUT0_IDX, 0, 0);
      #endif
          }
        }
#endif

        auto off = system_registry->runtime_info.getPowerOff();
        if (off)
        {
#if !defined (M5UNIFIED_PC_BUILD)
          internal_kanplay->mute();

          if (system_registry->runtime_info.getSntpSync()) {
            time_t t = time(nullptr)+1; // Advance one second.
            while (t > time(nullptr)) M5.delay(1);  /// Synchronization in seconds
            M5.Rtc.setDateTime( gmtime( &t ) );
          }
          if (off == def::command::system_control_t::sc_reset) {
            esp_restart();
          }
          M5.Power.powerOff();
#endif
        }
        auto br = system_registry->user_setting.getDisplayBrightness();
        static constexpr const uint8_t brightness_table[] = { 48, 64, 100, 160, 255 };
        br = brightness_table[br];
        if (br != M5.Display.getBrightness()) {
          M5.Display.setBrightness(br);
        }

        static bool prev_usb_power_enabled = false;
        bool usb_power_enabled = system_registry->midi_port_setting.getUSBPowerEnabled();
        if (usb_power_enabled) {
          // InstaChordリンクがUSBポートの場合は電力供給はしない
          if (system_registry->midi_port_setting.getInstaChordLinkPort() == def::command::instachord_link_port_t::iclp_usb) {
            usb_power_enabled = false;
          } else // USBホストモードでない場合は電力供給はしない
          if (system_registry->midi_port_setting.getUSBMode() != def::command::usb_mode_t::usb_host) {
            usb_power_enabled = false;
          } else {
            usb_power_enabled = (system_registry->runtime_info.getMidiPortStateUSB() != def::command::midiport_info_t::mp_off);
          }
        }
        if (prev_usb_power_enabled != usb_power_enabled)
        { // USBホスト使用時はUSBへの電源供給をオンにする
          prev_usb_power_enabled = usb_power_enabled;
          M5.Power.setUsbOutput(usb_power_enabled);
        }
      }
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
