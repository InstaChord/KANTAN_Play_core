// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_COMMON_DEFINE_HPP
#define KANPLAY_COMMON_DEFINE_HPP

#include "kantan-music/include/KANTANMusic.h"

#include <stdint.h>
#include <stddef.h>

#if __has_include (<sdkconfig.h>)
 #include <sdkconfig.h>
 #include <hal/gpio_types.h>
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
namespace def::lang {
  enum class language_t : uint8_t {
    en = 0,
    ja,
    max_language,
  };
};
//-------------------------------------------------------------------------
class text_t {
public:
  virtual const char* get(void) const = 0;
};

class simple_text_t : public text_t {
  const char* _text;
public:
  const char* get(void) const override { return _text; }
  constexpr simple_text_t(const char* text) : _text(text) {}
};

class text_array_t {
public:
  virtual const text_t* at(size_t index) const = 0;
  virtual size_t size(void) const = 0;
};

class simple_text_array_t : public text_array_t {
  const simple_text_t* _text_array;
  size_t _size;
public:
  const text_t* at(size_t index) const override { return &_text_array[index]; }
  size_t size(void) const override { return _size; }
  constexpr simple_text_array_t(size_t size, const simple_text_t* text_array) : _text_array { text_array }, _size { size } {}
};

class localize_text_t : public text_t {
  const char* text[(uint8_t)def::lang::language_t::max_language] = { nullptr, };
public:
  const char* get(void) const override;
  constexpr localize_text_t(const char* en_ = nullptr, const char* ja_ = nullptr) : text { en_, ja_ } {}
};

class localize_text_array_t : public text_array_t {
  const localize_text_t* _text_array;
  size_t _size;
public:
  const text_t* at(size_t index) const override { return &_text_array[index]; }
  size_t size(void) const override { return _size; }
  constexpr localize_text_array_t(size_t size, const localize_text_t* text_array) : _text_array { text_array }, _size { size } {}
};

//-------------------------------------------------------------------------
namespace def {

  enum menu_category_t : uint8_t {
    menu_none = 0,
    menu_system = 1,
    menu_part,
    menu_slot,
    menu_file,
  };
  enum notify_type_t : uint8_t {
    NOTIFY_NONE,
    NOTIFY_STORAGE_OPEN,
    NOTIFY_FILE_LOAD,
    NOTIFY_FILE_SAVE,
    NOTIFY_COPY_SLOT_SETTING,
    NOTIFY_PASTE_SLOT_SETTING,
    NOTIFY_COPY_PART_SETTING,
    NOTIFY_PASTE_PART_SETTING,
    NOTIFY_CLEAR_ALL_NOTES,
    NOTIFY_ALL_RESET,
    NOTIFY_DEVELOPER_MODE,
    MESSAGE_NEED_RESTART,
  };
  static constexpr const localize_text_array_t notify_name_array = { 12, (const localize_text_t[]){
    { "-"                 , nullptr },
    { "Storage Open"      , nullptr },
    { "File Load"         , nullptr },
    { "File Save"         , nullptr },
    { "Copy Slot"         , nullptr },
    { "Paste Slot"        , nullptr },
    { "Copy Part"         , nullptr },
    { "Paste Part"        , nullptr },
    { "Clear all notes"   , nullptr },
    { "All Reset"         , nullptr },
    { "Developer"         , nullptr },
    { "Please restart now", nullptr },
  }};

  enum qrcode_type_t : uint8_t {
    QRCODE_NONE,
    QRCODE_URL_MANUAL,
    QRCODE_AP_SSID,
    QRCODE_URL_DEVICE,
    QRCODE_MAX
  };

  namespace button_bitmask {
// 操作可能な本体ボタンを32ビットのビットマスクで表現
    enum button_bitmask_t : uint32_t {
/*
Button Index mapping
                  <21|23|22> KNOB
<30|31>ENC3       <24|26|25> ENC1
    +--+--+--+--+ <27|29|28> ENC2
+--+|15|16|17|18| +--+
|19+--+--+--+--+--+20|
+--|10|11|12|13|14|--+
   +--+--+--+--+--+
   | 5| 6| 7| 8| 9|
   +--+--+--+--+--+
   | 0| 1| 2| 3| 4|
   +--+--+--+--+--+
//*/
      MAIN_01   = 0x00000001,
      MAIN_02   = 0x00000002,
      MAIN_03   = 0x00000004,
      MAIN_04   = 0x00000008,
      MAIN_05   = 0x00000010,
      MAIN_06   = 0x00000020,
      MAIN_07   = 0x00000040,
      MAIN_08   = 0x00000080,
      MAIN_09   = 0x00000100,
      MAIN_10   = 0x00000200,
      MAIN_11   = 0x00000400,
      MAIN_12   = 0x00000800,
      MAIN_13   = 0x00001000,
      MAIN_14   = 0x00002000,
      MAIN_15   = 0x00004000,
      SUB_1     = 0x00008000,
      SUB_2     = 0x00010000,
      SUB_3     = 0x00020000,
      SUB_4     = 0x00040000,
      SIDE_1    = 0x00080000,
      SIDE_2    = 0x00100000,
      KNOB_L    = 0x00200000,
      KNOB_R    = 0x00400000,
      KNOB_K    = 0x00800000,
      ENC1_DOWN = 0x01000000,
      ENC1_UP   = 0x02000000,
      ENC1_PUSH = 0x04000000,
      ENC2_DOWN = 0x08000000,
      ENC2_UP   = 0x10000000,
      ENC2_PUSH = 0x20000000,
      ENC3_DOWN = 0x40000000,
      ENC3_UP   = 0x80000000,
    };
  };
  // かんぷれアプリの演奏モード
  namespace playmode {
    enum playmode_t : uint8_t {
      unknown = 0,
      chord_mode,       // コード演奏モード
      note_mode,        // ノート演奏モード
      drum_mode,        // ドラム演奏モード
      chord_edit_mode,  // コード編集モード (厳密には演奏モードではないが処理の都合上ここに含める)
      menu_mode,        // メニュー表示モード(厳密には演奏モードではないが処理の都合上ここに含める)
      playmode_max,
    };

    static constexpr const char* playmode_name_table[] = {
      "-", "Chord", "Note", "Drum", "ChordEdit",
    };
  };
  namespace midi {
    enum channel : uint8_t {
      channel_1 = 0, channel_2, channel_3, channel_4, channel_5, channel_6, channel_7, channel_8,
      channel_9, channel_10, channel_11, channel_12, channel_13, channel_14, channel_15, channel_16,
      channel_max,
    };
    enum status_byte_t : uint8_t {
      note_off = 0x80,
      note_on = 0x90,
      polyphonic_key_pressure = 0xA0,
      control_change = 0xB0,
      program_change = 0xC0,
      channel_pressure = 0xD0,
      pitch_bend = 0xE0,
      system_exclusive = 0xF0,
      system_common = 0xF1,
      song_position_pointer = 0xF2,
      song_select = 0xF3,
      tune_request = 0xF6,
      end_of_exclusive = 0xF7,
      timing_clock = 0xF8,
      start = 0xFA,
      continue_ = 0xFB,
      stop = 0xFC,
      active_sensing = 0xFE,
      system_reset = 0xFF,
    };

    static constexpr const size_t max_note = 128;

    static constexpr const simple_text_array_t program_name_table = { 129, (const simple_text_t[]){
    // static constexpr const char* program_name_table[129] = {
    "Piano1(Ac.)",  "Piano2(Brt.)",  "Piano3(E-Grd)",  "Honky tonk",
    "E-Piano 1",  "E-Piano 2",  "Harpsichord",  "Clavi",
    "Celesta",  "Glockenspiel",  "Music box",  "Vibraphone",
    "Marimba",  "Xylophone",  "Tubular Bell",  "Santur",
    "D.bar Organ",  "Prc. Organ",  "Rock Organ",  "Church Organ",
    "Reed Organ",  "Accordion",  "Harmonica",  "Bandoneon",
    "Ac-Gt.Nylon",  "Ac-Gt.Steel",  "E-Gt.Jazz",  "E-Gt.Clean",
    "E-Gt.Muted",  "E-Gt.OD",  "E-Gt.Dist.",  "E-Gt.Harm.",
    "Ac-Bass",  "E-Bass(fing.)",  "E-Bass(pick)",  "Fretless Bs.",
    "Slap Bass1",  "Slap Bass2",  "Synth Bass1",  "Synth Bass2",
    "Violin",  "Viola",  "Cello",  "Contrabass",
    "Tremolo",  "Pizzicato",  "Harp",  "Timpani",
    "Strings 1",  "Strings 2",  "Syn.Strings1",  "Syn.Strings2",
    "Voice Aahas",  "Voice Oohs",  "Synth Voice",  "Orchestra Hit",
    "Trumpet",  "Trombone",  "Tuba",  "Mut. Trumpet",
    "French horn",  "Brass Sect.",  "Syn. Brass1",  "Syn. Brass2",
    "Soprano Sax",  "Alto Sax",  "Tenor Sax",  "Baritone Sax",
    "Oboe",  "English Horn",  "Bassoon",  "Clarinet",
    "Piccolo",  "Flute",  "Recorder",  "Pan Flute",
    "Blown Bottle",  "Shakuhachi",  "Whistle",  "Ocarina",
    "LD1.square",  "LD2.saw",  "LD3.calliope",  "LD4.chiff",
    "LD5.charang",  "LD6.voice",  "LD7.fifths",  "LD8.bs+lead",
    "P1.Fantasia",  "P2.warm",  "P3.polysynth",  "P4.choir",
    "P5.bowed",  "P6.metallic",  "P7.halo",  "P8.sweep",
    "Fx1.rain",  "Fx2.snd.track",  "Fx3.crystal",  "Fx4.atmo.",
    "Fx5.bright.",  "Fx6.goblins",  "Fx7.echoes",  "Fx8.sci-fi",
    "Sitar",  "Banjo",  "Shamisen",  "Koto",
    "Kalimba",  "Bagpipe",  "Fiddle",  "Shanai",
    "Tinkle Bell",  "Agogo",  "Steel Drums",  "Woodblock",
    "Taiko Drum",  "Melodic Tom",  "Synth Drum",  "Rev. Cymbal",
    "Guitar Noise",  "Breath Noise",  "Seashore",  "Bird Tweet",
    "Tel Ring",  "Helicopter",  "Applause",  "Gunshot",

    "Drum" // ドラムパート用の表示名として129個目の音色名をセットしておく
    }};

    static constexpr const uint8_t drum_note_name_min = 35;
    static constexpr const uint8_t drum_note_name_max = 81;
    static constexpr const simple_text_array_t drum_note_name_tbl = { 47, (const simple_text_t[]){
    "35 Acoustic Bass Drum", "36 Bass Drum"      , "37 Side Stick"     , "38 Acoustic Snare" , "39 Hand Clap",
    "40 Electric Snare"    , "41 Low Floor Tom"  , "42 Closed High-Hat", "43 High Floor Tom" , "44 Pedal High-Hat",
    "45 Low Tom"           , "46 Open High-Hat"  , "47 Low-Mid Tom"    , "48 High-Mid Tom"   , "49 Crash Symbal 1",
    "50 High Tom"          , "51 Ride Symbal 1"  , "52 Chinese Symbal" , "53 Ride Bell"      , "54 Tambourine",
    "55 Splash Symbal"     , "56 Cowbell"        , "57 Crash Symbal 2" , "58 Vibraslap"      , "59 Ride Symbal 2",
    "60 High Bongo"        , "61 Low Bongo"      , "62 Mute High Conga", "63 Open High Conga", "64 Low Conga",
    "65 High Timbale"      , "66 Low Timbale"    , "67 High Agogo"     , "68 Low Agogo"      , "69 Cabasa",
    "70 Maracas"           , "71 Short Whistle"  , "72 Long Whistle"   , "73 Short Guiro"    , "74 Long Guiro",
    "75 Claves"            , "76 High Wood Block", "77 Low Wood Block" , "78 Close Cuica"    , "79 Open Cuica",
    "80 Mute Triangle"     , "81 Open Triangle",
    }};

    // ドラムモードで画面上のボタンに表示するドラム名
    // TODO:表示名を決めていないものが数字で入っているので、適切な名前を設定する
    static constexpr const char* drum_name_tbl[] = {
      "0", "1", "2", "3", "4", "5", "6", "7",
      "8", "9", "10", "11", "12", "13", "14", "15",
      "16", "17", "18", "19", "20", "21", "22", "23",
      "24", "25", "26", "27", "28", "29", "30", "31",
      "32", "33", "34", "AcBD", "BD", "STK", "AcSD", "HAN",
      "SD", "FTO", "CHH", "LTO", "44", "MTO", "OHH", "HTO",
      "48", "CY1", "50", "RID", "52", "53", "TMB", "55",
      "56", "CY2", "58", "59", "COW", "61", "62", "63",
      "64", "65", "66", "67", "68", "69", "70", "71",
      "72", "73", "74", "75", "76", "77", "78", "79",
      "80", "81", "82", "83", "84", "85", "86", "87",
      "88", "89", "90", "91", "92", "93", "94", "95",
      "96", "97", "98", "99", "100", "101", "102", "103",
      "104", "105", "106", "107", "108", "109", "110", "111",
      "112", "113", "114", "115", "116", "117", "118", "119",
      "120", "121", "122", "123", "124", "125", "126", "127",
    };
  };

  namespace command {
    // 内部コマンド
    enum command_t : uint8_t {
      none = 0,
      menu_function,
      slot_select,
      play_mode_set,
      part_off,
      part_on,
      part_edit,
      note_button,
      drum_button,
      chord_degree,       // コード選択ボタン 1~7 
      chord_modifier,     // パラメータは KANTANMusic::Modifierと合わせる
      chord_minor_swap,   // メジャーマイナー入替
      chord_semitone,     // 半音シフト 1:半音下げる(flat)down 2:半音上げる(sharp)up
      chord_bass_degree,     // オンコード演奏用のコード選択ボタン 1~7
      chord_bass_semitone,   // オンコード演奏用の半音シフト 1:半音下げる(flat)down 2:半音上げる(sharp)up
      edit_function,
      edit_exit,
      edit_enc2_target,
      autoplay_switch,
      note_scale_set, note_scale_ud,
      sound_effect,
      sub_button,
      mapping_switch,
      master_vol_ud, master_vol_set,
      master_key_ud, master_key_set,
      target_key_set, // InstaChord側から目的のキーを設定するためのコマンド
      slot_select_ud, // スロット選択ボタンの上下操作
      chord_beat,
      chord_step_reset_request, // 演奏ステップを次回オンビートのタイミングで先頭に戻す
      system_control,
      file_index_ud, file_index_set,
      file_load_notify,
      file_save_notify,
      edit_enc2_ud,
      set_velocity,
      menu_open,
      internal_button,        // メインボタンへのマッピング (WebSocket等で利用)
      play_control,
      command_max,
    };

    static constexpr const char** const command_name_table[] = {
      (const char*[]){ nullptr, },    // none
      (const char*[]){ "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "Back", "OK", "Down", "Up", "Exit" }, // menu_function
      (const char*[]){ "-", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8", }, // slot_select
      (const char*[]){ "-", "Chord", "Note", "Drum", "ChordEdit", },                  // play_mode_set
      (const char*[]){ "-", "1 off", "2 off", "3 off", "4 off", "5 off", "6 off", },  // part_off
      (const char*[]){ "-", "1 ON", "2 ON", "3 ON", "4 ON", "5 ON", "6 ON", },        // part_on
      (const char*[]){ "-", "1 Edt", "2 Edt", "3 Edt", "4 Edt", "5 Edt", "6 Edt", },  // part_edit
      (const char*[]){ "-", "N 1", "N 2", "N 3", "N 4", "N 5", "N 6", "N 7", "N 8", "N 9", "N 10", "N 11", "N 12", "N 13", "N 14", "N 15", }, // note_button
      (const char*[]){ "-", "D 1", "D 2", "D 3", "D 4", "D 5", "D 6", "D 7", "D 8", "D 9", "D 10", "D 11", "D 12", "D 13", "D 14", "D 15", }, // drum_button
      (const char*[]){ "-", "I", "II", "III", "IV", "V", "VI", "VII", }, // chord_degree
      (const char*[]){ "-", "dim", "m7-5", "sus4", "6", "7", "RESERVED", "add9", "M7", "aug", "7sus4", "dim7", }, // chord_modifier
      (const char*[]){ "-", "〜", },                  // chord_minor_swap
      (const char*[]){ "-", "♭", "♯", },            // chord_semitone
      (const char*[]){ "-", "/1", "/2", "/3", "/4", "/5", "/6", "/7", }, // chord_bass_degree
      (const char*[]){ "-", "♭", "♯", },            // chord_bass_semitone
      (const char*[]){ "-", "←", "→", "↓", "↑", "<<", ">>", "Home", "On", "Off", "Mute", "ON/of", "CLEAR", "Copy", "Paste" },            // edit_function
      (const char*[]){ "-", "Exit", "Save" },            // edit_exit
      (const char*[]){ "-", "Vol %", "Oct", "Voicing", "Velo %", "Tone", "Anchor", "LoopLen", "Stroke" },   // edit_enc2_target
      (const char*[]){ "-", "Auto", "Play", "Stop", "Pause" },  // autoplay_switch
      (const char*[]){ "-", "Penta", "Major", "Chroma", "Blues", "Japan", }, // note_scale_set
    };
    enum menu_function_t : uint8_t {
      mf_0, mf_1, mf_2, mf_3, mf_4, mf_5, mf_6, mf_7, mf_8, mf_9, mf_back, mf_enter, mf_down, mf_up, mf_exit,
    };
    enum edit_function_t : uint8_t {
      left = 1, right, edit_down, edit_up, page_left, page_right, backhome, ef_on, ef_off, ef_mute, onoff, clear, copy, paste,
    };
    enum edit_enc2_target_t : uint8_t {
      part_vol = 1, position, voicing, velocity, program, banlift, endpoint, displacement,
    };
    enum autoplay_switch_t : uint8_t {
      autoplay_off = 0, autoplay_toggle, autoplay_start, autoplay_stop, autoplay_pause,
    };
    enum sound_effect_t : uint8_t {
      single = 1, testplay,
    };
    enum edit_exit_t : uint8_t {
      discard = 1, save,
    };
    enum class wifi_mode_t : uint8_t {
      wifi_disable = 0,
      wifi_enable_sta,
      wifi_enable_ap,
      wifi_enable_sta_ap,
    };
    enum class wifi_operation_t : uint8_t {
      wfop_disable = 0,
      wfop_setup_ap,
      wfop_setup_wps,
      wfop_ota_begin,
      wfop_ota_progress,
    };
    enum wifi_ota_state_t : uint8_t {
      ota_update_done = 101,
      ota_connecting = 102,
      ota_update_failed = 251,
      ota_update_available = 252,
      ota_already_up_to_date = 253,
      ota_connection_error = 254,
    };
    enum class wifi_sta_info_t : uint8_t {
      wsi_off = 0,
      wsi_signal_1,
      wsi_signal_2,
      wsi_signal_3,
      wsi_signal_4,
      wsi_waiting,
      wsi_error,
    };
    enum class wifi_ap_info_t : uint8_t {
      wai_off = 0,
      wai_enabled,
      wai_waiting,
      wai_error,
    };
    enum class midiport_info_t : uint8_t {
      mp_off = 0,
      mp_enabled,
      mp_connecting,
      mp_connected,
    };

    enum ex_midi_mode_t : uint8_t {
      midi_off = 0,
      midi_output,
      midi_input,
      midi_input_output,
    };

    enum usb_mode_t : uint8_t {
      usb_host = 0,
      usb_device,
    };

    enum instachord_link_port_t : uint8_t {
      iclp_off = 0,
      iclp_ble,
      iclp_usb,
    };

    enum instachord_link_dev_t : uint8_t {
      icld_kanplay = 0,
      icld_instachord,
    };

    enum instachord_link_style_t : uint8_t {
      icls_button = 0,
      icls_pad,
    };

    enum play_control_t : uint8_t {
      pc_panic_stop = 0,
      pc_sustain,
      pc_reset_arpeggio,
    };

    enum system_control_t : uint8_t {
      sc_boot = 0,
      sc_power_off,
      sc_reset,
      sc_save_settings,
      sc_save_resume,
    };

    // コマンドとパラメータのペア
    struct command_param_t {
      union {
        uint16_t raw;
        struct {
          command_t command;
          int8_t param;
        };
      };

      constexpr int getParam(void) const { return param; }
      constexpr command_t getCommand(void) const { return command; }
      void setParam(int param) { this->param = param; }

      constexpr command_param_t (void) : raw { 0 } {}
      constexpr command_param_t (command_t command, int param) : command { command }, param { static_cast<int8_t>(param) } {}

      // uint16_t からの変換
      explicit constexpr command_param_t (uint16_t raw) : raw { raw } {}

      // uint16_t への変換
      explicit constexpr operator uint16_t (void) const { return raw; }

      // 比較演算子
      constexpr bool operator == (const command_param_t& rhs) const { return raw == rhs.raw; }
      constexpr bool operator != (const command_param_t& rhs) const { return raw != rhs.raw; }
      constexpr bool operator < (const command_param_t& rhs) const { return raw < rhs.raw; }
      constexpr bool operator > (const command_param_t& rhs) const { return raw > rhs.raw; }
    };

    struct command_param_array_t {
      union {
        struct {
          uint32_t raw32_0;
          uint32_t raw32_1;
        };
        command_param_t array[4];
      };

      constexpr bool empty(void) const { return raw32_0 == 0 && raw32_1 == 0; }

      constexpr command_param_array_t (const command_param_t& cp)
       : array{ cp, command_param_t(), command_param_t(), command_param_t() } {}

      constexpr command_param_array_t
        ( command_t command1 = none, int param1 = 0
        , command_t command2 = none, int param2 = 0
        , command_t command3 = none, int param3 = 0
        , command_t command4 = none, int param4 = 0
        )
       : array{ command_param_t(command1, param1)
              , command_param_t(command2, param2)
              , command_param_t(command3, param3)
              , command_param_t(command4, param4)
            } {}

      // uint32_t からの変換
      explicit constexpr command_param_array_t (uint32_t raw0, uint32_t raw1 = 0) : raw32_0 { raw0 }, raw32_1 { raw1 } {}

      // uint32_t への変換
      explicit constexpr operator uint32_t (void) const { return raw32_0; }

      // 比較演算子
      constexpr bool operator == (const command_param_array_t& rhs) const { return raw32_0 == rhs.raw32_0 && raw32_1 == rhs.raw32_1; }
      constexpr bool operator != (const command_param_array_t& rhs) const { return raw32_0 != rhs.raw32_0 || raw32_1 != rhs.raw32_1; }
    };

    // サブボタンのコマンド割当 (通常演奏時)
    static constexpr const command_param_array_t command_mapping_sub_button_normal_table[] = {
      { slot_select, 1 },
      { slot_select, 2 },
      { slot_select, 3 },
      { slot_select, 4 },
      { slot_select, 5 },
      { slot_select, 6 },
      { slot_select, 7 },
      { slot_select, 8 },
    };
    // サブボタンのコマンド割当 (コード編集時)
    static constexpr const command_param_array_t command_mapping_sub_button_edit_table[] = {
      { edit_enc2_target, edit_enc2_target_t::program     },
      { edit_enc2_target, edit_enc2_target_t::position    },
      { edit_enc2_target, edit_enc2_target_t::voicing     },
      { edit_enc2_target, edit_enc2_target_t::velocity    },
      { edit_enc2_target, edit_enc2_target_t::part_vol    },
      { edit_enc2_target, edit_enc2_target_t::endpoint    },
      { edit_enc2_target, edit_enc2_target_t::banlift     },
      { edit_enc2_target, edit_enc2_target_t::displacement},
    };
    // メニュー表示時のボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_menu_table[] = {
      { menu_function, mf_1 }, { menu_function, mf_2 }, { menu_function, mf_3 }, { menu_function, mf_0 }, { menu_function, mf_exit, },
      { menu_function, mf_4 }, { menu_function, mf_5 }, { menu_function, mf_6 }, { menu_function, mf_back }, { menu_function, mf_enter },
      { menu_function, mf_7 }, { menu_function, mf_8 }, { menu_function, mf_9 }, { autoplay_switch, autoplay_stop }, { autoplay_switch, autoplay_start },
      { sub_button  , 1 }, { sub_button, 2 }, { sub_button, 3 }, { sub_button, 4 },
      { menu_function, mf_back }, { menu_function, mf_enter }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { menu_function, mf_down }, { menu_function, mf_up }, { menu_function, mf_enter },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { menu_function, mf_up }, { menu_function, mf_down }, // ENC3_DOWN, ENC3_UP
    };

    // コード編集モードのボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_edit_alt_table[] = {
      { chord_degree, 1 }, { chord_degree  , 2 }, {   chord_degree, 3 }, { chord_minor_swap, 1                       } , { chord_modifier, KANTANMusic_Modifier_Add9 },
      { chord_degree, 4 }, { chord_degree  , 5 }, {   chord_degree, 6 }, { chord_modifier  , KANTANMusic_Modifier_7  } , { chord_modifier, KANTANMusic_Modifier_M7   },
      { chord_degree, 7 }, { chord_semitone, 1 }, { chord_semitone, 2 }, { chord_modifier  , KANTANMusic_Modifier_dim} , { chord_modifier, KANTANMusic_Modifier_sus4 },
      { sub_button  , 1 }, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_part }, { chord_degree, 1 }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { edit_enc2_ud , -1}, { edit_enc2_ud  , 1 }, { menu_open, menu_part },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // コード編集モードのボタン-レバー引いた時のコマンドマッピング
    static constexpr const command_param_array_t command_mapping_chord_edit_table[] = {
      { edit_function, edit_function_t::page_left}, { edit_function , edit_function_t::edit_down} , { edit_function, edit_function_t::page_right }, { edit_exit    , edit_exit_t::discard   }, { edit_exit, edit_exit_t::save },
      { edit_function, edit_function_t::left     }, { edit_function , edit_function_t::ef_mute } , { edit_function, edit_function_t::right      }, { edit_function, edit_function_t::ef_off }, { edit_function, edit_function_t::ef_on },
      { edit_function, edit_function_t::backhome }, { edit_function , edit_function_t::edit_up  } , { none                                            }, { edit_function, edit_function_t::copy  }, { edit_function, edit_function_t::paste },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_part }, { chord_degree, 1 }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { edit_enc2_ud , -1}, { edit_enc2_ud  , 1 }, { menu_open, menu_part },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // コード演奏モードのボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_chord_play_table[] = {
      { chord_degree, 1 }, { chord_degree  , 2 }, {   chord_degree, 3 }, { chord_minor_swap, 1                       } , { chord_modifier, KANTANMusic_Modifier_Add9 },
      { chord_degree, 4 }, { chord_degree  , 5 }, {   chord_degree, 6 }, { chord_modifier  , KANTANMusic_Modifier_7  } , { chord_modifier, KANTANMusic_Modifier_M7   },
      { chord_degree, 7 }, { chord_semitone, 1 }, { chord_semitone, 2 }, { chord_modifier  , KANTANMusic_Modifier_dim} , { chord_modifier, KANTANMusic_Modifier_sus4 },
      { sub_button  , 1 }, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { menu_open, menu_system },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ノート演奏モードのボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_note_play_table[] = {
      { note_button,  1 }, { note_button,  2 }, { note_button,  3 }, { note_button,  4 }, { note_button,  5 },
      { note_button,  6 }, { note_button,  7 }, { note_button,  8 }, { note_button,  9 }, { note_button, 10 },
      { note_button, 11 }, { note_button, 12 }, { note_button, 13 }, { note_button, 14 }, { note_button, 15 },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { menu_open, menu_system },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ドラム演奏モードのボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_drum_play_table[] = {
      { drum_button,  1 }, { drum_button,  2 }, { drum_button,  3 }, { drum_button,  4 }, { drum_button,  5 },
      { drum_button,  6 }, { drum_button,  7 }, { drum_button,  8 }, { drum_button,  9 }, { drum_button, 10 },
      { drum_button, 11 }, { drum_button, 12 }, { drum_button, 13 }, { drum_button, 14 }, { drum_button, 15 },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { menu_open, menu_system },  // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ボタンマッピングチェンジ状態(レバー手前)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_chord_alt1_table[] = {
      { part_on, 4 }, { part_on, 5 }, { part_on, 6 }, { none }, { none },
      { part_on, 1 }, { part_on, 2 }, { part_on, 3 }, { none }, { none },
      { play_mode_set, playmode::chord_mode}, { play_mode_set, playmode::note_mode}, { play_mode_set, playmode::drum_mode }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ボタンマッピングチェンジ状態(レバー奥)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_chord_alt2_table[] = {
      { part_off, 4 }, { part_off, 5 }, { part_off, 6 }, { none }, { none },
      { part_off, 1 }, { part_off, 2 }, { part_off, 3 }, { none }, { none },
      { play_mode_set, playmode::chord_mode}, { play_mode_set, playmode::note_mode}, { play_mode_set, playmode::drum_mode }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ボタンマッピングチェンジ状態でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_chord_alt3_table[] = {
      { part_edit, 4 }, { part_edit, 5 }, { part_edit, 6 }, { none }, { none },
      { part_edit, 1 }, { part_edit, 2 }, { part_edit, 3 }, { none }, { none },
      { play_mode_set, playmode::chord_mode}, { play_mode_set, playmode::note_mode}, { play_mode_set, playmode::drum_mode }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ノート演奏モードでのマッピング変更状態(レバー手前)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_note_alt1_table[] = {
      { note_scale_set, 1 }, { note_scale_set, 2 }, { note_scale_set, 3 }, { note_scale_set, 4 }, { note_scale_set, 5 },
      { none }, { none }, { none }, { none }, { none },
      { play_mode_set, playmode::chord_mode}, { play_mode_set, playmode::note_mode}, { play_mode_set, playmode::drum_mode }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // ドラム演奏モードでのマッピング変更状態(レバー手前)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_drum_alt1_table[] = {
      { none }, { none }, { none }, { none }, { none },
      { none }, { none }, { none }, { none }, { none },
      { play_mode_set, playmode::chord_mode}, { play_mode_set, playmode::note_mode}, { play_mode_set, playmode::drum_mode }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // メニュー表示時のボタンマッピングチェンジ状態(レバー手前)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_menu_alt1_table[] = {
      { part_on, 4 }, { part_on, 5 }, { part_on, 6 }, { none }, { none },
      { part_on, 1 }, { part_on, 2 }, { part_on, 3 }, { none }, { none },
      { none }, { none }, { none }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // メニュー表示時のボタンマッピングチェンジ状態(レバー奥)でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_menu_alt2_table[] = {
      { part_off, 4 }, { part_off, 5 }, { part_off, 6 }, { none }, { none },
      { part_off, 1 }, { part_off, 2 }, { part_off, 3 }, { none }, { none },
      { none }, { none }, { none }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };
    // メニュー表示時のボタンマッピングチェンジ状態でのコマンドマッピング
    static constexpr const command_param_array_t command_mapping_menu_alt3_table[] = {
      { part_edit, 4 }, { part_edit, 5 }, { part_edit, 6 }, { none }, { none },
      { part_edit, 1 }, { part_edit, 2 }, { part_edit, 3 }, { none }, { none },
      { none }, { none }, { none }, { none }, { none },
      { sub_button  , 1}, { sub_button, 2}, { sub_button, 3 }, { sub_button, 4 },
      { menu_open, menu_system }, { autoplay_switch, autoplay_toggle }, // SIDE_1, SIDE_2
      { mapping_switch, 1}, { mapping_switch, 2 }, { mapping_switch, 3}, // KNOB_L, KNOB_R, KNOB_K
      { master_vol_ud, -1}, { master_vol_ud , 1 }, { autoplay_switch, autoplay_pause, play_control, pc_sustain, play_control, pc_reset_arpeggio }, // ENC1_DOWN, ENC1_UP, ENC1_PUSH
      { none }, { none }, { none },   // ENC2_DOWN, ENC2_UP, ENC2_PUSH
      { master_key_ud, -1}, { master_key_ud,  1 }, // ENC3_DOWN, ENC3_UP
    };


    // Port A デバイス用のボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_external_table[] = {
      { chord_degree, 1}, { chord_degree, 2 }, { chord_degree, 3 }, { chord_degree    , 4 },
      { chord_degree, 5}, { chord_degree, 6 }, { chord_degree, 7 }, { chord_minor_swap, 1 },

      { none }, { none }, { none }, { none },
      { none }, { none }, { none }, { none },

      { none }, { none }, { none }, { none },
      { none }, { none }, { none }, { none },

      { none }, { none }, { none }, { none },
      { none }, { none }, { none }, { none },
    };

    // Port B デバイス用のボタン-コマンドマッピング
    static constexpr const command_param_array_t command_mapping_port_b_table[] = {
      { chord_beat, 1}, { chord_beat, 2 },
    };

    struct command_param_array_text_t {
      const char* text;
      const char* upper;
      const char* lower;
      command_param_array_t command;
    };

    static constexpr const command_param_array_text_t button_text_table[] = {
      "1"    , nullptr, nullptr, {                               command::chord_degree   , 1 },
      "2"    ,  "♭"  , nullptr, { command::chord_semitone, 1 ,  command::chord_degree   , 2 },
      "2"    , nullptr, nullptr, {                               command::chord_degree   , 2 },
      "3"    ,  "♭"  , nullptr, { command::chord_semitone, 1 ,  command::chord_degree   , 3 },
      "3"    , nullptr, nullptr, {                               command::chord_degree   , 3 },
      "4"    , nullptr, nullptr, {                               command::chord_degree   , 4 },
      "5"    ,  "♭"  , nullptr, { command::chord_semitone, 1 ,  command::chord_degree   , 5 },
      "5"    , nullptr, nullptr, {                               command::chord_degree   , 5 },
      "6"    ,  "♭"  , nullptr, { command::chord_semitone, 1 ,  command::chord_degree   , 6 },
      "6"    , nullptr, nullptr, {                               command::chord_degree   , 6 },
      "7"    ,  "♭"  , nullptr, { command::chord_semitone, 1 ,  command::chord_degree   , 7 },
      "7"    , nullptr, nullptr, {                               command::chord_degree   , 7 },
      "1"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 1 },
      "2"    ,  "♭"  , "〜"   , { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 2 },
      "2"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 2 },
      "3"    ,  "♭"  , "〜"   , { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 3 },
      "3"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 3 },
      "4"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 4 },
      "5"    ,  "♭"  , "〜"   , { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 5 },
      "5"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 5 },
      "6"    ,  "♭"  , "〜"   , { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 6 },
      "6"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 6 },
      "7"    ,  "♭"  , "〜"   , { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 7 },
      "7"    , nullptr, "〜"   , {                               command::chord_minor_swap, 1, command::chord_degree, 7 },
      "1"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 1 },
      "1"    , nullptr, "M7"   , { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 1 },
      "2"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 2 },
      "3"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 3 },
      "3"    , "〜"   , "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 3 },
      "4"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 4 },
      "4"    , nullptr, "M7"   , { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 4 },
      "5"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 5 },
      "6"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 6 },
      "7"    , nullptr, "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 7 },
      "7"    , "〜"   , "7"    , { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 7 },
      "7"    , nullptr, "m7-5" , { command::chord_modifier, KANTANMusic_Modifier_m7_5 ,                               command::chord_degree, 7 },
      "7"    , nullptr, "dim"  , { command::chord_modifier, KANTANMusic_Modifier_dim  ,                               command::chord_degree, 7 },
      "dim"  , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_dim  },
      "m7-5" , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_m7_5 },
      "sus4" , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_sus4 },
      "6"    , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_6    },
      "7"    , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_7    },
      "Add9" , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_Add9 },
      "M7"   , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_M7   },
      "aug"  , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_aug  },
      "7sus4", nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_7sus4},
      "dim7" , nullptr, nullptr, { command::chord_modifier, KANTANMusic_Modifier_dim7 },
      "〜"   , nullptr, nullptr, { command::chord_minor_swap, 1 },
      "♭"   , nullptr, nullptr, { command::chord_semitone  , 1 },
      "♯"   , nullptr, nullptr, { command::chord_semitone  , 2 },
      "/1"   , nullptr, nullptr, {                                  command::chord_bass_degree, 1 },
      "/2"   ,  "♭"  , nullptr, { command::chord_bass_semitone, 1, command::chord_bass_degree, 2 },
      "/2"   , nullptr, nullptr, {                                  command::chord_bass_degree, 2 },
      "/3"   ,  "♭"  , nullptr, { command::chord_bass_semitone, 1, command::chord_bass_degree, 3 },
      "/3"   , nullptr, nullptr, {                                  command::chord_bass_degree, 3 },
      "/4"   , nullptr, nullptr, {                                  command::chord_bass_degree, 4 },
      "/5"   ,  "♭"  , nullptr, { command::chord_bass_semitone, 1, command::chord_bass_degree, 5 },
      "/5"   , nullptr, nullptr, {                                  command::chord_bass_degree, 5 },
      "/6"   ,  "♭"  , nullptr, { command::chord_bass_semitone, 1, command::chord_bass_degree, 6 },
      "/6"   , nullptr, nullptr, {                                  command::chord_bass_degree, 6 },
      "/7"   ,  "♭"  , nullptr, { command::chord_bass_semitone, 1, command::chord_bass_degree, 7 },
      "/7"   , nullptr, nullptr, {                                  command::chord_bass_degree, 7 },
      "〜"   , nullptr, "7"    , {                                  command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    },
      "♭〜" , nullptr, nullptr, { command::chord_semitone, 1, command::chord_minor_swap, 1,                                                     },
      "♭〜" , nullptr, "7"    , { command::chord_semitone, 1, command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    },
      "♭"   , nullptr, "dim"  , { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_dim  },
      "♯"   , nullptr, "dim"  , { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_dim  },
      "♭"   , nullptr, "m7-5" , { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 },
      "♯"   , nullptr, "m7-5" , { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 },
      "5/7" , nullptr, nullptr, { command::chord_bass_degree, 7, command::chord_degree, 5 },
      nullptr,nullptr,nullptr, {},
    };
  };

  namespace play {

    enum auto_play_mode_t : uint8_t {
      auto_play_none,
      auto_play_waiting,
      auto_play_running,
      auto_play_paused,
      auto_play_beatmode,
      auto_play_max,
    };

    enum sustain_state_t : uint8_t {
      sustain_off = 0,
      sustain_on,
      sustain_max,
    };

    enum offbeat_style_t : uint8_t {
      offbeat_min = 0,
      offbeat_auto,
      offbeat_self,
      offbeat_max,
    };

    enum arpeggio_style_t : uint8_t
    {
      same_time,        // 同時に鳴らす
      low_to_high,      // 下から上に
      high_to_low,      // 上から下に
      mute,             // 無音にする
      arpeggio_style_max
    };

    const char* GetVoicingName(KANTANMusic_Voicing voicing);

    namespace note {
      static constexpr const size_t max_note_scale = 5;
      static constexpr const char* note_scale_name_table[max_note_scale] = {
        "Pentatonic",
        "Major ",
        "Chromatic",
        "Blues",
        "Japanese"
      };
      static constexpr const uint8_t note_scale_note_table[max_note_scale][15] = { // note_button_01 ~ note_button_15 に該当するMIDIノートナンバー
        { 48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76, 79, 81 }, // Pentatonic
        { 48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72 }, // Major 
        { 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62 }, // Chromatic
        { 48, 51, 53, 55, 58, 60, 63, 65, 67, 70, 72, 75, 77, 79, 82 }, // Blues
        { 48, 50, 51, 55, 57, 60, 62, 63, 67, 69, 72, 74, 75, 79, 81 }, // Japanese
      };
    };
    namespace drum {
      static constexpr const uint8_t drum_note_table[1][15] = { // note_button_01 ~ note_button_15 に該当するMIDIノートナンバー
        { 36, 40, 37, 39, 42,
          41, 43, 45, 47, 46,
          54, 51, 60, 49, 57 }, // Drum
      };
    };
  };

  namespace system {
    static constexpr const uint8_t task_priority_wifi = 1;       // WiFiよりも演奏の方が重要なので、WiFiは優先度を標準値にしておく
    static constexpr const uint8_t task_priority_spi = 1;        // 画面描画は演奏に直接影響しないので優先度を標準値にしておく
    static constexpr const uint8_t task_priority_i2s = 5;        // I2Sは後回しになると音が途切れる恐れがあり最も問題となるので優先度を一番高くしておく。一回当たりの処理時間は短いのでCPU負荷は低い
    static constexpr const uint8_t task_priority_i2c = 2;        // I2Cはかんぷれ本体の入力操作への応答に影響するので優先度は標準より上げておく
    static constexpr const uint8_t task_priority_port_a = 2;     // 外部ポートAの処理は拡張機能を使用する際に演奏入力に使用されるため i2c側と同じにしておく
    static constexpr const uint8_t task_priority_port_b = 2;     // 外部ポートB
    static constexpr const uint8_t task_priority_commander = 2;  // コマンド指示タスクは演奏に関連するので優先度を標準より上げておく
    static constexpr const uint8_t task_priority_operator = 2;   // コマンド処理タスクは commander と同格にしておく
    static constexpr const uint8_t task_priority_kantanplay = 2; // かんぷれの演奏指示処理はタイミングコントロールが重要なのでmidiと同格にしておく
    static constexpr const uint8_t task_priority_midi = 2;       // MIDIおよびMIDIサブタスクは指示タイミングがずれると演奏品質に問題が出るので優先度は標準より上げておく
    static constexpr const uint8_t task_priority_midi_sub = 2;

    // 演奏操作に関わるタスクのみCPU1に割り当てる
    // それ以外のタスクはCPU0に割り当てる
    static constexpr const uint8_t task_cpu_wifi = 0;
    static constexpr const uint8_t task_cpu_spi = 0;
    static constexpr const uint8_t task_cpu_i2s = 1;
    static constexpr const uint8_t task_cpu_i2c = 1;
    static constexpr const uint8_t task_cpu_midi = 1;
    static constexpr const uint8_t task_cpu_midi_sub = 0;   // MIDIサブタスクはCPU0に割り当ててMIDI親タスクと並列動作可能にしておく
    static constexpr const uint8_t task_cpu_commander = 1;
    static constexpr const uint8_t task_cpu_operator = 1;
    static constexpr const uint8_t task_cpu_kantanplay = 1;
    static constexpr const uint8_t task_cpu_port_a = 0;
    static constexpr const uint8_t task_cpu_port_b = 0;

    static constexpr const uint8_t internal_firmware_version = 4;   // かんぷれハードウェア内部STM32ファームウェアバージョン
  };
  namespace app {
    static constexpr const uint8_t max_slot = 8;                // 設定を保持するスロットの数
    static constexpr const uint8_t max_chord_part = 6;          // コード演奏のパート数
    static constexpr const uint8_t max_pitch_without_drum = 6;  // ピッチの数 (ドラム以外のパート)
    static constexpr const uint8_t max_pitch_with_drum = 7;     // ピッチの数 (ドラムパートを含む)
    static constexpr const uint8_t max_arpeggio_step = 64;      // コード演奏時のアルペジオパターンの最大ステップ数
    static constexpr const uint8_t max_play_key = 12;
    static constexpr const uint8_t max_program_number = 129;  // プログラムチェンジの最大値(MIDIの規格128＋ドラム用の1)
    static constexpr const uint8_t max_cursor_x = max_arpeggio_step;    // 編集時の横方向カーソル移動範囲
    static constexpr const uint8_t max_cursor_y = max_pitch_with_drum;  // 編集時の縦方向カーソル移動範囲

    static constexpr const int16_t tempo_bpm_min = 20;  // テンポ最小値
    static constexpr const int16_t tempo_bpm_default = 120;  //テンポ初期値
    static constexpr const int16_t tempo_bpm_max = 400; // テンポ最大値

    static constexpr const int16_t swing_percent_min = 0;  // スウィング最小値
    static constexpr const int16_t swing_percent_default = 0;  //スウィング初期値
    static constexpr const int16_t swing_percent_max = 100; // スウィング最大値

    static constexpr const int16_t input_tolerating_msec = 50; // 自動演奏時の遅延入力に対する許容時間 ( msec )

    static constexpr const int autorelease_msec = 5000; // コード演奏モードでの 自動ノートオフまでの時間 5秒
    static constexpr const float arpeggio_reset_timeout_beats = 4.2f;
    static constexpr const float auto_beat_reset_timeout_beats = 2.0f;

    static constexpr const int16_t step_per_beat_min = 1;  // 1ビートあたりのステップ数の最小値
    static constexpr const int16_t step_per_beat_default = 2; // 1ビートあたりのステップ数の初期値
    static constexpr const int16_t step_per_beat_max = 4; // 1ビートあたりのステップ数の最大値

    static constexpr const size_t max_file_len = 1024 * 256;   // パターンファイル保存時の最大バイト数

    static constexpr const char* wifi_ap_ssid = "kanplay-ap";  // WiFiアクセスポイントモードのSSID
    static constexpr const char* wifi_ap_pass = "01234567";    // WiFiアクセスポイントモードのPASS
    static constexpr const char* wifi_ap_type = "WPA";         // 暗号方式 (nopass, WPA)
    static constexpr const char* wifi_mdns = "kanplay";        // WiFi接続時のmDNS名 kanplay.local

    static constexpr const uint32_t app_version_major = 0;
    static constexpr const uint32_t app_version_minor = 5;
    static constexpr const uint32_t app_version_patch = 7;
    static constexpr const char app_version_string[] = "057";
    static constexpr const uint32_t app_version_raw = app_version_major<<16|app_version_minor<<8|app_version_patch;

    static constexpr const char url_manual[] = "https://kantan-play.com/core/manual/";

    // OTAデータの情報を配置した URL 情報
    static constexpr const char url_ota_info[] = "https://kantan-play.com/core/update/info.json";

    static constexpr const char* key_name_table[12] = {
      "C/Am",
      "D♭/B♭m",
      "D/Bm",
      "E♭/Cm",
      "E/C♯m",
      "F/Dm",
      "F♯/D♯m",
      "G/Em",
      "A♭/Fm",
      "A/F♯m",
      "B♭/Gm",
      "B/G♯m",
    };

    static constexpr const char* note_name_table[12] = {
      "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"
    };

    static constexpr const int min_position = -36;
    static constexpr const int max_position = 36;
    static constexpr const int position_table_offset = 36;
    // static constexpr const char* position_name_table[] = {
    static constexpr const simple_text_array_t position_name_table = { 73, (const simple_text_t[]){
      "-３",     "-２11/12", "-２ 5/6", "-２ 3/4",   // -33 ~ -36
      "-２ 2/3", "-２ 7/12", "-２ 1/2", "-２ 5/12",  // -29 ~ -32
      "-２ 1/3", "-２ 1/4",  "-２ 1/6", "-２ 1/12",  // -25 ~ -28
      "-２",     "-１11/12", "-１ 5/6", "-１ 3/4",   // -21 ~ -24
      "-１ 2/3", "-１ 7/12", "-１ 1/2", "-１ 5/12",  // -17 ~ -20
      "-１ 1/3", "-１ 1/4",  "-１ 1/6", "-１ 1/12",  // -13 ~ -16
      "-１",     "- 11/12",  "- 5/6",  "- 3/4",     // -9  ~ -12
      "- 2/3",   "- 7/12",   "- 1/2",  "- 5/12",    // -5  ~ -8
      "- 1/3",   "- 1/4",    "- 1/6",  "- 1/12",    // -1  ~ -4
      "±０",     "+ 1/12",   "+ 1/6",   "+ 1/4",    // 0   ~ 3
      "+ 1/3",   "+ 5/12",   "+ 1/2",   "+ 7/12",   // 4   ~ 7
      "+ 2/3",   "+ 3/4",    "+ 5/6",   "+ 11/12",  // 8   ~ 11
      "+１",     "+１ 1/12", "+１ 1/6", "+１ 1/4",   // 12  ~ 15
      "+１ 1/3", "+１ 5/12", "+１ 1/2",  "+１ 7/12", // 16  ~ 19
      "+１ 2/3", "+１ 3/4",  "+１ 5/6",  "+１11/12", // 20  ~ 23
      "+２",     "+２ 1/12", "+２ 1/6",  "+２ 1/4",  // 24  ~ 27
      "+２ 1/3", "+２ 5/12", "+２ 1/2",  "+２ 7/12", // 28  ~ 31
      "+２ 2/3", "+２ 3/4",  "+２ 5/6",  "+２11/12", // 32  ~ 35
      "+３",
    }};

    // ※ data_type_tとdata_pathは対応していること
    enum data_type_t : uint8_t {
      data_unknown = 0,
      data_song_users,
      data_song_extra,
      data_song_preset,
      data_setting,
      data_resume,
      data_type_max,
    };
    static constexpr const char* data_path[] = {
      "/songs/user/",
      "/songs/extra/",
      "",               // バイナリ埋め込みのためフォルダ情報なし
      "/setting.json",
      "/resume.json",
    };
    struct file_command_info_t {
      union {
        uint32_t raw;
        struct {
          data_type_t dir_type;
          uint8_t mem_index;
          uint16_t file_index;
        };
      };
      constexpr file_command_info_t(uint32_t raw_value = 0) : raw { raw_value } {}
      constexpr file_command_info_t(data_type_t dir_type, uint8_t mem_index, uint16_t file_index) : dir_type { dir_type }, mem_index { mem_index }, file_index { file_index } {}
    };
    // static constexpr const char setting_filename[] = "setting.json";
  };

  namespace ctrl_assign {
    struct control_assignment_t {
      const char* jsonname;
      localize_text_t text;
      def::command::command_param_array_t command;
    };
    int get_index_from_command(const control_assignment_t* data, const def::command::command_param_array_t& command);
    int get_index_from_jsonname(const control_assignment_t* data, const char* name);

    static constexpr const control_assignment_t playbutton_table[] = {
      { "1"            , { "1"             , nullptr               }, {                               command::chord_degree   , 1 } },
      { "2 flat"       , { "2♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 2 } },
      { "2"            , { "2"             , nullptr               }, {                               command::chord_degree   , 2 } },
      { "3 flat"       , { "3♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 3 } },
      { "3"            , { "3"             , nullptr               }, {                               command::chord_degree   , 3 } },
      { "4"            , { "4"             , nullptr               }, {                               command::chord_degree   , 4 } },
      { "5 flat"       , { "5♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 5 } },
      { "5"            , { "5"             , nullptr               }, {                               command::chord_degree   , 5 } },
      { "6 flat"       , { "6♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 6 } },
      { "6"            , { "6"             , nullptr               }, {                               command::chord_degree   , 6 } },
      { "7 flat"       , { "7♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 7 } },
      { "7"            , { "7"             , nullptr               }, {                               command::chord_degree   , 7 } },
      { "1 swap"       , { "1〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 1 } },
      { "2 flat swap"  , { "2♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 2 } },
      { "2 swap"       , { "2〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 2 } },
      { "3 flat swap"  , { "3♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "3 swap"       , { "3〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "4 swap"       , { "4〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 4 } },
      { "5 flat swap"  , { "5♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 5 } },
      { "5 swap"       , { "5〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 5 } },
      { "6 flat swap"  , { "6♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 6 } },
      { "6 swap"       , { "6〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 6 } },
      { "7 flat swap"  , { "7♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "7 swap"       , { "7〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "1[7]"         , { "1 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 1 } },
      { "1[M7]"        , { "1 [ M7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 1 } },
      { "2[7]"         , { "2 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 2 } },
      { "3[7]"         , { "3 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 3 } },
      { "3 swap[7]"    , { "3〜[ 7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "4[7]"         , { "4 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 4 } },
      { "4[M7]"        , { "4 [ M7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 4 } },
      { "5[7]"         , { "5 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 5 } },
      { "5 slash 7"    , { "5/7"           , nullptr               }, { command::chord_bass_degree, 7, command::chord_degree, 5 } },
      { "6[7]"         , { "6 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 6 } },
      { "7[7]"         , { "7 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 7 } },
      { "7 swap[7]"    , { "7〜[ 7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "7[m7_5]"      , { "7 [ m7-5 ]"    , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_m7_5 ,                               command::chord_degree, 7 } },
      { "7[dim]"       , { "7 [ dim ]"     , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim  ,                               command::chord_degree, 7 } },
      { "[dim]"        , { "[ dim ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "[m7_5]"       , { "[ m7-5 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { "[sus4]"       , { "[ sus4 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_sus4 } },
      { "[6]"          , { "[ 6 ]"         , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_6    } },
      { "[7]"          , { "[ 7 ]"         , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "[Add9]"       , { "[ Add9 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_Add9 } },
      { "[M7]"         , { "[ M7 ]"        , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   } },
      { "[aug]"        , { "[ aug ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_aug  } },
      { "[7sus4]"      , { "[ 7sus4 ]"     , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7sus4} },
      { "[dim7]"       , { "[ dim7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim7 } },
      { "swap"         , { "〜"            , nullptr               }, { command::chord_minor_swap, 1 } },
      { "flat"         , { "♭"            , nullptr               }, { command::chord_semitone  , 1 } },
      { "sharp"        , { "♯"             , nullptr               }, { command::chord_semitone  , 2 } },
      { "slash 1"      , { "/1"            , nullptr               }, {                                  command::chord_bass_degree, 1 } },
      { "slash 2 flat" , { "/2♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 2 } },
      { "slash 2"      , { "/2"            , nullptr               }, {                                  command::chord_bass_degree, 2 } },
      { "slash 3 flat" , { "/3♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 3 } },
      { "slash 3"      , { "/3"            , nullptr               }, {                                  command::chord_bass_degree, 3 } },
      { "slash 4"      , { "/4"            , nullptr               }, {                                  command::chord_bass_degree, 4 } },
      { "slash 5 flat" , { "/5♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 5 } },
      { "slash 5"      , { "/5"            , nullptr               }, {                                  command::chord_bass_degree, 5 } },
      { "slash 6 flat" , { "/6♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 6 } },
      { "slash 6"      , { "/6"            , nullptr               }, {                                  command::chord_bass_degree, 6 } },
      { "slash 7 flat" , { "/7♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 7 } },
      { "slash 7"      , { "/7"            , nullptr               }, {                                  command::chord_bass_degree, 7 } },
      { "swap[7]"      , { "〜[ 7 ]"        , nullptr              }, {                                  command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "flat swap"    , { "♭〜"           , nullptr              }, { command::chord_semitone, 1, command::chord_minor_swap, 1,                                                     } },
      { "flat swap[7]" , { "♭〜[ 7 ]"      , nullptr              }, { command::chord_semitone, 1, command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "flat[dim]"    , { "♭ [ dim ]"     , nullptr              }, { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "sharp[dim]"   , { "♯ [ dim ]"      , nullptr              }, { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "flat[m7_5]"   , { "♭ [ m7-5 ]"    , nullptr              }, { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { "sharp[m7_5]"  , { "♯ [ m7-5 ]"     , nullptr              }, { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { nullptr        , nullptr                                   , {} },
    };
    static constexpr const control_assignment_t external_table[] = {
      { "play 01"      , { "Play Button 01", "プレイボタン01"       },  {                               command::internal_button,  1 } },
      { "play 02"      , { "Play Button 02", "プレイボタン02"       },  {                               command::internal_button,  2 } },
      { "play 03"      , { "Play Button 03", "プレイボタン03"       },  {                               command::internal_button,  3 } },
      { "play 04"      , { "Play Button 04", "プレイボタン04"       },  {                               command::internal_button,  4 } },
      { "play 05"      , { "Play Button 05", "プレイボタン05"       },  {                               command::internal_button,  5 } },
      { "play 06"      , { "Play Button 06", "プレイボタン06"       },  {                               command::internal_button,  6 } },
      { "play 07"      , { "Play Button 07", "プレイボタン07"       },  {                               command::internal_button,  7 } },
      { "play 08"      , { "Play Button 08", "プレイボタン08"       },  {                               command::internal_button,  8 } },
      { "play 09"      , { "Play Button 09", "プレイボタン09"       },  {                               command::internal_button,  9 } },
      { "play 10"      , { "Play Button 10", "プレイボタン10"       },  {                               command::internal_button, 10 } },
      { "play 11"      , { "Play Button 11", "プレイボタン11"       },  {                               command::internal_button, 11 } },
      { "play 12"      , { "Play Button 12", "プレイボタン12"       },  {                               command::internal_button, 12 } },
      { "play 13"      , { "Play Button 13", "プレイボタン13"       },  {                               command::internal_button, 13 } },
      { "play 14"      , { "Play Button 14", "プレイボタン14"       },  {                               command::internal_button, 14 } },
      { "play 15"      , { "Play Button 15", "プレイボタン15"       },  {                               command::internal_button, 15 } },
      { "slot 01"      , { "Slot Button 1" , "スロットボタン1"      },  {                               command::internal_button, 16 } },
      { "slot 02"      , { "Slot Button 2" , "スロットボタン2"      },  {                               command::internal_button, 17 } },
      { "slot 03"      , { "Slot Button 3" , "スロットボタン3"      },  {                               command::internal_button, 18 } },
      { "slot 04"      , { "Slot Button 4" , "スロットボタン4"      },  {                               command::internal_button, 19 } },
      { "slot 05"      , { "Slot Button 5" , "スロットボタン5"      },  { command::internal_button, 22, command::internal_button, 16 } },
      { "slot 06"      , { "Slot Button 6" , "スロットボタン6"      },  { command::internal_button, 22, command::internal_button, 17 } },
      { "slot 07"      , { "Slot Button 7" , "スロットボタン7"      },  { command::internal_button, 22, command::internal_button, 18 } },
      { "slot 08"      , { "Slot Button 8" , "スロットボタン8"      },  { command::internal_button, 22, command::internal_button, 19 } },
      { "side l"       , { "Side Button L" , "左サイドボタン"       },  {                               command::internal_button, 20 } },
      { "side r"       , { "Side Button R" , "右サイドボタン"       },  {                               command::internal_button, 21 } },
      { "lever d"      , { "Lever Down"    , "シフトレバー下"       },  {                               command::internal_button, 22 } },
      { "lever u"      , { "Lever Up"      , "シフトレバー上"       },  {                               command::internal_button, 23 } },
      { "lever p"      , { "Lever Push"    , "シフトレバー押す"     },  {                               command::internal_button, 24 } },
      { "dial 1 l"     , { "Dial 1 Left"   , "上ダイヤル左回転"     },  {                               command::internal_button, 25 } },
      { "dial 1 r"     , { "Dial 1 Right"  , "上ダイヤル右回転"     },  {                               command::internal_button, 26 } },
      { "dial 1 p"     , { "Dial 1 Push"   , "上ダイヤル押す"       },  {                               command::internal_button, 27 } },
      { "dial 2 l"     , { "Dial 2 Left"   , "下ダイヤル左回転"     },  {                               command::internal_button, 28 } },
      { "dial 2 r"     , { "Dial 2 Right"  , "下ダイヤル右回転"     },  {                               command::internal_button, 29 } },
      { "dial 2 p"     , { "Dial 2 Push"   , "下ダイヤル押す"       },  {                               command::internal_button, 30 } },
      { "wheel l"      , { "Wheel Left"    , "ジョグダイヤル左回転" },  {                               command::internal_button, 31 } },
      { "wheel r"      , { "Wheel Right"   , "ジョグダイヤル右回転" },  {                               command::internal_button, 32 } },
      { "beat"         , { "beat"          , "ビート"              }, {                                command::chord_beat     , 1 } },
      { "1"            , { "1"             , nullptr               }, {                               command::chord_degree   , 1 } },
      { "2 flat"       , { "2♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 2 } },
      { "2"            , { "2"             , nullptr               }, {                               command::chord_degree   , 2 } },
      { "3 flat"       , { "3♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 3 } },
      { "3"            , { "3"             , nullptr               }, {                               command::chord_degree   , 3 } },
      { "4"            , { "4"             , nullptr               }, {                               command::chord_degree   , 4 } },
      { "5 flat"       , { "5♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 5 } },
      { "5"            , { "5"             , nullptr               }, {                               command::chord_degree   , 5 } },
      { "6 flat"       , { "6♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 6 } },
      { "6"            , { "6"             , nullptr               }, {                               command::chord_degree   , 6 } },
      { "7 flat"       , { "7♭"           , nullptr               }, { command::chord_semitone, 1 ,  command::chord_degree   , 7 } },
      { "7"            , { "7"             , nullptr               }, {                               command::chord_degree   , 7 } },
      { "1 swap"       , { "1〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 1 } },
      { "2 flat swap"  , { "2♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 2 } },
      { "2 swap"       , { "2〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 2 } },
      { "3 flat swap"  , { "3♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "3 swap"       , { "3〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "4 swap"       , { "4〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 4 } },
      { "5 flat swap"  , { "5♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 5 } },
      { "5 swap"       , { "5〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 5 } },
      { "6 flat swap"  , { "6♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 6 } },
      { "6 swap"       , { "6〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 6 } },
      { "7 flat swap"  , { "7♭〜"         , nullptr               }, { command::chord_semitone, 1 ,  command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "7 swap"       , { "7〜"           , nullptr               }, {                               command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "1[7]"         , { "1 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 1 } },
      { "1[M7]"        , { "1 [ M7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 1 } },
      { "2[7]"         , { "2 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 2 } },
      { "3[7]"         , { "3 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 3 } },
      { "3 swap[7]"    , { "3〜[ 7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 3 } },
      { "4[7]"         , { "4 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 4 } },
      { "4[M7]"        , { "4 [ M7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   ,                               command::chord_degree, 4 } },
      { "5[7]"         , { "5 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 5 } },
      { "5 slash 7"    , { "5/7"           , nullptr               }, { command::chord_bass_degree, 7, command::chord_degree, 5 } },
      { "6[7]"         , { "6 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 6 } },
      { "7[7]"         , { "7 [ 7 ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    ,                               command::chord_degree, 7 } },
      { "7 swap[7]"    , { "7〜[ 7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    , command::chord_minor_swap, 1, command::chord_degree, 7 } },
      { "7[m7_5]"      , { "7 [ m7-5 ]"    , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_m7_5 ,                               command::chord_degree, 7 } },
      { "7[dim]"       , { "7 [ dim ]"     , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim  ,                               command::chord_degree, 7 } },
      { "[dim]"        , { "[ dim ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "[m7_5]"       , { "[ m7-5 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { "[sus4]"       , { "[ sus4 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_sus4 } },
      { "[6]"          , { "[ 6 ]"         , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_6    } },
      { "[7]"          , { "[ 7 ]"         , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "[Add9]"       , { "[ Add9 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_Add9 } },
      { "[M7]"         , { "[ M7 ]"        , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_M7   } },
      { "[aug]"        , { "[ aug ]"       , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_aug  } },
      { "[7sus4]"      , { "[ 7sus4 ]"     , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_7sus4} },
      { "[dim7]"       , { "[ dim7 ]"      , nullptr               }, { command::chord_modifier, KANTANMusic_Modifier_dim7 } },
      { "swap"         , { "〜"            , nullptr               }, { command::chord_minor_swap, 1 } },
      { "flat"         , { "♭"            , nullptr               }, { command::chord_semitone  , 1 } },
      { "sharp"        , { "♯"             , nullptr               }, { command::chord_semitone  , 2 } },
      { "slash 1"      , { "/1"            , nullptr               }, {                                  command::chord_bass_degree, 1 } },
      { "slash 2 flat" , { "/2♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 2 } },
      { "slash 2"      , { "/2"            , nullptr               }, {                                  command::chord_bass_degree, 2 } },
      { "slash 3 flat" , { "/3♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 3 } },
      { "slash 3"      , { "/3"            , nullptr               }, {                                  command::chord_bass_degree, 3 } },
      { "slash 4"      , { "/4"            , nullptr               }, {                                  command::chord_bass_degree, 4 } },
      { "slash 5 flat" , { "/5♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 5 } },
      { "slash 5"      , { "/5"            , nullptr               }, {                                  command::chord_bass_degree, 5 } },
      { "slash 6 flat" , { "/6♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 6 } },
      { "slash 6"      , { "/6"            , nullptr               }, {                                  command::chord_bass_degree, 6 } },
      { "slash 7 flat" , { "/7♭"          , nullptr               }, { command::chord_bass_semitone, 1, command::chord_bass_degree, 7 } },
      { "slash 7"      , { "/7"            , nullptr               }, {                                  command::chord_bass_degree, 7 } },
      { "swap[7]"      , { "〜[ 7 ]"        , nullptr              }, {                                  command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "flat swap"    , { "♭〜"           , nullptr              }, { command::chord_semitone, 1, command::chord_minor_swap, 1,                                                     } },
      { "flat swap[7]" , { "♭〜[ 7 ]"      , nullptr              }, { command::chord_semitone, 1, command::chord_minor_swap, 1, command::chord_modifier, KANTANMusic_Modifier_7    } },
      { "flat[dim]"    , { "♭ [ dim ]"     , nullptr              }, { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "sharp[dim]"   , { "♯ [ dim ]"      , nullptr              }, { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_dim  } },
      { "flat[m7_5]"   , { "♭ [ m7-5 ]"    , nullptr              }, { command::chord_semitone, 1,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { "sharp[m7_5]"  , { "♯ [ m7-5 ]"     , nullptr              }, { command::chord_semitone, 2,                               command::chord_modifier, KANTANMusic_Modifier_m7_5 } },
      { "reset loop"   , { "Reset loop"     , "リセットループ"      }, { command::chord_step_reset_request, 1 } },
      { "p1_on"        , { "Part 1 ON"      , "パート1 ON"         }, { command::part_on, 1 } },
      { "p2_on"        , { "Part 2 ON"      , "パート2 ON"         }, { command::part_on, 2 } },
      { "p3_on"        , { "Part 3 ON"      , "パート3 ON"         }, { command::part_on, 3 } },
      { "p4_on"        , { "Part 4 ON"      , "パート4 ON"         }, { command::part_on, 4 } },
      { "p5_on"        , { "Part 5 ON"      , "パート5 ON"         }, { command::part_on, 5 } },
      { "p6_on"        , { "Part 6 ON"      , "パート6 ON"         }, { command::part_on, 6 } },
      { "p1_off"       , { "Part 1 OFF"     , "パート1 OFF"        }, { command::part_off, 1 } },
      { "p2_off"       , { "Part 2 OFF"     , "パート2 OFF"        }, { command::part_off, 2 } },
      { "p3_off"       , { "Part 3 OFF"     , "パート3 OFF"        }, { command::part_off, 3 } },
      { "p4_off"       , { "Part 4 OFF"     , "パート4 OFF"        }, { command::part_off, 4 } },
      { "p5_off"       , { "Part 5 OFF"     , "パート5 OFF"        }, { command::part_off, 5 } },
      { "p6_off"       , { "Part 6 OFF"     , "パート6 OFF"        }, { command::part_off, 6 } },
      { "p1_edit"      , { "Part 1 Edit"    , "パート1 編集"       }, { command::part_edit, 1 } },
      { "p2_edit"      , { "Part 2 Edit"    , "パート2 編集"       }, { command::part_edit, 2 } },
      { "p3_edit"      , { "Part 3 Edit"    , "パート3 編集"       }, { command::part_edit, 3 } },
      { "p4_edit"      , { "Part 4 Edit"    , "パート4 編集"       }, { command::part_edit, 4 } },
      { "p5_edit"      , { "Part 5 Edit"    , "パート5 編集"       }, { command::part_edit, 5 } },
      { "p6_edit"      , { "Part 6 Edit"    , "パート6 編集"       }, { command::part_edit, 6 } },
      { ""             , { "---"            , nullptr             }, {} },
      { nullptr        , nullptr                                   , {} },
    };
  }

  namespace ntp {
    static constexpr const char* server1 = "0.pool.ntp.org";
    static constexpr const char* server2 = "1.pool.ntp.org";
    static constexpr const char* server3 = "2.pool.ntp.org";
  }

  namespace hw {
    static constexpr const uint8_t max_rgb_led = 19;
    static constexpr const uint8_t max_button_mask = 32;
    static constexpr const uint8_t max_main_button = 15;
    static constexpr const uint8_t max_sub_button = 4;
    static constexpr const uint8_t max_port_b_pins = 2;

    namespace pin {
#if defined (CONFIG_IDF_TARGET_ESP32S3)
// for CoreS3
    static constexpr const gpio_num_t stm_int = GPIO_NUM_10;
    static constexpr const gpio_num_t midi_tx = GPIO_NUM_0;
    static constexpr const gpio_num_t i2s_ws = GPIO_NUM_6;
    static constexpr const gpio_num_t i2s_bck = GPIO_NUM_5;
    static constexpr const gpio_num_t i2s_mclk = GPIO_NUM_NC;
    static constexpr const gpio_num_t i2s_out = GPIO_NUM_13;
    static constexpr const gpio_num_t i2s_in = GPIO_NUM_14;
#elif defined (CONFIG_IDF_TARGET_ESP32)
// for Core2
    static constexpr const gpio_num_t stm_int = GPIO_NUM_35;
    static constexpr const gpio_num_t midi_tx = GPIO_NUM_0;
    static constexpr const gpio_num_t i2s_ws = GPIO_NUM_27;
    static constexpr const gpio_num_t i2s_bck = GPIO_NUM_25;
    static constexpr const gpio_num_t i2s_mclk = GPIO_NUM_NC;
    static constexpr const gpio_num_t i2s_out = GPIO_NUM_2;
    static constexpr const gpio_num_t i2s_in = GPIO_NUM_34;
/*
// for CoreFIRE
    static constexpr const gpio_num_t stm_int = GPIO_NUM_35;
    static constexpr const gpio_num_t midi_tx = GPIO_NUM_0;
    static constexpr const gpio_num_t i2s_ws = GPIO_NUM_12;
    static constexpr const gpio_num_t i2s_bck = GPIO_NUM_25;
    static constexpr const gpio_num_t i2s_mclk = GPIO_NUM_NC;
    static constexpr const gpio_num_t i2s_out = GPIO_NUM_15;
    static constexpr const gpio_num_t i2s_in = GPIO_NUM_34;
//*/
#else
    static constexpr const uint8_t stm_int = UINT8_MAX;
    static constexpr const uint8_t midi_tx = UINT8_MAX;
    static constexpr const uint8_t i2s_ws = UINT8_MAX;
    static constexpr const uint8_t i2s_bck = UINT8_MAX;
    static constexpr const uint8_t i2s_mclk = UINT8_MAX;
    static constexpr const uint8_t i2s_out = UINT8_MAX;
    static constexpr const uint8_t i2s_in = UINT8_MAX;
#endif
    };
  };
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
