// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_spi.hpp"
#include "gui.hpp"
#include "file_manage.hpp"

#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------

void task_spi_t::start(void)
{
  procFile();

  M5.Display.fillScreen(0);

  gui.init();
#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "spi", this);
#else
  TaskHandle_t handle = nullptr;
  xTaskCreatePinnedToCore((TaskFunction_t)task_func, "spi", 1024*3, this, def::system::task_priority_spi, &handle, def::system::task_cpu_spi);
  system_registry.file_command.setNotifyTaskHandle(handle);
#endif
}

void task_spi_t::procFile(void)
{
  // int proc_remain = 4;
  const registry_t::history_t* history;
  def::app::file_command_info_t file_command_info;
  while (nullptr != (history = system_registry.file_command.getHistory(_history_code))) {
    file_command_info.raw = history->value;
    if (file_command_info.raw == 0) { continue; }

M5_LOGV("file_command_info index:%d type:%d file:%d mem:%d ", history->index, file_command_info.dir_type, file_command_info.file_index, file_command_info.mem_index);

    switch (history->index) {
    default:
      break;

    case system_registry_t::reg_file_command_t::index_t::UPDATE_LIST:
      {
        file_manage.updateFileList(file_command_info.dir_type);
        system_registry.file_command.clearUpdateList();
      }
      break;

    case system_registry_t::reg_file_command_t::index_t::FILE_LOAD:
      {
        auto mem = file_manage.loadFile(file_command_info.dir_type, file_command_info.file_index);
        system_registry.file_command.clearFileLoadRequest();
        if (mem != nullptr) {
          system_registry.operator_command.addQueue( { def::command::file_load_notify, mem->index } );
        } else {
          //TODO:読込に失敗した通知を何らかの形で行う
        }
        if (mem == nullptr || mem->dir_type != def::app::data_type_t::data_setting) {
          system_registry.popup_notify.setPopup(mem != nullptr, def::notify_type_t::NOTIFY_FILE_LOAD);
        }
      }
      break;

    case system_registry_t::reg_file_command_t::index_t::FILE_SAVE:
      {
        bool result = file_manage.saveFile(file_command_info.dir_type, file_command_info.mem_index);
        system_registry.file_command.clearFileSaveRequest();
        if (file_command_info.dir_type != def::app::data_type_t::data_setting) {
          system_registry.popup_notify.setPopup(result, def::notify_type_t::NOTIFY_FILE_SAVE);
        }
        system_registry.operator_command.addQueue( { def::command::file_save_notify, file_command_info.mem_index } );

        // 未保存の編集の警告表示を更新する
        system_registry.checkSongModified();
      }
      break;
    }
  }  
}

void task_spi_t::task_func(task_spi_t* me)
{
  bool flg_notify = true;
  gui.startWrite();
  for (;;) {
    if (flg_notify) {
      gui.endWrite();

      me->procFile();

      gui.startWrite();
    }
    int wait = !gui.update() ? 8 : 1;
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(wait);
    flg_notify = true;
#else
    system_registry.task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
    flg_notify = ulTaskNotifyTake(pdTRUE, wait);
    system_registry.task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
#endif
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
