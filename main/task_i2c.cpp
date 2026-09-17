// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>
#include "task_midi.hpp"

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

void task_i2c_t::requestAudioCodecRestore(void)
{
  __atomic_store_n(&_audio_codec_restore_pending, true, __ATOMIC_RELEASE);
}

bool task_i2c_t::audioCodecRestorePending(void) const
{
  return __atomic_load_n(&_audio_codec_restore_pending, __ATOMIC_ACQUIRE);
}

void task_i2c_t::requestExternalInputEnabled(bool enabled)
{
  __atomic_store_n(&_external_input_enabled, enabled, __ATOMIC_RELEASE);
  __atomic_add_fetch(&_external_input_request_serial, 1u, __ATOMIC_ACQ_REL);
}

bool task_i2c_t::externalInputUpdatePending(void) const
{
  return __atomic_load_n(&_external_input_request_serial, __ATOMIC_ACQUIRE)
      != __atomic_load_n(&_external_input_applied_serial, __ATOMIC_ACQUIRE);
}

void task_i2c_t::task_func(task_i2c_t* me)
{
#if !defined ( M5UNIFIED_PC_BUILD )
  internal_kanplay->setupInterrupt();
#endif
  uint8_t bat_check_counter = -1;
  for (;;) {
#if !defined (M5UNIFIED_PC_BUILD)
    // A codec restore resets the ADC selector to INPUT1. Restore first so a
    // pending external-input request is always the final routing operation.
    if (__atomic_load_n(&me->_audio_codec_restore_pending, __ATOMIC_ACQUIRE)) {
      internal_kanplay->restoreAudioCodec();
      __atomic_store_n(&me->_audio_codec_restore_pending, false, __ATOMIC_RELEASE);
    }
#endif
    const uint32_t input_request = __atomic_load_n(
      &me->_external_input_request_serial, __ATOMIC_ACQUIRE);
    if (input_request != __atomic_load_n(
          &me->_external_input_applied_serial, __ATOMIC_ACQUIRE)) {
#if !defined (M5UNIFIED_PC_BUILD)
      internal_kanplay->setExternalInputEnabled(
        __atomic_load_n(&me->_external_input_enabled, __ATOMIC_ACQUIRE));
#endif
      __atomic_store_n(&me->_external_input_applied_serial,
                       input_request, __ATOMIC_RELEASE);
    }
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
#if !defined (KANPLAY_SAMPLER)
          gui.procTouchControl(td);
#endif
        }
        if (M5.BtnPWR.wasHold()) {
          system_registry->operator_command.addQueue( { def::command::system_control, def::command::system_control_t::sc_power_off } );
        }

        if (M5.BtnPWR.wasClicked() && M5.BtnPWR.getClickCount() == 8) {
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
            system_registry->midi_port_setting.setUSBPowerEnabled(false);
            M5.Power.setUsbOutput(false);
            M5.delay(750);
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

        // A soft restart does not necessarily reset the CoreS3 power IC's
        // USB output latch.  Initialize it explicitly so a previous USB-host
        // session cannot keep driving VBUS into a newly connected computer.
        static bool usb_power_initialized = false;
        static bool prev_usb_power_enabled = false;
        bool usb_power_enabled = system_registry->midi_port_setting.getUSBPowerEnabled();
        if (usb_power_enabled) {
          // InstaChordリンクがUSBポートの場合は電力供給はしない
          if (system_registry->midi_port_setting.getInstaChordLinkPort() == def::command::instachord_link_port_t::iclp_usb) {
            usb_power_enabled = false;
          } else // USBホストモードでない場合は電力供給はしない
          if (system_registry->midi_port_setting.getUSBMode() != def::command::usb_mode_t::usb_host) {
            usb_power_enabled = false;
          }
          // USB MIDI利用時は、ホストスタックとクライアントの準備完了後に
          // 物理VBUSを立ち上げる。単なるUSB給電として使う場合は待たない。
          if (system_registry->midi_port_setting.getUSBMIDI()
                != def::command::ex_midi_mode_t::midi_off
              && !task_midi_t{}.isUSBStackReady()) {
            usb_power_enabled = false;
          }
          // CoreS3のUSB-C端子はPCから給電されている間、VBUSを出力側へ
          // 切り替えてはいけない。ただし一度ホスト給電を始めると、ここで
          // 読めるVBUS電圧はM5Stack自身の5Vでもある。毎周期の電圧判定で
          // 給電を切ると、起動に時間がかかるUSB MIDI機器が列挙前に落ちる。
          // 給電を開始する瞬間だけ外部VBUSを判定し、開始後は維持する。
          if ((!usb_power_initialized || !prev_usb_power_enabled)
              && M5.Power.getVBUSVoltage() > 4000) {
            usb_power_enabled = false;
          }
        }
        if (!usb_power_initialized || prev_usb_power_enabled != usb_power_enabled)
        { // USBホスト使用時はUSBへの電源供給をオンにする
          usb_power_initialized = true;
          prev_usb_power_enabled = usb_power_enabled;
          M5.Power.setUsbOutput(usb_power_enabled);
        }
      }
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
