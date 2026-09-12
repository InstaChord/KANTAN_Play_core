// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_midi.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"
// #include "driver_midi.hpp"

#include "midi/midi_transport_uart.hpp"
#include "midi/midi_transport_ble.hpp"
#include "midi/midi_transport_usb.hpp"

// The SDL simulator has no BLE or USB transport implementation.  The rest of
// this file already provides the non-transport fallback paths when these
// include guards are absent.
#if defined(M5UNIFIED_PC_BUILD)
 #undef MIDI_TRANSPORT_BLE_HPP
 #undef MIDI_TRANSPORT_USB_HPP
#endif

#if __has_include(<freertos/FreeRTOS.h>)
 #include <freertos/FreeRTOS.h>
 #include <freertos/queue.h>
 #include <freertos/task.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
#if __has_include(<freertos/FreeRTOS.h>)
struct internal_realtime_midi_t {
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
};
static QueueHandle_t internal_realtime_midi_queue = nullptr;
#endif

class subtask_midi_t {
private:
  midi_driver::MIDIDriver _midi;
  system_registry_t::reg_task_status_t::bitindex_t _task_status_index;

// インスタコードリンク判定フラグ
  bool _flg_instachord_link = false;
  bool _flg_instachord_out = false;
  bool _flg_instachord_pad = false;

#if __has_include(<freertos/FreeRTOS.h>)
  TaskHandle_t _handle = nullptr;
#else
  SDL_Thread* _handle = nullptr;
#endif

public:
  subtask_midi_t(midi_driver::MIDI_Transport* transport, system_registry_t::reg_task_status_t::bitindex_t task_status_index)
  : _midi { transport }
  , _task_status_index { task_status_index }
  {
  }

  void start(void)
  {
    if (_handle == nullptr) {
      _midi.begin();
#if __has_include(<freertos/FreeRTOS.h>)
      // Internal synth output and BLE input are both realtime musical paths.
      // Keep them above transient UI transfers so Note On/Off cannot wait
      // behind a cached page copy on Core 0. I2S remains the top priority.
      const bool internal_synth =
        _task_status_index == system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL;
      const bool ble_input =
        _task_status_index == system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_BLE;
      // AMY render runs at I2S-1 on Core 0. Giving BLE decode the same
      // priority can starve one audio block while FX and notifications arrive
      // together, which sounds like a short repeat. BLE remains above normal
      // MIDI/UI work, while internal synth output keeps the lowest latency.
      const UBaseType_t priority = internal_synth
          ? def::system::task_priority_i2s - 1
          : ble_input ? def::system::task_priority_i2s - 2
                      : def::system::task_priority_midi_sub;
      xTaskCreatePinnedToCore((TaskFunction_t)task_func, "midi_subtask", 1024*3, this,
                              priority, &_handle, def::system::task_cpu_midi_sub);
#else
      _handle = SDL_CreateThread((SDL_ThreadFunction)task_func, "midi_subtask", this);
#endif
    }
  }

  void execNotify(void)
  {
    if (_handle != nullptr) {
#if __has_include(<freertos/FreeRTOS.h>)
      xTaskNotifyGive(_handle);
#endif
    }
  }

  void setInstaChordLink(bool enable, def::command::instachord_link_dev_t dev, def::command::instachord_link_style_t style) {
    _flg_instachord_link = enable;
    // 演奏デバイスがInstaChordの場合はアウトプット有効とする
    _flg_instachord_out = (dev == def::command::instachord_link_dev_t::icld_instachord);
    _flg_instachord_pad = (style == def::command::instachord_link_style_t::icls_pad);
  }

  static void task_func(subtask_midi_t* me)
  {
    auto midi = &(me->_midi);
    registry_t::history_code_t history_code_midi_out = 0;
    uint32_t prev_on_beat_msec = 0;
    degree_param_t prev_on_beat_degree;
    degree_param_t on_beat_degree;
    uint8_t prev_midi_volume = 0;
    bool prev_tx_enable = false;
    bool prev_rx_enable = false;
    uint8_t prev_slot_key = 255;

    uint8_t tx_count = 0;
    uint8_t rx_count = 0;

#if !defined (M5UNIFIED_PC_BUILD)
    midi->setNotifyTaskHandle(xTaskGetCurrentTaskHandle());
#endif

    for (;;) {

#if defined (M5UNIFIED_PC_BUILD)
      M5.delay(1);
#else
      if (ulTaskNotifyTake(pdTRUE, 0) == 0)
      {
        system_registry->task_status.setSuspend(me->_task_status_index);
        // ulTaskNotifyTake(pdTRUE, (prev_tx_enable) ? 32 : 512);
        ulTaskNotifyTake(pdTRUE, 2048);
        system_registry->task_status.setWorking(me->_task_status_index);
      } 
#endif
      midi->service();
      bool connected = midi->isConnected();
      bool tx_enable = connected && midi->getUseTx();
      bool rx_enable = connected && midi->getUseRx();
      if (rx_enable) {
        if (prev_rx_enable != rx_enable) {
          prev_rx_enable = rx_enable;
        }
        midi_driver::MIDI_Message message;
        if (midi->receiveMessage(&message)) {
          if (me->_flg_instachord_link && me->_flg_instachord_pad)
          { // インスタコードリンクでパッド演奏が有効な場合は、自動演奏ビートモードにする
            if (system_registry->runtime_info.getAutoplayState() != def::play::auto_play_state_t::auto_play_beatmode) {
              system_registry->operator_command.addQueue( { def::command::autoplay_switch, def::command::autoplay_switch_t::autoplay_beat } );
            }
          }

          do {
            ++rx_count;
//  printf("status:%02x  len:%d  data:%02x %02x\n", message.status, message.data.size(), message.data[0], message.data[1]);
//  fflush(stdout);
            // MIDIスルーフラグ
            bool midi_thru = true;
            uint8_t channel = message.channel;
            uint8_t velocity = 0;
            def::command::command_param_array_t command_param_array;

            if (me->_flg_instachord_link) {
              if (message.type == 0x0B && message.data[0] == 0x0F) { // Control Change ( for InstaChord 連携 )
                if (channel == 0x0E || channel == 0x0F) {
                  uint8_t control = message.data[1];
                  velocity = (control & 0x40) ? 127 : 0;
                  control &= 0x3F; // 0x00 ~ 0x3F
                  if (channel == 0x0E) {
                    command_param_array = system_registry->command_mapping_midicc15.getCommandParamArray(control);
                    velocity = 127;
                  } else if (channel == 0x0F) {
                    command_param_array = system_registry->command_mapping_midicc16.getCommandParamArray(control);
                    if (command_param_array.array[0].getCommand() == def::command::chord_degree) {
                      on_beat_degree = command_param_array.array[0].getParam();
                    }
                  }
                }
              }
              if (me->_flg_instachord_out) {
                // 出力が有効な場合は入力側を無効化する
                midi_thru = false;
              }
            }
            if ((message.type & ~1) == 0x08 && message.data.size() >= 2) { // Note On/Off
              velocity = (message.type == 0x09) // NoteOn
                                ? message.data[1]
                                : 0;
              // MIDI Learnを利用する別アプリへ、外部ポートから受けたノートを通知する。
              // Note On velocity=0 はMIDI規約上のNote Offとしてそのままvelocity=0で渡す。
              if (me->_task_status_index != system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL) {
                system_registry->midi_input.setNoteMessage(message.status, message.data[0], velocity);
              }
              // インスタコード連携モードでない場合は、ノートによる制御のためのコマンドを取得する
              if (!me->_flg_instachord_link) {
                command_param_array = system_registry->command_mapping_midinote.getCommandParamArray(message.data[0]);
                if (!command_param_array.empty() && velocity) {
                  if (system_registry->user_setting.getExtMidiVelocity()) {
                    system_registry->operator_command.addQueue( { def::command::set_velocity, velocity } );
                  }
                }
              } else
              { // インスタコード連携モードのときは、オンビート判定を行う
                if (me->_flg_instachord_pad && velocity) {
                  const uint32_t msec = M5.millis();
                  uint32_t diff_msec = (msec - prev_on_beat_msec);
                  prev_on_beat_msec = msec;
                  // 128msec以内の連続したオンビートはキャンセルする(暫定実装)
                  // パッドをストローク演奏した時、まれに110msec前後の間隔が空く場合があったため128msecに変更
                  // ※ Degreeが変化した場合はキャンセルしない
                  if (diff_msec >= 128 || prev_on_beat_degree.getDegree() != on_beat_degree.getDegree()) {
                    system_registry->operator_command.addQueue( { def::command::chord_beat, 0 } );
                    prev_on_beat_degree = on_beat_degree;
                  }
                }
              }
            }
            if (message.type == 0x0B && message.data.size() >= 2
             && me->_task_status_index != system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL) {
              system_registry->midi_input.setCCMessage(message.status, message.data[0], message.data[1]);
            }
#if defined(KANPLAY_SAMPLER)
            // サンプラーでは外部入力をsampler_app側で一元処理する。
            // ここでSAMへスルーすると、未アサインノートが二重送信になり、
            // アサイン済みノートまで発音してしまう。
            if (me->_task_status_index != system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL) {
              midi_thru = false;
              command_param_array = {};
            }
#endif
            if (!command_param_array.empty()) {
              midi_thru = false;
              for (auto command_param : command_param_array.array) {
                uint8_t command = command_param.getCommand();
                if (command == 0) { continue; }
                system_registry->operator_command.addQueue(command_param, velocity ? true : false);
              }
            }
            if (midi_thru == true && message.data.size() > 1 && message.data.size() <= 2) {
              // MIDIノートがコマンドマッピングされていない場合
              system_registry->midi_out_control.setMessage(message.status, message.data[0], message.data[1]);
            }
          } while (midi->receiveMessage(&message));
        }
      }

      bool queued = false;
      if (prev_tx_enable != tx_enable) {
        prev_tx_enable = tx_enable;
        if (tx_enable) {
          prev_midi_volume = 255;
          prev_slot_key = 255;
          history_code_midi_out = system_registry->midi_out_control.getHistoryCode();
          for (int i = 0; i < 16; ++i) { // CC#120はすべてのMIDI音を停止する
            midi->sendControlChange(def::midi::channel_1 + i, 120, 0);
          }
          queued = true;
        }
      }

      if (tx_enable) {
        if (me->_task_status_index == system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL) {
#if __has_include(<freertos/FreeRTOS.h>)
          internal_realtime_midi_t message;
          while (internal_realtime_midi_queue != nullptr
              && xQueueReceive(internal_realtime_midi_queue, &message, 0) == pdTRUE) {
            midi->sendMessage(message.status, message.data1, message.data2);
            queued = true;
          }
#endif
        }
        if (me->_flg_instachord_link)
        { // InstaChord連携モードのときは、かんぷれ側のキー変更をインスタコード側に反映する
          int master_key = system_registry->runtime_info.getMasterKey();
          int slot_key = master_key + (int8_t)system_registry->current_slot->slot_info.getKeyOffset();
          while (slot_key < 0) { slot_key += 12; }
          while (slot_key >= 12) { slot_key -= 12; }
          if (prev_slot_key != slot_key) {
            prev_slot_key = slot_key;
            // マスタースロットキー設定
            midi->sendControlChange(def::midi::channel_15, 0x0F, slot_key);
            queued = true;
          }
        }
        if (!me->_flg_instachord_link || me->_flg_instachord_out)
        {
          auto midi_volume = system_registry->user_setting.getMIDIMasterVolume();
          if (prev_midi_volume != midi_volume) {
            prev_midi_volume = midi_volume;

            // マスターボリューム設定
            midi->sendControlChange(def::midi::channel_1, 99, 55);
            midi->sendControlChange(def::midi::channel_1, 98,  7);
            midi->sendControlChange(def::midi::channel_1,  6, midi_volume);
            for (int i = 0; i < 16; ++i) {
              // チャンネルボリュームおよびプログラムチェンジを設定
              uint8_t vol = system_registry->midi_out_control.getChannelVolume(i);
              uint8_t prg = system_registry->midi_out_control.getProgramChange(i);
              midi->sendControlChange(def::midi::channel_1 + i, 7, vol);
              midi->sendProgramChange(def::midi::channel_1 + i, prg);
  // printf("MIDI Channel %d Volume: %d, Program: %d\n", i, vol, prg);
  // fflush(stdout);
            }
            queued = true;
          }

          const registry_t::history_t* history;
          while (nullptr != (history = system_registry->midi_out_control.getHistory(history_code_midi_out))) {
            uint8_t status = history->index & 0xFF;
//          uint8_t midi_ch = status & 0x0F;
            uint8_t data1 = history->value & 0xFF;
            uint8_t data2 = (history->value >> 8) & 0xFF;
            midi->sendMessage(status, data1, data2);
            queued = true;
          }
        }
        if (queued) {
          // MIDI送信バッファをフラッシュ
          if (midi->sendFlush()) {
            tx_count++;
          };
        }
      }

      switch (me->_task_status_index) {
      case system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_EXTERNAL:
        system_registry->runtime_info.setMidiTxCountPC(tx_count);
        system_registry->runtime_info.setMidiRxCountPC(rx_count);
        break;
      case system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_BLE:
        system_registry->runtime_info.setMidiTxCountBLE(tx_count);
        system_registry->runtime_info.setMidiRxCountBLE(rx_count);
        break;
      case system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_USB:
        system_registry->runtime_info.setMidiTxCountUSB(tx_count);
        system_registry->runtime_info.setMidiRxCountUSB(rx_count);
        break;
      default:
        break;
      }
    }
  }
};

#if defined (M5UNIFIED_PC_BUILD)

static subtask_midi_t* subtask_array[] = {};

#else

static midi_driver::MIDI_Transport_UART in_uart_midi_transport; // かんぷれ内部MIDI
static midi_driver::MIDI_Transport_UART portc_midi_transport; // PortC外部MIDI

static subtask_midi_t in_uart_midi_subtask { &in_uart_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_INTERNAL };
static subtask_midi_t portc_midi_subtask { &portc_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_EXTERNAL };

#ifdef MIDI_TRANSPORT_BLE_HPP
static midi_driver::MIDI_Transport_BLE ble_midi_transport; // BLE-MIDI
static subtask_midi_t ble_midi_subtask { &ble_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_BLE };
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
static midi_driver::MIDI_Transport_USB usb_midi_transport; // USB-MIDI
static subtask_midi_t usb_midi_subtask { &usb_midi_transport, system_registry_t::reg_task_status_t::bitindex_t::TASK_MIDI_USB };
#endif


static subtask_midi_t* subtask_array[] = {
  &in_uart_midi_subtask, // かんぷれ内部MIDI
  &portc_midi_subtask, // PortC外部MIDI
#ifdef MIDI_TRANSPORT_BLE_HPP
  &ble_midi_subtask, // BLE-MIDI
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  &usb_midi_subtask, // USB-MIDI
#endif
};
#endif
static constexpr const size_t max_subtask = sizeof(subtask_array)/sizeof(subtask_array[0]);

bool task_midi_t::sendInternalRealtime(uint8_t status, uint8_t data1, uint8_t data2)
{
#if __has_include(<freertos/FreeRTOS.h>)
  if (internal_realtime_midi_queue == nullptr) { return false; }
  internal_realtime_midi_t message { status, data1, data2 };
  if (xQueueSend(internal_realtime_midi_queue, &message, 0) != pdTRUE) { return false; }
  in_uart_midi_subtask.execNotify();
  return true;
#else
  (void)status;
  (void)data1;
  (void)data2;
  return false;
#endif
}

void task_midi_t::restoreInternalMidiOutput(void)
{
#if __has_include(<driver/uart.h>)
  in_uart_midi_transport.restorePins();
#endif
}

bool task_midi_t::startUSBHIDKeyboard(void)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.startHostForHID();
#else
  return false;
#endif
}

uint32_t task_midi_t::getBLEMidiPacketCount(void) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return ble_midi_transport.getReceivedPacketCount();
#else
  return 0;
#endif
}

void task_midi_t::getBLEMidiLastPacket(uint8_t* data, size_t* length) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getLastReceivedPacket(data, length);
#else
  if (length != nullptr) { *length = 0; }
#endif
}

void task_midi_t::getBLEMidiConnectionDiagnostic(bool* central, bool* peripheral, uint8_t* subscription) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getConnectionDiagnostic(central, peripheral, subscription);
#else
  if (central != nullptr) { *central = false; }
  if (peripheral != nullptr) { *peripheral = false; }
  if (subscription != nullptr) { *subscription = 0; }
#endif
}

void task_midi_t::getBLEMidiCentralDeviceName(char* name, size_t size) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getCentralDeviceName(name, size);
#else
  if (name != nullptr && size != 0) { name[0] = 0; }
#endif
}

void task_midi_t::getBLEMidiPeerAddresses(char* central, size_t central_size, char* peripheral, size_t peripheral_size) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getPeerAddresses(central, central_size, peripheral, peripheral_size);
#else
  if (central != nullptr && central_size != 0) { central[0] = 0; }
  if (peripheral != nullptr && peripheral_size != 0) { peripheral[0] = 0; }
#endif
}

uint8_t task_midi_t::getBLEMidiCentralProperties(void) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return ble_midi_transport.getCentralMIDIProperties();
#else
  return 0;
#endif
}

void task_midi_t::getBLEMidiSecurityDiagnostic(uint8_t* auth_state, uint8_t* cccd_value,
                                               uint8_t* registration_status) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getSecurityDiagnostic(auth_state, cccd_value, registration_status);
#else
  if (auth_state != nullptr) { *auth_state = 0; }
  if (cccd_value != nullptr) { *cccd_value = 0xFF; }
  if (registration_status != nullptr) { *registration_status = 0xFF; }
#endif
}

bool task_midi_t::clearBLEMidiCentralBond(void)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return ble_midi_transport.clearCentralBond();
#else
  return false;
#endif
}

void task_midi_t::requestBLEMidiScan(void)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.requestCentralScan();
#endif
}

void task_midi_t::cancelBLEMidiScan(void)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.cancelCentralScan();
#endif
}

task_midi_t::ble_scan_state_t task_midi_t::getBLEMidiScanState(void) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return (ble_scan_state_t)ble_midi_transport.getCentralScanState();
#else
  return ble_scan_state_t::failed;
#endif
}

size_t task_midi_t::getBLEMidiScanDevices(ble_scan_device_t* devices, size_t capacity) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  if (devices == nullptr || capacity == 0) { return 0; }
  midi_driver::MIDI_Transport_BLE::scan_device_t source[
    midi_driver::MIDI_Transport_BLE::max_scan_devices];
  const size_t count = ble_midi_transport.getCentralScanDevices(
    source, std::min(capacity, midi_driver::MIDI_Transport_BLE::max_scan_devices));
  for (size_t i = 0; i < count; ++i) {
    snprintf(devices[i].name, sizeof(devices[i].name), "%s", source[i].name);
    snprintf(devices[i].address, sizeof(devices[i].address), "%s", source[i].address);
    devices[i].rssi = source[i].rssi;
    devices[i].address_type = source[i].address_type;
    devices[i].advertises_midi = source[i].advertises_midi;
  }
  return count;
#else
  (void)devices;
  (void)capacity;
  return 0;
#endif
}

void task_midi_t::setBLEMidiPreferredDevice(const char* address, const char* name,
                                            bool force_fresh_pairing,
                                            int8_t address_type)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.setPreferredCentralDevice(
    address, name, force_fresh_pairing, address_type);
#else
  (void)address;
  (void)name;
  (void)force_fresh_pairing;
  (void)address_type;
#endif
}

void task_midi_t::getBLEMidiPreferredDevice(char* address, size_t address_size,
                                            char* name, size_t name_size) const
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  ble_midi_transport.getPreferredCentralDevice(address, address_size, name, name_size);
#else
  if (address != nullptr && address_size != 0) { address[0] = 0; }
  if (name != nullptr && name_size != 0) { name[0] = 0; }
#endif
}

bool task_midi_t::forgetBLEMidiPreferredDevice(void)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return ble_midi_transport.forgetPreferredCentralDevice();
#else
  return false;
#endif
}

uint8_t task_midi_t::consumeBLEMidiConnectCrashStage(uint16_t* free_internal_kb,
                                                     uint16_t* largest_internal_kb,
                                                     uint16_t* midi_stack_kb,
                                                     uint16_t* callback_stack_kb)
{
#ifdef MIDI_TRANSPORT_BLE_HPP
  return ble_midi_transport.consumePreviousConnectCrashStage(
    free_internal_kb, largest_internal_kb, midi_stack_kb, callback_stack_kb);
#else
  if (free_internal_kb) { *free_internal_kb = 0; }
  if (largest_internal_kb) { *largest_internal_kb = 0; }
  if (midi_stack_kb) { *midi_stack_kb = 0; }
  if (callback_stack_kb) { *callback_stack_kb = 0; }
  return 0;
#endif
}

bool task_midi_t::getUSBHIDKeyboardEvent(uint8_t* usage, bool* pressed)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.getHIDKeyboardEvent(usage, pressed);
#else
  (void)usage;
  (void)pressed;
  return false;
#endif
}

bool task_midi_t::startUSBHIDGamepad(void)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.startHostForHIDGamepad();
#else
  return false;
#endif
}

bool task_midi_t::getUSBHIDGamepadEvent(uint8_t* code, bool* pressed)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.getHIDGamepadEvent(code, pressed);
#else
  (void)code;
  (void)pressed;
  return false;
#endif
}

bool task_midi_t::isUSBStarted(void)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.isStarted();
#else
  return false;
#endif
}

bool task_midi_t::isUSBStackReady(void)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.isStackReady();
#else
  return false;
#endif
}

def::command::usb_mode_t task_midi_t::getUSBMode(void)
{
#ifdef MIDI_TRANSPORT_USB_HPP
  return usb_midi_transport.getUSBMode();
#else
  return def::command::usb_mode_t::usb_host;
#endif
}

void task_midi_t::getUSBHostDiagnostic(uint16_t* vendor_id, uint16_t* product_id,
                                       uint8_t* interface_class, uint8_t* interface_subclass,
                                       uint8_t* endpoint_count, bool* device_seen,
                                       bool* midi_interface, int* open_result,
                                       int* descriptor_result, int* claim_result) const
{
#ifdef MIDI_TRANSPORT_USB_HPP
  usb_midi_transport.getHostDiagnostic(vendor_id, product_id, interface_class, interface_subclass,
                                       endpoint_count, device_seen, midi_interface, open_result,
                                       descriptor_result, claim_result);
#else
  if (vendor_id) { *vendor_id = 0; }
  if (product_id) { *product_id = 0; }
  if (interface_class) { *interface_class = 0; }
  if (interface_subclass) { *interface_subclass = 0; }
  if (endpoint_count) { *endpoint_count = 0; }
  if (device_seen) { *device_seen = false; }
  if (midi_interface) { *midi_interface = false; }
  if (open_result) { *open_result = 0; }
  if (descriptor_result) { *descriptor_result = 0; }
  if (claim_result) { *claim_result = 0; }
#endif
}

void task_midi_t::start(void)
{

#if defined (M5UNIFIED_PC_BUILD)
  // windows_midi_transport_t::config_t config;

  // config.deviceID = 0;
  // windows_midi_transport.init(config);
  // windows_midi_transport.changeEnable(true, false);

  // auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "midi", this);
#else
  {
    midi_driver::MIDI_Transport_UART::config_t config;

    // 内部SAM音源用MIDI
    config.uart_port_num = 1; // UART_NUM_1
    config.pin_tx = def::hw::pin::midi_tx;
    config.pin_rx = GPIO_NUM_NC;
    in_uart_midi_transport.setConfig(config);
#if __has_include(<freertos/FreeRTOS.h>)
    if (internal_realtime_midi_queue == nullptr) {
      internal_realtime_midi_queue = xQueueCreate(64, sizeof(internal_realtime_midi_t));
    }
#endif
    in_uart_midi_subtask.start();
    in_uart_midi_transport.begin();
    in_uart_midi_transport.setUseTxRx(true, false);

    // 外部PortC用MIDI
    config.uart_port_num = 2; // UART_NUM_2;
    config.pin_tx = M5.getPin(m5::pin_name_t::port_c_txd);
    config.pin_rx = M5.getPin(m5::pin_name_t::port_c_rxd);
    portc_midi_transport.setConfig(config);
    portc_midi_transport.begin();
    // オン・オフはsystem_registryで設定する
  }
#ifdef MIDI_TRANSPORT_BLE_HPP
  {
    midi_driver::MIDI_Transport_BLE::config_t config;
    ble_midi_transport.setConfig(config);
    // begin() only restores the RTC crash guard; the controller itself is
    // initialized later by setUseTxRx().  Do this synchronously so the app can
    // suppress a failing saved-device reconnect before the MIDI task runs.
    ble_midi_transport.begin();
    // オン・オフはsystem_registryで設定する
  }
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  {
    midi_driver::MIDI_Transport_USB::config_t config;
    usb_midi_transport.setConfig(config);
    // usb_midi_transport.begin();
  }
#endif

  TaskHandle_t handle = nullptr;
  // This task owns BLE scan, SMP pairing and GATT service discovery.  Those
  // nested Bluedroid/C++ paths exceed the old 3 KB stack on some controllers,
  // especially M-VAVE during encrypted reconnect.  A stack overflow here
  // presents as an immediate reboot as soon as the selected device connects.
  const BaseType_t midi_task_result = xTaskCreatePinnedToCore(
    (TaskFunction_t)task_func, "midi", 1024 * 12, this,
    def::system::task_priority_midi, &handle, def::system::task_cpu_midi);
  if (midi_task_result != pdPASS) { handle = nullptr; }
  system_registry->midi_out_control.setNotifyTaskHandle(handle);
  system_registry->midi_port_setting.setNotifyTaskHandle(handle);
#endif

}

void task_midi_t::task_func(task_midi_t* me)
{
#if defined (M5UNIFIED_PC_BUILD)
  for (;;) {
    M5.delay(1);
  }
#else
  bool prev_portc_out = false;
  bool prev_portc_in = false;
#ifdef MIDI_TRANSPORT_BLE_HPP
  bool prev_ble_out = false;
  bool prev_ble_in = false;
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
  bool prev_usb_out = false;
  bool prev_usb_in = false;
  def::command::usb_mode_t prev_usb_mode = def::command::usb_mode_t::usb_host;
#endif

  auto prev_iclink_port = def::command::instachord_link_port_t::iclp_off;
  auto prev_iclink_dev = def::command::instachord_link_dev_t::icld_kanplay;
  auto prev_iclink_style = def::command::instachord_link_style_t::icls_button;

  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    auto iclink_port = system_registry->midi_port_setting.getInstaChordLinkPort();
    auto iclink_dev = system_registry->midi_port_setting.getInstaChordLinkDev();
    auto iclink_style = system_registry->midi_port_setting.getInstaChordLinkStyle();
    if (prev_iclink_port != iclink_port || prev_iclink_dev != iclink_dev || prev_iclink_style != iclink_style) {
      switch (prev_iclink_port) {
      case def::command::instachord_link_port_t::iclp_ble:
        ble_midi_subtask.setInstaChordLink(false, iclink_dev, iclink_style);
        break;
#ifdef MIDI_TRANSPORT_USB_HPP
      case def::command::instachord_link_port_t::iclp_usb:
        usb_midi_subtask.setInstaChordLink(false, iclink_dev, iclink_style);
        break;
#endif
        default:
        // 何もしない
        break;
      }
      prev_iclink_port = iclink_port;
      prev_iclink_dev = iclink_dev;
      prev_iclink_style = iclink_style;
      // InstaChord Linkモードのときは、チャンネルボリュームの最大値を 75 にする。
      uint8_t chvol_max = 75;

      switch (iclink_port) {
      case def::command::instachord_link_port_t::iclp_ble:
        ble_midi_subtask.setInstaChordLink(true, iclink_dev, iclink_style);
        break;
#ifdef MIDI_TRANSPORT_USB_HPP
      case def::command::instachord_link_port_t::iclp_usb:
        usb_midi_subtask.setInstaChordLink(true, iclink_dev, iclink_style);
        break;
#endif
      default:
        // InstaChord Linkモードでない場合は、チャンネルボリュームの最大値を127にする
        chvol_max = 127;
        break;
      }
      system_registry->runtime_info.setMIDIChannelVolumeMax(chvol_max);
    }

    auto portc_setting = system_registry->midi_port_setting.getPortCMIDI();
    bool portc_out = portc_setting & def::command::ex_midi_mode_t::midi_output;
    bool portc_in  = portc_setting & def::command::ex_midi_mode_t::midi_input;
    if (prev_portc_out != portc_out || prev_portc_in != portc_in) {
      if (portc_in || portc_out) {
        portc_midi_subtask.start();
      }
      prev_portc_out = portc_out;
      prev_portc_in  = portc_in;
      portc_midi_transport.setUseTxRx(portc_out, portc_in);
      kanplay_ns::system_registry->runtime_info.setMidiPortStatePC((portc_out || portc_in) ? kanplay_ns::def::command::midiport_info_t::mp_connected : kanplay_ns::def::command::midiport_info_t::mp_off);
    }
#ifdef MIDI_TRANSPORT_BLE_HPP
    auto ble_setting = system_registry->midi_port_setting.getBLEMIDI();
    bool ble_out = ble_setting & def::command::ex_midi_mode_t::midi_output;
    bool ble_in  = ble_setting & def::command::ex_midi_mode_t::midi_input;
    if (iclink_port == def::command::instachord_link_port_t::iclp_ble)
    { // InstaChord Link BLEモードのときは、BLE-MIDIを有効にする
      ble_out = true;
      ble_in = true;
    }
    if (prev_ble_out != ble_out || prev_ble_in != ble_in) {
      if (ble_in || ble_out) {
        ble_midi_subtask.start();
      }
      prev_ble_out = ble_out;
      prev_ble_in  = ble_in;
      ble_midi_transport.setUseTxRx(ble_out, ble_in);
    }
#endif
#ifdef MIDI_TRANSPORT_USB_HPP
    auto usb_mode = system_registry->midi_port_setting.getUSBMode();
    auto usb_setting = system_registry->midi_port_setting.getUSBMIDI();
    bool usb_out = usb_setting & def::command::ex_midi_mode_t::midi_output;
    bool usb_in  = usb_setting & def::command::ex_midi_mode_t::midi_input;
    if (iclink_port == def::command::instachord_link_port_t::iclp_usb)
    { // InstaChord Link USBモードのときは、USB-MIDIを有効にする
      usb_out = true;
      usb_in = true;
      usb_mode = def::command::usb_mode_t::usb_host; // InstaChord Link USBモードのときは、USBホストにする
    }
    if (prev_usb_mode != usb_mode || prev_usb_out != usb_out || prev_usb_in != usb_in) {
      if (!usb_midi_transport.setUSBMode(usb_mode)) {
        // 設定が変更できなかった場合
        system_registry->popup_notify.setMessage(def::notify_type_t::MESSAGE_NEED_RESTART);
        usb_mode = usb_midi_transport.getUSBMode();
      }
      if (usb_in || usb_out) {
        usb_midi_subtask.start();
      }
      prev_usb_mode = usb_mode;
      prev_usb_out = usb_out;
      prev_usb_in  = usb_in;
      usb_midi_transport.setUseTxRx(usb_out, usb_in);
    }
#endif

    for (auto &subtask : subtask_array) {
      subtask->execNotify();
    }
  }
#endif
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
