// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

// ガイド音が有効な場合にドラムノートでSE音をキューに積む
static void queueExecuteSound(uint8_t drum_note, bool force = false)
{
  if (!force && !system_registry->user_setting.getGuideSound()) { return; }
  uint8_t param = def::command::sound_effect_t::drum_note_preview_flag | (drum_note & 0x7F);
  system_registry->player_command.addQueue({ def::command::sound_effect, (int)param });
}

// ガイド音が有効な場合にカーソル音コマンドをキューに積む
void queueMenuCursorSound(int value)
{
  if (!system_registry->user_setting.getGuideSound()) { return; }
  int v = (value >= 200) ? value / 10 : value;
  system_registry->player_command.addQueue(
    def::command::command_param_t(uint16_t((uint16_t(v) << 8) | def::command::menu_cursor_sound)));
}

// 指定したメニューの直属の親階層のインデックスを取得する
static size_t getParentIndex(const menu_item_ptr_array &menu, size_t child_index)
{
  auto target_level = menu[child_index]->getLevel();
  for (size_t i = child_index; i > 0; --i) {
    if (menu[i]->getLevel() < target_level) {
      return i;
    }
  }
  return 0;
}

// 指定したメニューの直下の子階層インデックスリストを取得する
static int getSubMenuIndexList(std::vector<uint16_t> *index_list, const menu_item_ptr_array &menu, size_t parent_index)
{
  int result = 0;
  // 親階層よりひとつ深い階層のメニューを探索ターゲットとする
  auto target_level = 1 + menu[parent_index]->getLevel();
  for (size_t j = parent_index + 1; menu[j] != nullptr; ++j) {
    // 目的の階層より浅い階層のメニューが見つかったら終了
    if (menu[j]->getLevel() < target_level) { break; }
    // 目的の階層より深い階層のメニューは無視
    if (menu[j]->getLevel() > target_level) { continue; }
    if (!menu[j]->isVisible()) { continue; }
    ++result;
    if (index_list != nullptr)
    {
      index_list->push_back(j);
    }
  }
  // 取得した数を返す
  return result;
}

bool menu_item_t::exit(void) const
{
  if (_menu_id == 0) {
    return false;
  }
  auto array = getMenuArray(_category);

  auto parent_index = getParentIndex(array, _menu_id);
  auto level = array[parent_index]->getLevel();
  system_registry->menu_status.setCurrentLevel(level);
  system_registry->menu_status.setCurrentMenuID(parent_index);
  return true;
}

bool menu_item_t::enter(void) const
{
  _input_number_result = 0;
  auto array = getMenuArray(_category);

  system_registry->menu_status.setSelectIndex(_level - 1, _menu_id);
  system_registry->menu_status.setCurrentLevel(_level);
  system_registry->menu_status.setCurrentMenuID(_menu_id);
  if (array[_menu_id + 1] != nullptr && _level + 1 == array[_menu_id + 1]->getLevel()) {
    system_registry->menu_status.setSelectIndex(_level, _menu_id + 1);
    return true;
  }
  system_registry->menu_status.setSelectIndex(_level, _menu_id);
  return false;
}

struct mi_tree_t : public menu_item_t {
  constexpr mi_tree_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : menu_item_t { cate, menu_id, level, title } {}

  menu_item_type_t getType(void) const override { return menu_item_type_t::mt_tree; }

  size_t getSelectorCount(void) const override {
    auto array = getMenuArray(_category);

    return getSubMenuIndexList(nullptr, array, _menu_id);
  }

  bool inputNumber(uint8_t number) const override
  {
    auto array = getMenuArray(_category);

    std::vector<uint16_t> child_list;
    auto child_count = getSubMenuIndexList(&child_list, array, _menu_id);
    int max_value = child_count + getMinValue();

    int tmp = (_input_number_result * 10) + number;

    while (tmp > max_value && tmp >= 10)
    {
      int div = 10;
      if      (tmp >= 10000) { div = 10000; }
      else if (tmp >= 1000) { div = 1000; }
      else if (tmp >= 100) { div = 100; }
      tmp %= div;
    }

    _input_number_result = tmp;

    size_t cursor_pos = tmp - getMinValue();

    if (cursor_pos < child_count) {
      int enter_index = child_list[cursor_pos];
      auto item = array[enter_index];
      auto level = item->getLevel();
      system_registry->menu_status.setSelectIndex(level - 1, enter_index);
      queueMenuCursorSound(tmp);

      // 数字を押した時点ではサブメニューに入らない
      return true;

      // サブメニューに入る場合はこちら
      // return array[enter_index]->enter();
    }
    return false;
  }

  bool inputUpDown(int updown) const override
  {
    auto array = getMenuArray(_category);

    std::vector<uint16_t> child_list;
    auto child_count = getSubMenuIndexList(&child_list, array, _menu_id);

    if (!child_count) { return false; }

    int level = system_registry->menu_status.getCurrentLevel();
    int focus_index = system_registry->menu_status.getSelectIndex(level);

    auto list_position = 0;
    for (int i = 0; i < child_count; ++i) {
      if (child_list[i] == focus_index) {
        list_position = i;
        break;
      }
    }

    list_position += updown;
    if (list_position > child_count - 1) {
      list_position = child_count - 1;
    }
    if (list_position < 0) {
      list_position = 0;
    }
    focus_index = child_list[list_position];
    system_registry->menu_status.setSelectIndex(level, focus_index);
    if (!array[focus_index]->onFocus()) {
      queueMenuCursorSound(list_position + (int)getMinValue());
    }

    return true;
  }

protected:
};

struct mi_normal_t : public menu_item_t {
  constexpr mi_normal_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : menu_item_t { cate, menu_id, level, title } {}

  menu_item_type_t getType(void) const override { return menu_item_type_t::mt_normal; }

  size_t getSelectorCount(void) const override { return getMaxValue() - getMinValue() + 1; }

  bool enter(void) const override {
    _selecting_value = getValue();
    auto min_value = getMinValue();
    if (_selecting_value < min_value) { _selecting_value = min_value; }
    return menu_item_t::enter();
  }

  bool execute(void) const override
  {
    if (!setValue(_selecting_value)) { return false; }
    onExecute();
    return true;
  }

  int getSelectingValue(void) const override
  {
    return _selecting_value;
  }

  bool setSelectingValue(int value) const override
  {
    bool result = true;
    auto min_value = getMinValue();
    if (value < min_value) { value = min_value; result = false; }
    auto max_value = getMaxValue();
    if (value > max_value) { value = max_value; result = false; }
    _selecting_value = value;
    return result;
  }

  bool inputUpDown(int updown) const override
  {
    bool result = setSelectingValue(_selecting_value + updown);
    queueMenuCursorSound(_selecting_value);
    return result;
  }

  bool inputNumber(uint8_t number) const override
  {
    int tmp = (_input_number_result * 10) + number;
    int max_value = getMaxValue();

    while (tmp > max_value && tmp >= 10)
    {
      int div = 10;
      if      (tmp >= 10000) { div = 10000; }
      else if (tmp >= 1000) { div = 1000; }
      else if (tmp >= 100) { div = 100; }
      tmp %= div;
    }

    _input_number_result = tmp;
    return setSelectingValue(tmp);
  }


protected:
  static int _selecting_value;
};
int mi_normal_t::_selecting_value = 0;
