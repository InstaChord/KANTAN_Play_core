// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

struct ui_menu_header_t : public ui_base_t
{
protected:
  static constexpr const size_t max_ancestors_size = 6;
  struct ancestors_t {
    const char* title;
    int16_t current_x = main_area_width;
    int16_t target_x = main_area_width;
  } _ancestors[max_ancestors_size];

  def::menu_category_t _category;
  registry_t::history_code_t _history_code;
  bool _is_visible = true;
  bool _is_closing = false;
  int8_t _level = -1;

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override
  {
    auto show = system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_menu;
    auto category = static_cast<def::menu_category_t>(system_registry->menu_status.getMenuCategory());

    if (_is_visible && !_is_closing && (!show || _category != category)) {
      _is_closing = true;
      setTargetRect({0, header_height, main_area_width, 0 });
      _level = -1;
      for (int lv = 0; lv < max_ancestors_size; ++lv) {
        _ancestors[lv].target_x = main_area_width;
      }
    } else
    // 非表示状態からメニュー表示状態への遷移
    if (show && (!_is_visible || _is_closing) && category != def::menu_category_t::menu_none) {
      _is_visible = true;
      _is_closing = false;
      _category = category;
      setTargetRect({0, header_height, main_area_width, menu_header_height });
    }
    // メニュー表示状態から非表示状態への遷移
    bool invalidated = false;
    if (show) {
      auto code = system_registry->menu_status.getHistoryCode();
      if (_history_code != code) {
        _history_code = code;
        size_t level = system_registry->menu_status.getCurrentLevel();
        if (_level != level) {
          _level = level;
          invalidated = true;

          size_t lv = 0;
          int target_x = 0;
          _gfx->setTextSize(1, 2);
          for (; lv <= level; lv++) {
            auto item = menu_control.getItemByLevel(lv);
            _ancestors[lv].target_x = target_x;
            auto title = item->getTitleText();
            _ancestors[lv].title = title;
            target_x += 8 + _gfx->textWidth(title);
          }
          target_x -= 8;
          if (target_x > main_area_width) {
            int diff = main_area_width - target_x;
            for (int i = 0; i < lv; ++i) {
              _ancestors[i].target_x += diff;
            }
          }
          for (; lv < max_ancestors_size; ++lv) {
            _ancestors[lv].target_x = main_area_width;
          }
        }
      }
    }
    if (_is_visible) {
      for (int i = 0; i < max_ancestors_size; ++i) {
        if (_ancestors[i].current_x != _ancestors[i].target_x) {
          _ancestors[i].current_x = smooth_move(_ancestors[i].target_x, _ancestors[i].current_x, param->smooth_step);
          invalidated = true;
        } else {
          if (_ancestors[i].target_x >= main_area_width) {
            _ancestors[i].title = nullptr;
          }
        }
      }
      if (invalidated) {
        param->addInvalidatedRect({offset_x, offset_y, _client_rect.w, _client_rect.h});
      }
      if (_is_closing && getClientRect() == getTargetRect()) {
        _is_visible = false;
      }
    }
    ui_base_t::update_impl(param, offset_x, offset_y);
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (!_is_visible) { return; }
    canvas->fillRect(offset_x, offset_y, _client_rect.w, _client_rect.h, 0x1F1F1Fu);

    int y = offset_y + menu_header_height - 1;
    int x = 0;

    auto current_level = system_registry->menu_status.getCurrentLevel();
    canvas->setTextSize(1, 2);
    canvas->setTextDatum(m5gfx::textdatum_t::bottom_left);
    int lv = 0;
    for (auto& ancestor : _ancestors)
    {
      if (ancestor.title == nullptr) { break; }
      if (lv++ == current_level) {
        canvas->setTextColor(TFT_WHITE);
      } else {
        canvas->setTextColor(TFT_DARKGRAY);
      }
      x = offset_x + ancestor.current_x;
      auto str = ancestor.title;
      if (str[0]) {
        canvas->drawString(str, x, y);
      }
    }
    canvas->drawFastHLine(offset_x, y, _client_rect.w, TFT_YELLOW);
  }
};
static ui_menu_header_t ui_menu_header;

// メニューUI用の描画クラス
struct menu_drawer_t
{
  static constexpr const int scroll_gap = 32;
  menu_drawer_t(uint16_t menu_index, menu_item_ptr menu_item)
  : _menu_item { menu_item }
  , _menu_index { menu_index }
  {}

  virtual void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y)
  {
    if (_current_focus_rect != _target_focus_rect) {
      int move_diff = 0;
      if (_current_focus_rect.empty()) {
        _current_focus_rect = _target_focus_rect;
      } else {
        int prev_y = _current_focus_rect.y;
        _current_focus_rect.smooth_move(_target_focus_rect, param->smooth_step);
        move_diff = _current_focus_rect.y - prev_y;
      }
      int y = _current_focus_rect.y - _y_scroll;
      int invalidated_y0 = y + (move_diff < 0 ? 0 : -move_diff);
      int invalidated_y1 = y + (move_diff < 0 ? -move_diff : 0) + _current_focus_rect.h;
      if (invalidated_y0 < 0) { invalidated_y0 = 0; }
      if (invalidated_y1 > ui->getClientRect().h) { invalidated_y1 = ui->getClientRect().h; }
      if (invalidated_y1 > invalidated_y0) {
        param->addInvalidatedRect({_current_focus_rect.x + offset_x, invalidated_y0 + offset_y, _current_focus_rect.w, invalidated_y1 - invalidated_y0});
      }

      int new_y_scroll = _y_scroll;
      if (new_y_scroll < _current_focus_rect.bottom() - ui->getClientRect().h + scroll_gap) {
        new_y_scroll = _current_focus_rect.bottom() - ui->getClientRect().h + scroll_gap;
        if (new_y_scroll > _scroll_limit) { new_y_scroll = _scroll_limit; }
      }
      if (new_y_scroll > _current_focus_rect.y - scroll_gap ) {
        new_y_scroll = _current_focus_rect.y - scroll_gap;
      }
      if (new_y_scroll < 0) { new_y_scroll = 0; }
      if (_y_scroll != new_y_scroll) {
        _y_scroll = new_y_scroll;
        param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
      }
    }
  }
  virtual void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect)
  {
    canvas->fillRect(offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h, 0x1F1F1Fu);

    canvas->fillRoundRect
      ( offset_x + _current_focus_rect.x
      , offset_y + _current_focus_rect.y - _y_scroll
      , _current_focus_rect.w
      , _current_focus_rect.h
      , 4
      , 0x303080u);

    auto cr = ui->getClientRect();

    canvas->drawFastVLine
      ( offset_x
      , offset_y
      , cr.h
      , 0x808080u);

    canvas->drawFastVLine
      ( offset_x + cr.w - 1
      , offset_y
      , cr.h
      , 0x808080u);
  }
protected:
  menu_item_ptr _menu_item = nullptr;
  rect_t _current_focus_rect;
  rect_t _target_focus_rect;
  uint16_t _menu_index = 0;
  int16_t _y_scroll = 0;
  int16_t _scroll_limit = 0;
};

struct menu_drawer_list_t : public menu_drawer_t
{
protected:
  static constexpr const int default_item_height = 32;
  int16_t _min_value; // 選択肢の最小値
  int16_t _focus_pos = -1;
  int16_t _stored_pos = -1;
  bool _draw_index = true;
public:
  menu_drawer_list_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {
    _min_value = menu_item->getMinValue();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto item_height = getItemHeight();
    int item_index = getFocusPos();
    _target_focus_rect = { 0, item_index * item_height, ui->getTargetRect().w, item_height };

    _scroll_limit = getItemCount() * item_height - ui->getClientRect().h;
  }

  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);

    auto tr = ui->getClientRect();

    int y = offset_y - _y_scroll;
    int number_width = 18;
    int x = offset_x + 3;
    int w = tr.w - 6;
    if (_draw_index) {
      x += number_width;
      w -= number_width;
    }

    int count = getItemCount();
    int item_height = getItemHeight();
    canvas->setTextSize(1, 2);
    const char* str;
    for (int i = 0; i < count; ++i, y += item_height) {
      if (y + item_height < clip_rect->y) { continue; }
      if (y > clip_rect->y + clip_rect->h) { break; }

      canvas->setTextColor(_stored_pos == i ? TFT_YELLOW : (_focus_pos == i ? TFT_WHITE : TFT_LIGHTGRAY));

      canvas->setTextDatum(m5gfx::textdatum_t::top_left);
      if (_draw_index) {
        canvas->drawNumber(i + _min_value, x - number_width, y);
      }

      if (nullptr != (str = getLeftText(i))) {
        canvas->drawString(str, x, y);
      }

      if (nullptr != (str = getCenterText(i))) {
        canvas->setTextDatum(m5gfx::textdatum_t::top_center);
        canvas->drawString(str, x + (w >> 1), y);
      }

      if (nullptr != (str = getRightText(i))) {
        canvas->setTextDatum(m5gfx::textdatum_t::top_right);
        canvas->drawString(str, x + w, y);
      }
    }
  }
  virtual int getFocusPos(void) { return 0; }
  virtual int getItemCount(void) { return 0; }
  virtual int getItemHeight(void) { return default_item_height; }
  virtual const char* getLeftText(int index) { return nullptr; }
  virtual const char* getCenterText(int index) { return nullptr; }
  virtual const char* getRightText(int index) { return nullptr; }
};

struct menu_drawer_tree_t : public menu_drawer_list_t
{
protected:
  // static constexpr const int max_children_size = 10;
  // menu_item_ptr _item_array[max_children_size + 1];
  registry_t::history_code_t _history_code;
  std::vector<menu_item_ptr> _item_array;
  uint8_t _childrens_count = 0;
  uint8_t _level;
  uint32_t _dynamic_refresh_msec = 0;

public:
  menu_drawer_tree_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_list_t(menu_index, menu_item)
  {
    _level = menu_item->getLevel();
    std::vector<uint16_t> menu_id_list;
    _childrens_count = menu_control.getChildrenMenuIDList(&menu_id_list, menu_index);
    for (int i = 0; i < _childrens_count; ++i) {
      _item_array.push_back(menu_control.getItemByMenuID(menu_id_list[i]));
    }
    _item_array.shrink_to_fit();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    if (uint32_t(M5.millis() - _dynamic_refresh_msec) >= 250) {
      _dynamic_refresh_msec = M5.millis();
      for (auto item : _item_array) {
        if (item->isDynamic()) {
          param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
          break;
        }
      }
    }
    auto code = system_registry->menu_status.getHistoryCode();
    if (_history_code != code) {
      _history_code = code;

      auto focus_index = system_registry->menu_status.getSelectIndex(_level);
      auto focus_item = menu_control.getItemByMenuID(focus_index);

      for (int i = 0; i < _childrens_count; ++i) {
        auto item = _item_array[i];
        if (focus_item == item) {
          _focus_pos = i;
        }
      }
    }
    menu_drawer_list_t::update(ui, param, offset_x, offset_y);
  }
  int getItemCount(void) override { return _childrens_count; }
  int getFocusPos(void) override { return _focus_pos; }
  const char* getLeftText(int index) override { return _item_array[index]->getTitleText(); }
  const char* getRightText(int index) override { return _item_array[index]->getValueText(); }
};

struct menu_drawer_normal_t : public menu_drawer_list_t
{
protected:
  size_t _count;
  uint32_t _dynamic_refresh_msec = 0;
public:
  menu_drawer_normal_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_list_t(menu_index, menu_item)
  {
    _count = menu_item->getSelectorCount();
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    if (_menu_item->isDynamic() && uint32_t(M5.millis() - _dynamic_refresh_msec) >= 100) {
      _dynamic_refresh_msec = M5.millis();
      _count = _menu_item->getSelectorCount();
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
    int min_value = _menu_item->getMinValue();
    _focus_pos = _menu_item->getSelectingValue() - min_value;
    int stored_pos = _menu_item->getValue() - min_value;
    if (_stored_pos != stored_pos) {
      _stored_pos = stored_pos;
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
    menu_drawer_list_t::update(ui, param, offset_x, offset_y);
  }

  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_list_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
  }
  int getItemCount(void) override { return _count; }
  int getFocusPos(void) override { return _focus_pos; }
  const char* getCenterText(int index) override { return _menu_item->getSelectorText(index); }
};

struct menu_drawer_input_number_t : public menu_drawer_normal_t
{
  menu_drawer_input_number_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_normal_t(menu_index, menu_item)
  {}
};
/*
struct menu_drawer_qrcode_t : public menu_drawer_t
{
  M5Canvas _qr_canvas;
  uint8_t _value = 0xFF;
  std::string _qr_text;
  int8_t _current_scale = 0;
  int8_t _target_scale = 0;
public:
  menu_drawer_qrcode_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {
    _qr_canvas.setColorDepth(1);
    _qr_canvas.setPsram(true);
  }

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto value = _menu_item->getSelectingValue();
    if (_value != value) {
      _target_scale = 0;
      if (_current_scale == 0) {
        _value = value;
        _target_scale = 64;
        _qr_canvas.deleteSprite();
        auto qr_text = _menu_item->getString();
        _qr_canvas.createSprite(33, 33);
        _qr_canvas.fillScreen(TFT_WHITE);
        _qr_canvas.qrcode(qr_text.c_str());
      }
    }
    if (_current_scale != _target_scale) {
      _current_scale = smooth_move(_target_scale, _current_scale, param->smooth_step);
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
  }
  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
    auto cr = ui->getClientRect();
    float scale = _current_scale / 16.0f;
    _qr_canvas.pushRotateZoom(canvas, offset_x + (cr.w >> 1), offset_y + (cr.h >> 1), 0.0f, scale, scale);
  }
};
*/
struct menu_drawer_progress_t : public menu_drawer_t
{
  uint16_t _value = UINT16_MAX;
  std::string _text;
public:
  menu_drawer_progress_t(uint16_t menu_index, menu_item_ptr menu_item)
  : menu_drawer_t(menu_index, menu_item)
  {}

  void update(ui_base_t* ui, draw_param_t *param, int offset_x, int offset_y) override
  {
    menu_drawer_t::update(ui, param, offset_x, offset_y);
    auto value = _menu_item->getSelectingValue();
    if (_value != value) {
      _value = value;

      _text = _menu_item->getString();
      param->addInvalidatedRect({offset_x, offset_y, ui->getClientRect().w, ui->getClientRect().h});
    }
  }
  void draw(ui_base_t* ui, draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    menu_drawer_t::draw(ui, param, canvas, offset_x, offset_y, clip_rect);
    auto cr = ui->getClientRect();
    canvas->setTextColor(TFT_WHITE);
    canvas->setTextSize(1, 2);
    canvas->setTextDatum(m5gfx::textdatum_t::bottom_center);
    canvas->drawString(_text.c_str(), offset_x + (cr.w >> 1), offset_y + (cr.h >> 1));
    if (_value <= 100) {
      canvas->drawRect(offset_x + ((cr.w - 100) >> 1) - 2, offset_y + (cr.h >> 1)    , 100 + 4, (cr.h >> 3) + 4, TFT_WHITE);
      canvas->fillRect(offset_x + ((cr.w - 100) >> 1)    , offset_y + (cr.h >> 1) + 2,  _value, (cr.h >> 3)    , TFT_GREEN);
    }
  }
};

struct ui_menu_body_t : public ui_base_t
{
  ui_menu_body_t(bool odd_even) : _odd_even { odd_even } {
    setTargetRect({ main_area_width, header_height + menu_header_height, main_area_width, main_area_height - menu_header_height });
  }

protected:
  std::unique_ptr<menu_drawer_t> _drawer = nullptr;

  def::menu_category_t _category;
  bool _is_visible = true;
  bool _is_closing = false;
  const bool _odd_even;
  int8_t _level = -1;
  int8_t _prev_level = -1;

  void update_impl(draw_param_t *param, int offset_x, int offset_y) override
  {
    ui_base_t::update_impl(param, offset_x, offset_y);

    auto show = system_registry->runtime_info.getGuiMode() == def::gui_mode_t::gm_menu;
    auto category = static_cast<def::menu_category_t>(system_registry->menu_status.getMenuCategory());
    int current_level = system_registry->menu_status.getCurrentLevel();

    if ((current_level & 1) == _odd_even) {
      _level = current_level;
    }

    // メニュー表示状態から非表示状態への遷移
    if (_is_visible && !_is_closing && (!show || _category != category || current_level != _level)) {
      _is_closing = true;
      int x = (_level < current_level) ? -main_area_width : main_area_width;
      _target_rect.x = x;
    } else
    if (show && (!_is_visible || _is_closing) && category != def::menu_category_t::menu_none && current_level == _level)
    { // 非表示状態からメニュー表示状態への遷移
      _is_visible = true;
      _is_closing = false;
      _category = category;
      setTargetRect({0, header_height + menu_header_height, main_area_width, main_area_height - menu_header_height });

      _client_rect.x = (_prev_level <= current_level)
                      ?  main_area_width
                      : -main_area_width;

      auto current_index = _level ? system_registry->menu_status.getSelectIndex(_level - 1) : 0;
      auto menu_item = menu_control.getItemByMenuID(current_index);
      if (menu_item) {
        switch (menu_item->getType()) {
        default:
        case menu_item_type_t::mt_tree:
          _drawer.reset(new menu_drawer_tree_t(current_index, menu_item));
          break;

        case menu_item_type_t::mt_normal:
          _drawer.reset(new menu_drawer_normal_t(current_index, menu_item));
          break;

        case menu_item_type_t::input_number:
          _drawer.reset(new menu_drawer_input_number_t(current_index, menu_item));
          break;

        case menu_item_type_t::show_progress:
          _drawer.reset(new menu_drawer_progress_t(current_index, menu_item));
          break;

        // case menu_item_type_t::show_qrcode:
        //   _drawer.reset(new menu_drawer_qrcode_t(current_index, menu_item));
        //   break;
        }
      }
    }
    _prev_level = show ? current_level : -1;
    if (_is_visible && _is_closing && getClientRect() == getTargetRect()) {
      _is_visible = false;
    }

    if (show && _drawer) {
      _drawer->update(this, param, offset_x, offset_y);
    }
  }

  void draw_impl(draw_param_t *param, M5Canvas *canvas, int32_t offset_x,
                          int32_t offset_y, const rect_t *clip_rect) override
  {
    if (_is_visible && _drawer) {
      _drawer->draw(this, param, canvas, offset_x, offset_y, clip_rect);
    }
  }
};
static ui_menu_body_t ui_menu_bodys[2] = {
  ui_menu_body_t(0),
  ui_menu_body_t(1),
};
