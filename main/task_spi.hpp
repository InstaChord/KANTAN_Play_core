// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_SPI_HPP
#define KANPLAY_TASK_SPI_HPP

#include "registry.hpp"

/*
task_spi は SPI通信を利用するタスクです。
 - GUI画面描画
 - TFカード入出力
*/

namespace kanplay_ns {
//-------------------------------------------------------------------------
class task_spi_t {
public:
  void start(void);
private:
  static void task_func(task_spi_t* me);
  void procFile(void);
  registry_t::history_code_t _history_code = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
