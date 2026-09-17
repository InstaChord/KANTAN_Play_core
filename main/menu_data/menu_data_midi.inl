// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

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
    return system_registry->song_data.song_info.getTempo();
    // return system_registry->current_slot->slot_info.getTempo();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry->song_data.song_info.setTempo(value);
/*
    system_registry->current_slot->slot_info.setTempo(value);
    for (int i = 0; i < def::app::max_slot; ++i) {
      system_registry->song_data.slot[i].slot_info.setTempo(value);
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
    return system_registry->song_data.song_info.getSwing() / 10;
    // return system_registry->current_slot->slot_info.getSwing();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry->song_data.song_info.setSwing(value * 10);
/*
    system_registry->current_slot->slot_info.setSwing(value);
    for (int i = 0; i < def::app::max_slot; ++i) {
      system_registry->song_data.slot[i].slot_info.setSwing(value);
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
    int part_index = system_registry->chord_play.getEditTargetPart();
    return system_registry->current_slot->chord_part_drum[part_index].getDrumNoteNumber(_pitch_number);
  }
  bool queuePreviewNote(int note) const
  {
    if (!system_registry->user_setting.getGuideSound()) { return false; }
    if (note <= 0) { return false; }
    uint8_t param = def::command::sound_effect_t::drum_note_preview_flag | (note & 0x7F);
    system_registry->player_command.addQueue({ def::command::sound_effect, (int)param });
    return true;
  }
  bool onFocus(void) const override
  {
    return queuePreviewNote(getValue());
  }
  bool inputUpDown(int updown) const override
  {
    int current = (_selecting_value >= getMinValue()) ? _selecting_value : getValue();
    bool result = setSelectingValue(current + updown);
    queuePreviewNote(_selecting_value);
    return result;
  }
  bool inputNumber(uint8_t number) const override
  {
    bool result = mi_selector_t::inputNumber(number);
    queuePreviewNote(_selecting_value);
    return result;
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    int part_index = system_registry->chord_play.getEditTargetPart();
    system_registry->current_slot->chord_part_drum[part_index].setDrumNoteNumber(_pitch_number, value);
    queuePreviewNote(value);
    return true;
  }
};

struct mi_ctrl_assign_t : public mi_normal_t {
  constexpr mi_ctrl_assign_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const def::ctrl_assign::control_assignment_t table[], uint16_t size, def::mapping::target_t map_target)
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

  bool exit(void) const override
  {
    system_registry->updateControlMapping();
    return mi_normal_t::exit();
  }

protected:
  const def::ctrl_assign::control_assignment_t* _table;
  const uint16_t _size;
  const def::mapping::target_t _map_target;
};

struct mi_cmap_copy_t : public mi_cancel_exec_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Copy",   "コピー"   },
  }};

public:
  constexpr mi_cmap_copy_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, def::mapping::target_t map_target )
  : mi_cancel_exec_t { cate, menu_id, level, title, &name_array }
  , _map_target { map_target }
  {
  }

  const char* getValueText(void) const override { return "..."; }
  int getValue(void) const override { return getMinValue(); }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value == 1) {
      auto dst_mapping = &system_registry->control_mapping[(int)_map_target];
      auto src_mapping = &system_registry->control_mapping[1-(int)_map_target];
      dst_mapping->internal.assign(src_mapping->internal);
      dst_mapping->external.assign(src_mapping->external);
      dst_mapping->midinote.assign(src_mapping->midinote);

      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_COPY_CONTROL_MAPPING);
      system_registry->updateControlMapping();
    }
    return true;
  }

  void onExecute(void) const override { queueExecuteSound(54); } // Tambourine (Copy)

protected:
  const def::mapping::target_t _map_target;
};

struct mi_cmap_reset_default_t : public mi_cancel_exec_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Reset",  "リセット"  },
  }};

public:
  constexpr mi_cmap_reset_default_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_cancel_exec_t { cate, menu_id, level, title, &name_array }
  {
  }

  const char* getValueText(void) const override { return "..."; }
  int getValue(void) const override { return getMinValue(); }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value == 1) {
      // Mapping 1 (Device) を工場出荷時の既定値に戻す
      system_registry->resetDeviceMapping();
      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_RESET_CONTROL_MAPPING);
      system_registry->updateControlMapping();
    }
    return true;
  }
};

struct mi_cmap_delete_t : public mi_cancel_exec_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Delete", "削除"   },
  }};

public:
  constexpr mi_cmap_delete_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, def::mapping::target_t map_target )
  : mi_cancel_exec_t { cate, menu_id, level, title, &name_array }
  , _map_target { map_target }
  {
  }

  const char* getValueText(void) const override { return "..."; }
  int getValue(void) const override { return getMinValue(); }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    if (value == 1) {
      auto dst_mapping = &system_registry->control_mapping[(int)_map_target];
      dst_mapping->reset();
      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_DELETE_CONTROL_MAPPING);
    }
    return true;
  }

protected:
  const def::mapping::target_t _map_target;
};

// control assignment for internal
struct mi_ca_internal_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_internal_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, def::mapping::target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::playbutton_table, def::ctrl_assign::playbutton_play_size, map_target }
  , _button_index { button_index } {}

  virtual system_registry_t::reg_command_mapping_t* target(void) const { return &system_registry->control_mapping[(int)_map_target].internal; }

  int getValue(void) const override
  {
    auto cmd = target()->getCommandParamArray(_button_index);
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
    target()->setCommandParamArray(_button_index, _table[value].command);
    return true;
  }

protected:
  const uint8_t _button_index;
};

struct mi_ca_slotbutton_t : public mi_ca_internal_t {
public:
  constexpr mi_ca_slotbutton_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, def::mapping::target_t map_target)
  : mi_ca_internal_t { cate, menu_id, level, title, button_index, map_target } {}

  // スロットボタン専用レジスタを参照
  system_registry_t::reg_command_mapping_t* target(void) const override {
    return &system_registry->control_mapping[(int)_map_target].slot;
  }

  // コード系 + Slot±1/--- + Jump Slot 1〜numSlot の全エントリを表示
  size_t getSelectorCount(void) const override {
    return (size_t)def::ctrl_assign::playbutton_slot_start_index + 3
         + system_registry->song_data.song_info.getNumSlot();
  }
  int getMinValue(void) const override { return 0; }
  int getMaxValue(void) const override { return (int)getSelectorCount() - 1; }

  const char* getSelectorText(size_t index) const override { return _table[index].text.get(); }
  const char* getValueText(void) const override { return _table[getValue()].text.get(); }

  // getValue/setValue はテーブルの絶対インデックスで動作する
  int getValue(void) const override {
    auto cmd = target()->getCommandParamArray(_button_index);
    int index = def::ctrl_assign::get_index_from_command(_table, cmd);
    if (index < 0) { index = (int)def::ctrl_assign::playbutton_slot_ud_index; }  // デフォルト: Slot -1
    return index;
  }
  bool setValue(int value) const override {
    target()->setCommandParamArray(_button_index, _table[value].command);
    return mi_normal_t::setValue(value);
  }
};

struct mi_ca_external_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_external_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, def::mapping::target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::external_table, sizeof(def::ctrl_assign::external_table) / sizeof(def::ctrl_assign::external_table[0])-1, map_target }
  , _button_index { button_index } {}

  system_registry_t::reg_command_mapping_t* target(void) const { return &system_registry->control_mapping[(int)_map_target].external; }

  int getValue(void) const override
  {
    auto cmd = target()->getCommandParamArray(_button_index);
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
    target()->setCommandParamArray(_button_index, _table[value].command);
    return true;
  }

protected:
  const uint8_t _button_index;
};

struct mi_ca_midinote_t : public mi_ctrl_assign_t {
public:
  constexpr mi_ca_midinote_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, uint8_t button_index, def::mapping::target_t map_target)
  : mi_ctrl_assign_t { cate, menu_id, level, title, def::ctrl_assign::external_table, sizeof(def::ctrl_assign::external_table) / sizeof(def::ctrl_assign::external_table[0])-1, map_target }
  , _button_index { button_index } {}

  system_registry_t::reg_command_mapping_t* target(void) const { return &system_registry->control_mapping[(int)_map_target].midinote; }

  int getValue(void) const override
  {
    auto cmd = target()->getCommandParamArray(_button_index);
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
    target()->setCommandParamArray(_button_index, _table[value].command);
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

// Sampler's stable External Device model: one main input owns the radio/USB
// role at a time. A clean restart avoids rebuilding ESP USB Host, TinyUSB
// Device or BLE while another role still owns controller resources.
struct mi_external_input_source_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 4, (const localize_text_t[]){
    { "Off",                 "オフ" },
    { "USB MIDI Controller", "USB MIDIコントローラー" },
    { "USB MIDI Computer",   "USB MIDIコンピューター" },
    { "BLE MIDI",            nullptr },
  }};
  mutable restart_confirmation_state_t _confirmation;

  const char* pendingText(void) const {
    switch (static_cast<def::command::external_input_source_t>(_confirmation.pending)) {
    case def::command::external_input_usb_midi_host: return localize_text_t{"USB MIDI Controller", "USB MIDIコントローラー"}.get();
    case def::command::external_input_usb_midi_device: return localize_text_t{"USB MIDI Computer", "USB MIDIコンピューター"}.get();
    case def::command::external_input_ble_midi: return "BLE MIDI";
    default: return localize_text_t{"Off", "オフ"}.get();
    }
  }

public:
  constexpr mi_external_input_source_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  int getValue(void) const override {
    if (_confirmation.stage == restart_confirmation_state_t::stage_t::confirm) { return getMinValue() - 1; }
    return getMinValue() + system_registry->midi_port_setting.getExternalInputSource();
  }
  size_t getSelectorCount(void) const override {
    return _confirmation.stage == restart_confirmation_state_t::stage_t::confirm ? 3 : name_array.size();
  }
  const char* getTitleText(void) const override {
    return _confirmation.stage == restart_confirmation_state_t::stage_t::confirm
        ? localize_text_t{"Restart Required", "再起動が必要です"}.get()
        : mi_selector_t::getTitleText();
  }
  bool isDynamic(void) const override { return true; }
  const char* getSelectorText(size_t index) const override {
    if (_confirmation.stage == restart_confirmation_state_t::stage_t::confirm) {
      if (index == 0) {
        _title_text_buffer = std::string(localize_text_t{"Change to: ", "変更先: "}.get()) + pendingText();
        return _title_text_buffer.c_str();
      }
      if (index == 1) { return localize_text_t{"Cancel", "キャンセル"}.get(); }
      return localize_text_t{"Apply & Restart", "適用して再起動"}.get();
    }
    if (_save_failed && index == size_t(_selecting_value - getMinValue())) {
      return localize_text_t{"Save failed / Retry", "保存失敗 / 再試行"}.get();
    }
    return mi_selector_t::getSelectorText(index);
  }
  bool enter(void) const override {
    _confirmation.cancel();
    _save_failed = false;
    return mi_selector_t::enter();
  }
  mutable bool _save_failed = false;

  bool execute(void) const override {
    if (_confirmation.stage == restart_confirmation_state_t::stage_t::source) {
      const auto next = static_cast<def::command::external_input_source_t>(_selecting_value - getMinValue());
      if (!_confirmation.request(
            uint8_t(system_registry->midi_port_setting.getExternalInputSource()), uint8_t(next),
            task_midi_t::isBLESuspendedForWiFi())) { return true; }
      _selecting_value = getMinValue() + restart_confirmation_state_t::safe_default_row;
      return false;
    }
    const int action = _selecting_value - getMinValue();
    const auto decision = _confirmation.decide(action);
    if (decision == restart_confirmation_state_t::decision_t::none) { return false; }
    if (decision == restart_confirmation_state_t::decision_t::cancelled) {
      _selecting_value = getMinValue() + system_registry->midi_port_setting.getExternalInputSource();
      return false;
    }
    _save_failed = !sequencer_external::changeSource(
        static_cast<def::command::external_input_source_t>(_confirmation.pending));
    if (_save_failed) { _confirmation.cancel(); }
    return false;
  }
  bool exit(void) const override {
    if (_confirmation.stage == restart_confirmation_state_t::stage_t::confirm) {
      _confirmation.cancel();
      _selecting_value = getMinValue() + system_registry->midi_port_setting.getExternalInputSource();
      return true;
    }
    return mi_selector_t::exit();
  }
};

struct mi_ble_connection_t : public mi_tree_t {
  using mi_tree_t::mi_tree_t;
  bool isDynamic(void) const override { return true; }
  bool isVisible(void) const override {
    return system_registry->midi_port_setting.getExternalInputSource() == def::command::external_input_ble_midi;
  }
  const char* getValueText(void) const override {
    _title_text_buffer = sequencer_external::statusText(); return _title_text_buffer.c_str();
  }
};

struct mi_ble_scan_t : public mi_normal_t {
  using mi_normal_t::mi_normal_t;
  bool isDynamic(void) const override { return true; }
  size_t getSelectorCount(void) const override { return sequencer_external::rowCount(); }
  int getSelectingValue(void) const override {
    // Async completion may shrink a 12-device list to one status row.
    return std::max(getMinValue(), std::min(_selecting_value, getMaxValue()));
  }
  const char* getSelectorText(size_t index) const override {
    _title_text_buffer = sequencer_external::rowText(index); return _title_text_buffer.c_str();
  }
  bool enter(void) const override {
    sequencer_external::beginScan(); return mi_normal_t::enter();
  }
  bool execute(void) const override {
    _selecting_value = sequencer_external::select(_selecting_value - getMinValue()) + getMinValue();
    return false;
  }
  bool exit(void) const override {
    if (!sequencer_external::back()) { _selecting_value = getMinValue(); return true; }
    return mi_normal_t::exit();
  }
};

struct mi_ble_action_t : public mi_cancel_exec_t {
  constexpr mi_ble_action_t(def::menu_category_t cate, uint16_t id, uint8_t level, const localize_text_t& title, bool reset)
    : mi_cancel_exec_t{cate, id, level, title, &names}, _reset(reset) {}
  static constexpr const localize_text_array_t names = {2, (const localize_text_t[]){
    {"Cancel", "キャンセル"}, {"Execute", "実行"}
  }};
  const bool _reset;
  bool isDynamic(void) const override { return true; }
  const char* getSelectorText(size_t index) const override {
    if (index == 0) { return names.at(0)->get(); }
    if (_reset && !_done) { return localize_text_t{"Reset & Restart", "接続をリセットして再起動"}.get(); }
    _title_text_buffer = sequencer_external::rowText(0);
    // The action label remains descriptive until the operation succeeds.
    return _done ? _title_text_buffer.c_str() : localize_text_t{"Forget Device", "接続先を解除"}.get();
  }
  bool enter(void) const override { _done = false; return mi_cancel_exec_t::enter(); }
  bool execute(void) const override {
    if (_selecting_value == getMinValue()) { return exit(); }
    return mi_cancel_exec_t::execute();
  }
  bool setValue(int value) const override {
    if (value == getMinValue()) { return true; }
    if (value != getMinValue() + 1) { return false; }
    _done = true;
    return _reset ? sequencer_external::restartConnection() : sequencer_external::forgetDevice();
  }
  mutable bool _done = false;
};

struct mi_device_info_t : public mi_normal_t {
  using mi_normal_t::mi_normal_t;
  bool isDynamic(void) const override { return true; }
  size_t getSelectorCount(void) const override { return 6; }
  const char* getSelectorText(size_t index) const override {
    _title_text_buffer = sequencer_external::deviceInfo(index); return _title_text_buffer.c_str();
  }
  bool execute(void) const override { return false; }
};

// Sequencer has two mapping layers (device/song), unlike Sampler's Learn list.
// Offer the shared entry point without losing its existing mapping semantics.
struct mi_input_assign_link_t : public mi_normal_t {
  using mi_normal_t::mi_normal_t;
  size_t getSelectorCount(void) const override { return 1; }
  const char* getSelectorText(size_t) const override { return localize_text_t{"Open Control Mapping", "操作マッピングを開く"}.get(); }
  bool enter(void) const override {
    auto array = getMenuArray(_category);
    for (size_t i = 1; array[i]; ++i) {
      if (strcmp(array[i]->getTitleText(), localize_text_t{"Control Mapping", "操作マッピング"}.get()) == 0) {
        auto parent = getParentIndex(array, i);
        array[parent]->enter(); return array[i]->enter();
      }
    }
    return false;
  }
};

struct mi_portc_midi_t : public mi_midi_selector_t {
  constexpr mi_portc_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->midi_port_setting.getPortCMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setPortCMIDI( static_cast<def::command::ex_midi_mode_t>(value));
    return true;
  }
};

struct mi_ble_midi_t : public mi_midi_selector_t {
  constexpr mi_ble_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->midi_port_setting.getBLEMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setBLEMIDI( static_cast<def::command::ex_midi_mode_t>(value));
    return true;
  }
};

struct mi_usb_midi_t : public mi_midi_selector_t {
  constexpr mi_usb_midi_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_midi_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->midi_port_setting.getUSBMIDI();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setUSBMIDI( static_cast<def::command::ex_midi_mode_t>(value));
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
    return getMinValue() + system_registry->midi_port_setting.getUSBMode();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setUSBMode( static_cast<def::command::usb_mode_t>(value));
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
    return getMinValue() + system_registry->midi_port_setting.getUSBPowerEnabled();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setUSBPowerEnabled( static_cast<bool>(value));
    return true;
  }
  void onExecute(void) const override
  {
    bool is_on = (_selecting_value > getMinValue()); // 1=OFF, 2=ON
    queueExecuteSound(is_on ? 56 : 35); // ON: Cowbell, OFF: Acoustic Bass Drum
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
  int getValue(void) const override {
    return getMinValue() + system_registry->midi_port_setting.getInstaChordLinkPort();
  }
  const char* getSelectorText(size_t index) const override {
    auto source = system_registry->midi_port_setting.getExternalInputSource();
    if (index == 1 && source != def::command::external_input_ble_midi) {
      return localize_text_t{"Select BLE MIDI first", "入力ソースをBLE MIDIにして下さい"}.get();
    }
    if (index == 1 && task_midi_t::isBLESuspendedForWiFi()) {
      return localize_text_t{"Restart to use BLE", "BLEを使うには再起動して下さい"}.get();
    }
    if (index == 2 && source != def::command::external_input_usb_midi_host) {
      return localize_text_t{"Select USB MIDI Controller first", "入力ソースをUSBコントローラーにして下さい"}.get();
    }
    return mi_selector_t::getSelectorText(index);
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    const auto source = system_registry->midi_port_setting.getExternalInputSource();
    if (value == def::command::iclp_ble && (source != def::command::external_input_ble_midi
        || task_midi_t::isBLESuspendedForWiFi())) { return false; }
    if (value == def::command::iclp_usb && source != def::command::external_input_usb_midi_host) { return false; }
    system_registry->midi_port_setting.setInstaChordLinkPort( static_cast<def::command::instachord_link_port_t>(value));
    return true;
  }
};

struct mi_iclink_dev_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { def::app::firmware_display_name, def::app::firmware_display_name },
    { "InstaChord",  "インスタコード"},
  }};

public:
  constexpr mi_iclink_dev_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->midi_port_setting.getInstaChordLinkDev();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setInstaChordLinkDev( static_cast<def::command::instachord_link_dev_t>(value));
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
    return getMinValue() + system_registry->midi_port_setting.getInstaChordLinkStyle();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->midi_port_setting.setInstaChordLinkStyle( static_cast<def::command::instachord_link_style_t>(value));
    return true;
  }
};

struct mi_song_part_operation_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Auto",   "自動" },
    { "Manual", "手動" },
  }};

public:
  constexpr mi_song_part_operation_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->runtime_info.getSongPartOperation();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->runtime_info.setSongPartOperation((def::play::song_part_operation_t)value);
    return true;
  }
};

struct mi_back_to_freeplay_t : public mi_normal_t {
  constexpr mi_back_to_freeplay_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  menu_item_type_t getType(void) const override { return menu_item_type_t::mt_tree; }

  bool enter(void) const override {
    // フリープレイに切り替える
    system_registry->operator_command.addQueue({ def::command::play_mode_set, def::playmode::pm_free_play });
    // メニューを閉じる
    system_registry->operator_command.addQueue({ def::command::menu_function, def::command::mf_exit });
    return false;
  }
};
