// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_SPI_HPP
#define KANPLAY_TASK_SPI_HPP

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
    static constexpr const char mount_point[] = "/sdcard";
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
