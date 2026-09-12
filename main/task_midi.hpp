// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_MIDI_HPP
#define KANPLAY_TASK_MIDI_HPP

#include "common_define.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
class task_midi_t {
public:
  enum class ble_scan_state_t : uint8_t { idle, requested, scanning, ready, failed };
  struct ble_scan_device_t {
    char name[24] = {};
    char address[18] = {};
    int8_t rssi = -127;
    uint8_t address_type = 0;
    bool advertises_midi = false;
  };
  void start(void);
  // Performance-critical messages for the internal SAM2695. This bypasses
  // the parent MIDI task while preserving FIFO order in the UART subtask.
  bool sendInternalRealtime(uint8_t status, uint8_t data1, uint8_t data2 = 0);
  void restoreInternalMidiOutput(void);
  bool startUSBHIDKeyboard(void);
  bool getUSBHIDKeyboardEvent(uint8_t* usage, bool* pressed);
  bool startUSBHIDGamepad(void);
  bool getUSBHIDGamepadEvent(uint8_t* code, bool* pressed);
  bool isUSBStarted(void);
  bool isUSBStackReady(void);
  def::command::usb_mode_t getUSBMode(void);
  void getUSBHostDiagnostic(uint16_t* vendor_id, uint16_t* product_id,
                            uint8_t* interface_class, uint8_t* interface_subclass,
                            uint8_t* endpoint_count, bool* device_seen,
                            bool* midi_interface, int* open_result,
                            int* descriptor_result, int* claim_result) const;
  uint32_t getBLEMidiPacketCount(void) const;
  void getBLEMidiLastPacket(uint8_t* data, size_t* length) const;
  void getBLEMidiConnectionDiagnostic(bool* central, bool* peripheral, uint8_t* subscription) const;
  void getBLEMidiCentralDeviceName(char* name, size_t size) const;
  void getBLEMidiPeerAddresses(char* central, size_t central_size, char* peripheral, size_t peripheral_size) const;
  uint8_t getBLEMidiCentralProperties(void) const;
  void getBLEMidiSecurityDiagnostic(uint8_t* auth_state, uint8_t* cccd_value,
                                    uint8_t* registration_status) const;
  bool clearBLEMidiCentralBond(void);
  void requestBLEMidiScan(void);
  void cancelBLEMidiScan(void);
  ble_scan_state_t getBLEMidiScanState(void) const;
  size_t getBLEMidiScanDevices(ble_scan_device_t* devices, size_t capacity) const;
  void setBLEMidiPreferredDevice(const char* address, const char* name,
                                 bool force_fresh_pairing = false,
                                 int8_t address_type = -1);
  void getBLEMidiPreferredDevice(char* address, size_t address_size,
                                 char* name, size_t name_size) const;
  bool forgetBLEMidiPreferredDevice(void);
  uint8_t consumeBLEMidiConnectCrashStage(uint16_t* free_internal_kb = nullptr,
                                          uint16_t* largest_internal_kb = nullptr,
                                          uint16_t* midi_stack_kb = nullptr,
                                          uint16_t* callback_stack_kb = nullptr);
protected:
  static void task_func(task_midi_t* me);
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
