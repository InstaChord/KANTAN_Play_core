// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <stdio.h>

#include "task_spi.hpp"
#include "task_i2c.hpp"
#include "task_i2s.hpp"
#include "task_midi.hpp"
#include "task_wifi.hpp"
#include "task_port_a.hpp"
#include "task_port_b.hpp"
#include "task_operator.hpp"
#include "task_commander.hpp"
#include "task_kantanplay.hpp"
#include "system_registry.hpp"
#include "file_manage.hpp"

namespace kanplay_ns {
  static void log_memory(int index = 0)
  {
  #if CORE_DEBUG_LEVEL > 3 // && !defined ( M5UNIFIED_PC_BUILD )
    size_t free_heap_size = esp_get_free_heap_size() >> 10;
    size_t malloc_cap_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >> 10;
    size_t malloc_cap_8bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >> 10;
    size_t malloc_cap_32bit = heap_caps_get_largest_free_block(MALLOC_CAP_32BIT) >> 10;
    size_t malloc_cap_dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA) >> 10;
    size_t malloc_cap_dma_large = heap_caps_get_largest_free_block(MALLOC_CAP_DMA) >> 10;
    // size_t malloc_cap_spi = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >> 10;
    M5_LOGV("%d Mem heap:%5dKB  Internal:%5dKB  8bit:%5dKB  32bit:%5dKB  DMA free:%5dKB  DMA Large:%5dKB"
    , index, free_heap_size, malloc_cap_internal, malloc_cap_8bit, malloc_cap_32bit, malloc_cap_dma_free, malloc_cap_dma_large);
    fflush(stdout);
  #endif
  }


  static void init(void) {
#if !defined (M5UNIFIED_PC_BUILD)

// メモリブロックの断片化への対策として、最大領域を先回りして確保しておく。(これにより小さい断片化領域から使用させることができる)
    auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));


#endif
    log_memory(0);
    auto cfg = M5.config();
    cfg.output_power = false;
    cfg.internal_spk = false;
    M5.begin(cfg);

    M5.Display.setRotation(0);

  #if defined (CONFIG_IDF_TARGET_ESP32S3)
    // CoreS3内蔵音源AW88298への電力供給を止める
    M5.Power.Axp2101.setALDO1(0);
    M5.Power.Axp2101.setBLDO2(0);

    // CoreS3内蔵カメラへの電力供給を止める
    M5.Power.Axp2101.setALDO3(0);
    M5.Power.Axp2101.setBLDO1(0);
  #endif

    M5.Display.setTextSize(2);
    M5.Display.printf("KANTAN Play\nver%lu.%lu.%lu\n\nboot"
      , def::app::app_version_major, def::app::app_version_minor, def::app::app_version_patch);

    {
      static constexpr const uint8_t aw9523_i2c_addr = 0x58;
      static constexpr const uint8_t port0_reg = 0x02;
      static constexpr const uint8_t port1_reg = 0x03;
      static constexpr const uint32_t port0_bitmask_bus_en = 0b00000010; // BUS EN
      static constexpr const uint32_t port1_bitmask_boost = 0b10000000; // BOOST_EN

      uint8_t buf[2];
      M5.In_I2C.readRegister(aw9523_i2c_addr, port0_reg, buf, sizeof(buf), 100000);
      uint8_t bus_en_on = buf[0] | port0_bitmask_bus_en;
      uint8_t boost_on = buf[1] | port1_bitmask_boost;
      uint8_t bus_en_off = bus_en_on & ~port0_bitmask_bus_en;

      M5.In_I2C.writeRegister8(aw9523_i2c_addr, port0_reg, bus_en_off, 100000);
      M5.In_I2C.writeRegister8(aw9523_i2c_addr, port1_reg, boost_on, 100000);

      // BUS_ENをオンにした直後に短時間に電流が大量に流れるのを避けるため、
      // 高速にオンオフを繰り返して、電流の立ち上がりを抑える。
      // これをしないとバッテリ電圧が低い状況下では起動に失敗することがある。
      for (int i = 0; i < 256; ++i) {
        M5.In_I2C.writeRegister8(aw9523_i2c_addr, port0_reg, bus_en_off, 400000);
        M5.In_I2C.writeRegister8(aw9523_i2c_addr, port0_reg, bus_en_on, 400000);
        m5gfx::delayMicroseconds(i);
      }
    }

    M5.Power.setChargeCurrent(200);

    log_memory(1);

    system_registry = new system_registry_t();
    auto task_spi = new task_spi_t();
    auto task_i2c = new task_i2c_t();
    auto task_i2s = new task_i2s_t();
    auto task_midi = new task_midi_t();
    auto task_wifi = new task_wifi_t();
    auto task_port_a = new task_port_a_t();
    auto task_port_b = new task_port_b_t();
    auto task_operator = new task_operator_t();
    auto task_commander = new task_commander_t();
    auto task_kantanplay = new task_kantanplay_t();

    log_memory(2); M5.delay(8); M5.Display.print("."); system_registry->init();
    log_memory(3); M5.delay(8); M5.Display.print("."); task_i2s->start();
    log_memory(4); M5.delay(8); M5.Display.print(".");
    if (!task_i2c->start()) {
      M5.Display.print("\nhardware not found.\n");
      M5.delay(4096);
      M5.Power.powerOff();
    }
    log_memory(5); M5.delay(8); M5.Display.print("."); task_midi->start();
    log_memory(6); M5.delay(8); M5.Display.print("."); task_wifi->start();
    log_memory(7); M5.delay(8); M5.Display.print("."); task_kantanplay->start();
    log_memory(8); M5.delay(8); M5.Display.print("."); task_operator->start();
    log_memory(9); M5.delay(8); M5.Display.print("."); task_commander->start();
    log_memory(10); M5.delay(8); M5.Display.print("."); task_port_a->start();
    log_memory(11); M5.delay(8); M5.Display.print("."); task_port_b->start();

#if !defined (M5UNIFIED_PC_BUILD)
    m5gfx::heap_free(dummy);
#endif

    log_memory(12); M5.delay(8); M5.Display.print("."); task_spi->start();

    system_registry->operator_command.addQueue( { def::command::system_control, def::command::sc_boot } );
  }
};

void setup() {
  kanplay_ns::init();
}

void loop() {
  M5.delay(1024);
#if !defined ( M5UNIFIED_PC_BUILD )
  kanplay_ns::log_memory(); 
/*
  {
    auto t = time(nullptr);
    auto tm = localtime(&t);
    static constexpr const char* const wd[7] = {"Sun","Mon","Tue","Wed","Thr","Fri","Sat"};
    M5.Log.printf("localtime:%04d/%02d/%02d (%s)  %02d:%02d:%02d\r\n",
          tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
          wd[tm->tm_wday],
          tm->tm_hour, tm->tm_min, tm->tm_sec);
  }
//*/
#endif
/*
  M5_LOGV("perf: %d %d  spi:%d i2c:%d i2s:%d midi:%d cmd:%d kanplay:%d"
    , kanplay_ns::system_registry->task_status.getLowPowerCounter() >> 10
    , kanplay_ns::system_registry->task_status.getHighPowerCounter() >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_SPI_COUNTER) >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_I2C_COUNTER) >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_I2S_COUNTER) >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_MIDI_INTERNAL_COUNTER) >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_COMMANDER_COUNTER) >> 10
    , kanplay_ns::system_registry->task_status.getWorkingCounter(kanplay_ns::system_registry_t::reg_task_status_t::index_t::TASK_KANTANPLAY_COUNTER) >> 10
  );
//*/
}

#if !defined (M5UNIFIED_PC_BUILD) && !defined (ARDUINO)
extern "C" void app_main(void)
{
  setup();
  for (;;) {
    loop();
  }
}
#endif
