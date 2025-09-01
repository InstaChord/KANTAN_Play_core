// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_port_a.hpp"

#include "common_define.hpp"
#include "system_registry.hpp"

#include "ex_i2c/external_m5bytebutton.hpp"
#include "ex_i2c/external_m5extio2.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// 最大 4台、アドレスは デフォルトを基準に 4単位で上のアドレスを使用
static external_m5extio2_t     external_m5extio2[4]     = { { 0x45 }, { 0x49 }, { 0x4D }, { 0x51 } };  // default addr :0x45
static external_m5bytebutton_t external_m5bytebutton[4] = { { 0x47 }, { 0x4B }, { 0x4F }, { 0x53 } };  // default addr :0x47

static interface_external_t** groups[] =
{ (interface_external_t*[]){ &external_m5extio2[0], &external_m5bytebutton[0], nullptr },
  (interface_external_t*[]){ &external_m5extio2[1], &external_m5bytebutton[1], nullptr },
  (interface_external_t*[]){ &external_m5extio2[2], &external_m5bytebutton[2], nullptr },
  (interface_external_t*[]){ &external_m5extio2[3], &external_m5bytebutton[3], nullptr }
};

static constexpr size_t groups_number = sizeof(groups) / sizeof(groups[0]);

bool task_port_a_t::start(void)
{
  M5.Ex_I2C.begin();
#if defined (M5UNIFIED_PC_BUILD)
#else
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "port_a", 1024*3, this, def::system::task_priority_port_a, nullptr, def::system::task_cpu_port_a);
#endif
  return true;
}

void task_port_a_t::task_func(task_port_a_t* me)
{
  uint8_t loop_counter = 0xFF;

  for (;;) {
    ++loop_counter;
    uint32_t button_state = 0;
    for (size_t group_index = 0; group_index < groups_number; ++group_index) {
      M5.delay(1);
      auto device_array = groups[group_index];

      uint32_t bitmask = 0;
      int j = 0;
      auto device = device_array[0];
      do {
        if (!device->exists()) {
          if (loop_counter == group_index) {
            device->init();
          }  
        } else {
          if (device->exists()) {
            device->update(bitmask);
          }
        }
        device = device_array[++j];
      } while (device != nullptr);
      button_state |= bitmask << (group_index * 8);
      system_registry.external_input.setPortABitmask8(group_index, button_state >> (group_index * 8));
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
