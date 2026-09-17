// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

struct ui_playkey_select_t : public ui_timer_popup_t
{
protected:
    int _current_x256 = 0;
    int _target_x256 = 0;
    uint8_t _master_key = 0;
public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    bool updated = false;

    if (system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_menu) {
      close();
    } else {
      auto master_key = system_registry->runtime_info.getMasterKey();
      if (_master_key != master_key) {
        updated = true;
        _master_key = master_key;
        _target_x256 = master_key * 256;
      }
      if (_current_x256 != _target_x256) {
        updated = true;
        int diff = abs(_current_x256 - _target_x256) >> 8;
        if (diff > (def::app::max_play_key >> 1)) {
          if (_current_x256 < _target_x256) {
            _current_x256 += def::app::max_play_key << 8;
          } else {
            _current_x256 -= def::app::max_play_key << 8;
          }
        }
        _current_x256 = smooth_move(_target_x256, _current_x256, param->smooth_step);
      }
      procTime(updated ? 1000 : -param->smooth_step);
    }

    ui_timer_popup_t::update_impl(param, offset_x, offset_y);
    if (updated) {
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (_client_rect.h > (header_height * 7 >> 2))
    {
      int r = 79;
// M5_LOGV("x:%d y:%d cx:%d cy:%d", offset_x, offset_y, _client_rect.x, _client_rect.y);
      int x = offset_x + _client_rect.w - r - 1;
      int y = offset_y + (_client_rect.h >> 1);
      canvas->fillArc(x, y, r, r-54, 280, 80, 0x101020u);
      canvas->setTextDatum(m5gfx::datum_t::middle_center);
      canvas->setTextSize(1, 2);
      int i1 = (_current_x256 - 384) >> 8;
      int i2 = (_current_x256 + 640) >> 8;
      for (int i = i1; i <= i2; ++i) {
        int key = i;
        if (key < 0) {
          key += def::app::max_play_key;
        } else if (key >= def::app::max_play_key) {
          key -= def::app::max_play_key;
        }
        auto text = def::app::key_name_table[key];
        float angle = (((key + 3) << 8) - _current_x256) * M_PI / (256 * 6);
        int x1 = x + (int)((r * 3 / 4.0f) * sinf(angle)) - 6;
        int y1 = y + (int)((r * 3 / 4.0f) * cosf(angle)) - 2;
        canvas->setTextColor(i == _master_key ? 0xFFFF00u : 0xBBBBBBu);
        canvas->drawString(text, x1, y1);
      }
    } else {
      canvas->setTextDatum(m5gfx::datum_t::top_left);
      canvas->setTextSize(1, 2);
      canvas->setTextColor(0xFFFF00u);
      auto text = def::app::key_name_table[_master_key];
      int x = offset_x;
      int y = offset_y - 6;
      canvas->drawString(text, x, y);
    }
  }
};
ui_playkey_select_t ui_playkey_select;

struct ui_main_buttons_t : public ui_base_t
{
  protected:
    const char* _text_upper[def::hw::max_main_button] = { 0, };
    const char* _text_lower[def::hw::max_main_button] = { 0, };
    char _text[def::hw::max_main_button][8] = { { 0 }, };
    uint32_t _btns_color[def::hw::max_main_button] = { 0, };
    uint32_t _text_color[def::hw::max_main_button] = { 0, };
    uint8_t _text_width[def::hw::max_main_button] = { 0, };
    bool _text_wide[def::hw::max_main_button] = { false, };
    uint32_t _btn_bitmask = 0x00;
    def::gui_mode_t _gui_mode = def::gui_mode_t::gm_unknown;
    uint8_t _master_key = 0x80;
    uint8_t _minor_swap = 0x80;
    int8_t _semitone = 0x80;
    uint8_t _modifier = 0x80;
    int8_t _offset_key = 0x80;
    uint8_t _note_scale = 0x80;

    registry_t::history_code_t _mapping_history_code;
    uint32_t _working_command_change_count;
  public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);
    bool flg_update = false;
    def::gui_mode_t gui_mode = system_registry->runtime_info.getGuiMode();
    uint8_t master_key = system_registry->runtime_info.getMasterKey();
    uint8_t minor_swap = system_registry->chord_play.getChordMinorSwap();
    int8_t semitone = system_registry->chord_play.getChordSemitoneShift();
    uint8_t modifier = system_registry->chord_play.getChordModifier();
    int8_t offset_key = system_registry->current_slot->slot_info.getKeyOffset();

    uint8_t note_scale = system_registry->runtime_info.getNoteScale();
    auto mapping_history_code = system_registry->command_mapping_current.getHistoryCode();
    auto working_command_change_count = system_registry->working_command.getChangeCounter();
    if (_gui_mode != gui_mode
     || _master_key != master_key
     || _minor_swap != minor_swap
     || _semitone != semitone
     || _modifier != modifier
     || _offset_key != offset_key
     || _note_scale != note_scale
     || _mapping_history_code != mapping_history_code
     ) {
      _gui_mode = gui_mode;
      _master_key = master_key;
      _minor_swap = minor_swap;
      _semitone = semitone;
      _modifier = modifier;
      _offset_key = offset_key;
      _note_scale = note_scale;
      _mapping_history_code = mapping_history_code;
      flg_update = true;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }
    uint32_t prev_bitmask = _btn_bitmask;
    _btn_bitmask = 0x7FFF & system_registry->internal_input.getButtonBitmask();
    uint32_t xor_bitmask = prev_bitmask ^ _btn_bitmask;
    if (xor_bitmask) {
      flg_update = true;
    }
    _gfx->setTextSize(1, 2);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      uint32_t color = system_registry->rgbled_control.getColor(i);
      if (_btns_color[i] != color || xor_bitmask & (1 << i)) {
        _btns_color[i] = color;
        param->addInvalidatedRect(getButtonRect(i, offset_x, offset_y));
      }
    }
    if (flg_update || _working_command_change_count != working_command_change_count) {
      int slot_key = master_key + offset_key;
      while (slot_key < 0) { slot_key += 12; }
      while (slot_key >= 12) { slot_key -= 12; }

      _working_command_change_count = working_command_change_count;
      for (int i = 0; i < def::hw::max_main_button; ++i) {
        _text[i][0] = 0;
        _text_upper[i] = "";
        _text_lower[i] = "";

        auto cp_pair = system_registry->command_mapping_current.getCommandParamArray(i);
        def::command::command_param_t command_param;
        int pindex = 0;
        bool hit = true;
        for (int j = 0; cp_pair.array[j].command != def::command::none; ++j) {
          pindex = j;
          command_param = cp_pair.array[j];
          hit &= system_registry->working_command.check(command_param);
        }
        auto command = command_param.command;
        uint32_t text_color = system_registry->color_setting.getButtonPressedTextColor();
        if ((_btn_bitmask & (1 << i)) == 0) {
          if (hit) {
            text_color = system_registry->color_setting.getButtonWorkingTextColor();
          } else {
            text_color = system_registry->color_setting.getButtonDefaultTextColor();
          }
        }
        _text_color[i] = text_color;

        const char* name = def::command::getCommandName(command, command_param.param);

        for (auto data: def::command::button_text_table) {
          if (data.command == cp_pair) {
            name = data.text;
            _text_lower[i] = data.lower;
            _text_upper[i] = data.upper;
            break;
          }
        }
        // 単発コマンドの場合の表示
        if (pindex == 0) {
          switch (command) {
          default:
            break;

          case def::command::drum_button:
            { // ドラムモードボタンの場合の表示名処理
              uint8_t drum_index = command_param.param - 1;
              uint8_t note = system_registry->drum_mapping.get8(drum_index);
              name = def::midi::drum_name_tbl[note];
            }
            break;

          case def::command::note_button:
            { // ノートモードボタンの場合の表示名処理
              uint8_t note_button_index = command_param.param - 1;
              int32_t note = def::play::note::note_scale_note_table[note_scale][note_button_index];
              note += slot_key;
              if (note >= 0 && note < 128) {
                int name_index = note % def::app::max_play_key;
                auto note_name = def::app::note_name_table[name_index];
                if (note_name != nullptr) {
                  name = def::app::note_name_table[name_index];
                }
              }
            }
            break;

          case def::command::chord_modifier:
            {
  #if defined ( KANTAN_USE_MODIFIER_6_AS_7M7 )
              // 7とM7の同時押し時に6に表示を変換する
              auto table = def::command::command_name_table[command];
              if (command_param.param == KANTANMusic_Modifier_7) {
                if (system_registry->working_command.check( { def::command::chord_modifier, KANTANMusic_Modifier_M7 } )) {
                  name = table[KANTANMusic_Modifier_6];
                }
              }
              if (command_param.param == KANTANMusic_Modifier_M7) {
                if (system_registry->working_command.check( { def::command::chord_modifier, KANTANMusic_Modifier_7 } )) {
                  name = table[KANTANMusic_Modifier_6];
                }
              }
  #endif
            }
            break;

          case def::command::chord_degree:
            {
              auto degree_param = (degree_param_t)command_param.param;
              int degree = degree_param.getDegree();
              if (degree_param.raw == degree) {
                // 各キーに対して画面の表示をフラットに統一するかシャープに統一するかの分岐テーブル (0=flat / 1=sharp)
                static constexpr const uint8_t note_flat_sharp_tbl[12] =
                { 0, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, };
  
                // 各演奏ボタンに対してマイナー符号を付与するか分岐 (0=メジャー / 1=マイナー)
                static constexpr const uint8_t button_minor_tbl[7] =
                { 0, 1, 1, 0, 0, 1, 1, };

                KANTANMusic_GetMidiNoteNumberOptions options;
                KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
                options.minor_swap = _minor_swap | degree_param.getMinorSwap();
                int ss = _semitone + degree_param.getSemitoneShift();
                options.semitone_shift = ss < 0 ? -1 : (ss == 0 ? 0 : 1);
                uint8_t note = KANTANMusic_GetMidiNoteNumber
                  ( 1
                  , degree
                  , slot_key
                  , &options
                  );
                note %= 12;
                bool is_minor = false;
                // 以下の3つの修飾子の場合はメジャー・マイナーの概念がないのでマイナー表示をしない
                if (_modifier == KANTANMusic_Modifier_dim
                || _modifier == KANTANMusic_Modifier_sus4
                || _modifier == KANTANMusic_Modifier_aug) {
                  is_minor = false;
                }
                else
                if (_semitone == 0) {
                  is_minor = button_minor_tbl[degree - 1];
                  if (_minor_swap) { is_minor = !is_minor; }
                }
  
                auto notename = def::app::note_name_table[note];
                /// 12個の音階に対して半音部分を♭に置き換えるか判定処理
                if (notename[1] != 0x00) {
                  bool sharp = note_flat_sharp_tbl[_master_key];
                  notename = def::app::note_name_table[note + (sharp ? -1 : 1)];
                  _text_upper[i] = sharp ? "♯" : "♭";
                }
                _text_lower[i] = (is_minor) ? "m" : " ";
                snprintf(_text[i], sizeof(_text[i]), "%s", notename);
                _text_wide[i] = true;
                name = nullptr;
              }
            }
            break;
          }

        }
        if (name != nullptr) {
          snprintf(_text[i], sizeof(_text[i]), "%s", name);
          // 数字1文字のみのラベルも幅広表示にする
          _text_wide[i] = (name[0] >= '0' && name[0] <= '9' && name[1] == '\0');
        }

        _gfx->setTextSize(_text_wide[i] ? 2 : 1, 2);
        int tw = _gfx->textWidth(_text[i]);
        _gfx->setTextSize(1, 1);
        int lower_width = 0;
        int upper_width = 0;
        if (_text_lower[i] != nullptr) {
          lower_width = _gfx->textWidth(_text_lower[i]);
        }
        if (_text_upper[i] != nullptr) {
          upper_width = _gfx->textWidth(_text_upper[i]);
        }
        if (lower_width < upper_width) {
          lower_width = upper_width;
        }
        _text_width[i] = tw + lower_width;
      }
    }
  }
  rect_t getButtonRect(uint8_t index, int offset_x, int offset_y) {
    int x = index % 5;
    int xs = (x) * _client_rect.w / 5 + 1;
    int xe = (x + 1) * _client_rect.w / 5 - 1;
    int y = 2 - (index / 5);
    int ys = (y) * _client_rect.h / 3 + 1;
    int ye = (y + 1) * _client_rect.h / 3 - 1;
    return { xs + offset_x, ys + offset_y, xe - xs, ye - ys };
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    canvas->setTextDatum(m5gfx::datum_t::middle_left);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto rect = getButtonRect(i, offset_x, offset_y);
      int xs = rect.x;
      int ys = rect.y;
      if (!clip_rect->isIntersect(rect)) { continue; }

      draw_button(canvas, xs, ys, rect.w, rect.h, _btns_color[i]);
      int y = ys + (rect.h >> 1) - 1;

      canvas->setTextColor(_text_color[i]);

      canvas->setTextSize(_text_wide[i] ? 2 : 1, 2);
      int x = xs + ((rect.w - (int)_text_width[i]) >> 1);
      x += canvas->drawString(_text[i], x, y);

      canvas->setTextSize(1, 1);
      if (_text_upper[i] != nullptr) {
        canvas->drawString(_text_upper[i], x, y - 4);
      }
      if (_text_lower[i] != nullptr) {
        canvas->drawString(_text_lower[i], x, y + 6);
      }
    }
#if 0
    canvas->setTextDatum(m5gfx::datum_t::middle_center);
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto rect = getButtonRect(i, offset_x, offset_y);
      int xs = rect.x;
      int ys = rect.y;
      if (!clip_rect->isIntersect(rect)) { continue; }

      draw_button(canvas, xs, ys, rect.w, rect.h, _btns_color[i]);
      int y = ys + (rect.h >> 1) - 1;
// canvas->setTextColor(app_core.getBtnPressed(i) ? 0xFFFFFFu : 0xAAAAAAu);
      // bool isPressed = _btn_bitmask & (1 << i);
      // canvas->setTextColor(isPressed ? 0xFFFFFFu : 0xBBBBBBu);
      canvas->setTextColor(_text_color[i]);

      canvas->setTextSize(1, 1);
      if (_text_upper[i] != nullptr) {
        canvas->drawString(_text_upper[i], xs + 36, y - 4);
      }
      if (_text_lower[i] != nullptr) {
        canvas->drawString(_text_lower[i], xs + 36, y + 6);
      }
      canvas->setTextSize(1, 2);
      // canvas->drawString(_text[i], xs + 23, ys);
      canvas->drawString(_text[i], xs + 23, y);
    }
#endif
  }

  void draw_button(M5Canvas *canvas, int x, int y, int w, int h, uint32_t color)
  {  // ボタンのグラデーション表現用カラーテーブル
    static constexpr const uint32_t button_color_tbl[] = {
      0x383644, 0x3B3A3C, 0x212121, 0x080808,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      // 0x000000,
      0x000000, 0x000000, 0x000000,
      0x010101, 0x020202, 0x030303, 0x060606, 0x090909, 0x0D0D0D, 0x131313,
      0x1A1A1A, 0x212121, 0x272727, 0x292929, 0x141414, 0xEAEAEA, 0xCECECE,
    };
    static constexpr const uint32_t button_color_tbl_size = sizeof(button_color_tbl) / sizeof(button_color_tbl[0]);

    canvas->drawFastVLine(x+w-1, y, h, color);

    canvas->drawFastVLine(x, y, h, add_color(color, button_color_tbl[0]));
    if (h > button_color_tbl_size) { h = button_color_tbl_size; }
    for (int i = 0; i < h; ++i) {
      uint32_t c = button_color_tbl[i];
      c = (c == 0) ? color : add_color(color, c);
      canvas->writeFastHLine(x+1, y+i, w-2, c);
    }
  };
  void touchControl(draw_param_t *param, const m5::touch_detail_t& td) override
  {
/* 画面タッチでもボタン操作できるようにしようと思ったけど物理ボタンとの OR を取る必要があるのでいったん保留
    if (td->wasPressed() || td->wasReleased())
    {
      int x =   ((td->x - _client_rect.x) * 5 / _client_rect.w);
      int y = 2-((td->y - _client_rect.y) * 3 / _client_rect.h);
      app_core.setBtnRaw(x+y*5, param->frame_msec, td->isPressed());
    }
//*/
  }
};
static ui_main_buttons_t ui_main_buttons;

struct ui_sub_buttons_t : public ui_base_t
{
    static constexpr const size_t max_edit_button = def::hw::max_sub_button * 2;
    static constexpr const size_t max_slot_button = def::hw::max_slot_button;
    static constexpr const size_t columns = 4;
    static constexpr const size_t edit_rows = max_edit_button / columns;
  protected:
    // 演奏時（スロットボタン表示）用
    const char* _text_upper[max_slot_button] = { 0, };
    const char* _text_lower[max_slot_button] = { 0, };
    char _text[max_slot_button][8] = { { 0 }, };
    uint8_t _text_width[max_slot_button] = { 0, };
    bool _text_wide[max_slot_button] = { false, };
    uint32_t _text_color[max_slot_button] = { 0, };
    uint32_t _btns_color[max_slot_button] = { 0, };
    uint32_t _slot_btn_bitmask = 0;
    uint8_t _play_slot = 0xFF;
    uint8_t _master_key = 0x80;
    uint8_t _minor_swap = 0x80;
    int8_t _semitone = 0x80;
    uint8_t _modifier = 0x80;
    int8_t _offset_key = 0x80;
    uint8_t _note_scale = 0x80;
    registry_t::history_code_t _mapping_history_code;
    // アルペジオ編集時（従来のサブボタン表示）用
    char _edit_text[max_edit_button][8] = { { 0 }, };
    uint32_t _edit_btns_color[max_edit_button] = { 0, };
    uint32_t _edit_text_color[max_edit_button] = { 0, };
    uint32_t _edit_btn_bitmask = 0x00;
    registry_t::history_code_t _sub_button_history_code;
    // モード管理
    bool _is_part_edit = false;
    bool _is_melody_edit = false;
  public:
  void update_impl(draw_param_t *param, int offset_x, int offset_y) override {
    ui_base_t::update_impl(param, offset_x, offset_y);

    bool is_part_edit = (system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_part_edit);
    bool is_melody_edit = (system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_melody_edit);
    if (_is_part_edit != is_part_edit || _is_melody_edit != is_melody_edit) {
      _is_part_edit = is_part_edit;
      _is_melody_edit = is_melody_edit;
      param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
    }

    if (is_part_edit) {
      // ---- アルペジオ編集時：従来のサブボタン表示（swap対応、2段） ----
      bool flg_update = false;
      auto history_code = system_registry->sub_button.getHistoryCode();
      if (_sub_button_history_code != history_code) {
        _sub_button_history_code = history_code;
        flg_update = true;
      }
      uint32_t prev_bitmask = _edit_btn_bitmask;
      _edit_btn_bitmask = system_registry->internal_input.getButtonBitmask() >> def::hw::max_main_button;
      uint32_t xor_bitmask = prev_bitmask ^ _edit_btn_bitmask;
      if (xor_bitmask) { flg_update = true; }
      for (int i = 0; i < (int)max_edit_button; ++i) {
        uint32_t color = system_registry->sub_button.getSubButtonColor(i);
        if (_edit_btns_color[i] != color || xor_bitmask & (1 << i)) {
          _edit_btns_color[i] = color;
          param->addInvalidatedRect(getEditButtonRect(i, offset_x, offset_y));
        }
      }
      if (flg_update) {
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
        for (int i = 0; i < (int)max_edit_button; ++i) {
          _edit_text_color[i] = system_registry->color_setting.getButtonPressedTextColor();
          _edit_text[i][0] = 0;
          auto pair = system_registry->sub_button.getCommandParamArray(i);
          auto command_param = pair.array[0];
          bool sub_button_swap = system_registry->runtime_info.getSubButtonSwap();
          bool is_pressed = _edit_btn_bitmask & (1 << (i % def::hw::max_sub_button));
          if (!is_pressed) {
            uint32_t color = system_registry->color_setting.getButtonDefaultTextColor();
            if (sub_button_swap == (bool)(i < def::hw::max_sub_button)) {
              color = (color >> 1) & 0x7F7F7F;
            }
            if (system_registry->working_command.check(command_param)) {
              color = system_registry->color_setting.getButtonWorkingTextColor();
            }
            _edit_text_color[i] = color;
          }
          const auto command = command_param.getCommand();
          const auto p = command_param.getParam();
          const char* name = def::command::getCommandName(command, p);
          if (name != nullptr) { snprintf(_edit_text[i], sizeof(_edit_text[i]), "%s", name); }
        }
      }
    } else if (is_melody_edit) {
      // ---- メロディ編集時：Tone/Volumeのモーメンタリ修飾ボタン ----
      uint32_t button_bitmask = (system_registry->internal_input.getButtonBitmask()
                               >> def::hw::max_main_button)
                              & ((1u << def::hw::max_sub_button) - 1);
      uint32_t xor_mask = _slot_btn_bitmask ^ button_bitmask;
      _slot_btn_bitmask = button_bitmask;
      bool flg_update = xor_mask != 0;
      auto history_code = system_registry->sub_button.getHistoryCode();
      if (_sub_button_history_code != history_code) {
        _sub_button_history_code = history_code;
        flg_update = true;
      }
      int map_offset = system_registry->runtime_info.getSubButtonSwap()
                     ? def::hw::max_sub_button : 0;
      for (int i = 0; i < def::hw::max_sub_button; ++i) {
        uint32_t color = system_registry->sub_button.getSubButtonColor(i + map_offset);
        if (_btns_color[i] != color) {
          _btns_color[i] = color;
          flg_update = true;
        }
      }
      if (flg_update) {
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
        for (int i = 0; i < def::hw::max_sub_button; ++i) {
          _text[i][0] = 0;
          _text_upper[i] = "";
          _text_lower[i] = "";
          auto pair = system_registry->sub_button.getCommandParamArray(i + map_offset);
          auto command_param = pair.array[0];
          const char* name = def::command::getCommandName(
            command_param.getCommand(), command_param.getParam());
          if (name != nullptr) { snprintf(_text[i], sizeof(_text[i]), "%s", name); }
          bool working = system_registry->working_command.check(command_param);
          _text_color[i] = working || (button_bitmask & (1u << i))
                         ? system_registry->color_setting.getButtonPressedTextColor()
                         : system_registry->color_setting.getButtonDefaultTextColor();
          _gfx->setTextSize(1, 2);
          _text_width[i] = _gfx->textWidth(_text[i]);
          _text_wide[i] = false;
        }
      }
    } else {
      // ---- 演奏時：スロットボタン表示（1段、swap なし） ----
      // スロットボタン押下状態（getButtonBitmask の max_main_button ビット以降）
      uint32_t slot_bitmask = (system_registry->internal_input.getButtonBitmask() >> def::hw::max_main_button)
                            & ((1u << max_slot_button) - 1);
      uint32_t xor_slot = _slot_btn_bitmask ^ slot_bitmask;
      if (xor_slot) {
        _slot_btn_bitmask = slot_bitmask;
        for (int i = 0; i < (int)max_slot_button; ++i) {
          if (xor_slot & (1u << i)) {
            param->addInvalidatedRect(getButtonRect(i, offset_x, offset_y));
          }
        }
      }
      bool flg_update = false;
      uint8_t master_key = system_registry->runtime_info.getMasterKey();
      uint8_t minor_swap = system_registry->chord_play.getChordMinorSwap();
      int8_t semitone = system_registry->chord_play.getChordSemitoneShift();
      uint8_t modifier = system_registry->chord_play.getChordModifier();
      int8_t offset_key = system_registry->current_slot->slot_info.getKeyOffset();
      uint8_t note_scale = system_registry->runtime_info.getNoteScale();
      uint8_t play_slot = system_registry->runtime_info.getPlaySlot();
      auto mapping_history_code = system_registry->command_mapping_slot.getHistoryCode();
      if (_play_slot != play_slot || _master_key != master_key || _minor_swap != minor_swap || _semitone != semitone
       || _modifier != modifier || _offset_key != offset_key || _note_scale != note_scale
       || _mapping_history_code != mapping_history_code) {
        _play_slot = play_slot;
        _master_key = master_key; _minor_swap = minor_swap; _semitone = semitone;
        _modifier = modifier; _offset_key = offset_key; _note_scale = note_scale;
        _mapping_history_code = mapping_history_code;
        flg_update = true;
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      }
      if (flg_update) {
        int slot_key = master_key + offset_key;
        while (slot_key < 0) { slot_key += 12; }
        while (slot_key >= 12) { slot_key -= 12; }
        _gfx->setTextSize(1, 2);
        for (int i = 0; i < (int)max_slot_button; ++i) {
          _text[i][0] = 0;
          _text_upper[i] = "";
          _text_lower[i] = "";
          _text_color[i] = system_registry->color_setting.getButtonDefaultTextColor();
          _btns_color[i] = 0x333333u;
          auto cp_pair = system_registry->command_mapping_slot.getCommandParamArray(i);
          def::command::command_param_t command_param;
          int pindex = 0;
          bool is_current_slot_button = false;
          for (int j = 0; cp_pair.array[j].command != def::command::none; ++j) {
            pindex = j;
            command_param = cp_pair.array[j];
            if (command_param.getCommand() == def::command::slot_select
             && command_param.getParam() > 0
             && (uint8_t)(command_param.getParam() - 1) == play_slot) {
              is_current_slot_button = true;
            }
          }
          if (is_current_slot_button) {
            _btns_color[i] = system_registry->color_setting.getButtonDegreeColor();
            _text_color[i] = system_registry->color_setting.getButtonWorkingTextColor();
          }
          auto command = command_param.command;
          const char* name = def::command::getCommandName(command, command_param.param);
          for (auto data: def::command::button_text_table) {
            if (data.command == cp_pair) {
              name = data.text;
              _text_lower[i] = data.lower;
              _text_upper[i] = data.upper;
              break;
            }
          }
          if (pindex == 0) {
            switch (command) {
            default: break;
            case def::command::drum_button: {
              uint8_t drum_index = command_param.param - 1;
              uint8_t note = system_registry->drum_mapping.get8(drum_index);
              name = def::midi::drum_name_tbl[note];
            } break;
            case def::command::note_button: {
              uint8_t note_button_index = command_param.param - 1;
              int32_t note = def::play::note::note_scale_note_table[note_scale][note_button_index];
              note += slot_key;
              if (note >= 0 && note < 128) {
                int name_index = note % def::app::max_play_key;
                auto note_name = def::app::note_name_table[name_index];
                if (note_name != nullptr) { name = note_name; }
              }
            } break;
            case def::command::chord_degree: {
              auto degree_param = (degree_param_t)command_param.param;
              int degree = degree_param.getDegree();
              if (degree_param.raw == degree) {
                static constexpr const uint8_t note_flat_sharp_tbl[12] = { 0,0,1,0,1,0,1,1,0,1,0,1 };
                static constexpr const uint8_t button_minor_tbl[7] = { 0,1,1,0,0,1,1 };
                KANTANMusic_GetMidiNoteNumberOptions options;
                KANTANMusic_GetMidiNoteNumber_SetDefaultOptions(&options);
                options.minor_swap = minor_swap | degree_param.getMinorSwap();
                int ss = semitone + degree_param.getSemitoneShift();
                options.semitone_shift = ss < 0 ? -1 : (ss == 0 ? 0 : 1);
                uint8_t note = KANTANMusic_GetMidiNoteNumber(1, degree, slot_key, &options);
                note %= 12;
                bool is_minor = false;
                if (modifier != KANTANMusic_Modifier_dim && modifier != KANTANMusic_Modifier_sus4
                 && modifier != KANTANMusic_Modifier_aug && semitone == 0) {
                  is_minor = button_minor_tbl[degree - 1];
                  if (minor_swap) { is_minor = !is_minor; }
                }
                auto notename = def::app::note_name_table[note];
                if (notename[1] != 0x00) {
                  bool sharp = note_flat_sharp_tbl[master_key];
                  notename = def::app::note_name_table[note + (sharp ? -1 : 1)];
                  _text_upper[i] = sharp ? "♯" : "♭";
                }
                _text_lower[i] = is_minor ? "m" : " ";
                snprintf(_text[i], sizeof(_text[i]), "%s", notename);
                _text_wide[i] = true;
                name = nullptr;
              }
            } break;
            }
          }
          if (name != nullptr) {
            snprintf(_text[i], sizeof(_text[i]), "%s", name);
            // 数字1文字のみのラベルも幅広表示にする
            _text_wide[i] = (name[0] >= '0' && name[0] <= '9' && name[1] == '\0');
          }

          // upper/lower を含めた合計幅を計算してセンタリングに使う
          _gfx->setTextSize(_text_wide[i] ? 2 : 1, 2);
          int tw = _gfx->textWidth(_text[i]);
          _gfx->setTextSize(1, 1);
          int upper_w = (_text_upper[i] && _text_upper[i][0]) ? _gfx->textWidth(_text_upper[i]) : 0;
          int lower_w = (_text_lower[i] && _text_lower[i][0]) ? _gfx->textWidth(_text_lower[i]) : 0;
          _text_width[i] = (uint8_t)(tw + (upper_w > lower_w ? upper_w : lower_w));
        }
      }
    }
  }

  rect_t getEditButtonRect(uint8_t index, int offset_x, int offset_y) {
    int x = index % columns;
    int xs = (x) * _client_rect.w / columns + 1;
    int xe = (x + 1) * _client_rect.w / columns - 1;
    int y = index / columns;
    int ys = (y) * _client_rect.h / edit_rows + 1;
    int ye = (y + 1) * _client_rect.h / edit_rows - 1;
    return { xs + offset_x, ys + offset_y, xe - xs, ye - ys };
  }

  rect_t getButtonRect(uint8_t index, int offset_x, int offset_y) {
    static constexpr int vpad = 4;
    static constexpr int hpad = 3;
    int x = index % columns;
    int xs = (x) * _client_rect.w / columns + hpad;
    int xe = (x + 1) * _client_rect.w / columns - hpad;
    return { xs + offset_x, offset_y + vpad, xe - xs, _client_rect.h - vpad * 2 };
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (_is_part_edit) {
      // アルペジオ編集時：従来の2段サブボタン表示
      canvas->setTextDatum(m5gfx::textdatum_t::middle_center);
      canvas->setTextSize(1, 1);
      for (int i = 0; i < (int)max_edit_button; ++i) {
        auto rect = getEditButtonRect(i, offset_x, offset_y);
        if (!clip_rect->isIntersect(rect)) { continue; }
        draw_button(canvas, rect.x, rect.y, rect.w, rect.h, _edit_btns_color[i]);
        canvas->setTextColor(_edit_text_color[i]);
        canvas->drawString(_edit_text[i], rect.x + (rect.w >> 1), rect.y + (rect.h >> 1));
      }
    } else {
      // 演奏時：1段スロットボタン表示
      // ボタンエリア全体を黒で塗りつぶし（上下vpadの隙間部分）
      canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, TFT_BLACK);
      canvas->setTextDatum(m5gfx::datum_t::middle_left);
      int button_count = _is_melody_edit ? def::hw::max_sub_button : (int)max_slot_button;
      for (int i = 0; i < button_count; ++i) {
        auto rect = getButtonRect(i, offset_x, offset_y);
        if (!clip_rect->isIntersect(rect)) { continue; }
        bool is_pressed = (_slot_btn_bitmask >> i) & 1;
        uint32_t btn_color = is_pressed ? 0x888888u : _btns_color[i];
        draw_button(canvas, rect.x, rect.y, rect.w, rect.h, btn_color);
        int y = rect.y + (rect.h >> 1) - 1;
        canvas->setTextColor(is_pressed ? system_registry->color_setting.getButtonPressedTextColor() : _text_color[i]);
        canvas->setTextSize(_text_wide[i] ? 2 : 1, 2);
        int x = rect.x + ((rect.w - (int)_text_width[i]) >> 1);
        x += canvas->drawString(_text[i], x, y);
        canvas->setTextSize(1, 1);
        if (_text_upper[i] && _text_upper[i][0]) { canvas->drawString(_text_upper[i], x, y - 4); }
        if (_text_lower[i] && _text_lower[i][0]) { canvas->drawString(_text_lower[i], x, y + 6); }
      }
    }
  }

  void draw_button(M5Canvas *canvas, int x, int y, int w, int h, uint32_t color)
  {
    static constexpr const uint32_t button_color_tbl[] = {
      0x383644, 0x3B3A3C, 0x212121, 0x080808,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
      0x000000, 0x000000, 0x000000,
      0x010101, 0x020202, 0x030303, 0x060606, 0x090909, 0x0D0D0D, 0x131313,
      0x1A1A1A, 0x212121, 0x272727, 0x292929, 0x141414, 0xEAEAEA, 0xCECECE,
    };
    static constexpr const uint32_t button_color_tbl_size = sizeof(button_color_tbl) / sizeof(button_color_tbl[0]);
    canvas->drawFastVLine(x+w-1, y, h, color);
    canvas->drawFastVLine(x, y, h, add_color(color, button_color_tbl[0]));
    if (h > (int)button_color_tbl_size) { h = button_color_tbl_size; }
    for (int i = 0; i < h; ++i) {
      uint32_t c = button_color_tbl[i];
      c = (c == 0) ? color : add_color(color, c);
      canvas->writeFastHLine(x+1, y+i, w-2, c);
    }
  }

  void touchControl(draw_param_t *param, const m5::touch_detail_t& td) override {}
};
static ui_sub_buttons_t ui_sub_buttons;
