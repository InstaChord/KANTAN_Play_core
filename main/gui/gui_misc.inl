// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

struct ui_filename_t : public ui_base_t
{
  std::string _filename;
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto fn = file_manage.getDisplayFileName();
    if (_filename != fn) {
      _filename = fn;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override {
    canvas->setTextSize(1, 1);
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextDatum(m5gfx::textdatum_t::top_left);
    canvas->drawString(_filename.c_str(), offset_x, offset_y);
  }
};
ui_filename_t ui_filename;

struct ui_slot_label_t : public ui_base_t
{
  uint8_t _slot_index = 0xFF;
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    auto slot = system_registry->runtime_info.getPlaySlot();
    if (_slot_index != slot) {
      _slot_index = slot;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override {
    int num = (int)(_slot_index + 1);
    char slot_buf[6];
    if (num < 10) {
      snprintf(slot_buf, sizeof(slot_buf), "s %d", num);
    } else {
      snprintf(slot_buf, sizeof(slot_buf), "s%d", num);
    }
    canvas->setTextSize(1, 2);
    int32_t text_w = canvas->textWidth(slot_buf);
    int32_t bg_x = offset_x + _client_rect.w - text_w - 2;
    int32_t bg_w = text_w + 2;
    canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, TFT_BLACK);
    { // スロットボタンのフォーカス色と同じグラデーション
      static constexpr const uint32_t button_color_tbl[] = {
        0x383644, 0x3B3A3C, 0x212121, 0x080808,
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
        0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
        0x000000, 0x000000, 0x000000,
        0x010101, 0x020202, 0x030303, 0x060606, 0x090909, 0x0D0D0D, 0x131313,
        0x1A1A1A, 0x212121, 0x272727, 0x292929, 0x141414, 0xEAEAEA, 0xCECECE,
      };
      static constexpr int tbl_size = sizeof(button_color_tbl) / sizeof(button_color_tbl[0]);
      uint32_t base = system_registry->color_setting.getButtonDegreeColor();
      int h = _client_rect.h;
      if (h > tbl_size) { h = tbl_size; }
      canvas->drawFastVLine(bg_x, offset_y, h, add_color(base, button_color_tbl[0]));
      for (int i = 0; i < h; ++i) {
        uint32_t c = button_color_tbl[i];
        c = (c == 0) ? base : add_color(base, c);
        canvas->writeFastHLine(bg_x + 1, offset_y + i, bg_w - 1, c);
      }
    }
    canvas->setTextDatum(m5gfx::textdatum_t::middle_left);
    canvas->setTextColor(TFT_WHITE);
    canvas->drawString(slot_buf, bg_x + 1, offset_y + _client_rect.h / 2 + 2);
  }
};
ui_slot_label_t ui_slot_label;

static void update_header_container_width(void);

static constexpr int32_t slot_label_w = 52;
static constexpr int32_t slot_label_h = 32;

struct ui_left_icon_container_t : public ui_container_t
{
protected:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    int w = 0;
    for (auto ui : _ui_child) {
      ui->update(param, offset_x, offset_y);
      auto tr = ui->getTargetRect();
      if (tr.x != w) {
        param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
        tr.x = w;
        ui->setTargetRect(tr);
        param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
      }
      w += tr.w;
      if (tr.w) {
        w += 2;
      }
    }
    update_header_container_width();
    ui_base_t::update_impl(param, offset_x, offset_y);
  }
};
ui_left_icon_container_t ui_left_icon_container;

struct ui_right_icon_container_t : public ui_container_t
{
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    int32_t w = 0;
    for (auto ui : _ui_child) {
      ui->update(param, offset_x, offset_y);
      auto tr = ui->getTargetRect();
      tr.x = w;
      if (tr.w) {
        w += 2;
      }
      w += tr.w;
      ui->setTargetRect(tr);
    }
    _target_rect.w = w;
    int32_t new_x = disp_width - w;
    int32_t diff = _target_rect.x - new_x;
    _target_rect.x = new_x;
    _client_rect = _target_rect;

    w = 0;
    if (diff) {
      for (auto ui : _ui_child) {
        auto tr = ui->getClientRect();
        tr.x += diff;
        // param->addInvalidatedRect({offset_x + tr.x, offset_y, tr.w, tr.h});
        // param->addInvalidatedRect({offset_x +    x, offset_y, tr.w, tr.h});
        ui->setClientRect(tr);
      }
    }
    update_header_container_width();
    ui_base_t::update_impl(param, offset_x, offset_y);
  }
};
ui_right_icon_container_t ui_right_icon_container;

static void update_header_container_width(void)
{
  auto left = ui_left_icon_container.getTargetRect();
  auto right = ui_right_icon_container.getTargetRect();
  int32_t w = right.x - left.x - 1;
  if (w < 0) { w = 0; }
  if (left.w != w) {
    left.w = w;
    ui_left_icon_container.setTargetRect(left);
  }

  // スロットラベルを右アイコン群の左端に合わせてヘッダー下寄せで配置
  int32_t slot_x = right.x - slot_label_w - 1;
  int32_t slot_y = header_height - slot_label_h;
  auto slot_tr = ui_slot_label.getTargetRect();
  if (slot_tr.x != slot_x || slot_tr.y != slot_y) {
    rect_t nr = { slot_x, slot_y, slot_label_w, slot_label_h };
    ui_slot_label.setTargetRect(nr);
    ui_slot_label.setClientRect(nr);
  }
}

struct ui_raw_wave_t : public ui_base_t
{
protected:
  int _raw_wave_pos;
  int _prev_min_y = UINT8_MAX;
  int _prev_max_y = 0;
  int _min_x;
  int _max_x;
  bool _is_visible = false;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool visible;
    switch (system_registry->runtime_info.getGuiMode()) {
    case def::gui_mode_t::gm_menu:
    case def::gui_mode_t::gm_part_edit:
    case def::gui_mode_t::gm_melody_edit:
    case def::gui_mode_t::gm_song_play:
    case def::gui_mode_t::gm_song_recording:
      visible = false;
      break;
    default:
      visible = system_registry->user_setting.getGuiWaveView();
      break;
    }
    if (_is_visible != visible) {
      if (visible) {
        setTargetRect({ 0, disp_height - main_btns_height, disp_width, main_btns_height });
        _is_visible = visible;
      } else {
        setTargetRect({ 0, disp_height - (main_btns_height >> 1), disp_width, 0 });
        if (getClientRect().empty()) {
          _is_visible = false;
        }
      }
    }

    ui_base_t::update_impl(param, offset_x, offset_y);

    if (!_is_visible) { return; }

    int start_pos = system_registry->raw_wave_pos - disp_width;
    if (start_pos < 0) {
      start_pos += system_registry->raw_wave_length;
    }
    _raw_wave_pos = start_pos;

    uint8_t min_y = UINT8_MAX;
    uint8_t max_y = 0;
    int min_x = 0;
    int max_x = 0;
    auto wave = system_registry->raw_wave;
    const int loopend = system_registry->raw_wave_length;
    for (int x = 0; x < loopend; ++x) {
      if (min_y > wave[x].first) {
        min_y = wave[x].first;
        min_x = x;
      }
      if (max_y < wave[x].second) {
        max_y = wave[x].second;
        max_x = x;
      }
    }
    min_x -= start_pos;
    if (min_x < 0)
    {
      min_x += system_registry->raw_wave_length;
    }
    _min_x = min_x;

    max_x -= start_pos;
    if (max_x < 0)
    {
      max_x += system_registry->raw_wave_length;
    }
    _max_x = max_x;

    const int ch = _client_rect.h;
    int y0 = min_y < _prev_min_y ? min_y : _prev_min_y;
    int y1 = max_y > _prev_max_y ? max_y : _prev_max_y;
    y0 = (y0 * ch) >> 8;
    y1 = (y1 * ch) >> 8;

    _prev_min_y = (++_prev_min_y < min_y) ? _prev_min_y : min_y;
    _prev_max_y = (--_prev_max_y > max_y) ? _prev_max_y : max_y;

    if (y0 > 0) { --y0; }
    if (y1 < ch-1) { ++y1; }

    param->addInvalidatedRect({offset_x, offset_y + y0, _client_rect.w, y1 - y0});
  }
  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override {
    if (!_is_visible || _client_rect.empty()) { return; }

    image_dark_shift(canvas, offset_x, offset_y, _client_rect.w, _client_rect.h);

    const int ch = _client_rect.h;
    const int clip_h = clip_rect->h;
    const int min_y = (_prev_min_y * ch) >> 8;
    const int max_y = (_prev_max_y * ch) >> 8;
    for (int i = 1; i < 8; ++i) {
      int yy = (i * ch) >> 3;
      image_color_or( canvas
                    , offset_x
                    , offset_y + yy
                    , _client_rect.w, 1
                    , 0x03F0u);
    }
    image_color_or( canvas
                  , offset_x
                  , offset_y + min_y
                  , _client_rect.w, 1
                  , 0xC618u);
    image_color_or( canvas
                  , offset_x
                  , offset_y + max_y
                  , _client_rect.w, 1
                  , 0xC618u);
    { // 波形を描画する
      const int xe = clip_rect->right() - offset_x;
      const int ye = clip_rect->bottom() - offset_y;
      const auto wid = canvas->width();
      auto wave = system_registry->raw_wave;
      auto buf = (m5gfx::swap565_t*)(canvas->getBuffer());
      const auto raw_wave_length = system_registry->raw_wave_length;
      for (int x = -offset_x; x < xe; ++x) {
        int i = _raw_wave_pos + x;
        while (i >= raw_wave_length) {
          i -= raw_wave_length;
        }
        while (i < 0) {
          i += raw_wave_length;
        }
        int y0 = ((wave[i].first  * ch) >> 8) + offset_y;
        int y1 = ((wave[i].second * ch) >> 8) + offset_y;
        if (y0 < 0) { y0 = 0; }
        if (y1 > clip_h) { y1 = clip_h; }
        for (int y = y0; y < y1 && y < ye; ++y) { buf[y * wid].raw |= __builtin_bswap16(0xC600); }
        ++buf;
      }
    }
  }
};
ui_raw_wave_t ui_raw_wave;

void gui_t::init(void)
{
  _gfx = &M5.Display;
  _gfx->setRotation(0);
  _gfx->setColorDepth(color_depth);
  _gfx->setFont(&fonts::efontJA_16_b);

#if !defined (M5UNIFIED_PC_BUILD)
  // メモリブロックの断片化への対策として、小さい断片化領域から使用するため、敢えて最大領域を先回りして確保する。
  auto dummy = m5gfx::heap_alloc_dma(heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  // 上記の処理により、以下のメモリ確保は二番目に小さい領域から確保されることになる。
  for (int i = 0; i < disp_buf_count; ++i) {
    _draw_buffer[i] = (uint16_t*)m5gfx::heap_alloc_dma(max_disp_buf_pixels * color_depth >> 3);
  }
  // 先回りして確保しておいた領域を解放する。
  m5gfx::heap_free(dummy);
#endif
  for (int i = 0; i < disp_buf_count; ++i) {
    if (_draw_buffer[i] == nullptr) {
      _draw_buffer[i] = (uint16_t*)m5gfx::heap_alloc_dma(max_disp_buf_pixels * color_depth >> 3);
    }
  }

  // disp_width  = _gfx->width();
  // disp_height = _gfx->height();
  ui_background.setTargetRect({ 0,0,disp_width,disp_height });
  ui_background.setClientRect({ 0,0,disp_width,disp_height });
/*
  ui_header.setClientRect({0,0,disp_width,0});
  ui_header.setTargetRect({0,0,disp_width,header_height});
*/

  const int32_t battery_icon_width = 14;
  ui_main_buttons.setTargetRect({ 0, disp_height - main_btns_height, disp_width, main_btns_height });
  ui_main_buttons.setClientRect({ 0, disp_height, disp_width, 0 });

  // ui_progression_timeline.setTargetRect({ disp_width, disp_height - main_btns_height, 0, main_btns_height });
  ui_progression_timeline.setTargetRect({ 0, disp_height, disp_width, 0 });
  ui_progression_timeline.setClientRect(ui_progression_timeline.getTargetRect());

  ui_status_line.setTargetRect({ 0, disp_height, disp_width, 0 });
  ui_status_line.setClientRect(ui_status_line.getTargetRect());

  { rect_t sr = { disp_width - ui_step_counter_t::w, disp_height, ui_step_counter_t::w, ui_step_counter_t::h };
    ui_step_counter.setTargetRect(sr);
    ui_step_counter.setClientRect(sr); }

  ui_raw_wave.setTargetRect({ 0, disp_height - (main_btns_height >> 1), disp_width, 0 });

  ui_sub_buttons.setTargetRect({ 0, disp_height - main_btns_height - sub_btns_height, disp_width, sub_btns_height });
  ui_sub_buttons.setClientRect({ 0, disp_height, disp_width, 0 });

  ui_left_icon_container.setTargetRect({ 0, 0, disp_width, header_height });
  ui_left_icon_container.setClientRect({ 0, 0, 0, header_height });

  ui_right_icon_container.setTargetRect({ 0, 0, disp_width, header_height });
  ui_right_icon_container.setClientRect({ 0, 0, disp_width, 0 });

  ui_popup_notify.setClientRect({ disp_width >> 1, disp_height >> 1, 0, 0 });
  ui_popup_notify.setTargetRect(ui_popup_notify.getClientRect());
  ui_popup_notify.setHideRect(ui_popup_notify.getClientRect());

  ui_popup_qr.setClientRect({ disp_width>>1, disp_height>>1, 0, 0 });
  ui_popup_qr.setTargetRect(ui_popup_qr.getClientRect());

  int x = disp_width;// - header_height;
  ui_volume_info.setTargetRect({ x, 0, header_height , header_height });
  ui_volume_info.setClientRect({ x, 0, header_height , 0 });
  ui_battery_info.setTargetRect({ x, 0, battery_icon_width, header_height });
  ui_battery_info.setClientRect({ x, 0, battery_icon_width, 0 });

  rect_t r = { x, 0, 0, header_height };
  ui_wifi_sta_info.setTargetRect(r);
  ui_wifi_sta_info.setClientRect(r);
  ui_wifi_ap_info.setTargetRect(r);
  ui_wifi_ap_info.setClientRect(r);
  ui_icon_auto_play.setTargetRect(r);
  ui_icon_auto_play.setClientRect(r);
  ui_midiport_info.setTargetRect(r);
  ui_midiport_info.setClientRect(r);

  r.x = 0;
  ui_playkey_info.setClientRect(r);
  ui_song_modified.setClientRect(r);
  ui_song_modified.setTargetRect(r);
  ui_filename.setClientRect(r);

  r.w = 48;
  ui_playkey_info.setTargetRect(r);
  ui_filename.setTargetRect({ 0, 0, disp_width, header_height });

  // スロットラベル初期配置（update_header_container_widthで動的更新）
  {
    rect_t sr = { 0, header_height - slot_label_h, slot_label_w, slot_label_h };
    ui_slot_label.setTargetRect(sr);
    ui_slot_label.setClientRect(sr);
  }

  ui_playkey_select.setClientRect({ 0, 160-80,  0, 160 });
  ui_playkey_select.setTargetRect(ui_playkey_select.getClientRect());
  ui_playkey_select.setHideRect(ui_playkey_select.getClientRect());
  ui_playkey_select.setShowRect({ 0, 160-80, 60, 160 });


  ui_chord_part_container.setTargetRect({ 0, header_height, disp_width, disp_height - (header_height + main_btns_height + sub_btns_height) });

  auto side_width = 0;//24;
  auto arp_width = disp_width - side_width;
  auto arp_height = disp_height - (header_height + main_btns_height + sub_btns_height);
  {
    int32_t w = arp_width / 3;
    int32_t h = arp_height >> 1;
    for (int i = 0; i < def::app::max_chord_part; ++i) {
      ui_partinfo_list[i].setPartIndex(i);
      int x = (i % 3) * w;
      int y = (i / 3) * h;// + header_height;
      ui_partinfo_list[i].setClientRect( { x, y, w, 0 } );
      ui_partinfo_list[i].setTargetRect( { x, y, w, h } );
      ui_chord_part_container.addChild(&ui_partinfo_list[i]);
    }
  }
  ui_chord_part_container.addChild(&ui_arpeggio_edit);
  ui_melody_edit.setClientRect({ 0, 0, 0, 0 });
  ui_melody_edit.setTargetRect({ 0, 0, 0, 0 });
  ui_chord_part_container.addChild(&ui_melody_edit);
  ui_chord_part_container.shrink_to_fit();

  ui_left_icon_container.addChild(&ui_playkey_info);
  ui_left_icon_container.addChild(&ui_song_modified);
  ui_left_icon_container.addChild(&ui_filename);
  ui_left_icon_container.shrink_to_fit();

  ui_right_icon_container.addChild(&ui_icon_auto_play);  
  ui_right_icon_container.addChild(&ui_midiport_info);
  ui_right_icon_container.addChild(&ui_wifi_ap_info);
  ui_right_icon_container.addChild(&ui_wifi_sta_info);
  ui_right_icon_container.addChild(&ui_battery_info);
  ui_right_icon_container.addChild(&ui_volume_info);
  ui_right_icon_container.shrink_to_fit();

  ui_background.addChild(&ui_chord_part_container);
  ui_background.addChild(&ui_main_buttons);
  ui_background.addChild(&ui_sub_buttons);
  ui_background.addChild(&ui_progression_timeline);
  ui_background.addChild(&ui_status_line);
  ui_background.addChild(&ui_step_counter);
  ui_background.addChild(&ui_raw_wave);
  ui_background.addChild(&ui_left_icon_container);
  ui_background.addChild(&ui_right_icon_container);
  ui_background.addChild(&ui_slot_label);
  for (auto &ui : ui_menu_bodys) {
    ui_background.addChild(&ui);
  }
  ui_background.addChild(&ui_menu_header);
  ui_background.addChild(&ui_playkey_select);
  ui_background.addChild(&ui_popup_notify);
  ui_background.addChild(&ui_popup_qr);
  ui_background.addChild(&ui_restart_notice);
  ui_background.shrink_to_fit();

//-------------------------------------------------------------------------

  for (int i = 0; i < disp_buf_count; ++i) {
    disp_buf[i].setPsram(false);
    disp_buf[i].setColorDepth(color_depth);
    // disp_buf[i].setBuffer(_draw_buffer[i], disp_height, disp_buf_width, depth);
    // disp_buf[i].setRotation(0);
    // disp_buf[i].createSprite(disp_buf_width, disp_height);
    disp_buf[i].setFont(&fonts::efontJA_16_b);
  }
  uint32_t msec = M5.millis();
  _draw_param.prev_msec = msec;
  _draw_param.current_msec = msec;
// ティアリング対策 (LCDリフレッシュレートを下げる)
/*
  static constexpr const uint8_t B1_DIVA = 0x01;
  static constexpr const uint8_t B1_RTNA = 0x1C;
  static constexpr const uint8_t B5_VFP = 0x1E;
  static constexpr const uint8_t B5_VBP = 0x02;
  _gfx->startWrite();
  _gfx->writeCommand(0xB1);
  _gfx->writeData(B1_DIVA);
  _gfx->writeData(B1_RTNA);
  _gfx->writeCommand(0xB5);
  _gfx->writeData(B5_VFP);
  _gfx->writeData(B5_VBP);
  _gfx->writeData(0x16);
  _gfx->writeData(0x06);
  _gfx->endWrite();
//*/
_gfx->startWrite();
_gfx->writeCommand(0xB5);
_gfx->writeData(8);
_gfx->writeData(8);
_gfx->writeData(0x0A);
_gfx->writeData(0x14);
_gfx->endWrite();

  // _gfx->startWrite();
}

bool gui_t::update(void)
{
  // if (!app_core.getGuiEnabled()) {
  //   gfx->endWrite();
  //   do {
  //     M5.delay(1);
  //   } while (!app_core.getGuiEnabled());
  //   gfx->startWrite();
  // }

#if defined (M5UNIFIED_PC_BUILD)
  M5.delay(8);
#else

#if defined ( DEBUG_GUI )
  ++_fps_counter;
  uint32_t sec = M5.millis() / 1000;
  static uint32_t prev_sec = 0;
  if (prev_sec != sec) {
    prev_sec = sec;
    M5_LOGV("fps:%d  delay:%d", _fps_counter, _delay_counter);
    _fps_counter = 0;
    _delay_counter = 0;
    M5.delay(1);
  }
#endif

//*/
#endif

  auto param = &_draw_param;
  param->resetInvalidatedRect();
  param->is_menu_mode = (system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_menu);
  uint32_t msec = M5.millis();
  uint32_t prev_msec = param->current_msec;
  int diff = msec - prev_msec;
  param->current_msec = msec;
  if (diff <= 0) { return false; }
  if (diff > 255) { diff = 255; }
  param->prev_msec = prev_msec;
  param->smooth_step = diff;
  // param->flg_update = false;
  ui_background.update(param, 0, 0);
  if (!param->hasInvalidated) {
  // if (!param->flg_update) {
// static uint32_t prev_btnbitmask;
// uint32_t btnbitmask = system_registry->internal_input.getButtonBitmask();
// if (prev_btnbitmask != btnbitmask) {
//   prev_btnbitmask = btnbitmask;
//   _gfx->setCursor(0, 0);
//   _gfx->printf("%08x", (unsigned int)btnbitmask);
// }
    return false;
  }

  bool suspended = false;

#if defined ( DEBUG_GUI )
  uint32_t backcolor = rand();
#else
  uint32_t backcolor = 0;// param->color_set->background;
#endif

  for (int32_t i = 0; i < draw_param_t::max_clip_rect; ++i)
  {
    rect_t update_rect = param->invalidated_rect[i];
    auto canvas = &disp_buf[disp_buf_idx];
    if (!update_rect.empty()) {
      if (suspended) {
        system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
        suspended = false;
      }
      // clip_rect.x -= x;


      // canvas->deleteSprite();
//       if (nullptr == canvas->createSprite(update_rect.w, update_rect.h)) {
// M5_LOGE("createSprite failed");
// continue;
//       }
// M5_LOGE("rect x:%d y:%d w:%d h:%d", update_rect.x, update_rect.y, update_rect.w, update_rect.h);
      assert(update_rect.w * update_rect.h < max_disp_buf_pixels && "rect size overflow !!");

      if (update_rect.w * update_rect.h >= max_disp_buf_pixels) {
        M5_LOGE("rect size overflow !!  w:%d h:%d", update_rect.w, update_rect.h);
        abort();
      }
      canvas->setBuffer(_draw_buffer[disp_buf_idx], update_rect.w, update_rect.h, color_depth);

      if (++disp_buf_idx == disp_buf_count) { disp_buf_idx = 0; }

      canvas->clearClipRect();
      canvas->fillScreen(backcolor);
      // canvas->drawRect(0,0,canvas->width(),canvas->height(),backcolor);
      auto clip_rect = update_rect;
      clip_rect.x = 0;
      clip_rect.y = 0;
      ui_background.draw(param, canvas, -update_rect.x, -update_rect.y, &clip_rect);
// canvas->fillScreen(rand());
    }
    if (!suspended) {
      system_registry->task_status.setSuspend(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
      suspended = true;
    }

    if (!update_rect.empty()) {
#if !defined (M5UNIFIED_PC_BUILD)
      if (_gfx->dmaBusy()) {
        M5.delay(1);
        ++_delay_counter;
        do { taskYIELD(); } while (_gfx->dmaBusy());
      }

      if (update_rect.w < 128) {
        uint8_t l = _gfx->getScanLine() - update_rect.x;
        // ティアリング対策ウェイト
        if (l < update_rect.w) {
          M5.delay(1 + ((update_rect.w - l) * 9 >> 7));
        }
      }
#endif
      canvas->pushSprite(_gfx, update_rect.x, update_rect.y);
    }
  }
  if (suspended) {
    system_registry->task_status.setWorking(system_registry_t::reg_task_status_t::bitindex_t::TASK_SPI);
  }
  return true;
}

void gui_t::procTouchControl(const m5::touch_detail_t& td)
{
// この関数はSPIタスクではなくI2Cタスクから実行される
// 従ってSPIに影響のある処理は行わないこと
  auto ui = ui_background.searchUI(td.base_x, td.base_y);
  if (ui != nullptr) {
    // M5_LOGV("touch ui:%08x x:%d y:%d p:%d", ui, td.x, td.y, td.isPressed());
    ui->touchControl(&_draw_param, td);

    static uint32_t part_change_mask;
    if (td.wasPressed())
    { // 画面に触れたタイミングでパートパネル上に指があればenableチェンジができるようにする
      part_change_mask = 0;
      // 押した場所がボタンの位置にあるか否か
      for (int i = 0; i < def::app::max_chord_part; ++i)
      { // タッチ操作によるパート有効・無効が機能するようにしておく
        if (ui == &ui_partinfo_list[i])
        {
          part_change_mask = ~0u;
        }
      }
    }

    ui = ui_background.searchUI(td.x, td.y);
    for (int i = 0; i < def::app::max_chord_part; ++i)
    { // タッチ操作によるパート有効・無効の切替
      if (ui != &ui_partinfo_list[i]) { continue; }

      if (td.wasHold())
      { // 長押しで編集モードに移行
        system_registry->operator_command.addQueue( { def::command::part_edit_menu, i+1 } );
      } else
      {
        auto partinfo = &(system_registry->current_slot->chord_part[i].part_info);
        bool next_enabled = partinfo->getEnabled();
        if (td.wasClicked()
          || td.isFlicking()
          || (td.wasPressed() && !next_enabled))
        {
          if ((part_change_mask & (1 << i))) {
            part_change_mask &= ~(1 << i);
            auto command = next_enabled ? def::command::part_off : def::command::part_on;
            system_registry->operator_command.addQueue( { command, i + 1 } );
          }
        }
      }
      break;
    }
  }
}
