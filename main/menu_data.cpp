// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "menu_data.hpp"
#include "file_manage.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// extern instance
menu_control_t menu_control;

static std::string _title_text_buffer;
static int _input_number_result;

static menu_item_ptr_array getMenuArray(def::menu_category_t category);

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
  system_registry.menu_status.setCurrentLevel(level);
  system_registry.menu_status.setCurrentMenuID(parent_index);
  return true;
}

bool menu_item_t::enter(void) const
{
  _input_number_result = 0;
  auto array = getMenuArray(_category);

  system_registry.menu_status.setSelectIndex(_level - 1, _menu_id);
  system_registry.menu_status.setCurrentLevel(_level);
  system_registry.menu_status.setCurrentMenuID(_menu_id);
  if (array[_menu_id + 1] != nullptr && _level + 1 == array[_menu_id + 1]->getLevel()) {
    system_registry.menu_status.setSelectIndex(_level, _menu_id + 1);
    return true;
  }
  system_registry.menu_status.setSelectIndex(_level, _menu_id);
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
      system_registry.menu_status.setSelectIndex(level - 1, enter_index);

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

    int level = system_registry.menu_status.getCurrentLevel();
    int focus_index = system_registry.menu_status.getSelectIndex(level);

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
    system_registry.menu_status.setSelectIndex(level, focus_index);

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
    // 値を確定したときに親階層に戻る場合はここでexit
    // exit();
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
    return setSelectingValue(_selecting_value + updown);
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


struct mi_selector_t : public mi_normal_t {
  constexpr mi_selector_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const text_array_t* names)
  : mi_normal_t { cate, menu_id, level, title }
  , _names { names }
  {}

  const char* getSelectorText(size_t index) const override { return _names->at(index)->get(); }
  size_t getSelectorCount(void) const override { return _names->size(); }

  const char* getValueText(void) const override
  {
    return _names->at(getValue() - getMinValue())->get();
  }

protected:
  const text_array_t* _names;
};

struct mi_language_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "English", "English" },
    { "日本語", "日本語" },
  }};

public:
  constexpr mi_language_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry.user_setting.getLanguage());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto lang = static_cast<def::lang::language_t>(value - getMinValue());
    system_registry.user_setting.setLanguage(lang);
    return true;
  }
};


struct mi_imu_velocity_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 3, (const localize_text_t[]){
    { "Disable", "無効" },
    { "Normal",  "標準" },
    { "Strong",  "強め" },
  }};

public:
  constexpr mi_imu_velocity_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  int getValue(void) const override
  {
    return getMinValue() + system_registry.user_setting.getImuVelocityLevel();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.user_setting.setImuVelocityLevel(value);
    return true;
  }
};

struct mi_brightness_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 5, (const localize_text_t[]){
    { "Very Low",  "最暗" },     // Level 1
    { "Low",      "暗め" },     // Level 2
    { "Medium",   "標準" },     // Level 3
    { "High",     "明るめ" },   // Level 4
    { "Very High", "最明" },     // Level 5
  }};

public:
  constexpr mi_brightness_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
};

struct mi_lcd_backlight_t : public mi_brightness_t {
public:
  constexpr mi_lcd_backlight_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_brightness_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.user_setting.getDisplayBrightness();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.user_setting.setDisplayBrightness(value);
    return true;
  }
};

struct mi_led_brightness_t : public mi_brightness_t {
public:
  constexpr mi_led_brightness_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_brightness_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.user_setting.getLedBrightness();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.user_setting.setLedBrightness(value);
    system_registry.rgbled_control.refresh();
    return true;
  }
};

struct mi_vol_midi_t : public mi_normal_t {
  constexpr mi_vol_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  int getMinValue(void) const override { return 10; }
  int getMaxValue(void) const override { return 127; }

  int getValue(void) const override
  {
    return system_registry.user_setting.getMIDIMasterVolume();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry.user_setting.setMIDIMasterVolume(value);
    return true;
  }
  const char* getSelectorText(size_t index) const override {
    int tmp = index + getMinValue();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", tmp);
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
  const char* getValueText(void) const override
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", getValue());
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
};

struct mi_vol_adcmic_t : public mi_normal_t {
  constexpr mi_vol_adcmic_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  int getMinValue(void) const override { return 0; }
  int getMaxValue(void) const override { return 11; }

  int getValue(void) const override
  {
    return system_registry.user_setting.getADCMicAmp();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry.user_setting.setADCMicAmp(value);
    return true;
  }
  const char* getSelectorText(size_t index) const override {
    int tmp = index + getMinValue();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", tmp);
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
  const char* getValueText(void) const override
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", getValue());
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
};

struct mi_detail_view_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "icon view"  , "アイコン表示" },
    { "detail view", "詳細表示"     },
  }};

public:
  constexpr mi_detail_view_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  int getValue(void) const override
  {
    return getMinValue() + system_registry.user_setting.getGuiDetailMode();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.user_setting.setGuiDetailMode(value);
    return true;
  }
};

struct mi_enable_selector_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Off",     "オフ"  },
    { "On",      "オン"  },
  }};

public:
  constexpr mi_enable_selector_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
};

struct mi_wave_view_t : public mi_enable_selector_t {
public:
  constexpr mi_wave_view_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry.user_setting.getGuiWaveView());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.user_setting.setGuiWaveView(value);
    return true;
  }
};


struct mi_webserver_t : public mi_enable_selector_t {
public:
  constexpr mi_webserver_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry.wifi_control.getWebServerMode());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.wifi_control.setWebServerMode(static_cast<def::command::webserver_mode_t>(value));
    return true;
  }
};

/*
struct mi_usewifi_t : public mi_enable_selector_t {
public:
  constexpr mi_usewifi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry.wifi_control.getMode());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.wifi_control.setMode(static_cast<def::command::wifi_mode_t>(value));
    return true;
  }
};
//*/

struct mi_all_reset_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Reset",  "リセット"   },
  }};

public:
  constexpr mi_all_reset_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  const char* getValueText(void) const override { return "..."; }

  int getValue(void) const override
  {
    return getMinValue();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value == 1) {
      system_registry.reset();
      system_registry.save();
      system_registry.popup_notify.setPopup(true, def::notify_type_t::NOTIFY_ALL_RESET);
    }
    return true;
  }
};

  
#if 0
struct mi_intvalue_t : public mi_normal_t {
  constexpr mi_intvalue_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const int16_t min_value, const int16_t max_value, const int16_t step)
  : mi_normal_t { cate, menu_id, level, title }
  , _min_value { min_value }
  , _max_value { max_value }
  , _step { step }
  {}
  virtual menu_item_type_t getType(void) const { return menu_item_type_t::input_number; }

  const char* getValueText(void) const override
  {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d", getValue());
    return buf;
  }

  bool inputUpDown(int updown) const override
  {
    int value = getValue();
    value += updown * _step;
    if (value < _min_value) { value = _min_value; }
    if (value > _max_value) { value = _max_value; }
    return setValue(value);
  }

  bool inputNumber(uint8_t value) const override
  {
    if (value < _min_value) { value = _min_value; }
    if (value > _max_value) { value = _max_value; }
    return setValue(value);
  }

protected:
  int16_t _min_value;
  int16_t _max_value;
  int16_t _step;
};
#endif

struct mi_program_t : public mi_selector_t {
  constexpr mi_program_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &def::midi::program_name_table }
  {}
protected:
  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getTone() + getMinValue();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setTone(value);
    return true;
  }
};

struct mi_octave_t : public mi_normal_t {
  constexpr mi_octave_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  const char* getSelectorText(size_t index) const override {
    return def::app::position_name_table.at(index * 4)->get();
  }
  size_t getSelectorCount(void) const override { return (def::app::position_name_table.size() >> 2) + 1; }

  const char* getValueText(void) const override
  {
    return def::app::position_name_table.at( (getValue() - getMinValue()) << 2 )->get();
  }

  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return (system_registry.current_slot->chord_part[part_index].part_info.getPosition() >> 2) + 10;
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    int v = (value - 10) << 2;
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setPosition(v);
    return true;
  }
};

struct mi_voicing_t : public mi_normal_t {
  constexpr mi_voicing_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  const char* getSelectorText(size_t index) const override {
    return def::play::GetVoicingName(static_cast<KANTANMusic_Voicing>(index));
  }
  size_t getSelectorCount(void) const override { return KANTANMusic_Voicing::KANTANMusic_MAX_VOICING; }

  const char* getValueText(void) const override
  {
    return def::play::GetVoicingName(static_cast<KANTANMusic_Voicing>(getValue() - getMinValue()));
  }

  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getVoicing() + getMinValue();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setVoicing(value);
    return true;
  }
};

struct mi_clear_notes_t : public mi_normal_t {
  constexpr mi_clear_notes_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  const char* getValueText(void) const override { return "..."; }
  const char* getSelectorText(size_t index) const override { return "Clear All Notes"; }

  bool execute(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].arpeggio.reset();
    system_registry.popup_notify.setPopup(true, def::notify_type_t::NOTIFY_CLEAR_ALL_NOTES);
    return mi_normal_t::execute();
  }

  size_t getSelectorCount(void) const override { return 1; }
  int getValue(void) const override { return 0; }
  bool setValue(int value) const override { return true;}
};

struct mi_sequence_mode_t : public mi_selector_t {
  static constexpr const localize_text_array_t name_array = { 5, (const localize_text_t[]){
    { "Free Play",  "手動演奏" },
    { "Beat Play",  "自動ビート演奏" },
    { "Guide Play", "ガイド付き演奏" },
    { "Auto Song",  "完全自動演奏" },
    { "Song Edit",  "演奏記録" },
  }};

  constexpr mi_sequence_mode_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    uint32_t res = system_registry.runtime_info.getSequenceMode();
    if (res >= def::seqmode::seqmode_max) {
      res = 0;
    }
    return static_cast<int>(res) + getMinValue();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value < 0) {
      value = 0;
    }
    if (value >= def::seqmode::seqmode_t::seqmode_max) {
      value = 0;
    }
    static constexpr def::seqmode::seqmode_t modes[] = {
      def::seqmode::seqmode_t::seq_free_play,
      def::seqmode::seqmode_t::seq_beat_play,
      def::seqmode::seqmode_t::seq_guide_play,
      def::seqmode::seqmode_t::seq_auto_song,
      def::seqmode::seqmode_t::seq_song_edit,
    };
    auto mode = modes[value];

    system_registry.operator_command.addQueue({ def::command::sequence_mode_set, mode });
    return true;
  }
};
/*
struct mi_clear_seq_t : public mi_normal_t {
  constexpr mi_clear_seq_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  const char* getValueText(void) const override { return "..."; }
  const char* getSelectorText(size_t index) const override { return "Clear"; }

  bool execute(void) const override
  {
    system_registry.current_sequence->deleteAfter(system_registry.runtime_info.getSequenceStepIndex());
    return mi_normal_t::execute();
  }

  size_t getSelectorCount(void) const override { return 1; }
  int getValue(void) const override { return 0; }
  bool setValue(int value) const override { return true;}
};
*/

struct mi_clear_seq_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Clear",  "クリア"   },
  }};

public:
  constexpr mi_clear_seq_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  const char* getValueText(void) const override { return "..."; }

  int getValue(void) const override
  {
    return getMinValue();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value == 1) {
      system_registry.current_sequence->deleteAfter(system_registry.runtime_info.getSequenceStepIndex());
      system_registry.popup_notify.setPopup(true, def::notify_type_t::NOTIFY_CLEAR_AFTER_CURSOR);
    }
    return true;
  }
/*
  bool execute(void) const override
  {
    if (!setValue(_selecting_value)) { return false; }
    // 値を確定したときに親階層に戻る場合はここでexit
    exit();
    return true;
  }
//*/
};


struct mi_percent_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 20, (const simple_text_t[]){
     "5%",  "10%",  "15%",  "20%",
    "25%",  "30%",  "35%",  "40%",
    "45%",  "50%",  "55%",  "60%",
    "65%",  "70%",  "75%",  "80%",
    "85%",  "90%",  "95%", "100%",
  }};

  constexpr mi_percent_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}
};

struct mi_partvolume_t : public mi_percent_t {
  constexpr mi_partvolume_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_percent_t { cate, menu_id, level, title}
  {}
protected:
  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getVolume() / 5;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setVolume(value * 5);
    return true;
  }
};

struct mi_velocity_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 21, (const simple_text_t[]){
    "mute",
      "5%",  "10%",  "15%",  "20%",
    "25%",  "30%",  "35%",  "40%",
    "45%",  "50%",  "55%",  "60%",
    "65%",  "70%",  "75%",  "80%",
    "85%",  "90%",  "95%", "100%",
  }};

  constexpr mi_velocity_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

protected:
  int getValue(void) const override
  {
    int velo = system_registry.runtime_info.getEditVelocity();
    if (velo < 0) return 1;
    return 1 + (velo / 5);
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    int velo = (value == 1) ? -5 : (value - 1) * 5;
    system_registry.runtime_info.setEditVelocity(velo);
    return true;
  }
};

struct mi_arpeggio_step_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 32, (const simple_text_t[]){
    "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
    "9",  "10", "11", "12", "13", "14", "15", "16",
    "17", "18", "19", "20", "21", "22", "23", "24",
    "25", "26", "27", "28", "29", "30", "31", "32",
  }};

  constexpr mi_arpeggio_step_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}
};

struct mi_loop_length_t : public mi_arpeggio_step_t {
  constexpr mi_loop_length_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_arpeggio_step_t { cate, menu_id, level, title }
  {}
protected:
  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getLoopStep() / 2 + 1;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setLoopStep(value * 2 - 1);
    return true;
  }
};

struct mi_anchor_step_t : public mi_arpeggio_step_t {
  constexpr mi_anchor_step_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_arpeggio_step_t { cate, menu_id, level, title }
  {}
protected:
  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getAnchorStep() / 2 + 1;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setAnchorStep(value * 2 - 2);
    return true;
  }
};

struct mi_stroke_speed_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 10, (const simple_text_t[]){
    "5 msec",  "10 msec",  "15 msec",  "20 msec",
    "25 msec", "30 msec",  "35 msec",  "40 msec",
    "45 msec", "50 msec"
  }};

  constexpr mi_stroke_speed_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}
protected:
  int getValue(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.current_slot->chord_part[part_index].part_info.getStrokeSpeed() / 5;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.current_slot->chord_part[part_index].part_info.setStrokeSpeed(value * 5);
    return true;
  }
};

struct mi_offbeat_style_t : public mi_selector_t {
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Auto", "自動" },
    { "Self", "手動" },
  }};

  constexpr mi_offbeat_style_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    return system_registry.user_setting.getOffbeatStyle();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto style = def::play::offbeat_style_t::offbeat_auto;
    switch (value) {
    default: break; 
    case 2: style = def::play::offbeat_style_t::offbeat_self; break;
    }
    system_registry.user_setting.setOffbeatStyle(style);
    return true;
  }
};

struct mi_slot_playmode_t : public mi_selector_t {
  static constexpr const localize_text_array_t name_array = { 3, (const localize_text_t[]){
    { "Chord Mode", "コード" },
    { "Note Mode",  "ノート" },
    { "Drum Mode",  "ドラム" },
  }};

  constexpr mi_slot_playmode_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    return system_registry.current_slot->slot_info.getPlayMode();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto mode = def::playmode::playmode_t::chord_mode;
    switch (value) {
    default: 
    case 1: break;
    case 2: mode = def::playmode::playmode_t::note_mode; break;
    case 3: mode = def::playmode::playmode_t::drum_mode; break;
    }
    system_registry.operator_command.addQueue({ def::command::play_mode_set, mode });
    // system_registry.current_slot->slot_info.setPlayMode(mode);
    return true;
  }
};

struct mi_slot_key_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 23, (const simple_text_t[]){
    "- 11","- 10","- 9", " -8",
    "- 7", "- 6", "- 5", " -4",
    "- 3", "- 2", "- 1", "± 0",
    "+ 1", "+ 2", "+ 3", "+ 4",
    "+ 5", "+ 6", "+ 7", "+ 8",
    "+ 9", "+ 10","+ 11"
  }};

  constexpr mi_slot_key_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    auto key_offset = system_registry.current_slot->slot_info.getKeyOffset();
    return key_offset + 12;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto key_offset = value - 12;
    system_registry.current_slot->slot_info.setKeyOffset(key_offset);
    return true;
  }
};

// スロット別のステップ/ビート
struct mi_slot_step_beat_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 4, (const simple_text_t[]){
    "1", "2", "3", "4"
  }};

  constexpr mi_slot_step_beat_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    return system_registry.current_slot->slot_info.getStepPerBeat();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    system_registry.current_slot->slot_info.setStepPerBeat(value);
    return true;
  }
};

// 全スロット一括のステップ/ビート
struct mi_song_step_beat_t : public mi_selector_t {
  static constexpr const simple_text_array_t name_array = { 5, (const simple_text_t[]){
    "1", "2", "3", "4", "Each"
  }};

  constexpr mi_song_step_beat_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  int getValue(void) const override
  {
    auto step_per_beat = system_registry.current_slot->slot_info.getStepPerBeat();
    for (size_t i = 0; i < def::app::max_slot; ++i) {
      if (system_registry.song_data.slot[i].slot_info.getStepPerBeat() != step_per_beat) {
        return 5; // "Each"
      }
    }
    return step_per_beat;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    if (value == 5) { // "Each"
      return true;
    }
    auto prev_value = getValue();
    for (size_t i = 0; i < def::app::max_slot; ++i) {
      system_registry.song_data.slot[i].slot_info.setStepPerBeat(value);
    }
    if ((prev_value != value) && (prev_value <= 4)) {
      auto tempo = system_registry.song_data.song_info.getTempo();

      // ステップ/ビートが変更された場合、テンポを調整する
      uint16_t new_tempo = (tempo * prev_value) / value;
      system_registry.song_data.song_info.setTempo(new_tempo);
    }

    return true;
  }
};

struct mi_slot_clipboard_t : public mi_selector_t {
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Copy Setting"  , "設定コピー" },
    { "Paste Setting" , "設定ペースト" },
  }};

  const char* getValueText(void) const override { return "..."; }

  constexpr mi_slot_clipboard_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  bool execute(void) const override
  {
    // auto part_index = system_registry.chord_play.getEditTargetPart();
    // auto slot_index = system_registry.runtime_info.getPlaySlot();
    // auto slot = &system_registry.song_data.slot[slot_index];
    switch (getSelectingValue()) {
    case 1:
      system_registry.clipboard_slot.assign(*system_registry.current_slot);
      system_registry.popup_notify.setPopup(true, def::notify_type_t::NOTIFY_COPY_SLOT_SETTING);
      system_registry.clipboard_content = system_registry_t::clipboard_contetn_t::CLIPBOARD_CONTENT_SLOT;
      // M5_LOGV("mi_slot_clipboard_t: Copy Setting");
      break;

    case 2:
      {
        bool flg = (system_registry.clipboard_content == system_registry_t::clipboard_contetn_t::CLIPBOARD_CONTENT_SLOT);
        if (flg) {
          system_registry.current_slot->assign(system_registry.clipboard_slot);
        }
        system_registry.popup_notify.setPopup(flg, def::notify_type_t::NOTIFY_PASTE_SLOT_SETTING);
      }
      break;

    default:
      // M5_LOGV("mi_slot_clipboard_t: unknown: %d", getValue());
      return false;
    }
    return mi_selector_t::execute();
  }
};

struct mi_part_clipboard_t : public mi_selector_t {
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Copy Part"    , "パートコピー" },
    { "Paste Part"   , "パートペースト" },
  }};

  const char* getValueText(void) const override { return "..."; }

  constexpr mi_part_clipboard_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array }
  {}

  bool execute(void) const override
  {
    auto part_index = system_registry.chord_play.getEditTargetPart();
    switch (getSelectingValue()) {
    case 1:
      system_registry.clipboard_slot.chord_part[0].assign(system_registry.current_slot->chord_part[part_index]);
      system_registry.popup_notify.setPopup(true, def::notify_type_t::NOTIFY_COPY_PART_SETTING);
      system_registry.clipboard_content = system_registry_t::clipboard_contetn_t::CLIPBOARD_CONTENT_PART;
      break;

    case 2:
      {
        bool flg = (system_registry.clipboard_content == system_registry_t::clipboard_contetn_t::CLIPBOARD_CONTENT_PART);
        if (flg) {
          system_registry.current_slot->chord_part[part_index].assign(system_registry.clipboard_slot.chord_part[0]);
        }
        system_registry.popup_notify.setPopup(flg, def::notify_type_t::NOTIFY_PASTE_PART_SETTING);
      }
      break;

    default:
      // M5_LOGV("mi_part_clipboard_t: unknown: %d", getValue());
      return false;
    }
    return mi_selector_t::execute();
  }
};

struct mi_song_tempo_t : public mi_normal_t {
  constexpr mi_song_tempo_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  int getMinValue(void) const override { return def::app::tempo_bpm_min; }
  int getMaxValue(void) const override { return def::app::tempo_bpm_max; }
  // size_t getSelectorCount(void) const override { return def::app::tempo_bpm_max - def::app::tempo_bpm_min + 1; }

  int getValue(void) const override
  {
    return system_registry.song_data.song_info.getTempo();
    // return system_registry.current_slot->slot_info.getTempo();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry.song_data.song_info.setTempo(value);
/*
    system_registry.current_slot->slot_info.setTempo(value);
    for (int i = 0; i < def::app::max_slot; ++i) {
      system_registry.song_data.slot[i].slot_info.setTempo(value);
    }
*/
    return true;
  }
  const char* getSelectorText(size_t index) const override {
    int tempo = index + getMinValue();
    char buf[16];
    snprintf(buf, sizeof(buf), "%d bpm", tempo);
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
  const char* getValueText(void) const override
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d bpm", getValue());
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
};

struct mi_song_swing_t : public mi_normal_t {
  constexpr mi_song_swing_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title }
  {}
protected:
  int getMinValue(void) const override { return def::app::swing_percent_min; }
  int getMaxValue(void) const override { return def::app::swing_percent_max / 10; }

  int getValue(void) const override
  {
    return system_registry.song_data.song_info.getSwing() / 10;
    // return system_registry.current_slot->slot_info.getSwing();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry.song_data.song_info.setSwing(value * 10);
/*
    system_registry.current_slot->slot_info.setSwing(value);
    for (int i = 0; i < def::app::max_slot; ++i) {
      system_registry.song_data.slot[i].slot_info.setSwing(value);
    }
*/
    return true;
  }
  const char* getSelectorText(size_t index) const override {
    int sw = index * 10;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", sw);
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
  const char* getValueText(void) const override
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", getValue() * 10);
    _title_text_buffer = buf;
    return _title_text_buffer.c_str();
  }
};

struct mi_drum_note_t : public mi_selector_t {
  constexpr mi_drum_note_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t pitch_number )
  : mi_selector_t { cate, menu_id, level, title, &def::midi::drum_note_name_tbl } // 35 = Acoustic Bass Drum, 81 = Open Triangle
  , _pitch_number { pitch_number }
  {}
protected:
  const uint8_t _pitch_number;

  // 設定可能な最小値を取得する
  int getMinValue(void) const override { return def::midi::drum_note_name_min; }

  int getValue(void) const override
  {
    int part_index = system_registry.chord_play.getEditTargetPart();
    return system_registry.song_data.chord_part_drum[part_index].getDrumNoteNumber(_pitch_number);
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    int part_index = system_registry.chord_play.getEditTargetPart();
    system_registry.song_data.chord_part_drum[part_index].setDrumNoteNumber(_pitch_number, value);
    return true;
  }
};

enum class ctrl_map_target_t : uint8_t {
  ctrl_map_device = 0,
  ctrl_map_song = 1,
};

struct mi_ctrl_assign_t : public mi_normal_t {
  constexpr mi_ctrl_assign_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const def::ctrl_assign::control_assignment_t table[], uint16_t size, ctrl_map_target_t map_target)
  : mi_normal_t { cate, menu_id, level, title }
  , _table { table }
  , _size { size }
  , _map_target { map_target }
  {}

  const char* getSelectorText(size_t index) const override { return _table[index].text.get(); }
  size_t getSelectorCount(void) const override { return _size; }

  const char* getValueText(void) const override
  {
    return _table[getValue() - getMinValue()].text.get();
  }

protected:
  const def::ctrl_assign::control_assignment_t* _table;
  const uint16_t _size;
  const ctrl_map_target_t _map_target;
};

// control assignment for internal
struct mi_ca_internal_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_internal_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, ctrl_map_target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::playbutton_table, sizeof(def::ctrl_assign::playbutton_table) / sizeof(def::ctrl_assign::playbutton_table[0])-1, map_target }
  , _button_index { button_index } {}

  int getValue(void) const override
  {
    auto cmd = system_registry.command_mapping_custom_main.getCommandParamArray(_button_index);
    int index = def::ctrl_assign::get_index_from_command(_table, cmd);
    if (index < 0) {
      index = 0;
    }
    return getMinValue() + index;
  }
  bool setValue(int value) const override
  {
    if (mi_ctrl_assign_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.command_mapping_custom_main.setCommandParamArray(_button_index, _table[value].command);
    return true;
  }

protected:
  const uint8_t _button_index;
};

// control assignment for internal
struct mi_ca_external_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_external_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, ctrl_map_target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::external_table, sizeof(def::ctrl_assign::external_table) / sizeof(def::ctrl_assign::external_table[0])-1, map_target }
  , _button_index { button_index } {}

  int getValue(void) const override
  {
    auto cmd = system_registry.command_mapping_external.getCommandParamArray(_button_index);
    int index = def::ctrl_assign::get_index_from_command(_table, cmd);
    if (index < 0) {
      index = 0;
    }
    return getMinValue() + index;
  }
  bool setValue(int value) const override
  {
    if (mi_ctrl_assign_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.command_mapping_external.setCommandParamArray(_button_index, _table[value].command);
    return true;
  }

protected:
  const uint8_t _button_index;
};

struct mi_ca_midinote_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_midinote_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, ctrl_map_target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::external_table, sizeof(def::ctrl_assign::external_table) / sizeof(def::ctrl_assign::external_table[0])-1, map_target }
  , _button_index { button_index } {}

  int getValue(void) const override
  {
    auto cmd = system_registry.command_mapping_midinote.getCommandParamArray(_button_index);
    int index = def::ctrl_assign::get_index_from_command(_table, cmd);
    if (index < 0) {
      index = 0;
    }
    return getMinValue() + index;
  }
  bool setValue(int value) const override
  {
    if (mi_ctrl_assign_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.command_mapping_midinote.setCommandParamArray(_button_index, _table[value].command);
    return true;
  }

protected:
  const uint8_t _button_index;
};

struct mi_midi_selector_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 4, (const localize_text_t[]){
    { "Off",      "オフ" },
    { "Output",   "出力" },
    { "Input",    "入力" },
    { "In + Out", "入出力" },
  }};

public:
  constexpr mi_midi_selector_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
};

struct mi_portc_midi_t : public mi_midi_selector_t {
  constexpr mi_portc_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getPortCMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setPortCMIDI( static_cast<def::command::ex_midi_mode_t>(value));
    return true;
  }
};

struct mi_ble_midi_t : public mi_midi_selector_t {
  constexpr mi_ble_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getBLEMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setBLEMIDI( static_cast<def::command::ex_midi_mode_t>(value));
    return true;
  }
};

struct mi_usb_midi_t : public mi_midi_selector_t {
  constexpr mi_usb_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getUSBMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setUSBMIDI( static_cast<def::command::ex_midi_mode_t>(value));
    return true;
  }
};

struct mi_usb_mode_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Host",            "ホスト" },
    { "Device (to PC)" , "デバイス(→PC)"   },
  }};

public:
  constexpr mi_usb_mode_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getUSBMode();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setUSBMode( static_cast<def::command::usb_mode_t>(value));
    return true;
  }
};

struct mi_usb_power_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Off", "給電しない" },
    { "On" , "給電する"   },
  }};

public:
  constexpr mi_usb_power_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getUSBPowerEnabled();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setUSBPowerEnabled( static_cast<bool>(value));
    return true;
  }
};

struct mi_iclink_port_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 3, (const localize_text_t[]){
    { "Off",   "オフ" },
    { "BLE", nullptr },
    { "USB", nullptr },
  }};

public:
  constexpr mi_iclink_port_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getInstaChordLinkPort();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setInstaChordLinkPort( static_cast<def::command::instachord_link_port_t>(value));
    return true;
  }
};

struct mi_iclink_dev_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "KANTAN Play", "かんぷれ" },
    { "InstaChord",  "インスタコード"},
  }};

public:
  constexpr mi_iclink_dev_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getInstaChordLinkDev();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setInstaChordLinkDev( static_cast<def::command::instachord_link_dev_t>(value));
    return true;
  }
};

struct mi_iclink_style_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Button", "ボタン" },
    { "Pad",  "パッド"},
  }};

public:
  constexpr mi_iclink_style_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry.midi_port_setting.getInstaChordLinkStyle();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry.midi_port_setting.setInstaChordLinkStyle( static_cast<def::command::instachord_link_style_t>(value));
    return true;
  }
};

struct mi_otaupdate_t : public mi_normal_t {
  constexpr mi_otaupdate_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}
  menu_item_type_t getType(void) const override { return menu_item_type_t::show_progress; }

  bool setSelectingValue(int value) const override { return false; }
  bool execute(void) const override { return false; }
  bool inputUpDown(int updown) const override { return false; }
  bool inputNumber(uint8_t number) const override { return false; }

  bool enter(void) const override
  {
    // OTAを実施する際にオートプレイは無効にする
    system_registry.runtime_info.setAutoplayState(def::play::auto_play_mode_t::auto_play_none);

    system_registry.runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connecting);
    system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_begin);
    return mi_normal_t::enter();
  }
  bool exit(void) const override
  {
    auto v = getSelectingValue();
    if (0 < v && v <= 100) {
      // OTAの途中でメニューを閉じることはできない
      return true;
    }
    system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry.runtime_info.setWiFiOtaProgress(0);
    return mi_normal_t::exit();
  }

  std::string getString(void) const override {
    char buf[32];
    std::string result;
    auto v = getSelectingValue();
    switch (v) {
    case (uint8_t)def::command::wifi_ota_state_t::ota_connecting:         snprintf(buf, sizeof(buf), "Connecting.");           break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_connection_error:   snprintf(buf, sizeof(buf), "Connection error.");     break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_available:   snprintf(buf, sizeof(buf), "Download.");             break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_already_up_to_date: snprintf(buf, sizeof(buf), "Already up to date.");   break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_failed:      snprintf(buf, sizeof(buf), "Update failed.");        break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_done:        snprintf(buf, sizeof(buf), "Update Done.");          break;
    default:
      snprintf(buf, sizeof(buf), "Download :% 3d %%", v);
      break;
    }
    return std::string(buf);
  }

  int getSelectingValue(void) const override
  {
    return system_registry.runtime_info.getWiFiOtaProgress();
  }
};

struct mi_wifiap_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Use Smartphone", "スマホで設定" },
    { "WPS"           , "WPSで設定"   },
  }};

public:
  constexpr mi_wifiap_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  const char* getValueText(void) const override { return "..."; }

  int getSelectingValue(void) const override
  {
    auto qrtype = def::qrcode_type_t::QRCODE_NONE;

    auto result = mi_selector_t::getSelectingValue();

    if (result == 1) {
      if (system_registry.wifi_control.getOperation() == def::command::wifi_operation_t::wfop_setup_ap) {
        qrtype = system_registry.runtime_info.getWiFiStationCount()
                    ? def::qrcode_type_t::QRCODE_URL_DEVICE
                    : def::qrcode_type_t::QRCODE_AP_SSID;
      }
    }
    if (system_registry.popup_qr.getQRCodeType() != qrtype) {
      system_registry.popup_qr.setQRCodeType(qrtype);
      if (result == 1 && qrtype == def::qrcode_type_t::QRCODE_NONE) {
        exit();
      }
    }
    return result;
  }
  bool execute(void) const override
  {
    if (getSelectingValue() == 1) {
      system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_setup_ap);
    } else {
      system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_setup_wps);
    }
    return false;
  }

  bool exit(void) const override
  {
    system_registry.wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry.popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};

struct mi_manual_qr_t : public mi_normal_t {
  constexpr mi_manual_qr_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  const char* getValueText(void) const override { return "..."; }
  const char* getSelectorText(size_t index) const override { return _title.get(); }

  size_t getSelectorCount(void) const override { return 1; }

  bool execute(void) const override
  {
    system_registry.popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_URL_MANUAL);
    return false;
  }

  bool exit(void) const override
  {
    system_registry.popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};


static std::string _tmp_filename;
struct mi_filelist_t : public mi_normal_t {
  constexpr mi_filelist_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, def::app::data_type_t dir_type )
  : mi_normal_t { cate, menu_id, level, title }
  , _dir_type { dir_type }
  {}
protected:
  def::app::data_type_t _dir_type;

  const char* getSelectorText(size_t index) const override {
    auto fileinfo = file_manage.getFileInfo(_dir_type, index);
    _tmp_filename = fileinfo->filename;

    // 末尾の拡張子 .json を削除
    auto pos = _tmp_filename.rfind(".json");
    if (pos != std::string::npos) {
      _tmp_filename = _tmp_filename.substr(0, pos);
    }

    return _tmp_filename.c_str();
    // return fileinfo->filename.c_str();
  }

  size_t getSelectorCount(void) const override { return file_manage.getDirManage(_dir_type)->getCount(); }

  const char* getValueText(void) const override
  {
    return "...";
  }

  int getValue(void) const override
  {
    auto songinfo = system_registry.file_command.getCurrentSongInfo();
    if (songinfo.dir_type == _dir_type) {
      return songinfo.file_index + getMinValue();
    }
    return 0;
  }
};

struct mi_load_file_t : public mi_filelist_t {
  constexpr mi_load_file_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, def::app::data_type_t dir_type, size_t top_index = 1 )
  : mi_filelist_t { cate, menu_id, level, title, dir_type }
  , _top_index { top_index }
  {
  }
protected:
  const size_t _top_index;
  int getMinValue(void) const { return _top_index; }

  bool enter(void) const override
  {
    system_registry.backup_song_data.assign(system_registry.song_data);
    system_registry.file_command.setUpdateList(_dir_type);
    M5.delay(64);
    return mi_filelist_t::enter();
  }
/*
  bool exit(void) const override
  {
    system_registry.song_data.assign(system_registry.backup_song_data);
    return mi_filelist_t::exit();
  }
//*/
  bool execute(void) const override
  {
    auto songinfo = system_registry.file_command.getCurrentSongInfo();
    songinfo.file_index = _selecting_value - getMinValue();
    songinfo.dir_type = _dir_type;
    system_registry.file_command.setFileLoadRequest(songinfo);
    system_registry.file_command.setCurrentSongInfo(songinfo);
    return mi_filelist_t::execute();
  }
};

struct mi_save_t : public mi_normal_t {
  constexpr mi_save_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, def::app::data_type_t dir_type )
  : mi_normal_t { cate, menu_id, level, title }
  , _dir_type { dir_type }
  {}
  static constexpr const size_t max_filenames = 4;
  def::app::data_type_t _dir_type;
protected:
  const char* getSelectorText(size_t index) const override {
    return _filenames[index].c_str();
  }

  size_t getSelectorCount(void) const override { return max_filenames; }

  const char* getValueText(void) const override
  {
    return "...";
  }

  bool enter(void) const override
  {
    auto fn = file_manage.getDisplayFileName();
    if (fn.empty()) {
      fn = "new_song";
    }
    _filenames[0] = fn + ".json";
    _filenames[1] = fn + "_.json";
    _filenames[2] = "_" + fn + ".json";

    auto t = time(nullptr);
    auto tm = localtime(&t);
    char buf[64];

    snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d.json",
          tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
          tm->tm_hour, tm->tm_min, tm->tm_sec);
    _filenames[3] = buf;

    _selecting_value = getMinValue();

    return mi_normal_t::enter();
  }

  bool execute(void) const override
  {
    auto mem = file_manage.createMemoryInfo(def::app::max_file_len);
    auto index = _selecting_value - getMinValue();
    mem->filename = _filenames[index];
    mem->dir_type = _dir_type;

    system_registry.unchanged_song_data.assign(system_registry.song_data);
    auto len = system_registry.unchanged_song_data.saveSongJSON(mem->data, def::app::max_file_len);
    mem->size = len;
    if (len == 0 || mem->data[0] != '{') {
      system_registry.popup_notify.setPopup(false, def::notify_type_t::NOTIFY_FILE_SAVE);
      M5_LOGE("mi_save_t: saveSongJSON failed");
      return false;
    }
    def::app::file_command_info_t info;
    info.mem_index = mem->index;
    info.dir_type = _dir_type;
    info.file_index = -1;
    system_registry.file_command.setFileSaveRequest(info);
    return mi_normal_t::execute();
  }
protected:
  static std::string _filenames[max_filenames];
};
std::string mi_save_t::_filenames[max_filenames];


static constexpr const size_t START_COUNTER_SYSTEM = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_SYSTEM)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_system, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_system[] = {
  MENU_BUILDER(mi_tree_t          ,0    , { "Menu"           , "メニュー"    }),
  MENU_BUILDER(mi_tree_t          , 1   , { "File"           , "ファイル"    }),
  MENU_BUILDER(mi_tree_t          ,  2  , { "Open"           , "開く"        }),
  MENU_BUILDER(mi_load_file_t     ,   3 , { "Preset Songs"   , "プリセットソング" }, def::app::data_type_t::data_song_preset, 0 ),
  MENU_BUILDER(mi_load_file_t     ,   3 , { "Extra Songs (SD)","エクストラソング(SD)" }, def::app::data_type_t::data_song_extra ),
  MENU_BUILDER(mi_load_file_t     ,   3 , { "User Songs (SD)", "ユーザソング(SD)"}, def::app::data_type_t::data_song_users ),
  MENU_BUILDER(mi_save_t          ,  2  , { "Save"           , "保存"          }, def::app::data_type_t::data_song_users ),
  MENU_BUILDER(mi_tree_t          , 1   , { "Slot Setting"   , "スロット設定"  }),
  MENU_BUILDER(mi_slot_playmode_t ,  2  , { "Play Mode"      , "演奏モード"    }),
  MENU_BUILDER(mi_slot_key_t      ,  2  , { "Key Modulation" , "キー転調"      }),
  MENU_BUILDER(mi_slot_step_beat_t,  2  , { "Step / Beat"    , "ステップ／ビート"}),
  MENU_BUILDER(mi_slot_clipboard_t,  2  , { "Copy/Paste"     , "コピー/ペースト" }),
  MENU_BUILDER(mi_tree_t          , 1   , { "Tempo & Groove" , "テンポ＆グルーヴ設定"  }),
  MENU_BUILDER(mi_song_tempo_t    ,  2  , { "BPM"            , "テンポ(BPM)"   }),
  MENU_BUILDER(mi_song_swing_t    ,  2  , { "Swing"          , "スウィング"    }),
  MENU_BUILDER(mi_offbeat_style_t ,  2  , { "Offbeat Control", "裏拍演奏"     }),
  MENU_BUILDER(mi_song_step_beat_t,  2  , { "Step / Beat"    , "ステップ／ビート"}),
  MENU_BUILDER(mi_tree_t          , 1   , { "System"         , "システム"     }),
  MENU_BUILDER(mi_tree_t          ,  2  , { "WiFi"           , "WiFi通信"     }),
  MENU_BUILDER(mi_webserver_t     ,   3 , { "Web server"     , "Webサーバ"       }),
  MENU_BUILDER(mi_otaupdate_t     ,   3 , { "Firm Update"    , "ファーム更新" }),
  MENU_BUILDER(mi_wifiap_t        ,   3 , { "WiFi Setup"     , "WiFi設定"     }),
  MENU_BUILDER(mi_tree_t          ,  2   , { "Control Mapping", "操作マッピング" }),
  MENU_BUILDER(mi_tree_t          ,   3  , { "Mapping 1(Device)", "マッピング1 (本体)" }),
  MENU_BUILDER(mi_tree_t          ,    4 , { "Play Button"   , "プレイボタン" }),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 1"      , "ボタン 1"     },  1 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 2"      , "ボタン 2"     },  2 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 3"      , "ボタン 3"     },  3 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 4"      , "ボタン 4"     },  4 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 5"      , "ボタン 5"     },  5 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 6"      , "ボタン 6"     },  6 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 7"      , "ボタン 7"     },  7 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 8"      , "ボタン 8"     },  8 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 9"      , "ボタン 9"     },  9 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 10"     , "ボタン 10"    }, 10 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 11"     , "ボタン 11"    }, 11 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 12"     , "ボタン 12"    }, 12 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 13"     , "ボタン 13"    }, 13 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 14"     , "ボタン 14"    }, 14 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 15"     , "ボタン 15"    }, 15 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_tree_t          ,    4 , { "Ext Input"     , "拡張入力"     }),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 1"        , "拡張 1"       },   1 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 2"        , "拡張 2"       },   2 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 3"        , "拡張 3"       },   3 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 4"        , "拡張 4"       },   4 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 5"        , "拡張 5"       },   5 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 6"        , "拡張 6"       },   6 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 7"        , "拡張 7"       },   7 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 8"        , "拡張 8"       },   8 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 9"        , "拡張 9"       },   9 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 10"       , "拡張 10"      },  10 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 11"       , "拡張 11"      },  11 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 12"       , "拡張 12"      },  12 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 13"       , "拡張 13"      },  13 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 14"       , "拡張 14"      },  14 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 15"       , "拡張 15"      },  15 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 16"       , "拡張 16"      },  16 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 17"       , "拡張 17"      },  17 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 18"       , "拡張 18"      },  18 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 19"       , "拡張 19"      },  19 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 20"       , "拡張 20"      },  20 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 21"       , "拡張 21"      },  21 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 22"       , "拡張 22"      },  22 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 23"       , "拡張 23"      },  23 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 24"       , "拡張 24"      },  24 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 25"       , "拡張 25"      },  25 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 26"       , "拡張 26"      },  26 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 27"       , "拡張 27"      },  27 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 28"       , "拡張 28"      },  28 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 29"       , "拡張 29"      },  29 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 30"       , "拡張 30"      },  30 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 31"       , "拡張 31"      },  31 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 32"       , "拡張 32"      },  32 - 1, ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_tree_t          ,    4 , { "MIDI Note"     , "MIDI Note"    }),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C#-1" , nullptr },   1 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D -1" , nullptr },   2 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D#-1" , nullptr },   3 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E -1" , nullptr },   4 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F -1" , nullptr },   5 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F#-1" , nullptr },   6 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G -1" , nullptr },   7 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G#-1" , nullptr },   8 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A -1" , nullptr },   9 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A#-1" , nullptr },  10 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B -1" , nullptr },  11 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  0" , nullptr },  12 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 0" , nullptr },  13 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  0" , nullptr },  14 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 0" , nullptr },  15 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  0" , nullptr },  16 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  0" , nullptr },  17 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 0" , nullptr },  18 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  0" , nullptr },  19 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 0" , nullptr },  20 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  0" , nullptr },  21 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 0" , nullptr },  22 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  0" , nullptr },  23 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  1" , nullptr },  24 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 1" , nullptr },  25 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  1" , nullptr },  26 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 1" , nullptr },  27 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  1" , nullptr },  28 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  1" , nullptr },  29 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 1" , nullptr },  30 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  1" , nullptr },  31 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 1" , nullptr },  32 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  1" , nullptr },  33 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 1" , nullptr },  34 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  1" , nullptr },  35 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  2" , nullptr },  36 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 2" , nullptr },  37 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  2" , nullptr },  38 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 2" , nullptr },  39 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  2" , nullptr },  40 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  2" , nullptr },  41 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 2" , nullptr },  42 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  2" , nullptr },  43 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 2" , nullptr },  44 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  2" , nullptr },  45 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 2" , nullptr },  46 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  2" , nullptr },  47 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  3" , nullptr },  48 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 3" , nullptr },  49 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  3" , nullptr },  50 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 3" , nullptr },  51 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  3" , nullptr },  52 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  3" , nullptr },  53 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 3" , nullptr },  54 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  3" , nullptr },  55 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 3" , nullptr },  56 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  3" , nullptr },  57 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 3" , nullptr },  58 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  3" , nullptr },  59 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  4" , nullptr },  60 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 4" , nullptr },  61 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  4" , nullptr },  62 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 4" , nullptr },  63 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  4" , nullptr },  64 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  4" , nullptr },  65 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 4" , nullptr },  66 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  4" , nullptr },  67 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 4" , nullptr },  68 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  4" , nullptr },  69 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 4" , nullptr },  70 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  4" , nullptr },  71 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  5" , nullptr },  72 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 5" , nullptr },  73 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  5" , nullptr },  74 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 5" , nullptr },  75 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  5" , nullptr },  76 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  5" , nullptr },  77 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 5" , nullptr },  78 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  5" , nullptr },  79 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 5" , nullptr },  80 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  5" , nullptr },  81 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 5" , nullptr },  82 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  5" , nullptr },  83 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  6" , nullptr },  84 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 6" , nullptr },  85 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  6" , nullptr },  86 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 6" , nullptr },  87 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  6" , nullptr },  88 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  6" , nullptr },  89 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 6" , nullptr },  90 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  6" , nullptr },  91 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 6" , nullptr },  92 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  6" , nullptr },  93 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 6" , nullptr },  94 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  6" , nullptr },  95 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  7" , nullptr },  96 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 7" , nullptr },  97 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  7" , nullptr },  98 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 7" , nullptr },  99 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  7" , nullptr }, 100 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  7" , nullptr }, 101 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 7" , nullptr }, 102 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  7" , nullptr }, 103 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 7" , nullptr }, 104 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  7" , nullptr }, 105 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 7" , nullptr }, 106 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  7" , nullptr }, 107 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  8" , nullptr }, 108 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 8" , nullptr }, 109 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  8" , nullptr }, 110 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 8" , nullptr }, 111 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  8" , nullptr }, 112 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  8" , nullptr }, 113 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 8" , nullptr }, 114 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  8" , nullptr }, 115 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 8" , nullptr }, 116 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  8" , nullptr }, 117 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 8" , nullptr }, 118 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  8" , nullptr }, 119 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  9" , nullptr }, 120 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 9" , nullptr }, 121 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  9" , nullptr }, 122 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 9" , nullptr }, 123 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  9" , nullptr }, 124 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  9" , nullptr }, 125 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 9" , nullptr }, 126 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  9" , nullptr }, 127 , ctrl_map_target_t::ctrl_map_device),
  MENU_BUILDER(mi_tree_t          ,   3  , { "Mapping 2(Song)", "マッピング1 (ソング)" }),
  MENU_BUILDER(mi_tree_t          ,    4 , { "Play Button"   , "プレイボタン" }),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 1"      , "ボタン 1"     },  1 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 2"      , "ボタン 2"     },  2 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 3"      , "ボタン 3"     },  3 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 4"      , "ボタン 4"     },  4 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 5"      , "ボタン 5"     },  5 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 6"      , "ボタン 6"     },  6 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 7"      , "ボタン 7"     },  7 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 8"      , "ボタン 8"     },  8 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 9"      , "ボタン 9"     },  9 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 10"     , "ボタン 10"    }, 10 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 11"     , "ボタン 11"    }, 11 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 12"     , "ボタン 12"    }, 12 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 13"     , "ボタン 13"    }, 13 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 14"     , "ボタン 14"    }, 14 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_internal_t   ,     5, { "Button 15"     , "ボタン 15"    }, 15 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_tree_t          ,    4 , { "Ext Input"     , "拡張入力"     }),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 1"        , "拡張 1"       },   1 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 2"        , "拡張 2"       },   2 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 3"        , "拡張 3"       },   3 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 4"        , "拡張 4"       },   4 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 5"        , "拡張 5"       },   5 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 6"        , "拡張 6"       },   6 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 7"        , "拡張 7"       },   7 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 8"        , "拡張 8"       },   8 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 9"        , "拡張 9"       },   9 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 10"       , "拡張 10"      },  10 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 11"       , "拡張 11"      },  11 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 12"       , "拡張 12"      },  12 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 13"       , "拡張 13"      },  13 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 14"       , "拡張 14"      },  14 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 15"       , "拡張 15"      },  15 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 16"       , "拡張 16"      },  16 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 17"       , "拡張 17"      },  17 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 18"       , "拡張 18"      },  18 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 19"       , "拡張 19"      },  19 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 20"       , "拡張 20"      },  20 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 21"       , "拡張 21"      },  21 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 22"       , "拡張 22"      },  22 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 23"       , "拡張 23"      },  23 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 24"       , "拡張 24"      },  24 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 25"       , "拡張 25"      },  25 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 26"       , "拡張 26"      },  26 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 27"       , "拡張 27"      },  27 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 28"       , "拡張 28"      },  28 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 29"       , "拡張 29"      },  29 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 30"       , "拡張 30"      },  30 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 31"       , "拡張 31"      },  31 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_external_t   ,     5, { " Ext 32"       , "拡張 32"      },  32 - 1, ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_tree_t          ,    4 , { "MIDI Note"     , "MIDI Note"    }),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C#-1" , nullptr },   1 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D -1" , nullptr },   2 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D#-1" , nullptr },   3 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E -1" , nullptr },   4 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F -1" , nullptr },   5 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F#-1" , nullptr },   6 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G -1" , nullptr },   7 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G#-1" , nullptr },   8 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A -1" , nullptr },   9 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A#-1" , nullptr },  10 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B -1" , nullptr },  11 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  0" , nullptr },  12 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 0" , nullptr },  13 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  0" , nullptr },  14 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 0" , nullptr },  15 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  0" , nullptr },  16 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  0" , nullptr },  17 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 0" , nullptr },  18 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  0" , nullptr },  19 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 0" , nullptr },  20 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  0" , nullptr },  21 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 0" , nullptr },  22 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  0" , nullptr },  23 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  1" , nullptr },  24 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 1" , nullptr },  25 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  1" , nullptr },  26 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 1" , nullptr },  27 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  1" , nullptr },  28 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  1" , nullptr },  29 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 1" , nullptr },  30 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  1" , nullptr },  31 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 1" , nullptr },  32 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  1" , nullptr },  33 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 1" , nullptr },  34 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  1" , nullptr },  35 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  2" , nullptr },  36 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 2" , nullptr },  37 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  2" , nullptr },  38 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 2" , nullptr },  39 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  2" , nullptr },  40 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  2" , nullptr },  41 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 2" , nullptr },  42 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  2" , nullptr },  43 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 2" , nullptr },  44 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  2" , nullptr },  45 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 2" , nullptr },  46 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  2" , nullptr },  47 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  3" , nullptr },  48 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 3" , nullptr },  49 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  3" , nullptr },  50 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 3" , nullptr },  51 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  3" , nullptr },  52 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  3" , nullptr },  53 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 3" , nullptr },  54 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  3" , nullptr },  55 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 3" , nullptr },  56 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  3" , nullptr },  57 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 3" , nullptr },  58 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  3" , nullptr },  59 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  4" , nullptr },  60 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 4" , nullptr },  61 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  4" , nullptr },  62 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 4" , nullptr },  63 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  4" , nullptr },  64 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  4" , nullptr },  65 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 4" , nullptr },  66 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  4" , nullptr },  67 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 4" , nullptr },  68 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  4" , nullptr },  69 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 4" , nullptr },  70 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  4" , nullptr },  71 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  5" , nullptr },  72 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 5" , nullptr },  73 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  5" , nullptr },  74 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 5" , nullptr },  75 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  5" , nullptr },  76 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  5" , nullptr },  77 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 5" , nullptr },  78 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  5" , nullptr },  79 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 5" , nullptr },  80 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  5" , nullptr },  81 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 5" , nullptr },  82 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  5" , nullptr },  83 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  6" , nullptr },  84 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 6" , nullptr },  85 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  6" , nullptr },  86 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 6" , nullptr },  87 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  6" , nullptr },  88 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  6" , nullptr },  89 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 6" , nullptr },  90 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  6" , nullptr },  91 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 6" , nullptr },  92 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  6" , nullptr },  93 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 6" , nullptr },  94 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  6" , nullptr },  95 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  7" , nullptr },  96 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 7" , nullptr },  97 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  7" , nullptr },  98 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 7" , nullptr },  99 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  7" , nullptr }, 100 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  7" , nullptr }, 101 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 7" , nullptr }, 102 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  7" , nullptr }, 103 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 7" , nullptr }, 104 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  7" , nullptr }, 105 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 7" , nullptr }, 106 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  7" , nullptr }, 107 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  8" , nullptr }, 108 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 8" , nullptr }, 109 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  8" , nullptr }, 110 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 8" , nullptr }, 111 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  8" , nullptr }, 112 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  8" , nullptr }, 113 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 8" , nullptr }, 114 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  8" , nullptr }, 115 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G# 8" , nullptr }, 116 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A  8" , nullptr }, 117 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  A# 8" , nullptr }, 118 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  B  8" , nullptr }, 119 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C  9" , nullptr }, 120 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  C# 9" , nullptr }, 121 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D  9" , nullptr }, 122 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  D# 9" , nullptr }, 123 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  E  9" , nullptr }, 124 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F  9" , nullptr }, 125 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  F# 9" , nullptr }, 126 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_ca_midinote_t   ,     5, { "  G  9" , nullptr }, 127 , ctrl_map_target_t::ctrl_map_song),
  MENU_BUILDER(mi_tree_t          ,  2   , { "External Device", "外部デバイス" }),
  MENU_BUILDER(mi_portc_midi_t    ,   3  , { "PortC MIDI"     , "ポートC MIDI" }),
  MENU_BUILDER(mi_ble_midi_t      ,   3  , { "BLE MIDI"       , nullptr     }),
  MENU_BUILDER(mi_tree_t          ,   3  , { "USB"            , nullptr}),
  MENU_BUILDER(mi_usb_mode_t      ,    4 , { "USB MODE"       , "USBモード設定" }),
  MENU_BUILDER(mi_usb_power_t     ,    4 , { "Host Power Supply", "ホスト給電設定" }),
  MENU_BUILDER(mi_usb_midi_t      ,    4 , { "USB MIDI"       , nullptr     }),
  MENU_BUILDER(mi_tree_t          ,   3  , { "InstaChord Link", "インスタコードリンク"}),
  MENU_BUILDER(mi_iclink_port_t   ,    4 , { "Connect"        , "接続方法"   }),
  MENU_BUILDER(mi_iclink_dev_t    ,    4 , { "Play Device"    , "演奏デバイス"}),
  MENU_BUILDER(mi_iclink_style_t  ,    4 , { "Play Style"     , "演奏スタイル"}),
  MENU_BUILDER(mi_imu_velocity_t  ,  2   , { "IMU Velocity"   , "IMUベロシティ"}),
  MENU_BUILDER(mi_tree_t          ,  2   , { "Display"        , "表示"        }),
  MENU_BUILDER(mi_lcd_backlight_t ,   3  , { "Backlight"      , "画面の輝度"  }),
  MENU_BUILDER(mi_led_brightness_t,   3  , { "LED Brightness" , "LEDの輝度"   }),
  MENU_BUILDER(mi_detail_view_t   ,   3  , { "Detail View"    , "詳細表示"    }),
  MENU_BUILDER(mi_wave_view_t     ,   3  , { "Wave View"      , "波形表示"    }),
  MENU_BUILDER(mi_language_t      ,  2   , { "Language"       , "言語"        }),
  MENU_BUILDER(mi_tree_t          ,  2   , { "Volume"         , "音量"        }),
  MENU_BUILDER(mi_vol_midi_t      ,   3  , { "MIDI Mastervol" , "MIDIマスター音量"}),
  MENU_BUILDER(mi_vol_adcmic_t    ,   3  , { "ADC MicAmp"     , "ADCマイクアンプ" }),
  MENU_BUILDER(mi_all_reset_t     ,  2   , { "Reset All Settings", "全設定リセット"    }),
  MENU_BUILDER(mi_manual_qr_t     , 1    , { "Manual QR"      , "説明書QR"     }),
  nullptr, // end of menu
};
// const size_t menu_system_size = sizeof(menu_system) / sizeof(menu_system[0]) - 1;

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_PART = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_PART)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_part, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_part[] = {
  MENU_BUILDER(mi_tree_t          ,0  , { "Part"       , "パート"           }),
  MENU_BUILDER(mi_program_t       , 1 , { "Tone"       , "音色"             }),
  MENU_BUILDER(mi_octave_t        , 1 , { "Octave"     , "オクターブ"       }),
  MENU_BUILDER(mi_voicing_t       , 1 , { "Voicing"    , "ボイシング"       }),
  MENU_BUILDER(mi_velocity_t      , 1 , { "Velocity"   , "ベロシティ値"     }),
  MENU_BUILDER(mi_partvolume_t    , 1 , { "Part Volume", "パート音量"       }),
  MENU_BUILDER(mi_loop_length_t   , 1 , { "Loop Length", "ループ長"         }),
  MENU_BUILDER(mi_anchor_step_t   , 1 , { "Anchor Step", "アンカーステップ" }),
  MENU_BUILDER(mi_stroke_speed_t  , 1 , { "Stroke Speed", "ストローク速度"  }),
  MENU_BUILDER(mi_tree_t          , 1 , { "DrumNote"   , "ドラムノート"     }),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch1"     , "ピッチ1"          }, 0),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch2"     , "ピッチ2"          }, 1),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch3"     , "ピッチ3"          }, 2),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch4"     , "ピッチ4"          }, 3),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch5"     , "ピッチ5"          }, 4),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch6"     , "ピッチ6"          }, 5),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch7"     , "ピッチ7"          }, 6),
  MENU_BUILDER(mi_part_clipboard_t, 1 , { "Copy/Paste" , "コピー/ペースト"  }),
  MENU_BUILDER(mi_clear_notes_t   , 1 , { "Clear All Notes", "ノートをクリア"}),
  nullptr, // end of menu
};
// const size_t menu_part_size = sizeof(menu_part) / sizeof(menu_part[0]) - 1;

static constexpr menu_item_ptr menu_seqmode[] = {
  (const mi_sequence_mode_t []){{ def::menu_category_t::menu_seqmode,  0,0  , { "Select Mode"       , "モード選択"        }}},
  nullptr, // end of menu
};

static constexpr menu_item_ptr menu_seqedit[] = {
  (const mi_tree_t          []){{ def::menu_category_t::menu_seqedit,  0,0  , { "Sequence"          , "シーケンス"        }}},
  (const mi_clear_seq_t     []){{ def::menu_category_t::menu_seqedit,  1, 1 , { "Clear After Cursor", "カーソル後をクリア"}}},
  nullptr, // end of menu
};

void menu_control_t::openMenu(def::menu_category_t category)
{
  system_registry.menu_status.reset();

  _menu_array = getMenuArray(category);
  bool hasSubMenu = (_menu_array[1] != nullptr);
  system_registry.menu_status.setSelectIndex(0, hasSubMenu ? 1 : 0);
  system_registry.menu_status.setCurrentLevel(0);
  system_registry.menu_status.setCurrentMenuID(0);
  system_registry.menu_status.setMenuCategory( category );

  _category = category;

  // system_registry.runtime_info.setPlayMode( def::playmode::playmode_t::menu_mode );
  system_registry.runtime_info.setMenuVisible( true );
  if (!hasSubMenu) {
    _menu_array[0]->enter();
  }
}

bool menu_control_t::enter(void)
{
  auto current_level = system_registry.menu_status.getCurrentLevel();
  auto select_index = system_registry.menu_status.getSelectIndex(current_level);
  auto current_menu_id = system_registry.menu_status.getCurrentMenuID();

  if (current_menu_id == select_index) {
    return _menu_array[select_index]->execute();
  }
  return _menu_array[select_index]->enter();
}

bool menu_control_t::exit(void)
{
  auto current_index = system_registry.menu_status.getCurrentMenuID();
  return _menu_array[current_index]->exit();
}

bool menu_control_t::inputNumber(uint8_t number)
{
  auto current_index = system_registry.menu_status.getCurrentMenuID();
  return _menu_array[current_index]->inputNumber(number);
}

bool menu_control_t::inputUpDown(int updown)
{
  auto current_index = system_registry.menu_status.getCurrentMenuID();

  return _menu_array[current_index]->inputUpDown(updown);
}

int menu_control_t::getChildrenMenuIDList(std::vector<uint16_t>* index_list, uint16_t parent_index)
{
  return getSubMenuIndexList(index_list, _menu_array, parent_index);
}

#if defined ( M5UNIFIED_PC_BUILD )
// メニューの定義部に間違いがないか確認する関数
// PCビルド時のみ有効
static bool menu_id_check(const menu_item_ptr_array &menu, def::menu_category_t cat)
{
  for (size_t i = 0; menu[i] != nullptr; ++i) {
    if (menu[i]->getCategory() != cat) {
      printf("menu_id_check error: category mismatch at index %zu\n", i);
      fflush(stdout);
      return false;
    }
    if (menu[i]->getMenuID() != i) {
      printf("menu_id_check error: menu ID mismatch at index %zu\n", i);
      fflush(stdout);
      return false;
    }
  }
  return true;
}
#endif

static menu_item_ptr_array getMenuArray(def::menu_category_t category)
{
#if defined ( M5UNIFIED_PC_BUILD )
  assert(menu_id_check(menu_system , def::menu_category_t::menu_system  ) && "menu_system definition error");
  assert(menu_id_check(menu_part   , def::menu_category_t::menu_part    ) && "menu_part definition error");
  assert(menu_id_check(menu_seqmode, def::menu_category_t::menu_seqmode ) && "menu_seqmode definition error");
  assert(menu_id_check(menu_seqedit, def::menu_category_t::menu_seqedit ) && "menu_seqedit definition error");
#endif

  switch (category) {
  default:
  case def::menu_category_t::menu_system:
    return menu_system;
  case def::menu_category_t::menu_part:
    return menu_part;
  case def::menu_category_t::menu_seqmode:
    return menu_seqmode;
  case def::menu_category_t::menu_seqedit:
    return menu_seqedit;
  }
  return nullptr;
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
