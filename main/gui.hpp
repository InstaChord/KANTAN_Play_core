// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_GUI_HPP
#define KANPLAY_GUI_HPP

#include <M5Unified.h>

namespace kanplay_ns {
//-------------------------------------------------------------------------
class gui_t {
public:

  void init(void);
  bool update(void);
  void startWrite(void);
  void endWrite(void);
  void procTouchControl(const m5::touch_detail_t& td);

  static constexpr const size_t disp_buf_count = 2;
  static constexpr const size_t max_disp_buf_pixels = 40 * 42 + 2;
  static constexpr const uint8_t color_depth = 16;
protected:
  uint16_t* _draw_buffer[disp_buf_count] = { nullptr, };
  M5Canvas disp_buf[disp_buf_count];
  uint16_t _delay_counter = 0;
  uint8_t _fps_counter = 0;
  uint8_t disp_buf_idx   = 0;
  uint8_t disp_queue_idx = 0;
};

extern gui_t gui;
//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
