// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace kanplay_ns {
// Pure state shared with host tests. Row 0 explains the change; row 1 is the
// safe default; row 2 is the only action that permits a restart.
struct restart_confirmation_state_t {
  enum class stage_t : uint8_t { source, confirm };
  enum class decision_t : uint8_t { none, cancelled, apply };
  stage_t stage = stage_t::source;
  uint8_t pending = 0;
  bool request(uint8_t current, uint8_t next, bool force_restart) {
    if (current == next && !force_restart) { return false; }
    pending = next;
    stage = stage_t::confirm;
    return true;
  }
  static constexpr int safe_default_row = 1;
  decision_t decide(int row) {
    if (stage != stage_t::confirm || row < 1 || row > 2) { return decision_t::none; }
    if (row == 1) { stage = stage_t::source; return decision_t::cancelled; }
    return decision_t::apply;
  }
  void cancel() { stage = stage_t::source; }
};
}
