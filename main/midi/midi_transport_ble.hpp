// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef MIDI_TRANSPORT_BLE_HPP
#define MIDI_TRANSPORT_BLE_HPP

#include "midi_driver.hpp"

namespace midi_driver {

class MIDI_Transport_BLE : public MIDI_Transport {
public:
  static constexpr size_t max_scan_devices = 12;
  enum class scan_state_t : uint8_t { idle, requested, scanning, ready, failed };
  struct scan_device_t {
    char name[24] = {};
    char address[18] = {};
    int8_t rssi = -127;
    uint8_t address_type = 0;
    bool advertises_midi = false;
  };

  struct config_t {
    const char* device_name = "KANTAN-Play";
  };

  MIDI_Transport_BLE(void) = default;
  ~MIDI_Transport_BLE();

  void setConfig(const config_t& config) { _config = config; }

  bool begin(void) override;
  void end(void) override;
  // size_t write(const uint8_t* data, size_t length) override;
  // size_t read(uint8_t* data, size_t length) override;

  void addMessage(const uint8_t* data, size_t length) override;
  bool sendFlush(void) override;

  std::vector<uint8_t> read(void) override;
  bool readInto(MIDI_Decoder& decoder) override;
  void service(void) override;
  uint32_t getReceivedPacketCount(void) const;
  void getLastReceivedPacket(uint8_t* data, size_t* length) const;
  void getConnectionDiagnostic(bool* central, bool* peripheral, uint8_t* subscription) const;
  void getCentralDeviceName(char* name, size_t size) const;
  void getPeerAddresses(char* central, size_t central_size, char* peripheral, size_t peripheral_size) const;
  uint8_t getCentralMIDIProperties(void) const;
  void getSecurityDiagnostic(uint8_t* auth_state, uint8_t* cccd_value, uint8_t* registration_status) const;
  bool clearCentralBond(void);
  void requestCentralScan(void);
  void cancelCentralScan(void);
  scan_state_t getCentralScanState(void) const;
  size_t getCentralScanDevices(scan_device_t* devices, size_t capacity) const;
  void setPreferredCentralDevice(const char* address, const char* name,
                                 bool force_fresh_pairing = false,
                                 int8_t address_type = -1);
  void getPreferredCentralDevice(char* address, size_t address_size,
                                 char* name, size_t name_size) const;
  bool forgetPreferredCentralDevice(void);
  uint8_t consumePreviousConnectCrashStage(uint16_t* free_internal_kb = nullptr,
                                           uint16_t* largest_internal_kb = nullptr,
                                           uint16_t* midi_stack_kb = nullptr,
                                           uint16_t* callback_stack_kb = nullptr);

  void setUseTxRx(bool use_tx, bool use_rx) override;

  // Returns true only when the packet appended a musical message. BLE MIDI
  // clock/active-sensing packets can then be discarded without waking the
  // performance worker dozens of times per second.
  static bool decodeReceive(const uint8_t* data, size_t length);
  void setCentralConnected(bool connected);
  void setPeripheralConnected(bool connected);

private:

  std::vector<uint8_t> _tx_data;
  config_t _config;
  uint8_t _tx_runningStatus = 0;
  bool _is_begin = false;

  bool _central_connected = false;
  bool _peripheral_connected = false;
  bool _connecting = false;
  uint32_t _last_central_scan_msec = 0;
  void updateState(void);
};

} // namespace midi_driver

#endif // MIDI_TRANSPORT_UART_HPP
