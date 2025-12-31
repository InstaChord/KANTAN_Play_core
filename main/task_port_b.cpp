// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_port_b.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
static uint8_t pin_index[def::hw::max_port_b_pins] = { UINT8_MAX, UINT8_MAX };

bool task_port_b_t::start(void)
{
#if defined (M5UNIFIED_PC_BUILD)
#else
  pin_index[0] = M5.getPin(m5::pin_name_t::port_b_pin1); // pin1 == input side
  pin_index[1] = M5.getPin(m5::pin_name_t::port_b_pin2); // pin2 == output side

  for (int i = 0; i < def::hw::max_port_b_pins; ++i) {
    m5gfx::pinMode(pin_index[i], m5gfx::pin_mode_t::input_pullup);
  }

  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "port_b", 2048, this, def::system::task_priority_port_b, nullptr, def::system::task_cpu_port_b);
#endif

  return true;
}

void task_port_b_t::task_func(task_port_b_t* me)
{
  uint8_t pin_level[def::hw::max_port_b_pins];

  for (int i = 0; i < def::hw::max_port_b_pins; ++i) {
    pin_level[i] = m5gfx::gpio_in(pin_index[i]) ? 0 : 255;
  }

  for (;;) {
    M5.delay(1);

    for (int i = 0; i < def::hw::max_port_b_pins; ++i) {
      uint8_t level = m5gfx::gpio_in(pin_index[i]) ? 0 : 255;
      if (pin_level[i] != level) {
        int diff = (int32_t)(pin_level[i] - level);
        diff = diff * 3;
        level += (diff < 0 ? diff + 1 : diff) >> 2;
        pin_level[i] = level;
        system_registry->external_input.setPortBValue8(i, level);
      }
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
