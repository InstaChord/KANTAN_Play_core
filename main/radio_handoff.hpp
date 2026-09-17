// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kanplay_ns {
// Owned by the Wi-Fi worker. Completion comes from the MIDI owner, never
// from a disconnected icon (which does not prove that Bluedroid freed RAM).
class radio_handoff_t {
public:
  enum class state_t { idle, stopping, settling, ready, failed };
  state_t step(uint32_t now, bool requested, bool stopped) {
    if (!requested) { _state = state_t::idle; return _state; }
    if (_state == state_t::idle) {
      _state = state_t::stopping;
      _deadline = now + 25000;
    }
    if (_state == state_t::stopping) {
      if (stopped) { _state = state_t::settling; _settle = now + 1000; }
      else if (int32_t(now - _deadline) >= 0) { _state = state_t::failed; }
    }
    if (_state == state_t::settling) {
      if (!stopped) { _state = state_t::stopping; }
      else if (int32_t(now - _settle) >= 0) { _state = state_t::ready; }
    }
    return _state;
  }
private:
  state_t _state = state_t::idle;
  uint32_t _deadline = 0, _settle = 0;
};
}
