// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace kanplay_ns {
// UI transitions, independent of the asynchronous BLE transport.
struct ble_selection_state_t {
  enum class phase_t { idle, scanning, list, confirm, connecting, connected, failed, blocked, forgotten, save_failed };
  phase_t phase = phase_t::idle;
  size_t count = 0, selected = 0;
  uint32_t deadline = 0;
  void scan(uint32_t now) { phase = phase_t::scanning; count = selected = 0; deadline = now + 30000; }
  void scanReady(size_t n) { count = n; phase = n ? phase_t::list : phase_t::failed; }
  bool choose(size_t index) {
    if (phase != phase_t::list || index >= count) { return false; }
    selected = index; phase = phase_t::confirm; return true;
  }
  void connect(uint32_t now) { phase = phase_t::connecting; deadline = now + 30000; }
  void service(uint32_t now, bool central_connected) {
    if (phase == phase_t::connecting && central_connected) { phase = phase_t::connected; }
    else if ((phase == phase_t::connecting || phase == phase_t::scanning)
             && int32_t(now - deadline) >= 0) { phase = phase_t::failed; }
  }
  // Back in confirmation returns to the unchanged list. No connection occurs.
  bool back() {
    if (phase == phase_t::confirm) { phase = phase_t::list; return false; }
    if (phase != phase_t::connecting) { phase = phase_t::idle; }
    return true;
  }
};
}
