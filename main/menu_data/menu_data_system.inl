// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

struct mi_selector_t : public mi_normal_t {
  constexpr mi_selector_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const text_array_t* names)
  : mi_normal_t { cate, menu_id, level, title }
  , _names { names }
  {}

  const char* getSelectorText(size_t index) const override { return _names->at(index)->get(); }
  size_t getSelectorCount(void) const override { return _names->size(); }

  const char* getValueText(void) const override
  {
    size_t idx = getValue() - getMinValue();
    if (idx >= _names->size()) { idx = _names->size() - 1; }
    return _names->at(idx)->get();
  }

protected:
  const text_array_t* _names;
};

// Cancel/実行 の2択メニュー共通基底。1番目=Cancel、2番目以降=実行
struct mi_open_main_menu_t : public mi_normal_t {
  constexpr mi_open_main_menu_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  menu_item_type_t getType(void) const override { return menu_item_type_t::mt_tree; }
  size_t getSelectorCount(void) const override { return 0; }

  bool enter(void) const override {
    system_registry->operator_command.addQueue({ def::command::menu_open, def::menu_category_t::menu_system });
    return false;
  }
};

struct mi_cancel_exec_t : public mi_selector_t {
  constexpr mi_cancel_exec_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title, const text_array_t* names )
  : mi_selector_t { cate, menu_id, level, title, names } {}

  void onExecute(void) const override
  {
    bool is_cancel = (_selecting_value <= getMinValue());
    queueExecuteSound(is_cancel ? 69 : 51); // Cancel: Cabasa, 実行: Ride Cymbal
  }
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
    return getMinValue() + static_cast<uint8_t>(system_registry->user_setting.getLanguage());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto lang = static_cast<def::lang::language_t>(value - getMinValue());
    system_registry->user_setting.setLanguage(lang);
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
    return getMinValue() + system_registry->user_setting.getImuVelocityLevel();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setImuVelocityLevel(value);
    return true;
  }
};

struct mi_ext_midi_velocity_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Disabled", "無効" },
    { "Enabled",  "有効"},
  }};

public:
  constexpr mi_ext_midi_velocity_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->user_setting.getExtMidiVelocity();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setExtMidiVelocity(value);
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
    return getMinValue() + system_registry->user_setting.getDisplayBrightness();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setDisplayBrightness(value);
    return true;
  }
};

struct mi_led_brightness_t : public mi_brightness_t {
public:
  constexpr mi_led_brightness_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_brightness_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->user_setting.getLedBrightness();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setLedBrightness(value);
    system_registry->rgbled_control.refresh();
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
    return system_registry->user_setting.getMIDIMasterVolume();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry->user_setting.setMIDIMasterVolume(value);
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
    return system_registry->user_setting.getADCMicAmp();
  }
  bool setValue(int value) const override
  {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry->user_setting.setADCMicAmp(value);
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
    return getMinValue() + system_registry->user_setting.getGuiDetailMode();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setGuiDetailMode(value);
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

  void onExecute(void) const override
  {
    bool is_on = (_selecting_value > getMinValue()); // 1=OFF, 2=ON
    queueExecuteSound(is_on ? 56 : 35); // ON: Cowbell, OFF: Acoustic Bass Drum
  }
};

struct mi_wave_view_t : public mi_enable_selector_t {
public:
  constexpr mi_wave_view_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry->user_setting.getGuiWaveView());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->user_setting.setGuiWaveView(value);
    return true;
  }
};


struct mi_guide_sound_t : public mi_enable_selector_t {
public:
  constexpr mi_guide_sound_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry->user_setting.getGuideSound());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    system_registry->user_setting.setGuideSound(value - getMinValue());
    return true;
  }
  void onExecute(void) const override
  {
    bool is_on = (_selecting_value > getMinValue()); // 1=OFF, 2=ON
    queueExecuteSound(is_on ? 56 : 35, true); // ON: Cowbell, OFF: Acoustic Bass Drum
  }
};

struct mi_webserver_t : public mi_enable_selector_t {
public:
  constexpr mi_webserver_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}

  int getValue(void) const override
  {
    return getMinValue() + static_cast<uint8_t>(system_registry->wifi_control.getWebServerMode());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    auto mode = static_cast<def::command::webserver_mode_t>(value);
    system_registry->wifi_control.setWebServerMode(mode);
    if (mode == def::command::webserver_mode_t::ws_disable) {
      system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
      system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
    }
    return true;
  }
};

struct mi_otaupdate_t : public mi_selector_t {
protected:
  static constexpr const localize_text_array_t name_array = { 3, (const localize_text_t[]){
    { def::app::firmware_display_name,      def::app::firmware_display_name      },
    { def::app::firmware_beta_display_name, def::app::firmware_beta_display_name },
    { "Developer",        "Developer"        },
  }};

  static bool isOtaRunning(uint8_t value)
  {
    return 0 < value && value <= 100;
  }

public:
  bool isDynamic(void) const override { return true; }
  const char* getSelectorText(size_t index) const override {
    const char* status = sequencer_external::wifiStatusText();
    return status[0] && int(index) == _selecting_value - getMinValue()
        ? status : mi_selector_t::getSelectorText(index);
  }
  constexpr mi_otaupdate_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_selector_t { cate, menu_id, level, title, &name_array } {}

  size_t getSelectorCount(void) const override
  {
    return system_registry->runtime_info.getDeveloperMode() ? 3 : 2;
  }

  bool enter(void) const override
  {
    system_registry->runtime_info.setWiFiOtaProgress(0);
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_progress);
    return mi_selector_t::enter();
  }

  bool exit(void) const override
  {
    auto v = system_registry->runtime_info.getWiFiOtaProgress();
    if (isOtaRunning(v)) {
      // OTAの途中でメニューを閉じることはできない
      return true;
    }
    if (system_registry->wifi_control.getOperation() == def::command::wifi_operation_t::wfop_ota_progress) {
      system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    }
    system_registry->runtime_info.setWiFiOtaProgress(0);
    return mi_selector_t::exit();
  }

  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    auto channel = static_cast<def::command::firmware_channel_t>(value - getMinValue());
    if (channel == def::command::firmware_channel_t::developer
     && !system_registry->runtime_info.getDeveloperMode()) {
      return false;
    }
    system_registry->runtime_info.setFirmwareChannel(channel);

    // OTAを実施する際にオートプレイは無効にする
    system_registry->runtime_info.setAutoplayState(def::play::auto_play_state_t::auto_play_none);

    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connecting);
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_begin);
    return true;
  }

  int getValue(void) const override
  {
    auto channel = system_registry->runtime_info.getFirmwareChannel();
    if (channel == def::command::firmware_channel_t::developer
     && !system_registry->runtime_info.getDeveloperMode()) {
      channel = def::command::firmware_channel_t::stable;
    }
    return getMinValue() + static_cast<uint8_t>(channel);
  }

  const char* getValueText(void) const override {
    static char buf[32];
    auto v = system_registry->runtime_info.getWiFiOtaProgress();
    switch (v) {
    case 0:
      snprintf(buf, sizeof(buf), "v%d.%d.%d", (int)def::app::app_version_major, (int)def::app::app_version_minor, (int)def::app::app_version_patch);
      break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_connecting:         snprintf(buf, sizeof(buf), "Connecting"); break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_connection_error:   snprintf(buf, sizeof(buf), "Error");      break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_available:   snprintf(buf, sizeof(buf), "Download");   break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_already_up_to_date: snprintf(buf, sizeof(buf), "Latest");     break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_failed:      snprintf(buf, sizeof(buf), "Failed");     break;
    case (uint8_t)def::command::wifi_ota_state_t::ota_update_done:        snprintf(buf, sizeof(buf), "Done");       break;
    default:
      snprintf(buf, sizeof(buf), "%d%%", v);
      break;
    }
    return buf;
  }

  std::string getString(void) const override {
    char buf[32];
    std::string result;
    auto v = system_registry->runtime_info.getWiFiOtaProgress();
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
    return mi_selector_t::getSelectingValue();
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

  bool isDynamic(void) const override { return true; }
  const char* getValueText(void) const override { return sequencer_external::wifiStatusText(); }
  const char* getSelectorText(size_t index) const override {
    const char* status = sequencer_external::wifiStatusText();
    return status[0] && int(index) == _selecting_value - getMinValue()
        ? status : mi_selector_t::getSelectorText(index);
  }

  int getSelectingValue(void) const override
  {
    auto qrtype = def::qrcode_type_t::QRCODE_NONE;

    auto result = mi_selector_t::getSelectingValue();

    if (result == 1) {
      if (system_registry->wifi_control.getOperation() == def::command::wifi_operation_t::wfop_setup_ap
          && system_registry->runtime_info.getWiFiAPInfo() != def::command::wifi_ap_info_t::wai_off
          && sequencer_external::getWiFiStatus() == sequencer_external::wifi_status_t::ready) {
        qrtype = system_registry->runtime_info.getWiFiStationCount()
                    ? def::qrcode_type_t::QRCODE_URL_WIFI_SETUP
                    : def::qrcode_type_t::QRCODE_AP_SSID;
      }
    }
    if (system_registry->popup_qr.getQRCodeType() != qrtype) {
      system_registry->popup_qr.setQRCodeType(qrtype);
      if (result == 1 && qrtype == def::qrcode_type_t::QRCODE_NONE) {
        exit();
      }
    }
    return result;
  }
  bool execute(void) const override
  {
    if (getSelectingValue() == 1) {
      system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_setup_ap);
    } else {
      system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_setup_wps);
    }
    queueExecuteSound(75); // Claves
    return false;
  }

  bool exit(void) const override
  {
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};

struct mi_song_autorepeat_t : public mi_enable_selector_t {
public:
  constexpr mi_song_autorepeat_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_enable_selector_t { cate, menu_id, level, title } {}
  int getValue(void) const override
  {
    return getMinValue() + system_registry->runtime_info.getSongAutoRepeat();
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->runtime_info.setSongAutoRepeat(value);
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
    return getMinValue() + static_cast<uint8_t>(system_registry->wifi_control.getMode());
  }
  bool setValue(int value) const override
  {
    if (mi_selector_t::setValue(value) == false) { return false; }
    value -= getMinValue();
    system_registry->wifi_control.setMode(static_cast<def::command::wifi_mode_t>(value));
    return true;
  }
};
//*/

struct mi_system_info_t : public mi_normal_t {
  constexpr mi_system_info_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  const char* getValueText(void) const override {
    static char buf[16];
    snprintf(buf, sizeof(buf), "v%d.%d.%d", (int)def::app::app_version_major, (int)def::app::app_version_minor, (int)def::app::app_version_patch);
    return buf;
  }
  static constexpr const localize_text_t _selector_text = { "Show QR code", "QRコードを表示" };
  const char* getSelectorText(size_t index) const override { return _selector_text.get(); }

  size_t getSelectorCount(void) const override { return 1; }

  bool execute(void) const override
  {
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_URL_SYSTEM_INFO);
    queueExecuteSound(75); // Claves
    return false;
  }

  bool exit(void) const override
  {
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};

struct mi_manual_qr_t : public mi_normal_t {
  constexpr mi_manual_qr_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  const char* getValueText(void) const override { return "..."; }
  static constexpr const localize_text_t _selector_text_manual = { "Show QR code", "QRコードを表示" };
  const char* getSelectorText(size_t index) const override { return _selector_text_manual.get(); }

  size_t getSelectorCount(void) const override { return 1; }

  bool execute(void) const override
  {
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_URL_MANUAL);
    queueExecuteSound(75); // Claves
    return false;
  }

  bool exit(void) const override
  {
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};

struct mi_web_filer_t : public mi_normal_t {
  constexpr mi_web_filer_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  const char* getValueText(void) const override { return "..."; }
  static constexpr const localize_text_t _selector_text = { "Open Song Manager", "ソング管理を開く" };
  bool isDynamic(void) const override { return true; }
  const char* getSelectorText(size_t index) const override {
    const char* status = sequencer_external::wifiStatusText();
    return status[0] ? status : _selector_text.get();
  }

  size_t getSelectorCount(void) const override { return 1; }

  bool execute(void) const override
  {
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_web_filer);
    queueExecuteSound(75); // Claves
    return false;
  }

  bool exit(void) const override
  {
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
    system_registry->popup_qr.setQRCodeType(def::qrcode_type_t::QRCODE_NONE);
    return mi_normal_t::exit();
  }
};

struct mi_num_slot_t : public mi_normal_t {
public:
  constexpr mi_num_slot_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_normal_t { cate, menu_id, level, title } {}

  int getMinValue(void) const override { return def::app::min_active_slot; }
  int getMaxValue(void) const override { return def::app::max_slot; }

  int getValue(void) const override {
    return system_registry->song_data.song_info.getNumSlot();
  }

  // 選択中の値に対するメモリ使用量を示す文字列を返す
  const char* getSelectorText(size_t index) const override {
    static char buf[24];
    int n = (int)(index + getMinValue());
    uint32_t kb = ((uint32_t)n * def::app::bytes_per_slot * 2 + 1023) / 1024;
    const char* warn = "";
    uint32_t total_bytes = (uint32_t)n * def::app::bytes_per_slot * 2;
    if (total_bytes >= def::app::slot_memory_limit_bytes) { warn = "!!"; }
    else if (total_bytes >= def::app::slot_memory_warn_bytes) { warn = "!"; }
    snprintf(buf, sizeof(buf), "%d  (%dKB%s)", n, (int)kb, warn);
    return buf;
  }

  const char* getValueText(void) const override {
    static char buf[8];
    snprintf(buf, sizeof(buf), "%d", getValue());
    return buf;
  }

  bool setValue(int value) const override {
    if (mi_normal_t::setValue(value) == false) { return false; }
    system_registry->song_data.song_info.setNumSlot((uint8_t)value);
    // 現在スロットが範囲外なら先頭スロットに戻す
    auto cur = system_registry->runtime_info.getPlaySlot();
    if (cur >= (uint8_t)value) {
      system_registry->runtime_info.setPlaySlot(0);
    }
    // メモリ警告
    uint32_t total_bytes = (uint32_t)value * def::app::bytes_per_slot * 2;
    if (total_bytes >= def::app::slot_memory_limit_bytes) {
      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_SLOT_MEMORY_HIGH);
    } else if (total_bytes >= def::app::slot_memory_warn_bytes) {
      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_SLOT_MEMORY_WARN);
    }
    // ソングデータとして保存（system設定ではなくソング固有）
    system_registry->runtime_info.setSongModified(true);
    return true;
  }
};

struct mi_all_reset_t : public mi_cancel_exec_t {
protected:
  static constexpr const localize_text_array_t name_array = { 2, (const localize_text_t[]){
    { "Cancel", "キャンセル" },
    { "Reset",  "リセット"   },
  }};

public:
  constexpr mi_all_reset_t( def::menu_category_t cate, uint16_t menu_id, uint8_t level, const localize_text_t& title )
  : mi_cancel_exec_t { cate, menu_id, level, title, &name_array } {}

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
      system_registry->reset();
      system_registry->save();
      system_registry->popup_notify.setPopup(true, def::notify_type_t::NOTIFY_ALL_RESET);
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
