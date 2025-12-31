// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include "system_registry.hpp"

#include "file_manage.hpp"

#include <M5Unified.hpp>
#include <set>
#include <mutex>

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
system_registry_t* system_registry;

static std::set<def::command::command_param_t> working_command_param;
// static std::map<def::command::command_param_t, uint16_t> working_command_counter;
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
  bool notify = false;
  {
    // M5_LOGV("set %04x", command_param);
    std::lock_guard<std::mutex> lock(mtx_working_command_param);
    auto it = working_command_param.find(command_param);
    if (it == working_command_param.end()) {
      working_command_param.insert(command_param);
      ++_working_command_change_counter;
      notify = true;
    }
  }
  if (notify) {
    _execNotify();
  }
}
void system_registry_t::reg_working_command_t::clear(const def::command::command_param_t& command_param)
{
  bool notify = false;
  {
    // M5_LOGV("clear %04x", command_param);
    std::lock_guard<std::mutex> lock(mtx_working_command_param);
    auto it = working_command_param.find(command_param);
    if (it != working_command_param.end()) {
      working_command_param.erase(it);
      ++_working_command_change_counter;
      notify = true;
    }
  }
  if (notify) {
    _execNotify();
  }
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
  wifi_control.init();
  task_status.init();
  sub_button.init();
  internal_input.init();
  external_input.init();
  internal_imu.init();
  rgbled_control.init();
  midi_out_control.init();
  operator_command.init();
  player_command.init();
  chord_play.init();
  color_setting.init();
  command_mapping_current.init();

  // 以下のデータはPSRAM配置として初期化する
  control_mapping[0].init(true);
  control_mapping[1].init(true);
  command_mapping_internal.init(true);
  command_mapping_external.init(true);
  command_mapping_midinote.init(true);
  command_mapping_port_b.init(true);
  command_mapping_midicc15.init(true);
  command_mapping_midicc16.init(true);
  drum_mapping.init(true);
  menu_status.init(true);
  popup_notify.init(true);
  popup_qr.init(true);
  song_data.init(true);
  backup_song_data.init(true);
  clipboard_slot.init(true);
  clipboard_arpeggio.init(true);
// command_mapping_custom_main.init(true);
//sequence_play.init(true);
//current_sequence_timeline.init(true);

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

//-------------------------------------------------------------------------
struct note_cp_t {
  uint8_t note;
  def::command::command_param_array_t command_param;
};

void system_registry_t::updateControlMapping(void)
{
  if (control_mapping[0].internal.empty()) {
    // コード演奏時のメインボタンのカスタマイズ用マッピングを準備
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto pair = def::command::command_mapping_chord_play_table[i];
      control_mapping[0].internal.setCommandParamArray(i, pair);
    }
  }

  if (control_mapping[0].external.empty()) {
    for (int i = 0; i < def::hw::max_button_mask; ++i) {
      control_mapping[0].external.setCommandParamArray(i, def::command::command_mapping_external_table[i]);
    }
  }

  if (control_mapping[0].midinote.empty()) {
    static constexpr const note_cp_t note_cp_table[] = {
      { 53, { def::command::chord_modifier  , KANTANMusic_Modifier_dim } },
      { 55, { def::command::chord_modifier  , KANTANMusic_Modifier_7 } },
      { 56, { def::command::chord_modifier  , KANTANMusic_Modifier_sus4 } },
      { 57, { def::command::chord_minor_swap, 1 } },
      { 58, { def::command::chord_modifier  , KANTANMusic_Modifier_Add9 } },
      { 59, { def::command::chord_modifier  , KANTANMusic_Modifier_M7 } },
      { 60, { def::command::chord_degree, make_degree(1, false               ) } },
      { 61, { def::command::chord_degree, make_degree(2, false, semitone_flat) } },
      { 62, { def::command::chord_degree, make_degree(2, false               ) } },
      { 63, { def::command::chord_degree, make_degree(3, false, semitone_flat) } },
      { 64, { def::command::chord_degree, make_degree(3, false               ) } },
      { 65, { def::command::chord_degree, make_degree(4, false               ) } },
      { 66, { def::command::chord_degree, make_degree(5, false, semitone_flat) } },
      { 67, { def::command::chord_degree, make_degree(5, false               ) } },
      { 68, { def::command::chord_degree, make_degree(6, false, semitone_flat) } },
      { 69, { def::command::chord_degree, make_degree(6, false               ) } },
      { 70, { def::command::chord_degree, make_degree(7, false, semitone_flat) } },
      { 71, { def::command::chord_degree, make_degree(7, false               ) } },
    };
    for (const auto& cp : note_cp_table) {
      control_mapping[0].midinote.setCommandParamArray(cp.note, cp.command_param);
    }
  }

  // command_mapping_internal = &(control_mapping[control_mapping[1].internal.empty() ? 0 : 1].internal);
  // command_mapping_external = &(control_mapping[control_mapping[1].external.empty() ? 0 : 1].external);
  // command_mapping_midinote = &(control_mapping[control_mapping[1].midinote.empty() ? 0 : 1].midinote);
  command_mapping_internal.assign(control_mapping[0].internal);
  command_mapping_external.assign(control_mapping[0].external);
  command_mapping_midinote.assign(control_mapping[0].midinote);

  // マッピング１のうち値があるものを優先的に使用する
  if (!control_mapping[1].internal.empty()) {
    for (int i = 0; i < def::hw::max_main_button; ++i) {
      auto command_param_array = control_mapping[1].internal.getCommandParamArray(i);
      if (!command_param_array.empty()) {
        command_mapping_internal.setCommandParamArray(i, command_param_array);
      }
    }
  }
  if (!control_mapping[1].external.empty()) {
    for (int i = 0; i < def::hw::max_button_mask; ++i) {
      auto command_param_array = control_mapping[1].external.getCommandParamArray(i);
      if (!command_param_array.empty()) {
        command_mapping_external.setCommandParamArray(i, command_param_array);
      }
    }
  }
  if (!control_mapping[1].midinote.empty()) {
    for (int i = 0; i < def::midi::max_note; ++i) {
      auto command_param_array = control_mapping[1].midinote.getCommandParamArray(i);
      if (!command_param_array.empty()) {
        command_mapping_midinote.setCommandParamArray(i, command_param_array);
      }
    }
  }
  
}
//-------------------------------------------------------------------------

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
  user_setting.setMasterVolume(75);

  // スロット選択
  runtime_info.setPlaySlot(0);

  // 編集時のエンコーダ２のターゲット設定
  chord_play.setEditEnc2Target(def::command::edit_enc2_target_t::program);

  // 演奏時ベロシティ設定
  runtime_info.setPressVelocity(127);


  command_mapping_port_b.reset();
  for (int i = 0; i < def::hw::max_port_b_pins; ++i) {
    command_mapping_port_b.setCommandParamArray(i, def::command::command_mapping_port_b_table[i]);
  }

  { // InstaChord連携用のコントロールチェンジへの機能マッピング
    // CC15: キーチェンジ
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

    // CC16: 演奏操作
    command_mapping_midicc16.reset();
    static constexpr const note_cp_t cc16_cp_table[] = {
//    {  1,                                                                               }, // 十字ボタン中央  
      {  2, { def::command::slot_select_ud  , def::command::slot_select_ud_t::slot_next } }, // 十字ボタン上(楽器順送り)
      {  3, { def::command::slot_select_ud  , def::command::slot_select_ud_t::slot_prev } }, // 十字ボタン下(楽器逆送り)
//    {  4,                                                                               }, // 十字ボタン♭  
//    {  5,                                                                               }, // 十字ボタン＃  
//    {  6,                                                                               }, // 21ボタン pos  
      {  7, { def::command::internal_button , 21                                   } }, // 21ボタン menu
      {  8, { def::command::internal_button , 27                                   } }, // 21ボタン key
      {  9, { def::command::chord_semitone  ,  1                                   } }, // 21ボタン ♭
      { 10, { def::command::chord_minor_swap,  1                                   } }, // 21ボタン ～
      { 11, { def::command::chord_modifier  , KANTANMusic_Modifier_m7_5            } }, // 21ボタン コード種 m7-5
      { 12, { def::command::chord_modifier  , KANTANMusic_Modifier_7               } }, // 21ボタン コード種 7
      { 13, { def::command::chord_modifier  , KANTANMusic_Modifier_M7              } }, // 21ボタン コード種 M7
      { 14, { def::command::chord_modifier  , KANTANMusic_Modifier_sus4            } }, // 21ボタン コード種 sus4
      { 15, { def::command::chord_modifier  , KANTANMusic_Modifier_dim             } }, // 21ボタン コード種 dim
      { 16, { def::command::chord_modifier  , KANTANMusic_Modifier_Add9            } }, // 21ボタン コード種 add9
      { 17, { def::command::chord_modifier  , KANTANMusic_Modifier_aug             } }, // 21ボタン コード種 aug
      { 18, { def::command::chord_degree    , make_degree(1                      ) } }, // 21ボタン ルート根1
      { 19, { def::command::chord_degree    , make_degree(2                      ) } }, // 21ボタン ルート根2
      { 20, { def::command::chord_degree    , make_degree(3                      ) } }, // 21ボタン ルート根3
      { 21, { def::command::chord_degree    , make_degree(4                      ) } }, // 21ボタン ルート根4
      { 22, { def::command::chord_degree    , make_degree(5                      ) } }, // 21ボタン ルート根5
      { 23, { def::command::chord_degree    , make_degree(6                      ) } }, // 21ボタン ルート根6
      { 24, { def::command::chord_degree    , make_degree(7                      ) } }, // 21ボタン ルート根7
      { 25, { def::command::chord_degree    , make_degree(3, false, semitone_flat) } }, // 21ボタン ルート根8
      { 26, { def::command::chord_degree    , make_degree(7, false, semitone_flat) } }, // 21ボタン ルート根9
      { 27, { def::command::chord_degree    , make_degree(6                      ) } }, // 21ボタン ルート根1(短調モード時)
      { 28, { def::command::chord_degree    , make_degree(7                      ) } }, // 21ボタン ルート根2(短調モード時)
      { 29, { def::command::chord_degree    , make_degree(1                      ) } }, // 21ボタン ルート根3(短調モード時)
      { 30, { def::command::chord_degree    , make_degree(2                      ) } }, // 21ボタン ルート根4(短調モード時)
      { 31, { def::command::chord_degree    , make_degree(3, true                ) } }, // 21ボタン ルート根5(短調モード時)
      { 32, { def::command::chord_degree    , make_degree(4                      ) } }, // 21ボタン ルート根6(短調モード時)
      { 33, { def::command::chord_degree    , make_degree(5                      ) } }, // 21ボタン ルート根7(短調モード時)
      { 34, { def::command::chord_degree    , make_degree(3, false, semitone_flat) } }, // 21ボタン ルート根8(短調モード時)
      { 35, { def::command::chord_degree    , make_degree(7, false, semitone_flat) } }, // 21ボタン ルート根9(短調モード時)
    };
    for (const auto& cp : cc16_cp_table) {
      command_mapping_midicc16.setCommandParamArray(cp.note, cp.command_param);
    }
  }

  control_mapping[0].internal.reset();
  control_mapping[0].external.reset();
  control_mapping[0].midinote.reset();
  updateControlMapping();

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


bool system_registry_t::save_impl(const char* filename)
{
  // 各種設定を内蔵フラッシュに保存
  auto mem = file_manage.createMemoryInfo(def::app::max_file_len);
  mem->filename = filename;
  mem->dir_type = def::app::data_type_t::data_system;
  size_t len = 0;
  if (strcmp(filename, def::app::filename_setting) == 0) {
    len = saveSettingJSON(mem->data, def::app::max_file_len);
  } else if (strcmp(filename, def::app::filename_resume) == 0) {
    len = saveResumeJSON(mem->data, def::app::max_file_len);
  } else if (strcmp(filename, def::app::filename_mapping_device) == 0) {
    len = control_mapping[0].saveJSON(mem->data, def::app::max_file_len);
  } else if (strcmp(filename, def::app::filename_mapping_song) == 0) {
    len = control_mapping[1].saveJSON(mem->data, def::app::max_file_len);
  } else {
    mem->release();
    return false;
  }
  mem->size = len;
M5_LOGV("save_impl %s %d", filename, (int)len);

  return file_manage.saveFile(mem->dir_type, mem->index);
}

// 設定を保存する
bool system_registry_t::save(void)
{
  bool result = true;

  uint32_t crc;
  crc = calcSettingCRC32();
  if (_last_setting_crc32 != crc) {
    if (save_impl(def::app::filename_setting)) {
      _last_setting_crc32 = crc;
    } else {
      result = false;
    }
  }
  crc = calcMappingCRC32();
  if (_last_mapping_crc32 != crc) {
    bool result_device = save_impl(def::app::filename_mapping_device);
    bool result_song = save_impl(def::app::filename_mapping_song);
// printf("save mapping device=%d song=%d\n", result_device ? 1 : 0, result_song ? 1 : 0);
    if (result_device && result_song) {
      _last_mapping_crc32 = crc;
    } else {
      result = false;
    }
  }
  crc = calcResumeCRC32();
  if (_last_resume_crc32 != crc) {
    if (save_impl(def::app::filename_resume)) {
      _last_resume_crc32 = crc;
    } else {
      result = false;
    }
  }
  return result;
}

// 設定を読み込む
bool system_registry_t::loadSetting(void)
{
  bool result = false;
  auto mem = file_manage.loadFile(def::app::data_type_t::data_system, def::app::filename_setting);
  if (mem != nullptr) {
    result = loadSettingJSON(mem->data, mem->size);
  }

  return result;
}

bool system_registry_t::loadMapping(void)
{
  bool result = false;
  {
    auto mem = file_manage.loadFile(def::app::data_type_t::data_system, def::app::filename_mapping_device);
    if (mem != nullptr) {
      result =  control_mapping[0].loadJSON(mem->data, mem->size);
    }
  }
  {
    auto mem = file_manage.loadFile(def::app::data_type_t::data_system, def::app::filename_mapping_song);
    if (mem != nullptr) {
      result =  control_mapping[1].loadJSON(mem->data, mem->size);
    }
  }

  return result;
}

bool system_registry_t::loadResume(void)
{
  bool result = false;
  auto mem = file_manage.loadFile(def::app::data_type_t::data_system, def::app::filename_resume);
  if (mem != nullptr) {
    result = loadResumeJSON(mem->data, mem->size);
  }

  if (result)
  {
    checkSongModified();
  } else {
    auto mem = file_manage.loadFile(def::app::data_type_t::data_song_preset, (int)0);
    if (mem != nullptr) {
      operator_command.addQueue( { def::command::file_load_notify, mem->index } );
    } else {

    }
  }
  return result;
}

bool system_registry_t::load(void)
{
  reset();

  bool result = true;
  if (!loadSetting()) {
    result = false;
  }
  if (!loadMapping()) {
    result = false;
  }
  if (!loadResume()) {
    result = false;
  }
  updateCRC32();
  return result;
}

void system_registry_t::updateCRC32(void)
{
  _last_setting_crc32 = calcSettingCRC32();
  _last_mapping_crc32 = calcMappingCRC32();
  _last_resume_crc32 = calcResumeCRC32();
}

//-------------------------------------------------------------------------
uint32_t system_registry_t::calcSettingCRC32(void) const
{
  uint32_t crc = user_setting.crc32();
  crc = midi_port_setting.crc32(crc);
  return crc;
}

uint32_t system_registry_t::calcMappingCRC32(void) const
{
  // コントロールマッピングデータのCRC32を計算する
  // ※ リジューム目的なのでソングに付帯するコントロールマッピングも含む
  uint32_t crc = control_mapping[0].crc32();
  crc = control_mapping[1].crc32(crc);
  return crc;
}

uint32_t system_registry_t::calcResumeCRC32(void) const
{
  uint32_t crc = song_data.crc32();
  crc = calc_crc32(&unchanged_song_crc32, sizeof(unchanged_song_crc32), crc);
  crc = calc_crc32(&unchanged_kmap_crc32, sizeof(unchanged_kmap_crc32), crc);
  return crc;
}

uint32_t system_registry_t::calcSongCRC32(void) const
{
  // ソングデータ本体のCRC32を計算する
  uint32_t crc = system_registry->song_data.crc32();
  return crc;
}
uint32_t system_registry_t::calcKmapCRC32(void) const
{
  // ソングに付帯するコントロールマッピングデータのCRC32を計算する
  // ※ デバイス側コントロールマッピングは除外する
  uint32_t crc = control_mapping[1].crc32();
  return crc;
}

void system_registry_t::checkSongModified(void) const {
    auto song_crc32 = calcSongCRC32();
    auto kmap_crc32 = calcKmapCRC32();
    bool mod = song_crc32 != unchanged_song_crc32;
    mod |= kmap_crc32 != unchanged_kmap_crc32;
M5_LOGV("checkSongModified: song_crc32=0x%08X (unchanged=0x%08X) kmap_crc32=0x%08X (unchanged=0x%08X) mod=%d", song_crc32, unchanged_song_crc32, kmap_crc32, unchanged_kmap_crc32, mod);
    system_registry->runtime_info.setSongModified(mod);
}

//-------------------------------------------------------------------------

// 動作に影響のあるパラメータを演奏タスクに同期する。
// 設定のリセットやロード後に呼び出すこと。
void system_registry_t::syncParams(void)
{
  // マスターボリューム設定
  operator_command.addQueue( { def::command::master_vol_set, user_setting.getMasterVolume() } );

  // スロット選択
  operator_command.addQueue( { def::command::slot_select, runtime_info.getPlaySlot() + 1 } );

  // 編集時のエンコーダ２のターゲット設定
  operator_command.addQueue( { def::command::edit_enc2_target, chord_play.getEditEnc2Target() } );

  // 演奏時ベロシティ設定
  operator_command.addQueue( { def::command::set_velocity, runtime_info.getPressVelocity() } );
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
{ auto i = (uint8_t)system_registry->user_setting.getLanguage(); return text[i] ? text[i] : text[0]; }


static bool saveMappingInternal(system_registry_t::reg_command_mapping_t* mapping, JsonObject &json, const def::ctrl_assign::control_assignment_t* table)
{
  size_t count = mapping->getButtonCount();
  for (int num = 0; num < count; ++num) {
    auto cmd = mapping->getCommandParamArray(num);
    if (cmd.empty()) { continue; }
    auto index = def::ctrl_assign::get_index_from_command(table, cmd);
    if (index < 0) { continue; }
    json[std::to_string(num + 1)] = table[index].jsonname;
  }
  return true;
}

static bool loadMappingInternal(system_registry_t::reg_command_mapping_t* mapping, const JsonObject &json, const def::ctrl_assign::control_assignment_t* table)
{
  if (!json.isNull()) {
    mapping->reset();
    size_t count = mapping->getButtonCount();
    for (int num = 0; num < count; ++num) {
      auto name = json[std::to_string(num + 1)].as<const char*>();
      if (name == nullptr) { continue; }
      auto index = def::ctrl_assign::get_index_from_jsonname(table, name);
      if (index < 0) { continue; }
      mapping->setCommandParamArray(num, table[index].command);
    }
    return true;
  }
  return false;
}

//-------------------------------------------------------------------------
bool system_registry_t::saveSettingInternal(JsonVariant& json_root)
{
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
    json["instachord_link_style"] = (uint8_t)midi_port_setting.getInstaChordLinkStyle();
    json["usb_mode"] = (uint8_t)midi_port_setting.getUSBMode();
    json["usb_power"] = (uint8_t)midi_port_setting.getUSBPowerEnabled();
  }

/* 以下廃止、新仕様では control_mapping に統一
  auto json_key_mapping = json_root["key_mapping"].to<JsonObject>();
  {
    {
      auto json = json_key_mapping["chord_play"].to<JsonObject>();
      saveMappingInternal(&command_mapping_custom_main, json, def::ctrl_assign::playbutton_table);
    }
    {
      auto json = json_key_mapping["external"].to<JsonObject>();
      saveMappingInternal(&command_mapping_external, json, def::ctrl_assign::external_table);
    }
    {
      auto json = json_key_mapping["midinote"].to<JsonObject>();
      saveMappingInternal(&command_mapping_midinote, json, def::ctrl_assign::external_table);
    }
  }
//*/
  return true;
}

size_t system_registry_t::saveSettingJSON(uint8_t* data, size_t data_length)
{
  ArduinoJson::JsonDocument json_root;

  json_root["format"] = "KANTANPlayCore";

  {
    json_root["type"] = "Config";
    json_root["version"] = 2;
    auto variant = json_root.as<JsonVariant>();
    saveSettingInternal(variant);
  }

  auto result = serializeJson(json_root, (char*)data, data_length);
// ESP_LOGV("sysreg", "saveSettingJSON result: %d\n", result);

  return result;
}

bool system_registry_t::loadSettingInternal(JsonVariant& json_root)
{
  auto data_version = json_root["version"].as<int>();

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
    midi_port_setting.setInstaChordLinkStyle((def::command::instachord_link_style_t)json["instachord_link_style"].as<uint8_t>());
    midi_port_setting.setUSBMode((def::command::usb_mode_t)json["usb_mode"].as<uint8_t>());
    midi_port_setting.setUSBPowerEnabled(json["usb_power"].as<bool>());
  }

  {
    // control_assignment::play button ( 旧名 key mapping )
    auto json_key_mapping = json_root["key_mapping"].as<JsonObject>();
    if (!json_key_mapping.isNull())
    {
      {
        auto json = json_key_mapping["chord_play"].as<JsonObject>();
        if (!json.isNull()) {
          if (data_version == 1 && json["9"] == "7") {
            // 旧仕様のキーアサイン設定で Degree 7 と 7th を混同するケースがあったので、ここで古いデータを無視する
          } else {
            loadMappingInternal(&control_mapping[0].internal, json, def::ctrl_assign::playbutton_table);
            // loadMappingInternal(&command_mapping_custom_main, json, def::ctrl_assign::playbutton_table);
          }
        }
      }
      {
        auto json = json_key_mapping["external"].as<JsonObject>();
        loadMappingInternal(&control_mapping[0].external, json, def::ctrl_assign::external_table);
        // loadMappingInternal(&command_mapping_external, json, def::ctrl_assign::external_table);
      }
      {
        auto json = json_key_mapping["midinote"].as<JsonObject>();
        loadMappingInternal(&control_mapping[0].midinote, json, def::ctrl_assign::external_table);
        // loadMappingInternal(&command_mapping_midinote, json, def::ctrl_assign::external_table);
      }
    }
  }
  return true;
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

  auto data_version = json_root["version"].as<int>();
  if (data_version < 4 && json_root["type"] == "Config")
  {
    auto variant = json_root.as<JsonVariant>();
    return loadSettingInternal(variant);
  }
/*
  auto variant = json_root["setting"].as<JsonVariant>();
  if (!variant.isNull()) {
    return loadSettingInternal(variant);
  }
//*/
  return false;
}

bool system_registry_t::control_mapping_t::saveJSON(JsonVariant &json)
{
  json["type"] = "Mapping";
  json["version"] = 1;

  if (!internal.empty())
  {
    auto json_internal = json["internal"].to<JsonObject>();
    saveMappingInternal(&internal, json_internal, def::ctrl_assign::playbutton_table);
  }
  if (!external.empty())
  {
    auto json_external = json["external"].to<JsonObject>();
    saveMappingInternal(&external, json_external, def::ctrl_assign::external_table);
  }
  if (!midinote.empty())
  {
    auto json_midinote = json["midinote"].to<JsonObject>();
    saveMappingInternal(&midinote, json_midinote, def::ctrl_assign::external_table);
  }
  return false;
}

bool system_registry_t::control_mapping_t::loadJSON(const JsonVariant &json)
{
  bool res = false;
  auto data_version = json["version"].as<int>();
  if (data_version <= 1 && json["type"] == "Mapping")
  {
    reset();
    res = true;
    {
      auto json_internal = json["internal"].as<JsonObject>();
      if (!json_internal.isNull()) {
        loadMappingInternal(&internal, json_internal, def::ctrl_assign::playbutton_table);
      }
    }
    {
      auto json_external = json["external"].as<JsonObject>();
      if (!json_external.isNull()) {
        loadMappingInternal(&external, json_external, def::ctrl_assign::external_table);
      }
    }
    {
      auto json_midinote = json["midinote"].as<JsonObject>();
      if (!json_midinote.isNull()) {
        loadMappingInternal(&midinote, json_midinote, def::ctrl_assign::external_table);
      }
    }
  }

  system_registry->updateControlMapping();

  return res;
}

size_t system_registry_t::control_mapping_t::saveJSON(uint8_t* data, size_t data_length)
{
  ArduinoJson::JsonDocument json_root;

  json_root["format"] = "KANTANPlayCore";

  {
    auto variant = json_root.as<JsonVariant>();
    saveJSON(variant);
  }

  auto result = serializeJson(json_root, (char*)data, data_length);
// ESP_LOGV("sysreg", "saveSettingJSON result: %d\n", result);

  return result;
}

bool system_registry_t::control_mapping_t::loadJSON(const uint8_t* data, size_t data_length)
{
  ArduinoJson::JsonDocument json_root;
  auto error = deserializeJson(json_root, (const char*)data, data_length);
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

  auto variant = json_root.as<JsonVariant>();
  return loadJSON(variant);
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

static int degree_param_to_str(const degree_param_t& param, char* buf, size_t bufsize)
{
  const char* semitone = "";
  const char* swap = "";
  switch (param.getSemitone()) {
  case semitone_t::semitone_flat:  semitone = "b"; break;
  case semitone_t::semitone_sharp: semitone = "#"; break;
  default: break;
  }
  if (param.getMinorSwap()) swap = "~";
  return snprintf(buf, bufsize, "%d%s%s", param.getDegree(), semitone, swap);
}

static void degree_param_from_str(const char* str, degree_param_t& param)
{
  param.setSemitone(semitone_t::semitone_none);
  param.setMinorSwap(false);

  int i = 0;
  int degree = 0;
  if (str[i] >= '0' && str[i] <= '9') {
    degree = str[i] - '0';
    ++i;
  }
  param.setDegree(degree);

  auto semitone = semitone_t::semitone_none;
  if (str[i] == 'b' || str[i] == '#') {
    semitone = (str[i] == 'b') ? semitone_t::semitone_flat : semitone_t::semitone_sharp;
    ++i;
  }
  param.setSemitone(semitone);

  bool swap = (str[i] == '~');
  param.setMinorSwap(swap);
}

bool system_registry_t::reg_sequence_timeline_t::saveJson(JsonVariant &json)
{
  char buf[32];
  sequence_chord_desc_t prev_desc;
  prev_desc.setSlotIndex(0xFF); // 強制的に最初のデータを保存させるため

  auto it = begin();
  auto it_e = end();
  for (; it != it_e; ++it)
  {
    auto &pair = *it;
    if (prev_desc == pair.second) { continue; }

    itoa(pair.first, buf, 10);
    auto obj = json[buf].to<JsonObject>();
    degree_param_to_str(pair.second.main_degree, buf, sizeof(buf));
    obj["main"] = buf;

    {
      auto modifier = pair.second.getModifier();
      if (KANTANMusic_Modifier_None != modifier) {
        obj["mod"] = def::command::command_name_table[def::command::chord_modifier][modifier];
      }
    }
    if (0 != pair.second.bass_degree.raw) {
      degree_param_to_str(pair.second.bass_degree, buf, sizeof(buf));
      obj["bass"] = buf;
    }
    auto slot_index = pair.second.getSlotIndex();
    if (prev_desc.getSlotIndex() != slot_index) {
      obj["slot"] = slot_index;
    }
    if (prev_desc.getPartBits() != pair.second.getPartBits()) {
      auto parts = obj["part"].to<JsonArray>();
      for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
      {
        if (pair.second.getPartEnable(part_index)) {
          parts.add(part_index);
        }
      }
    }
    prev_desc = pair.second;
  }
  return true;
}

bool system_registry_t::reg_sequence_timeline_t::loadJson(const JsonVariant &json)
{
  // decltype(_data) tmpdata;
  auto it = begin();
  size_t count = 0;
  size_t limit = max_count();

  sequence_chord_desc_t desc;
  for (auto kvp : json.as<JsonObject>())
  {
    uint_fast16_t step = atoi(kvp.key().c_str());
    auto obj = kvp.value().as<JsonObject>();

    {
      const char* main_str = obj["main"].as<const char*>();
      if (main_str != nullptr) {
        degree_param_from_str(main_str, desc.main_degree);
      }
    }
    {
      desc.bass_degree = degree_param_t(); // リセット
      const char* bass_str = obj["bass"].as<const char*>();
      if (bass_str != nullptr) {
        degree_param_from_str(bass_str, desc.bass_degree);
      }
    }

    {
      auto modifier = KANTANMusic_Modifier_None;
      const char* mod_str = obj["mod"].as<const char*>();
      if (mod_str != nullptr) {
        for (int i = 0; i < KANTANMusic_MAX_MODIFIER; ++i) {
          if (strcmp(mod_str, def::command::command_name_table[def::command::chord_modifier][i]) == 0) {
            modifier = (KANTANMusic_Modifier)i;
            break;
          }
        }
      }
      desc.setModifier(modifier);
    }

    if (obj["slot"].is<int>())
    {
      int slot_index = obj["slot"].as<int>();
      if (slot_index >= 0 && slot_index < def::app::max_slot) {
        desc.setSlotIndex((uint8_t)slot_index);
      }
    }
    if (obj["part"].is<JsonArray>())
    {
      auto parts = obj["part"].as<JsonArray>();
      if (!parts.isNull()) {
        desc.clearPartEnable();
        for (auto part_index : parts) {
          if (part_index >= 0 && part_index < def::app::max_chord_part) {
            desc.setPartEnable(part_index, true);
          }
        }
      }
    }
    it->first = step;
    it->second = desc;
    ++it;
    ++count;
    if (count >= limit) { break; }
  }
  _data_count = count;

  return true;
}

static bool saveSequenceInternal(system_registry_t::sequence_data_t* sequence, JsonVariant &json)
{
  json["version"] = 1;
  json["length"] = sequence->info.getLength();
  auto json_timeline = json["timeline"].to<JsonVariant>();
  return sequence->timeline.saveJson(json_timeline);
}

static bool loadSequenceInternal(system_registry_t::sequence_data_t* sequence, const JsonVariant &json)
{
  if (json.isNull()) { return false; }
  if (json.size() == 0) { return false; }

  if (json["version"] > 1)
  {
    M5_LOGV("version mismatch: %d", json["version"].as<int>());
  }
  auto json_timeline = json["timeline"].as<JsonVariant>();

  sequence->reset();
  sequence->timeline.loadJson(json_timeline);
  sequence->info.setLength(json["length"].as<int>());

  return true;
}

static bool saveSongInternal(system_registry_t::song_data_t* song, JsonVariant &json)
{
  json["version"] = 2;
  json["tempo"] = song->song_info.getTempo();
  json["swing"] = song->song_info.getSwing();
  json["base_key"] = system_registry->runtime_info.getMasterKey();

  if (song->sequence.info.getLength() > 0)
  {
    auto json_sequence = json["sequence"].to<JsonVariant>();
    saveSequenceInternal(&song->sequence, json_sequence);
  }

  auto drum_note = json["drum_note"].to<JsonArray>();
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
  {
    auto gp = &song->chord_part_drum[part_index];
    auto drum_note_array = drum_note.add<JsonArray>();
    for (int pitch = 0; pitch < def::app::max_pitch_with_drum; ++pitch)
    {
      drum_note_array.add(gp->getDrumNoteNumber(pitch));
    }
  }

  system_registry_t::kanplay_slot_t slot_default;
  slot_default.init();
  slot_default.reset();

  auto json_slot = json["slot"].to<JsonArray>();
  for (int slot_index = 0; slot_index < def::app::max_slot; ++slot_index)
  {
    auto reg_slot = &song->slot[slot_index];
    auto reg_chord_part = reg_slot->chord_part;
    auto slot_info = json_slot.add<JsonObject>();
    if (*reg_slot == slot_default) { continue; }
    if (slot_index != 0 && *reg_slot == song->slot[slot_index - 1]) { continue; }

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
  return true;
}

static bool loadSongInternal(system_registry_t::song_data_t* song, const JsonVariant &json)
{
  if (json["version"] > 2)
  {
    M5_LOGV("version mismatch: %d", json["version"].as<int>());
  }

  song->song_info.setTempo(json["tempo"].as<int>());
  song->song_info.setSwing(json["swing"].as<int>());
  song->song_info.setBaseKey(json["base_key"].as<int>());

  system_registry->runtime_info.setMasterKey(json["base_key"].as<int>());

  {
    loadSequenceInternal(&(song->sequence), json["sequence"].as<JsonVariant>());
    system_registry->runtime_info.setSequenceStepIndex(0);
  }

  auto drum_note = json["drum_note"].as<JsonArray>();
  for (int part_index = 0; part_index < def::app::max_chord_part; ++part_index)
  {
    auto gp = &song->chord_part_drum[part_index];
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
    auto reg_slot = &song->slot[slot_index];
    if (slot_index >= slot_size || 
        json_slot[slot_index].as<JsonObject>().size() == 0)
    {
      if (slot_index > 0) { // 先頭以外のスロットで項目が省略されている場合は前のスロットの内容をコピー
        song->slot[slot_index].assign(song->slot[slot_index - 1]);
// M5_LOGV("slot copy: %d", slot_index);
      }
      continue;
    }

    auto reg_chord_part = reg_slot->chord_part;
    auto slot_info = json_slot[slot_index].as<JsonObject>();
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

size_t system_registry_t::song_data_t::saveSongJSON(uint8_t* data_buffer, size_t data_length)
{
  ArduinoJson::JsonDocument json;

  json["format"] = "KANTANPlayCore";
  json["type"] = "Song";

  auto variant = json.as<JsonVariant>();
  saveSongInternal(this, variant);

  return serializeJson(json, (char*)data_buffer, data_length);
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

  auto variant = json.as<JsonVariant>();
  return loadSongInternal(this, variant);
}

size_t system_registry_t::saveResumeJSON(uint8_t* data, size_t data_length)
{
  ArduinoJson::JsonDocument json;

  json["format"] = "KANTANPlayCore";
  json["type"] = "Resume";

  json["version"] = 1;

  json["slot_index"] = runtime_info.getPlaySlot() + 1;

  // 現在のソングデータの情報を保存
  auto json_song = json["song"].to<JsonVariant>();
  saveSongInternal(&song_data, json_song);

  // 未変更のソングデータの情報を保存
  auto json_unchanged_song = json["unchanged_song"].to<JsonVariant>();
  {
    json_unchanged_song["filename"] =          file_manage.getLatestFileName();
    json_unchanged_song["datatype"] = (uint8_t)file_manage.getLatestDataType();
    json_unchanged_song["song_crc32"] = unchanged_song_crc32;
    json_unchanged_song["kmap_crc32"] = unchanged_kmap_crc32;
  }

  return serializeJson(json, (char*)data, data_length);
}


bool system_registry_t::loadResumeJSON(const uint8_t* data, size_t data_length)
{
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

  if (json["type"] != "Resume")
  {
    M5_LOGE("type error: %s", json["type"].as<const char*>());
    return false;
  }
  if (json["version"] > 1)
  {
    M5_LOGV("version mismatch: %d", json["version"].as<int>());
  }

  bool result = false;

  // 最後に開いたソングデータの情報
  // 初期値として空の情報をセットしておく
  auto song_datatype = kanplay_ns::def::app::data_type_t::data_song_preset;
  file_manage.updateFileList(song_datatype);
  const char* song_filename = file_manage.getDirManage(song_datatype)->getInfo(0)->filename;

  auto json_unchanged_song = json["unchanged_song"].as<JsonVariant>();
  if (!json_unchanged_song.isNull()) {
    if (json_unchanged_song["song_crc32"].isNull()) {
      loadSongInternal(&song_data, json_unchanged_song);
      unchanged_song_crc32 = system_registry->calcSongCRC32();
    } else {
      unchanged_song_crc32 = json_unchanged_song["song_crc32"].as<uint32_t>();
    }
    if (!json_unchanged_song["kmap_crc32"].isNull()) {
      unchanged_kmap_crc32 = json_unchanged_song["kmap_crc32"].as<uint32_t>();
    }
    // ファイル名情報があれば取り込む
    auto fn = json_unchanged_song["filename"].as<const char*>();
    if (fn != nullptr && fn[0] != 0) {
      song_filename = fn;
      song_datatype = (def::app::data_type_t)(json_unchanged_song["datatype"].as<uint8_t>());
    }
  }
  file_manage.setLatestFileInfo(song_datatype, song_filename);
  auto json_song = json["song"].as<JsonVariant>();
  if (!json_song.isNull()) {
    result = loadSongInternal(&song_data, json_song);
  }

  auto slot_index = json["slot_index"].as<int>();
  operator_command.addQueue( { def::command::slot_select, slot_index } );

  return result;
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
