// SPDX-License-Identifier: MIT
// Song専用メロディトラックの7行ピアノロール。

struct ui_melody_edit_t : public ui_base_t
{
  bool _visible = false;
  uint16_t _step = 0xFFFF;
  uint8_t _pitch = 0xFF;
  bool _fold = false;
  uint32_t _melody_crc = 0;
  uint32_t _working_change = 0;

  static bool isScalePitch(int pitch)
  {
    static constexpr uint8_t major[] = { 0, 2, 4, 5, 7, 9, 11 };
    int pc = (pitch - system_registry->runtime_info.getMasterKey()) % 12;
    if (pc < 0) { pc += 12; }
    return std::find(std::begin(major), std::end(major), pc) != std::end(major);
  }
  static int adjacentScalePitch(int pitch, int direction)
  {
    int result = pitch;
    do {
      result += direction;
      if (result < 0 || result > 127) { return pitch; }
    } while (!isScalePitch(result));
    return result;
  }
  void getRows(int rows[7]) const
  {
    rows[3] = _pitch;
    for (int i = 2; i >= 0; --i) { rows[i] = _fold ? adjacentScalePitch(rows[i + 1], 1) : rows[i + 1] + 1; }
    for (int i = 4; i < 7; ++i) { rows[i] = _fold ? adjacentScalePitch(rows[i - 1], -1) : rows[i - 1] - 1; }
  }
  void update_impl(draw_param_t* param, int offset_x, int offset_y) override
  {
    bool visible = system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_melody_edit;
    if (visible != _visible) {
      _visible = visible;
      setTargetRect(visible ? rect_t{ 0, 0, main_area_width, main_area_height } : rect_t{ 0, 0, 0, 0 });
      param->addInvalidatedRect({ offset_x, offset_y, main_area_width, main_area_height });
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
    if (!visible) { return; }
    auto step = system_registry->runtime_info.getMelodyCursorStep();
    auto pitch = system_registry->runtime_info.getMelodyCursorPitch();
    auto fold = system_registry->runtime_info.getMelodyScaleFold();
    auto crc = system_registry->song_data.melody.crc32();
    auto working_change = system_registry->working_command.getChangeCounter();
    if (_step != step || _pitch != pitch || _fold != fold || _melody_crc != crc
     || _working_change != working_change
     || (param->prev_msec >> 8) != (param->current_msec >> 8)) {
      _step = step; _pitch = pitch; _fold = fold; _melody_crc = crc;
      _working_change = working_change;
      param->addInvalidatedRect({ offset_x, offset_y, _client_rect.w, _client_rect.h });
    }
  }
  void draw_impl(draw_param_t* param, M5Canvas* canvas, int32_t offset_x,
                 int32_t offset_y, const rect_t* clip_rect) override
  {
    if (!_visible || _client_rect.empty()) { return; }
    constexpr int title_h = 20;
    constexpr int step_per_beat = def::app::melody_steps_per_beat;
    constexpr int steps_per_page = def::app::melody_steps_per_page;
    const int page_start = (_step / steps_per_page) * steps_per_page;
    const int grid_y = offset_y + title_h;
    const int grid_h = _client_rect.h - title_h;
    const int row_h = grid_h / 7;
    const int col_w = std::max(1, _client_rect.w / steps_per_page);
    int rows[7]; getRows(rows);

    canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, 0x080D18u);
    canvas->setTextDatum(m5gfx::textdatum_t::middle_left);
    canvas->setTextSize(1, 1);
    canvas->setTextColor(0xFFFFFFu);
    canvas->drawString("MELODY", offset_x + 4, offset_y + title_h / 2);
    bool tone_edit = system_registry->working_command.check({
      def::command::melody_edit_modifier, def::command::melody_modifier_tone });
    bool volume_edit = system_registry->working_command.check({
      def::command::melody_edit_modifier, def::command::melody_modifier_volume });
    uint8_t tone = system_registry->song_data.melody.info.getTone();
    uint8_t volume = system_registry->song_data.melody.info.getVolume();
    char setting_text[24];
    if (tone_edit) {
      auto tone_name = def::midi::program_name_table.at(tone)->get();
      snprintf(setting_text, sizeof(setting_text), "%03u %.13s", tone + 1, tone_name);
      canvas->setTextDatum(m5gfx::textdatum_t::middle_right);
      canvas->setTextColor(0x66DDFFu);
      canvas->drawString(setting_text, offset_x + _client_rect.w - 4, offset_y + title_h / 2);
    } else if (volume_edit) {
      snprintf(setting_text, sizeof(setting_text), "VOLUME %u", volume);
      canvas->setTextDatum(m5gfx::textdatum_t::middle_right);
      canvas->setTextColor(0x66DDFFu);
      canvas->drawString(setting_text, offset_x + _client_rect.w - 4, offset_y + title_h / 2);
    } else {
      snprintf(setting_text, sizeof(setting_text), "P%03u V%u", tone + 1, volume);
      canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
      canvas->setTextColor(0xAABBD0u);
      canvas->drawString(setting_text, offset_x + _client_rect.w / 2, offset_y + title_h / 2);
      canvas->setTextDatum(m5gfx::textdatum_t::middle_right);
      canvas->setTextColor(_fold ? 0x66DDFFu : 0xFFBB55u);
      canvas->drawString(_fold ? "SCALE FOLD" : "CHROMATIC", offset_x + _client_rect.w - 4, offset_y + title_h / 2);
    }

    for (int row = 0; row < 7; ++row) {
      int y = grid_y + row * row_h;
      int pc = rows[row] % 12;
      int degree = (pc - system_registry->runtime_info.getMasterKey()) % 12;
      if (degree < 0) { degree += 12; }
      bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
      uint32_t row_color = black ? 0x111827u : 0x182235u;
      if (degree == 0) {        // 主音: 少し青くして縦移動の基準にする
        row_color = 0x203A50u;
      } else if (degree == 7) { // 5度: 主音と区別できる控えめな紫
        row_color = 0x2B2E4Au;
      }
      canvas->fillRect(offset_x, y, _client_rect.w, row_h, row_color);
      canvas->drawFastHLine(offset_x, y, _client_rect.w, 0x35445Au);
    }
    for (int col = 0; col <= steps_per_page; ++col) {
      int x = offset_x + col * col_w;
      uint32_t color = ((page_start + col) % step_per_beat) == 0 ? 0x607080u : 0x2A3545u;
      canvas->drawFastVLine(x, grid_y, grid_h, color);
    }
    auto rowForPitch = [&](int note) -> int {
      for (int r = 0; r < 7; ++r) { if (rows[r] == note) { return r; } }
      return note > rows[0] ? -1 : (note < rows[6] ? 7 : -2);
    };

    for (int col = 0; col < steps_per_page; ++col) {
      uint16_t data_step = page_start + col;
      if (data_step >= def::app::max_progression_length) { break; }
      int x = offset_x + col * col_w;
      // 空ステップは直前音を保持するため、末尾イベント以降も表示ページ内では
      // 次のNote/Muteイベントが来るまで持続線を伸ばして入力結果を即座に見せる。
      int active = system_registry->song_data.melody.timeline.getActiveNote(data_step);
      if (active >= 0) {
        int row = rowForPitch(active);
        if (row >= 0 && row < 7) {
          int y = grid_y + row * row_h + row_h / 2;
          canvas->fillRect(x, y - 2, col_w, 5, 0x4EB7FFu);
        } else if (row == -1) {
          canvas->fillTriangle(x + col_w / 2, grid_y + 1, x + 2, grid_y + 6, x + col_w - 2, grid_y + 6, 0x4EB7FFu);
        } else if (row == 7) {
          int y = grid_y + grid_h - 1;
          canvas->fillTriangle(x + col_w / 2, y, x + 2, y - 5, x + col_w - 2, y - 5, 0x4EB7FFu);
        }
      }
      auto event = system_registry->song_data.melody.getEvent(data_step);
      if (event.isNote()) {
        int row = rowForPitch(event.getNote());
        if (row >= 0 && row < 7) {
          int y = grid_y + row * row_h + 2;
          canvas->fillRoundRect(x + 2, y, std::max(3, col_w - 4), std::max(3, row_h - 4), 3, 0x19C6FFu);
        } else if (_fold && row == -2) {
          int insert = 0;
          while (insert < 6 && event.getNote() < rows[insert + 1]) { ++insert; }
          int y = grid_y + (insert + 1) * row_h;
          canvas->drawFastHLine(x + 2, y, std::max(3, col_w - 4), 0xFF66CCu);
          canvas->drawFastHLine(x + 2, y + 1, std::max(3, col_w - 4), 0xFF66CCu);
        }
      } else if (event.isMute()) {
        canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
        canvas->setTextColor(0xFF7777u);
        canvas->drawString("M", x + col_w / 2, grid_y + grid_h / 2);
      }
    }
    int cursor_col = _step - page_start;
    int cursor_row = rowForPitch(_pitch);
    if (cursor_row >= 0 && cursor_row < 7) {
      int x = offset_x + cursor_col * col_w;
      int y = grid_y + cursor_row * row_h;
      uint32_t c = (param->current_msec & 0x100) ? TFT_YELLOW : TFT_RED;
      canvas->drawRect(x + 1, y + 1, std::max(2, col_w - 2), std::max(2, row_h - 2), c);
      canvas->drawRect(x + 2, y + 2, std::max(2, col_w - 4), std::max(2, row_h - 4), c);
    }
  }
};

static ui_melody_edit_t ui_melody_edit;
