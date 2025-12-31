// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <mutex>

#include "task_spi.hpp"
#include "gui.hpp"
#include "file_manage.hpp"

#include "system_registry.hpp"

#if __has_include (<freertos/freertos.h>)
 #include <freertos/FreeRTOS.h>
 #include <freertos/task.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------

#if __has_include (<freertos/FreeRTOS.h>)
  TaskHandle_t spi_task_handle = nullptr;  
#endif

static std::mutex spi_mutex;
std::atomic<int> spi_lock_count = 0;

void spi_lock(void) // 画面制御以外でSPIを使用する際に使用する
{
  M5_LOGV("spi_lock");
  spi_lock_count++;
#if __has_include (<freertos/FreeRTOS.h>)
  if (spi_task_handle != nullptr) { xTaskNotifyGive(spi_task_handle); }
#endif
  spi_mutex.lock();
}

void spi_unlock(void)
{
  M5_LOGV("spi_unlock");
  spi_mutex.unlock();
  spi_lock_count--;
#if __has_include (<freertos/FreeRTOS.h>)
  if (spi_task_handle != nullptr) { xTaskNotifyGive(spi_task_handle); }
#endif
}

void task_spi_t::start(void)
{
  M5.Display.fillScreen(0);

  gui.init();
#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "spi", this);
#else
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "spi", 1024*3, this, def::system::task_priority_spi, &spi_task_handle, def::system::task_cpu_spi);
#endif
}

void task_spi_t::task_func(task_spi_t* me)
{
  spi_mutex.lock();
  gui.startWrite();
  for (;;) {
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(gui.update() ? 2 : 16);
#else
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
    bool updated = gui.update();
    system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
    ulTaskNotifyTake(pdTRUE, updated ? 2 : 16);
#endif
    if (spi_lock_count > 0) {
      gui.endWrite();
      spi_mutex.unlock();
      do {
#if defined (M5UNIFIED_PC_BUILD)
        M5.delay(1);
#else
        ulTaskNotifyTake(pdTRUE, 1);
#endif
      } while (spi_lock_count > 0);
      spi_mutex.lock();
      gui.startWrite();
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
