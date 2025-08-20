// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "system_registry.hpp"

#include "file_manage.hpp"

#include <M5Unified.hpp>
#include <set>
#include <mutex>

#include <ArduinoJson.h>

#if !defined (M5UNIFIED_PC_BUILD)

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <soc/rtc.h>
#include <spinlock.h>
static rtc_cpu_freq_config_t conf_80mhz;
static rtc_cpu_freq_config_t conf_160mhz;
static std::mutex mutex_debug;

#if CORE_DEBUG_LEVEL > 3
// #define DEBUG_PERFOMANCE_CHECK
// #define DEBUG_GPIO_MONITORING
#endif


#if defined (DEBUG_PERFOMANCE_CHECK)
static uint32_t prev_msec;
#endif
#if defined (DEBUG_GPIO_MONITORING)
static uint8_t pin_debug[6] = { 0, 0, 0, 0, 0, 0 };
#endif

#endif


namespace kanplay_ns {
//-------------------------------------------------------------------------
// extern instance
system_registry_t system_registry;

static std::set<def::command::command_param_t> working_command_param;
static std::mutex mtx_working_command_param;

#if __has_include (<freertos/freertos.h>)
void system_registry_t::reg_working_command_t::setNotifyTaskHandle(TaskHandle_t handle)
{
    if (_task_handle != nullptr) {
        M5_LOGE("task handle already set");
        return;
    }
    _task_handle = handle;
}
#endif

void system_registry_t::reg_working_command_t::set(const def::command::command_param_t& command_param)
{
  std::lock_guard<std::mutex> lock(mtx_working_command_param);
   // M5_LOGV("set %04x", command_param);
  working_command_param.insert(command_param);
  ++_working_command_change_counter;
  _execNotify();
}
void system_registry_t::reg_working_command_t::clear(const def::command::command_param_t& command_param)
{
  std::lock_guard<std::mutex> lock(mtx_working_command_param);
  // M5_LOGV("clear %04x", command_param);
  working_command_param.erase(command_param);
  ++_working_command_change_counter;
  _execNotify();
}
bool system_registry_t::reg_working_command_t::check(const def::command::command_param_t& command_param) const
{
  std::lock_guard<std::mutex> lock(mtx_working_command_param);
  return working_command_param.find(command_param) != working_command_param.end();
}

void system_registry_t::init(void)
{
  user_setting.init();
  midi_port_setting.init();

  runtime_info.init();
  popup_notify.init();
  popup_qr.init();
  wifi_control.init();
  menu_status.init();
  task_status.init();
  sub_button.init();
  internal_input.init();
  internal_imu.init();
  button_basecolor.init();
  rgbled_control.init();
  midi_out_control.init();
  operator_command.init();
  player_command.init();
  chord_play.init();
  color_setting.init();
  external_input.init();
  command_mapping_current.init();
  command_mapping_external.init();
  command_mapping_port_b.init();
  command_mapping_midinote.init();
  command_mapping_midicc15.init();
  command_mapping_midicc16.init();
  drum_mapping.init();
  file_command.init();

  // 以下のデータはPSRAM配置として初期化する
  song_data.init(true);
  backup_song_data.init(true);
  unchanged_song_data.init(true);
  clipboard_slot.init(true);
  clipboard_arpeggio.init(true);
  command_mapping_custom_main.init(true);

  // ソングデータ読込 (incbinで埋め込んだプリセットデータを読み込む)
  // song_data.loadSongJSON(preset_1_1, (size_t)sizeof_preset_1_1);
  // unchanged_song_data.assign(song_data);

  // 設定値を読み込む
  load();

#if !defined (M5UNIFIED_PC_BUILD)
// 動的にCPUクロックを変更するため80MHzと160MHzの設定を用意しておく
// ※ 240MHzは使用しない。240MHzへの動的変更は電圧設定など必要な制御が増えるため。
  rtc_clk_cpu_freq_mhz_to_config(160, &conf_160mhz);
  rtc_clk_cpu_freq_mhz_to_config(80, &conf_80mhz);
  rtc_clk_cpu_freq_set_config_fast(&conf_160mhz);
#if defined (DEBUG_GPIO_MONITORING)
  pin_debug[0] = M5.getPin(m5::pin_name_t::port_a_pin2);
  pin_debug[1] = M5.getPin(m5::pin_name_t::port_a_pin1);
  pin_debug[2] = M5.getPin(m5::pin_name_t::port_b_pin2);
  pin_debug[3] = M5.getPin(m5::pin_name_t::port_b_pin1);
  pin_debug[4] = M5.getPin(m5::pin_name_t::port_c_pin2);
  pin_debug[5] = M5.getPin(m5::pin_name_t::port_c_pin1);
  for (int i = 0; i < 6; ++i) {
    m5gfx::pinMode(pin_debug[i], m5gfx::output);
  }
#endif
#if defined (DEBUG_PERFOMANCE_CHECK)
  prev_msec = M5.micros();
#endif
#endif
}

void system_registry_t::reset(void)
{
  // LEDの輝度
  user_setting.setLedBrightness(2);

  // 画面バックライト輝度
  user_setting.setDisplayBrightness(2);

  // 言語設定
  user_setting.setLanguage(def::lang::language_t::en);

  // GUIの詳細モード
  user_setting.setGuiDetailMode(0);

  // 波形表示
  user_setting.setGuiWaveView(0);

  // MIDIマスターボリューム設定
  user_setting.setMIDIMasterVolume(127);

  // ADCマイクアンプ設定
  user_setting.setADCMicAmp(0);

  // オフビートスタイル設定
  user_setting.setOffbeatStyle(def::play::offbeat_style_t::offbeat_auto);

  // IMU プレスベロシティ設定 (0は不使用)
  user_setting.setImuVelocityLevel(0);

  // チャタリング防止のための閾値(msec)
  user_setting.setChatteringThreshold(64);

  // タイムゾーン +9 (JST)
  user_setting.setTimeZone(9);

  // パターン編集時ベロシティ設定
  runtime_info.setEditVelocity(100);

  runtime_info.setMIDIChannelVolumeMax(127);

  // InstaChord連携デバイス設定
  midi_port_setting.setInstaChordLinkDev(def::command::instachord_link_dev_t::icld_kanplay);

  // USBホスト時パワーサプライ
  midi_port_setting.setUSBPowerEnabled(true);

  // マスターボリューム設定
  operator_command.addQueue( { def::command::master_vol_set, 75 } );

  // スロット選択
  operator_command.addQueue( { def::command::slot_select, 1 } );

  // 編集時のエンコーダ２のターゲット設定
  operator_command.addQueue( { def::command::edit_enc2_target, def::command::edit_enc2_target_t::program } );

  // 演奏時ベロシティ設定
  operator_command.addQueue( { def::command::set_velocity, 127 } );


  command_mapping_external.reset();
  for (int i = 0; i < def::hw::max_button_mask; ++i) {
    command_mapping_external.setCommandParamArray(i, def::command::command_mapping_external_table[i]);
  }

  command_mapping_port_b.reset();
  for (int i = 0; i < def::hw::max_port_b_pins; ++i) {
    command_mapping_port_b.setCommandParamArray(i, def::command::command_mapping_port_b_table[i]);
  }

  struct note_cp_t {
    uint8_t note;
    def::command::command_param_array_t command_param;
  };
  {
    command_mapping_midinote.reset();
    static constexpr const note_cp_t note_cp_table[] = {
      { 53, { def::command::chord_modifier  , KANTANMusic_Modifier_dim } },
      { 55, { def::command::chord_modifier  , KANTANMusic_Modifier_7 } },
      { 56, { def::command::chord_modifier  , KANTANMusic_Modifier_sus4 } },
      { 57, { def::command::chord_minor_swap, 1 } },
      { 58, { def::command::chord_modifier  , KANTANMusic_Modifier_Add9 } },
      { 59, { def::command::chord_modifier  , KANTANMusic_Modifier_M7 } },
      { 60, {                                  def::command::chord_degree, 1 } },
      { 61, { def::command::chord_semitone, 1, def::command::chord_degree, 2 } },
      { 62, {                                  def::command::chord_degree, 2 } },
      { 63, { def::command::chord_semitone, 1, def::command::chord_degree, 3 } },
      { 64, {                                  def::command::chord_degree, 3 } },
      { 65, {                                  def::command::chord_degree, 4 } },
      { 66, { def::command::chord_semitone, 1, def::command::chord_degree, 5 } },
      { 67, {                                  def::command::chord_degree, 5 } },
      { 68, { def::command::chord_semitone, 1, def::command::chord_degree, 6 } },
      { 69, {                                  def::command::chord_degree, 6 } },
      { 70, { def::command::chord_semitone, 1, def::command::chord_degree, 7 } },
      { 71, {                                  def::command::chord_degree, 7 } },
    };
    for (const auto& cp : note_cp_table) {
      command_mapping_midinote.setCommandParamArray(cp.note, cp.command_param);
    }
  }

  { // InstaChord連携用のコントロールチェンジへの機能マッピング
    command_mapping_midicc15.reset();
    static constexpr const note_cp_t cc15_cp_table[] = {
      {  0, { def::command::target_key_set  ,  0 } },
      {  1, { def::command::target_key_set  ,  1 } },
      {  2, { def::command::target_key_set  ,  2 } },
      {  3, { def::command::target_key_set  ,  3 } },
      {  4, { def::command::target_key_set  ,  4 } },
      {  5, { def::command::target_key_set  ,  5 } },
      {  6, { def::command::target_key_set  ,  6 } },
      {  7, { def::command::target_key_set  ,  7 } },
      {  8, { def::command::target_key_set  ,  8 } },
      {  9, { def::command::target_key_set  ,  9 } },
      { 10, { def::command::target_key_set  , 10 } },
      { 11, { def::command::target_key_set  , 11 } },
    };
    for (const auto& cp : cc15_cp_table) {
      command_mapping_midicc15.setCommandParamArray(cp.note, cp.command_param);
    }

    command_mapping_midicc16.reset();
    static constexpr const note_cp_t cc16_cp_table[] = {
      {  2, { def::command::slot_select_ud  ,  1 } },
      {  3, { def::command::slot_select_ud  , -1 } },
      {  7, { def::command::internal_button , 21 } },
      {  8, { def::command::internal_button , 27 } },
      {  9, { def::command::chord_semitone  ,  1 } },
      { 10, { def::command::chord_minor_swap,  1 } },
      { 11, { def::command::chord_modifier  , KANTANMusic_Modifier_m7_5 } },
      { 12, { def::command::chord_modifier  , KANTANMusic_Modifier_7    } },
      { 13, { def::command::chord_modifier  , KANTANMusic_Modifier_M7   } },
      { 14, { def::command::chord_modifier  , KANTANMusic_Modifier_sus4 } },
      { 15, { def::command::chord_modifier  , KANTANMusic_Modifier_dim  } },
      { 16, { def::command::chord_modifier  , KANTANMusic_Modifier_Add9 } },
      { 17, { def::command::chord_modifier  , KANTANMusic_Modifier_aug  } },
      { 18, { def::command::chord_degree    ,  1 } },
      { 19, { def::command::chord_degree    ,  2 } },
      { 20, { def::command::chord_degree    ,  3 } },
      { 21, { def::command::chord_degree    ,  4 } },
      { 22, { def::command::chord_degree    ,  5 } },
      { 23, { def::command::chord_degree    ,  6 } },
      { 24, { def::command::chord_degree    ,  7 } },
      { 25, { def::command::chord_semitone  ,  1 , def::command::chord_degree, 3 } },
      { 26, { def::command::chord_semitone  ,  1 , def::command::chord_degree, 7 } },
      { 27, { def::command::chord_degree    ,  6 } },
      { 28, { def::command::chord_degree    ,  7 } },
      { 29, { def::command::chord_degree    ,  1 } },
      { 30, { def::command::chord_degree    ,  2 } },
      { 31, { def::command::chord_minor_swap,  1 , def::command::chord_degree, 3 } },
      { 32, { def::command::chord_degree    ,  4 } },
      { 33, { def::command::chord_degree    ,  5 } },
      { 34, { def::command::chord_semitone  ,  1 , def::command::chord_degree, 3 } },
      { 35, { def::command::chord_semitone  ,  1 , def::command::chord_degree, 7 } },
    };
    for (const auto& cp : cc16_cp_table) {
      command_mapping_midicc16.setCommandParamArray(cp.note, cp.command_param);
    }
  }

  // コード演奏時のメインボタンのカスタマイズ用マッピングを準備
  for (int i = 0; i < def::hw::max_button_mask; ++i) {
    auto pair = def::command::command_mapping_chord_play_table[i];
    system_registry.command_mapping_custom_main.setCommandParamArray(i, pair);
  }

  // ドラム演奏モードのボタンマッピング設定
  // TODO:ソングデータへの保存と読み込みを実装する
  for (int i = 0; i < 15; ++i) {
    drum_mapping.set8(i, def::play::drum::drum_note_table[0][i]);
  }

  color_setting.setArpeggioNoteBackColor(0x103E8D);
  color_setting.setEnablePartColor(      0x204E9D);
  color_setting.setDisablePartColor(     0x0B255E);
  color_setting.setArpeggioNoteForeColor(0xDDEEFF);
  color_setting.setArpeggioNoteBackColor(0x103E8D);
  color_setting.setArpeggioStepColor(    0x3876A5);

  color_setting.setButtonDegreeColor(    0x8888CC); // コード選択ボタンの色
  color_setting.setButtonModifierColor(  0x555555); // Modifierボタンの色
  color_setting.setButtonMinorSwapColor( 0xFF8736); //0xEE8C49u); // メジャー・マイナースワップボタンの色
  color_setting.setButtonSemitoneColor(  0x6D865A); //0x00BC00u); // 半音上げ下げボタンの色
  color_setting.setButtonNoteColor(      0xFF4499); // ノート演奏モードのボタンの色
  color_setting.setButtonDrumColor(      0x2200D0); // ドラム演奏モードのボタンの色
  color_setting.setButtonCursorColor(    0x669966); // カーソルボタンの色
  color_setting.setButtonDefaultColor(   0x333333); // その他のボタンの色
  color_setting.setButtonMenuNumberColor(0x666699); // メニュー表示時の番号ボタンの色
  color_setting.setButtonPartColor(      0x2781FF); // パート選択ボタンの色

  color_setting.setButtonPressedTextColor(0xFFFFDD); // ボタンが押された時のテキスト色
  color_setting.setButtonWorkingTextColor(0xFFFFFF); // ボタンが動作中の時のテキスト色
  color_setting.setButtonDefaultTextColor(0xBBBBBB); // ボタンのデフォルトのテキスト色
}

// 設定を保存する
void system_registry_t::save(void)
{
  auto mem = file_manage.createMemoryInfo(def::app::max_file_len);
  mem->filename = "";
  mem->dir_type = def::app::data_type_t::data_setting;
  auto len = saveSettingJSON(mem->data, def::app::max_file_len);
  mem->size = len;
  def::app::file_command_info_t info;
  info.mem_index = mem->index;
  info.dir_type = def::app::data_type_t::data_setting;
  info.file_index = -1;
  file_command.setFileSaveRequest(info);
}

// 設定を読み込む
void system_registry_t::load(void)
{
  reset();

  system_registry.file_command.setUpdateList(def::app::data_type_t::data_setting);

  def::app::file_command_info_t songinfo;
  songinfo.file_index = 0;
  songinfo.dir_type = def::app::data_type_t::data_setting;
  system_registry.file_command.setFileLoadRequest(songinfo);
}

//-------------------------------------------------------------------------

void system_registry_t::reg_task_status_t::setWorking(bitindex_t index)
{
#if !defined (M5UNIFIED_PC_BUILD)
#if defined (DEBUG_GPIO_MONITORING)
  if (TASK_I2C <= index && index < TASK_I2C + 5) {
    m5gfx::gpio_hi(pin_debug[index - TASK_I2C]);
  }
#endif
  std::lock_guard<std::mutex> lock(mutex_debug);
#endif
  uint32_t bitmask = _reg_data_32[TASK_STATUS >> 2];
  bool working = bitmask;

#if defined (DEBUG_PERFOMANCE_CHECK)
  uint32_t msec = M5.micros();
  int diff = msec - prev_msec;
  prev_msec = msec;
  uint32_t mask_shift = 1;
  for (int i = 0; i < MAX_TASK; ++i) {
    if (bitmask & mask_shift) {
      uint32_t dst = (HIGH_POWER_COUNTER + 4 + i * 4) >> 2;
      _reg_data_32[dst] += diff;
    }
    mask_shift <<= 1;
  }
  _reg_data_32[(working ? HIGH_POWER_COUNTER : LOW_POWER_COUNTER) >> 2] += diff;
#endif
  bitmask |= 1 << index;
  _reg_data_32[TASK_STATUS >> 2] = bitmask;

#if !defined (M5UNIFIED_PC_BUILD)
  if (!working) {
    rtc_clk_cpu_freq_set_config_fast(&conf_160mhz);

#if defined (DEBUG_GPIO_MONITORING)
    m5gfx::gpio_hi(pin_debug[5]);
#endif
  }
#endif
}

void system_registry_t::reg_task_status_t::setSuspend(bitindex_t index)
{
#if !defined (M5UNIFIED_PC_BUILD)
  std::lock_guard<std::mutex> lock(mutex_debug);
#endif
  uint32_t bitmask = _reg_data_32[TASK_STATUS >> 2];
#if defined (DEBUG_PERFOMANCE_CHECK)
  uint32_t msec = M5.micros();
  bool working = bitmask;
  int diff = msec - prev_msec;
  prev_msec = msec;
  uint32_t mask_shift = 1;
  for (int i = 0; i < MAX_TASK; ++i) {
    if (bitmask & mask_shift) {
      uint32_t dst = (HIGH_POWER_COUNTER + 4 + i * 4) >> 2;
      _reg_data_32[dst] += diff;
    }
    mask_shift <<= 1;
  }
  _reg_data_32[(working ? HIGH_POWER_COUNTER : LOW_POWER_COUNTER) >> 2] += diff;
#endif

  bitmask &= ~(1 << index);
  _reg_data_32[TASK_STATUS >> 2] = bitmask;

#if !defined (M5UNIFIED_PC_BUILD)
  if (!isWorking()) {
    rtc_clk_cpu_freq_set_config_fast(&conf_80mhz);
#if defined (DEBUG_GPIO_MONITORING)
    m5gfx::gpio_lo(pin_debug[5]);
#endif
  }
#if defined (DEBUG_GPIO_MONITORING)
// 割り込みを再度許可する

  if (TASK_I2C <= index && index < TASK_I2C + 5) {
    m5gfx::gpio_lo(pin_debug[index - TASK_I2C]);
  }
#endif
#endif
}

//-------------------------------------------------------------------------

void system_registry_t::reg_user_setting_t::setTimeZone15min(int8_t offset)
{
  set8(TIMEZONE, offset);
#if !defined (M5UNIFIED_PC_BUILD)
  configTime(offset * 15 * 60, 0, def::ntp::server1, def::ntp::server2, def::ntp::server3);
#endif
}

//-------------------------------------------------------------------------
namespace def::ctrl_assign {
  int get_index_from_command(const control_assignment_t* data, const def::command::command_param_array_t& command)
  {
    for (int i = 0; data[i].jsonname != nullptr; i++) {
      if (data[i].command == command) {
        return i;
      }
    }
    return -1;
  }

  int get_index_from_jsonname(const control_assignment_t* data, const char* name)
  {
    for (int i = 0; data[i].jsonname != nullptr; i++) {
      if (strcmp(data[i].jsonname, name) == 0) {
        return i;
      }
    }
    return -1;
  }
}

const char* localize_text_t::get(void) const
{ auto i = (uint8_t)system_registry.user_setting.getLanguage(); return text[i] ? text[i] : text[0]; }

//-------------------------------------------------------------------------
size_t system_registry_t::saveSettingJSON(uint8_t* data, size_t data_length)
{
  ArduinoJson::JsonDocument json_root;

  json_root["format"] = "KANTANPlayCore";
  json_root["type"] = "Config";
  json_root["version"] = 2;

  {
    auto json = json_root["user_setting"].to<JsonObject>();

    json["led_brightness"]       = user_setting.getLedBrightness();
    json["display_brightness"]   = user_setting.getDisplayBrightness();
    json["language"]    = (uint8_t)user_setting.getLanguage();
    json["gui_detail_mode"]      = user_setting.getGuiDetailMode();
    json["gui_wave_view"]        = user_setting.getGuiWaveView();
    json["master_volume"]        = user_setting.getMasterVolume();
    json["midi_master_volume"]   = user_setting.getMIDIMasterVolume();
    json["adc_mic_amp"]          = user_setting.getADCMicAmp();
    json["offbeat_style"]        = user_setting.getOffbeatStyle();
    json["imu_velocity_level"]   = user_setting.getImuVelocityLevel();
    json["chattering_threshold"] = user_setting.getChatteringThreshold();
    json["timezone"]             = user_setting.getTimeZone();
  }
  {
    auto json = json_root["midi_port_setting"].to<JsonObject>();
    json["instachord_link_dev"] = (uint8_t)midi_port_setting.getInstaChordLinkDev();
    json["usb_mode"] = (uint8_t)midi_port_setting.getUSBMode();
    json["usb_power"] = (uint8_t)midi_port_setting.getUSBPowerEnabled();
  }

  auto json_key_mapping = json_root["key_mapping"].to<JsonObject>();
  {
    {
      auto json = json_key_mapping["chord_play"].to<JsonObject>();
      for (int btn = 1; btn <= 15; ++btn) {
        auto cmd = command_mapping_custom_main.getCommandParamArray(btn - 1);
        auto index = def::ctrl_assign::get_index_from_command(def::ctrl_assign::playbutton_table, cmd);
        if (index < 0) { continue; }
        json[std::to_string(btn)] = def::ctrl_assign::playbutton_table[index].jsonname;
      }
    }
    {
      auto json = json_key_mapping["external"].to<JsonObject>();
      for (int btn = 1; btn <= def::hw::max_button_mask; ++btn) {
        auto cmd = command_mapping_external.getCommandParamArray(btn - 1);
        if (cmd.empty()) { continue; }
        auto index = def::ctrl_assign::get_index_from_command(def::ctrl_assign::external_table, cmd);
        if (index < 0) { continue; }
        json[std::to_string(btn)] = def::ctrl_assign::external_table[index].jsonname;
      }
    }
    {
      auto json = json_key_mapping["midinote"].to<JsonObject>();
      for (int btn = 1; btn <= def::midi::max_note; ++btn) {
        auto cmd = command_mapping_midinote.getCommandParamArray(btn - 1);
        if (cmd.empty()) { continue; }
        auto index = def::ctrl_assign::get_index_from_command(def::ctrl_assign::external_table, cmd);
        if (index < 0) { continue; }
        json[std::to_string(btn)] = def::ctrl_assign::external_table[index].jsonname;
      }
    }
  }

  auto result = serializeJson(json_root, (char*)data, data_length);
// ESP_LOGV("sysreg", "saveSettingJSON result: %d\n", result);

  return result;
}

bool system_registry_t::loadSettingJSON(const uint8_t* data, size_t data_length)
{

  ArduinoJson::JsonDocument json_root;
  auto error = deserializeJson(json_root, (char*)data, data_length);
  if (error)
  {
    M5_LOGE("deserializeJson error: %s", error.c_str());
    return false;
  }

  if (json_root["format"] != "KANTANPlayCore")
  {
    M5_LOGE("format error: %s", json_root["format"].as<const char*>());
    return false;
  }

  if (json_root["type"] != "Config")
  {
    M5_LOGE("type error: %s", json_root["type"].as<const char*>());
    return false;
  }

  auto data_version = json_root["version"].as<int>();

  if (json_root["version"] > 2)
  {
    M5_LOGV("version mismatch: %d", json_root["version"].as<int>());
  }
  {
    auto json = json_root["user_setting"].as<JsonObject>();
    user_setting.setLedBrightness(                           json["led_brightness"      ].as<uint8_t>());
    user_setting.setDisplayBrightness(                       json["display_brightness"  ].as<uint8_t>());
    user_setting.setLanguage((def::lang::language_t)         json["language"            ].as<uint8_t>());
    user_setting.setGuiDetailMode(                           json["gui_detail_mode"     ].as<bool>());
    user_setting.setGuiWaveView(                             json["gui_wave_view"       ].as<bool>());
    user_setting.setMasterVolume(                            json["master_volume"       ].as<uint8_t>());
    user_setting.setMIDIMasterVolume(                        json["midi_master_volume"  ].as<uint8_t>());
    user_setting.setADCMicAmp(                               json["adc_mic_amp"         ].as<uint8_t>());
    user_setting.setOffbeatStyle((def::play::offbeat_style_t)json["offbeat_style"       ].as<uint8_t>());
    user_setting.setImuVelocityLevel(                        json["imu_velocity_level"  ].as<uint8_t>());
    user_setting.setChatteringThreshold(                     json["chattering_threshold"].as<uint8_t>());
    user_setting.setTimeZone(                                json["timezone"            ].as<int8_t>());
  }
  {
    auto json = json_root["midi_port_setting"].as<JsonObject>();
    midi_port_setting.setInstaChordLinkDev((def::command::instachord_link_dev_t)json["instachord_link_dev"      ].as<uint8_t>());
    midi_port_setting.setUSBMode((def::command::usb_mode_t)json["usb_mode"].as<uint8_t>());
    midi_port_setting.setUSBPowerEnabled(json["usb_power"].as<bool>());
  }

  // control_assignment::play button ( 旧名 key mapping )
  {
    auto json_key_mapping = json_root["key_mapping"].as<JsonObject>();
    if (!json_key_mapping.isNull())
    {
      {
        auto json = json_key_mapping["chord_play"].as<JsonObject>();
        if (!json.isNull()) {
          if (data_version == 1 && json["9"] == "7") {
            // 旧仕様のキーアサイン設定で Degree 7 と 7th を混同するケースがあったので、ここで古いデータを無視する
          } else {
            for (int btn = 1; btn <= 15; ++btn) {
              auto name = json[std::to_string(btn)].as<const char*>();
              if (name == nullptr) { continue; }
              auto index = def::ctrl_assign::get_index_from_jsonname(def::ctrl_assign::playbutton_table, name);
              if (index < 0) { continue; }
              command_mapping_custom_main.setCommandParamArray(btn - 1, def::ctrl_assign::playbutton_table[index].command);
            }
          }
        }
      }
      {
        auto json = json_key_mapping["external"].as<JsonObject>();
        if (!json.isNull()) {
          command_mapping_external.reset();
          for (int btn = 1; btn <= def::hw::max_button_mask; ++btn) {
            auto name = json[std::to_string(btn)].as<const char*>();
            if (name == nullptr) { continue; }
            auto index = def::ctrl_assign::get_index_from_jsonname(def::ctrl_assign::external_table, name);
            if (index < 0) { continue; }
            command_mapping_external.setCommandParamArray(btn - 1, def::ctrl_assign::external_table[index].command);
          }
        }
      }
      {
        auto json = json_key_mapping["midinote"].as<JsonObject>();
        if (!json.isNull()) {
          command_mapping_midinote.reset();
          for (int btn = 1; btn <= def::midi::max_note; ++btn) {
            auto name = json[std::to_string(btn)].as<const char*>();
            if (name == nullptr) { continue; }
            auto index = def::ctrl_assign::get_index_from_jsonname(def::ctrl_assign::external_table, name);
            if (index < 0) { continue; }
            command_mapping_midinote.setCommandParamArray(btn - 1, def::ctrl_assign::external_table[index].command);
          }
        }
      }
    }
  }

  return true;
}

//-------------------------------------------------------------------------

  // データファイル内のキーワード
  // 保存時の順序に影響するので、変更する場合は注意
  enum datafile_key_t {
    kwd_Unknown = -1,
    kwd_Set,
    kwd_Slot,
    kwd_Mode,
    kwd_Part,
    kwd_Drum,
    kwd_Volume,
    kwd_Tone,
    kwd_Position,
    kwd_Octave,
    kwd_Voicing,
    kwd_BanLift,
    kwd_End,
    kwd_Pitch,
    kwd_Style,
    kwd_max
  };
  // データファイル内のキーワード(文字列)
  // datafile_key_t と順序を一致させること
  static constexpr const char* datafile_key[] = {
    "Set",     // 互換性のため残す。今後はSlotに統一する 
    "Slot", 
    "Mode",
    "Part",
    "Drum",
    "Volume",
    "Tone",
    "Position",
    "Octave",   // 互換性のため残す。今後はPositionに統一する
    "Voicing",
    "BanLift",
    "End",
    "Pitch",
    "Style",
  };

bool system_registry_t::song_data_t::loadText(uint8_t* data, size_t data_length)
{
  reset();
  {
    uint8_t c;
    size_t data_index = 0;
    uint_fast16_t value_idx = 0;
    datafile_key_t kwd = datafile_key_t::kwd_Unknown;
    size_t kwd_index = 0;

    // partset_info_t* ps = partset_list;
    // part_info_t* pi = ps->getChannelInfo(0);
    // play_control_t::global_part_info_t* gp = global_part_info;
    auto ps = &slot[0];
    auto pi = ps->chord_part;
    auto gp = &chord_part_drum[0];

    while (data_index < data_length) {
      c = data[data_index];
      if (c == '\n' || c == '\r') {
        ++data_index;
        kwd = datafile_key_t::kwd_Unknown;
        continue;
      }
      if (kwd == datafile_key_t::kwd_Unknown) {
        // 区切り文字直後のスペースをスキップ
        while (data[data_index] == ' ' && ++data_index < data_length) {};
      }

      // 現在の位置を記憶しておき、次に現れる区切り文字を探す
      size_t hit_index = data_index;
      do {
        c = data[data_index];
        // スペースは有効な文字扱い。ここで探したいのは ',' '\t' '\r' '\n' などの区切り文字
      } while (c >= ' ' && c != ',' && ++data_index < data_length);

      // 有効な文字が見つからなかった場合は一文字進んで再試行
      if (hit_index == data_index) {
        ++data_index;
        continue;
      }

      // size_t hit_index = data_index;
      // while (++data_index < data_length && (data[data_index] > ',' || data[data_index] == ' ')) {};

      int val = 0;
      {
        const char* line_buf = (char*)&data[hit_index];
        c = line_buf[0];
        if ((c <= '9' && c >= '0') || c == '-' || c == '+') {
          bool is_minus = (c == '-');
          if (is_minus || c == '+') { ++line_buf; }
          while (line_buf[0] >= '0' && line_buf[0] <= '9') {
            val *= 10;
            val += *line_buf - '0';
            ++line_buf;
          }
          if (is_minus) { val = -val; }
        }
        else
        if (kwd == datafile_key_t::kwd_Unknown && c >= 'A' && c <= 'Z')
        {
// if (line_idx) { M5.Log.printf("%s \r\n", line_buf); }
          datafile_key_t tmp = datafile_key_t::kwd_Unknown;
          int i = 0;
          for (; i < (sizeof(datafile_key) / sizeof(datafile_key[0])); ++i) {
            size_t hit_len = 0;
            while (datafile_key[i][hit_len] == line_buf[hit_len]) { ++hit_len; }
            if (datafile_key[i][hit_len] == 0) {
              tmp = (datafile_key_t)i;
              line_buf += hit_len;
              break;
            }
          }
          if (tmp != datafile_key_t::kwd_Unknown) {
            kwd = tmp;
            value_idx = 0;
            kwd_index = 0;
            c = line_buf[0];
            if (c <= '9' && c >= '0') {
              do {
                kwd_index *= 10;
                kwd_index += *line_buf - '0';
              } while (++line_buf, line_buf[0] >= '0' && line_buf[0] <= '9');
            }
            continue;
          }
        }
// M5.Log.printf("unknown : %s \r\n", line_buf);
// M5.Log.printf("unknown : %s = %d\r\n", line_buf, val);

        auto k = kwd;
        kwd = datafile_key_t::kwd_Unknown;
        switch (k) {
        case datafile_key_t::kwd_Set:
        case datafile_key_t::kwd_Slot:
          if ((uint_fast8_t)val < def::app::max_slot) {  // 旧multipart_number_max
// M5_LOGV("slot change: %d", val);
            ps = &slot[val];
            pi = ps->chord_part;
            ps->slot_info.reset();
          }
          break;

        case datafile_key_t::kwd_Mode:
          {
            auto m = def::playmode::playmode_t::chord_mode;
            switch (c) {
            case 'n':  case 'N':  m = def::playmode::playmode_t::note_mode;    break;
            case 'd':  case 'D':  m = def::playmode::playmode_t::drum_mode;    break;
            default: break;
            }
M5_LOGV("mode change: %02x", c);
            ps->slot_info.setPlayMode(m);
          }
          break;

        case datafile_key_t::kwd_Part:
          if ((uint_fast8_t)val < def::app::max_chord_part) {  // 旧part_number_max
// M5.Log.printf("part change: %d\r\n", val);
            pi = &(ps->chord_part[val]);   // pi = ps->getChannelInfo(val);
            gp = &chord_part_drum[val];
            // gp = &(global_part_info[val]);
          }
          break;

        case datafile_key_t::kwd_Tone:  { pi->part_info.setTone(((uint_fast8_t)(val < def::app::max_program_number) ? val : def::app::max_program_number) - 1); }  break;
        case datafile_key_t::kwd_Volume:   if ((uint_fast8_t)val <= 100) { pi->part_info.setVolume(val); }     break;
        case datafile_key_t::kwd_BanLift: if ((uint_fast8_t)val < def::app::max_arpeggio_step && val) { pi->part_info.setAnchorStep(val); }     break;
        case datafile_key_t::kwd_End: --val; if ((uint_fast8_t)val < def::app::max_arpeggio_step && val) { pi->part_info.setLoopStep(val); }     break;
        case datafile_key_t::kwd_Position:   { pi->part_info.setPosition(val); }     break;
        case datafile_key_t::kwd_Octave:   { pi->part_info.setPosition(val * 4); }     break;
        case datafile_key_t::kwd_Voicing:
          {
            auto v = KANTANMusic_Voicing_Close;
            switch (c) {
            default:
            case 'c':  case 'C':  v = KANTANMusic_Voicing_Close;    break;
            case 'g':  case 'G':  v = KANTANMusic_Voicing_Guitar;   break;
            case 's':  case 'S':  v = KANTANMusic_Voicing_Static;   break;
            case 'u':  case 'U':  v = KANTANMusic_Voicing_Ukulele;  break;
            }
            pi->part_info.setVoicing((uint8_t)v);
          }
          break;

        case datafile_key_t::kwd_Pitch: //0: case datafile_key_t::kwd_Pitch1: case datafile_key_t::kwd_Pitch2: case datafile_key_t::kwd_Pitch3: case datafile_key_t::kwd_Pitch4: case datafile_key_t::kwd_Pitch5: case datafile_key_t::kwd_Pitch6:
          {
            if (value_idx < def::app::max_arpeggio_step) {
              kwd = k;
              int pitch = kwd_index; //k - datafile_key_t::kwd_Pitch0;
              pi->arpeggio.setVelocity(value_idx, pitch, val);
            }
          }
          break;

        case kwd_Drum: //0: case kwd_Drum1: case kwd_Drum2: case kwd_Drum3: case kwd_Drum4: case kwd_Drum5: case kwd_Drum6:
          if (val && val < 128) {
  // M5.Log.printf("hit : %s %d \r\n", keyword[k], val);
            int pitch = kwd_index; //k - datafile_key_t::kwd_Drum0;
            gp->setDrumNoteNumber(pitch, val);
          }
          break;

        case datafile_key_t::kwd_Style:
            if (value_idx < def::app::max_arpeggio_step) {
            kwd = k;
            auto style = def::play::arpeggio_style_t::same_time;
            switch (c) {
            case 'u':  case 'U':
              style = def::play::arpeggio_style_t::high_to_low;
              break;

            case 'd':  case 'D':
              style = def::play::arpeggio_style_t::low_to_high;
              break;

            case 'm':  case 'M':
              style = def::play::arpeggio_style_t::mute;
              break;

            default: break;
            }
            pi->arpeggio.setStyle(value_idx, style);
          }
          break;

        default:
          break;
        }
      }
      ++value_idx;
    }
  }
  return true;
}

size_t system_registry_t::song_data_t::saveText(uint8_t* data_buffer, size_t data_length)
{
  size_t result = 0;

  auto buf = (char*)data_buffer;

  for (int setidx = 0; setidx < def::app::max_slot; ++setidx)
  {
    // partset_info_t* ps = &partset_list[setidx];
    auto ps = &slot[setidx];
    int len = 0;
    len = snprintf(buf, data_length - result, "%s\t%d\n", datafile_key[datafile_key_t::kwd_Slot], setidx);
    buf += len;
    result += len;

    len = snprintf(buf, data_length - result, "%s\t%s\n", datafile_key[datafile_key_t::kwd_Mode], def::playmode::playmode_name_table[ps->slot_info.getPlayMode()]);
// M5_LOGI("%s\t%s", datafile_key[datafile_key_t::kwd_Mode], def::playmode::playmode_name_table[ps->slot_info.getPlayMode()]);
    buf += len;
    result += len;

    for (int partidx = 0; partidx < def::app::max_chord_part; ++partidx)
    {
      auto pi = &(ps->chord_part[partidx]);
      auto gp = &(chord_part_drum[partidx]);
      // part_info_t* pi = ps->getChannelInfo(partidx);
      // play_control_t::global_part_info_t* gp = &(global_part_info[partidx]);
      // gp は slot_set_info に変更

      for (int k = 0; k < kwd_max; ++k) {
        auto kwd = (datafile_key_t)k;
        // int len = snprintf(linebuf, sizeof(linebuf), "\n%s\t", datafile_key[k]);
        // file.write(linebuf, len);

        int32_t val = INT32_MAX;
        int len = 0;
        switch (kwd) {
        case datafile_key_t::kwd_Part:     val = partidx;                               break;
        case datafile_key_t::kwd_Tone:     val = pi->part_info.getTone() + 1;  break;
        case datafile_key_t::kwd_Volume:   val = pi->part_info.getVolume();             break;
        case datafile_key_t::kwd_BanLift:  val = pi->part_info.getAnchorStep();       break;
        case datafile_key_t::kwd_End:      val = pi->part_info.getLoopStep() + 1;       break;
        case datafile_key_t::kwd_Position: val = pi->part_info.getPosition();           break;
        case datafile_key_t::kwd_Voicing:
          {
            len = snprintf(buf, 32, "%s\t%s\n", datafile_key[k], def::play::GetVoicingName(pi->part_info.getVoicing()));
            buf += len;
            result += len;
            len = 0;
          }
          break;

        case datafile_key_t::kwd_Pitch:
          for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
            len = snprintf(buf, 16, "%s%d", datafile_key[k], pitch);
            buf += len;
            result += len;
            len = 0;
            for (int step = 0; step < def::app::max_arpeggio_step; ++step) {
              snprintf(buf, 8, "\t%d  ", pi->arpeggio.getVelocity(step, pitch));
              buf += 4;
              result += 4;
            }
            buf[0] = '\n';
            buf += 1;
            result += 1;
          }
          break;

        case kwd_Drum:
          if (setidx == 0)
          {
            for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch) {
              len = snprintf(buf, 16, "%s%d", datafile_key[k], pitch);
              buf += len;
              result += len;
              len = snprintf(buf, 8, "\t%d\n", gp->getDrumNoteNumber(pitch));
              buf += len;
              result += len;
            }
          }
          break;

        case datafile_key_t::kwd_Style:
          {
            len = snprintf(buf, 16, "%s", datafile_key[k]);
            buf += len;
            result += len;
            len = 0;
            for (int step = 0; step < def::app::max_arpeggio_step; ++step) {
              const char* style_name = "\t ";
              switch (pi->arpeggio.getStyle(step))
              {
              default:
              case def::play::arpeggio_style_t::same_time: break;
              case def::play::arpeggio_style_t::high_to_low:
                style_name = "\tU";
              break;

              case def::play::arpeggio_style_t::low_to_high:
                style_name = "\tD";
                break;

              case def::play::arpeggio_style_t::mute:
                style_name = "\tM";
                break;
              }
              snprintf(buf, 3, "%s", style_name);
              buf += 2;
              result += 2;
            }
            buf[0] = '\n';
            buf += 1;
            result += 1;
          }
          break;

        default:
          break;

        }
        if (val != INT32_MAX) {
          len = snprintf(buf, 16, "%s\t%d\n", datafile_key[k], (int)val);
          buf += len;
          result += len;
        }
      }
      buf[0] = '\n';
      buf += 1;
      result += 1;
    }
  }
  return result;
}



static KANTANMusic_Voicing getVoicing(const char* voicing)
{
  if (voicing != nullptr) {
    for (int i = 0; i < KANTANMusic_MAX_VOICING; ++i) {
      if (strcmp(voicing, def::play::GetVoicingName((KANTANMusic_Voicing)i)) == 0) {
        return (KANTANMusic_Voicing)i;
      }
    }
  }
  return KANTANMusic_Voicing_Close;
}

static const char* getPlayModeName(def::playmode::playmode_t mode)
{
  switch (mode)
  {
  case def::playmode::playmode_t::chord_mode: return "Chord";
  case def::playmode::playmode_t::note_mode: return "Note";
  case def::playmode::playmode_t::drum_mode: return "Drum";
  default: break;
  }
  return "Unknown";
}

static def::playmode::playmode_t getPlayMode(const char* name)
{
  if (name != nullptr) {
    for (int i = 0; i < def::playmode::playmode_max; ++i) {
      auto mode = def::playmode::playmode_t(i);
      if (strcmp(name, getPlayModeName(mode)) == 0) {
        return (def::playmode::playmode_t)i;
      }
    }
  }
  return def::playmode::playmode_t::chord_mode;
}


size_t system_registry_t::song_data_t::saveSongJSON(uint8_t* data_buffer, size_t data_length)
{
  ArduinoJson::JsonDocument json;

  json["format"] = "KANTANPlayCore";
  json["type"] = "Song";
  json["version"] = 2;
  json["tempo"] = song_info.getTempo();
  json["swing"] = song_info.getSwing();
  json["base_key"] = system_registry.runtime_info.getMasterKey();

  auto drum_note = json["drum_note"].to<JsonArray>();
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
  {
    auto gp = &chord_part_drum[part_index];
    auto drum_note_array = drum_note.add<JsonArray>();
    for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch)
    {
      drum_note_array.add(gp->getDrumNoteNumber(pitch));
    }
  }

  kanplay_slot_t slot_default;
  slot_default.init();
  slot_default.reset();

  auto json_slot = json["slot"].to<JsonArray>();
  for (int slot_index = 0; slot_index < def::app::max_slot; ++slot_index)
  {
    auto reg_slot = &slot[slot_index];
    auto reg_chord_part = reg_slot->chord_part;
    auto slot_info = json_slot.add<JsonObject>();
    if (*reg_slot == slot_default) { continue; }
    if (slot_index != 0 && *reg_slot == slot[slot_index - 1]) { continue; }

    slot_info["play_mode"] = getPlayModeName(reg_slot->slot_info.getPlayMode());
    slot_info["key_offset"] = reg_slot->slot_info.getKeyOffset();
    slot_info["step_per_beat"] = reg_slot->slot_info.getStepPerBeat();
    auto chord_mode = slot_info["chord_mode"].to<JsonObject>();
    auto part = chord_mode["part"].to<JsonArray>();
    for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
    {
      auto part_info = part.add<JsonObject>();
      auto reg_part = &reg_chord_part[part_index];
      if (slot_default.chord_part[part_index] == *reg_part) { continue; }

      part_info["volume"] = reg_part->part_info.getVolume();
      part_info["tone"] = reg_part->part_info.getTone();
      part_info["octave"] = reg_part->part_info.getPosition();
      part_info["voicing"] = def::play::GetVoicingName(reg_part->part_info.getVoicing());
      part_info["loop_step"] = reg_part->part_info.getLoopStep();
      part_info["anchor_step"] = reg_part->part_info.getAnchorStep();
      part_info["stroke_speed"] = reg_part->part_info.getStrokeSpeed();
      part_info["enabled"] = reg_part->part_info.getEnabled();

      if (reg_part->arpeggio == slot_default.chord_part[part_index].arpeggio) { continue; }

      auto arpeggio = part_info["arpeggio"].to<JsonArray>();
      int hit_pitch = 0;
      for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch)
      {
        int8_t values[def::app::max_arpeggio_step];
        int hit_step = 0;
        for (int step = 0; step < def::app::max_arpeggio_step; ++step)
        {
          int v = reg_part->arpeggio.getVelocity(step, pitch);
          values[step] = v;
          if (v) { hit_step = step + 1; }
        }
        auto pitch_array = arpeggio.add<JsonArray>();
        if (hit_step) {
          hit_pitch = pitch + 1;
          for (int step = 0; step < hit_step; ++step)
          {
            pitch_array.add(values[step]);
          }
        }
      }
      if (hit_pitch == 0) {
        part_info.remove("arpeggio");
      }
      {
        int8_t values[def::app::max_arpeggio_step];
        int hit_step = 0;
        for (int step = 0; step < def::app::max_arpeggio_step; ++step)
        {
          int v = reg_part->arpeggio.getStyle(step);
          values[step] = v;
          if (v) { hit_step = step + 1; }
        }
        auto style = part_info["style"].to<JsonArray>();
        for (int step = 0; step < hit_step; ++step)
        {
          const char* style_name = "";
          switch (values[step])
          {
          default:
          case def::play::arpeggio_style_t::same_time: break;
          case def::play::arpeggio_style_t::high_to_low:
            style_name = "U"; break;
          case def::play::arpeggio_style_t::low_to_high:
            style_name = "D"; break;
          case def::play::arpeggio_style_t::mute:
            style_name = "M"; break;
          }
          style.add(style_name);
        }
      }
    }
  }

  auto result = serializeJson(json, (char*)data_buffer, data_length);
  return result;
}

bool system_registry_t::song_data_t::loadSongJSON(const uint8_t* data, size_t data_length)
{
  reset();

  ArduinoJson::JsonDocument json;
  auto error = deserializeJson(json, (char*)data, data_length);
  if (error)
  {
    M5_LOGE("deserializeJson error: %s", error.c_str());
    return false;
  }

  if (json["format"] != "KANTANPlayCore")
  {
    M5_LOGE("format error: %s", json["format"].as<const char*>());
    return false;
  }

  if (json["type"] != "Song")
  {
    M5_LOGE("type error: %s", json["type"].as<const char*>());
    return false;
  }

  if (json["version"] > 1)
  {
    M5_LOGV("version mismatch: %d", json["version"].as<int>());
  }

  song_info.setTempo(json["tempo"].as<int>());
  song_info.setSwing(json["swing"].as<int>());
  song_info.setBaseKey(json["base_key"].as<int>());
  system_registry.runtime_info.setMasterKey(json["base_key"].as<int>());

  auto drum_note = json["drum_note"].as<JsonArray>();
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
  {
    auto gp = &chord_part_drum[part_index];
    auto drum_note_array = drum_note[part_index].as<JsonArray>();
    for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch)
    {
      gp->setDrumNoteNumber(pitch, drum_note_array[pitch].as<int>());
    }
  }

  auto json_slot = json["slot"].as<JsonArray>();
  size_t slot_size = json_slot.size();
  if (slot_size > def::app::max_slot) { slot_size = def::app::max_slot; }
  for (int slot_index = 0; slot_index < def::app::max_slot; ++slot_index)
  {
    auto reg_slot = &slot[slot_index];
    if (slot_index >= slot_size || 
        json_slot[slot_index].as<JsonObject>().size() == 0)
    {
      if (slot_index > 0) { // 先頭以外のスロットで項目が省略されている場合は前のスロットの内容をコピー
        slot[slot_index].assign(slot[slot_index - 1]);
// M5_LOGV("slot copy: %d", slot_index);
      }
      continue;
    }

    auto reg_chord_part = reg_slot->chord_part;
    auto slot_info = json_slot[slot_index].as<JsonObject>();
    reg_slot->slot_info.setPlayMode(getPlayMode(slot_info["play_mode"].as<const char*>()));
    reg_slot->slot_info.setKeyOffset(slot_info["key_offset"].as<int>());
    reg_slot->slot_info.setStepPerBeat(slot_info["step_per_beat"].as<int>());
    auto chord_mode = slot_info["chord_mode"].as<JsonObject>();
    auto part = chord_mode["part"].as<JsonArray>();
    size_t part_size = part.size();

    if (part_size > def::app::max_chord_part) { part_size = def::app::max_chord_part; }
    for (int part_index = 0; part_index < part_size; ++part_index)
    {
      auto part_info = part[part_index].as<JsonObject>();
      auto reg_part = &reg_chord_part[part_index];
      reg_part->part_info.setVolume(part_info["volume"].as<int>());
      reg_part->part_info.setTone(part_info["tone"].as<int>());
      reg_part->part_info.setPosition(part_info["octave"].as<int>());
      reg_part->part_info.setVoicing(getVoicing(part_info["voicing"].as<const char*>()));
      reg_part->part_info.setLoopStep(part_info["loop_step"].as<int>());
      if (part_info["anchor_step" ].is<int>())  { reg_part->part_info.setAnchorStep( part_info["anchor_step" ].as<int>()); }
      if (part_info["stroke_speed"].is<int>())  { reg_part->part_info.setStrokeSpeed(part_info["stroke_speed"].as<int>()); }
      if (part_info["enabled"     ].is<bool>()) { reg_part->part_info.setEnabled(    part_info["enabled"     ].as<bool>()); }
      if (part_info["arpeggio"].is<JsonArray>()) {
        auto arpeggio = part_info["arpeggio"].as<JsonArray>();
        for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch)
        {
          auto pitch_array = arpeggio[pitch].as<JsonArray>();
          size_t len = pitch_array.size();
          if (len > def::app::max_arpeggio_step) { len = def::app::max_arpeggio_step; }
          for (size_t step = 0; step < len; ++step)
          {
            reg_part->arpeggio.setVelocity(step, pitch, pitch_array[step].as<int>());
          }
        }
      }
      if (part_info["style"].is<JsonArray>()) {
        auto style = part_info["style"].as<JsonArray>();
        size_t len = style.size();
        if (len > def::app::max_arpeggio_step) { len = def::app::max_arpeggio_step; }
        for (size_t step = 0; step < len; ++step)
        {
          const char* style_name = style[step].as<const char*>();
          def::play::arpeggio_style_t style_value = def::play::arpeggio_style_t::same_time;
          switch (style_name[0]) {
          default: break;
          case 'U': style_value = def::play::arpeggio_style_t::high_to_low;  break;
          case 'D': style_value = def::play::arpeggio_style_t::low_to_high;  break;
          case 'M': style_value = def::play::arpeggio_style_t::mute;         break;
          }
          reg_part->arpeggio.setStyle(step, style_value);
        }
      }
    }
  }
  return true;
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
