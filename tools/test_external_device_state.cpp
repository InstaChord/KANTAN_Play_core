// SPDX-License-Identifier: MIT
// Host test of the exact transition code used by the firmware.
#include "../main/radio_handoff.hpp"
#include "../main/ble_selection_state.hpp"
#include "../main/restart_confirmation_state.hpp"
#include <cassert>
#include <cstdio>
using namespace kanplay_ns;

int main() {
  using radio = radio_handoff_t::state_t;
  radio_handoff_t gate;
  assert(gate.step(0, false, false) == radio::idle);
  assert(gate.step(10, true, false) == radio::stopping);
  assert(gate.step(20000, true, false) == radio::stopping);
  assert(gate.step(24000, true, true) == radio::settling);
  assert(gate.step(24999, true, true) == radio::settling);
  assert(gate.step(25000, true, true) == radio::ready);
  assert(gate.step(25001, false, true) == radio::idle);
  // A subsequent AP/STA operation still observes the settling interval.
  assert(gate.step(26000, true, true) == radio::settling);
  assert(gate.step(27000, true, true) == radio::ready);

  radio_handoff_t failure;
  assert(failure.step(100, true, false) == radio::stopping);
  assert(failure.step(25100, true, false) == radio::failed);
  assert(failure.step(26000, true, true) == radio::failed); // no late surprise start
  assert(failure.step(27000, false, true) == radio::idle); // cancel/retry
  assert(failure.step(28000, true, true) == radio::settling);

  radio_handoff_t cancellation;
  assert(cancellation.step(1, true, false) == radio::stopping);
  assert(cancellation.step(2, false, false) == radio::idle);
  assert(cancellation.step(50000, false, true) == radio::idle);

  radio_handoff_t wrap;
  const uint32_t near_wrap = UINT32_MAX - 499;
  assert(wrap.step(near_wrap, true, true) == radio::settling);
  assert(wrap.step(499, true, true) == radio::settling);
  assert(wrap.step(500, true, true) == radio::ready);

  using phase = ble_selection_state_t::phase_t;
  ble_selection_state_t menu;
  menu.scan(100);
  assert(menu.phase == phase::scanning);
  assert(!menu.choose(0)); // no stale list while a fresh scan is running
  menu.scanReady(12);
  assert(menu.phase == phase::list);
  assert(!menu.choose(12));
  assert(menu.choose(4));
  assert(menu.phase == phase::confirm && menu.selected == 4);
  menu.service(500, false);
  assert(menu.phase == phase::confirm); // selecting alone never connects
  assert(!menu.back());
  assert(menu.phase == phase::list && menu.count == 12 && menu.selected == 4);
  assert(menu.choose(4));
  menu.connect(600);
  menu.service(700, false); // a peripheral connection is not a central success
  assert(menu.phase == phase::connecting);
  menu.service(800, true);
  assert(menu.phase == phase::connected);
  assert(menu.back());
  assert(menu.phase == phase::idle);

  menu.scan(1000);
  menu.scanReady(0);
  assert(menu.phase == phase::failed);
  menu.scan(2000);
  menu.service(32000, false);
  assert(menu.phase == phase::failed);
  menu.scan(33000);
  menu.scanReady(1);
  assert(menu.choose(0));
  menu.connect(34000);
  assert(menu.back()); // leave UI, bounded connection continues
  menu.service(64000, false);
  assert(menu.phase == phase::failed);
  menu.scan(near_wrap);
  menu.service(near_wrap + 29999, false);
  assert(menu.phase == phase::scanning);
  menu.service(near_wrap + 30000, false);
  assert(menu.phase == phase::failed);

  using confirm = restart_confirmation_state_t;
  confirm restart;
  assert(!restart.request(3, 3, false)); // selecting current source is inert
  assert(restart.stage == confirm::stage_t::source);
  assert(restart.request(0, 3, false));
  assert(restart.stage == confirm::stage_t::confirm && restart.pending == 3);
  assert(confirm::safe_default_row == 1);
  assert(restart.decide(0) == confirm::decision_t::none); // explanation cannot restart
  assert(restart.decide(1) == confirm::decision_t::cancelled);
  assert(restart.stage == confirm::stage_t::source);
  assert(restart.request(3, 3, true)); // Wi-Fi-suspended BLE needs restart
  assert(restart.decide(2) == confirm::decision_t::apply);
  assert(restart.stage == confirm::stage_t::confirm); // visible until restart screen takes over
  puts("PASS: radio stop/settle/timeout/cancel/wrap; BLE scan/select/connect; restart confirmation safe default/cancel/apply");
}
