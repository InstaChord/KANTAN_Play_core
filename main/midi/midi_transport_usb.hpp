// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#if __has_include(<usb/usb_host.h>)
#ifndef MIDI_TRANSPORT_USB_HPP
#define MIDI_TRANSPORT_USB_HPP

#include "midi_driver.hpp"
#include "../common_define.hpp"

#include <usb/usb_host.h>

namespace midi_driver {

class MIDI_Transport_USB : public MIDI_Transport {
public:
  struct config_t {
    const char* device_name = "KANTAN-Play";
  };

  MIDI_Transport_USB(void) = default;
  ~MIDI_Transport_USB();

  void setConfig(const config_t& config) { _config = config; }
  config_t& getConfig(void) { return _config; }

  bool setUSBMode(kanplay_ns::def::command::usb_mode_t mode);
  kanplay_ns::def::command::usb_mode_t getUSBMode(void) const { return _usb_mode; }

  bool begin(void) override;
  void end(void) override;
  // size_t write(const uint8_t* data, size_t length) override;
  std::vector<uint8_t> read(void) override;
  void addMessage(const uint8_t* data, size_t length) override;
  bool sendFlush(void) override;

  void setUseTxRx(bool tx_enable, bool rx_enable) override;

  void setConnected(bool flg);

private:

  std::vector<uint8_t> _tx_data;
  config_t _config;
  bool _is_begin = false;
  kanplay_ns::def::command::usb_mode_t _usb_mode = kanplay_ns::def::command::usb_mode_t::usb_host;
};

} // namespace midi_driver

#endif // MIDI_TRANSPORT_USB_HPP
#endif // __has_include(<usb/usb_host.h>)
