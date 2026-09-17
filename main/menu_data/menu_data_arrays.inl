// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

static constexpr const size_t START_COUNTER_SYSTEM = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_SYSTEM)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_system, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_system[] = {
  MENU_BUILDER(mi_tree_t              ,0     , { "Menu"             , "メニュー"    }),
  MENU_BUILDER(mi_tree_t              , 1    , { "Genre"            , "ジャンル"    }),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Pop"             , "ポップス"      }, def::app::data_type_t::data_song_preset_genre_pop,      0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Rock"            , "ロック"        }, def::app::data_type_t::data_song_preset_genre_rock,     0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Dance"           , "ダンス"        }, def::app::data_type_t::data_song_preset_genre_dance,    0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Funk"            , "ファンク"      }, def::app::data_type_t::data_song_preset_genre_funk,     0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "R&B"             , "R&B"          }, def::app::data_type_t::data_song_preset_genre_rnb,      0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Jazz"            , "ジャズ"        }, def::app::data_type_t::data_song_preset_genre_jazz,     0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Latin"           , "ラテン"        }, def::app::data_type_t::data_song_preset_genre_latin,    0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Acoustic"        , "アコースティック"}, def::app::data_type_t::data_song_preset_genre_acoustic, 0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Ballad"          , "バラード"      }, def::app::data_type_t::data_song_preset_genre_ballad,   0 ),
  MENU_BUILDER(mi_load_file_t         ,  2   , {  "Specialty"       , "特殊リズム"    }, def::app::data_type_t::data_song_preset_genre_specialty,0 ),
  MENU_BUILDER(mi_tree_t              , 1    , { "Song"             , "ソング"        }),
  MENU_BUILDER(mi_tree_t              ,  2   , {  "Open Song"       , "ソングを開く"   }),
  MENU_BUILDER(mi_load_file_t         ,   3  , {   "Preset Song"    , "プリセットソング"}, def::app::data_type_t::data_song_preset_song, 0 ),
  MENU_BUILDER(mi_load_file_t         ,   3  , {   "User Song (SD)" , "ユーザソング(SD)"}, def::app::data_type_t::data_song_users ),
  MENU_BUILDER(mi_load_file_t         ,   3  , {   "Extra Song (SD)", "エクストラソング(SD)"}, def::app::data_type_t::data_song_extra ),
  MENU_BUILDER(mi_save_t              ,  2   , {  "Save Song"       , "ソングを保存"   }, def::app::data_type_t::data_song_users ),
  MENU_BUILDER(mi_web_filer_t         ,  2   , {  "Song Manager"    , "ソング管理"    }),
  MENU_BUILDER(mi_reset_song_t        ,  2   , {  "Reset Song"      , "ソングデータリセット"}),
  MENU_BUILDER(mi_tree_t              , 1    , { "Sequence"         , "コード進行"    }),
  MENU_BUILDER(mi_tree_t              ,  2   , {  "Load Sequence"   , "コード進行を開く"}),
  MENU_BUILDER(mi_load_progression_t  ,   3  , {   "Load Preset"    , "プリセットを開く"}, def::app::data_type_t::data_progression_preset ),
  MENU_BUILDER(mi_load_progression_t  ,   3  , {   "Load User's (SD)", "ユーザデータ(SD)"}, def::app::data_type_t::data_progression_users ),
  MENU_BUILDER(mi_save_progression_t  ,  2   , {  "Save Sequence"   , "コード進行を保存"}, def::app::data_type_t::data_progression_users ),
  MENU_BUILDER(mi_recording_t         ,  2   , {  "Recording"       , "レコーディング" }),
  MENU_BUILDER(mi_reset_progression_t ,  2   , {  "Reset Sequence"  , "コード進行をリセット"}),
  MENU_BUILDER(mi_play_mode_t         , 1    , { "Play Mode"        , "プレイモード"  }),
  MENU_BUILDER(mi_tree_t              , 1    , { "Edit"             , "編集"          }),
  MENU_BUILDER(mi_tree_t              ,  2   , {  "Tempo & Groove"  , "テンポ＆グルーヴ設定"}),
  MENU_BUILDER(mi_song_tempo_t        ,   3  , {   "BPM"            , "テンポ(BPM)"   }),
  MENU_BUILDER(mi_song_swing_t        ,   3  , {   "Swing"          , "スウィング"    }),
  MENU_BUILDER(mi_offbeat_style_t     ,   3  , {   "Offbeat Control", "裏拍演奏"     }),
  MENU_BUILDER(mi_song_step_beat_t    ,   3  , {   "Step / Beat"    , "ステップ／ビート"}),
  MENU_BUILDER(mi_tree_t              ,  2   , {  "Slot Setting"    , "スロット設定"  }),
  MENU_BUILDER(mi_num_slot_t          ,   3  , {   "Number of Slots" , "スロット数"    }),
  MENU_BUILDER(mi_slot_perform_style_t,   3  , {   "Play Mode"      , "演奏モード"    }),
  MENU_BUILDER(mi_slot_key_t          ,   3  , {   "Key Modulation" , "キー転調"      }),
  MENU_BUILDER(mi_slot_step_beat_t    ,   3  , {   "Step / Beat"    , "ステップ／ビート"}),
  MENU_BUILDER(mi_slot_clipboard_t    ,   3  , {   "Copy/Paste"     , "コピー/ペースト" }),
  MENU_BUILDER(mi_slot_clear_notes_t  ,   3  , {   "Clear All Notes", "全てのノートを削除" }),
  MENU_BUILDER(mi_recording_t         ,  2   , {  "Recording"       , "レコーディング" }),
  MENU_BUILDER(mi_save_t              ,  2   , {  "Save Song"       , "ソングを保存"   }, def::app::data_type_t::data_song_users ),
  MENU_BUILDER(mi_reset_song_t        ,  2   , {  "Reset Song"      , "ソングデータリセット"}),
  MENU_BUILDER(mi_tree_t              , 1    , { "External Device", "外部デバイス" }),
  MENU_BUILDER(mi_external_input_source_t, 2 , { "Input Source"    , "入力ソース"   }),
  MENU_BUILDER(mi_input_assign_link_t ,  2   , { "Input Assign"    , "入力割り当て" }),
  MENU_BUILDER(mi_ble_connection_t    ,  2   , { "BLE MIDI Connection", "BLE MIDI接続" }),
  MENU_BUILDER(mi_ble_scan_t          ,   3  , { "Scan & Connect"  , "検索して接続" }),
  MENU_BUILDER(mi_ble_action_t        ,   3  , { "Forget Device"   , "接続先を解除" }, false),
  MENU_BUILDER(mi_ble_action_t        ,   3  , { "Reset BLE Connection", "BLE接続をリセット" }, true),
  MENU_BUILDER(mi_device_info_t       ,  2   , { "Device Info"     , "デバイス情報" }),
  MENU_BUILDER(mi_portc_midi_t        ,  2   , { "PortC MIDI"      , "ポートC MIDI" }),
  MENU_BUILDER(mi_tree_t              ,  2   , { "InstaChord Link", "インスタコードリンク"}),
  MENU_BUILDER(mi_iclink_port_t       ,   3  , { "Connect"         , "接続方法"   }),
  MENU_BUILDER(mi_iclink_dev_t        ,   3  , { "Play Device"     , "演奏デバイス"}),
  MENU_BUILDER(mi_iclink_style_t      ,   3  , { "Play Style"      , "演奏スタイル"}),
  MENU_BUILDER(mi_tree_t              , 1    , { "Wi-Fi"           , "Wi-Fi"       }),
  MENU_BUILDER(mi_otaupdate_t         ,  2   , { "Update"          , "ファーム更新" }),
  MENU_BUILDER(mi_wifiap_t            ,  2   , { "Wi-Fi Setup"     , "Wi-Fi設定"    }),
  MENU_BUILDER(mi_web_filer_t         ,  2   , { "File Editor"     , "ファイル編集" }),
  MENU_BUILDER(mi_webserver_t         ,  2   , { "Web server"      , "Webサーバ"    }),
  MENU_BUILDER(mi_tree_t              , 1    , { "System"           , "システム"     }),
  MENU_BUILDER(mi_tree_t              ,  2   , { "Control Mapping", "操作マッピング" }),
  MENU_BUILDER(mi_tree_t              ,   3  , { "Mapping 1(Device)", "マッピング1 (本体)" }),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Play Button"   , "プレイボタン" }),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 1"      , "ボタン 1"     },  1 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 2"      , "ボタン 2"     },  2 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 3"      , "ボタン 3"     },  3 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 4"      , "ボタン 4"     },  4 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 5"      , "ボタン 5"     },  5 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 6"      , "ボタン 6"     },  6 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 7"      , "ボタン 7"     },  7 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 8"      , "ボタン 8"     },  8 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 9"      , "ボタン 9"     },  9 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 10"     , "ボタン 10"    }, 10 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 11"     , "ボタン 11"    }, 11 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 12"     , "ボタン 12"    }, 12 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 13"     , "ボタン 13"    }, 13 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 14"     , "ボタン 14"    }, 14 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 15"     , "ボタン 15"    }, 15 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Slot Button"   , "スロットボタン" }),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 1"    , "スロットBtn 1" },  1 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 2"    , "スロットBtn 2" },  2 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 3"    , "スロットBtn 3" },  3 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 4"    , "スロットBtn 4" },  4 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Ext Input"     , "拡張入力"     }),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 1"        , "拡張 1"       },   1 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 2"        , "拡張 2"       },   2 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 3"        , "拡張 3"       },   3 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 4"        , "拡張 4"       },   4 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 5"        , "拡張 5"       },   5 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 6"        , "拡張 6"       },   6 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 7"        , "拡張 7"       },   7 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 8"        , "拡張 8"       },   8 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 9"        , "拡張 9"       },   9 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 10"       , "拡張 10"      },  10 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 11"       , "拡張 11"      },  11 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 12"       , "拡張 12"      },  12 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 13"       , "拡張 13"      },  13 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 14"       , "拡張 14"      },  14 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 15"       , "拡張 15"      },  15 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 16"       , "拡張 16"      },  16 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 17"       , "拡張 17"      },  17 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 18"       , "拡張 18"      },  18 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 19"       , "拡張 19"      },  19 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 20"       , "拡張 20"      },  20 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 21"       , "拡張 21"      },  21 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 22"       , "拡張 22"      },  22 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 23"       , "拡張 23"      },  23 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 24"       , "拡張 24"      },  24 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 25"       , "拡張 25"      },  25 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 26"       , "拡張 26"      },  26 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 27"       , "拡張 27"      },  27 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 28"       , "拡張 28"      },  28 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 29"       , "拡張 29"      },  29 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 30"       , "拡張 30"      },  30 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 31"       , "拡張 31"      },  31 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 32"       , "拡張 32"      },  32 - 1, def::mapping::target_t::device),
  MENU_BUILDER(mi_tree_t              ,    4 , { "MIDI Note"     , "MIDI Note"    }),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C#-1" , nullptr },   1 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D -1" , nullptr },   2 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D#-1" , nullptr },   3 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E -1" , nullptr },   4 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F -1" , nullptr },   5 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F#-1" , nullptr },   6 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G -1" , nullptr },   7 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G#-1" , nullptr },   8 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A -1" , nullptr },   9 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A#-1" , nullptr },  10 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B -1" , nullptr },  11 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  0" , nullptr },  12 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 0" , nullptr },  13 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  0" , nullptr },  14 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 0" , nullptr },  15 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  0" , nullptr },  16 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  0" , nullptr },  17 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 0" , nullptr },  18 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  0" , nullptr },  19 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 0" , nullptr },  20 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  0" , nullptr },  21 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 0" , nullptr },  22 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  0" , nullptr },  23 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  1" , nullptr },  24 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 1" , nullptr },  25 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  1" , nullptr },  26 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 1" , nullptr },  27 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  1" , nullptr },  28 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  1" , nullptr },  29 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 1" , nullptr },  30 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  1" , nullptr },  31 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 1" , nullptr },  32 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  1" , nullptr },  33 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 1" , nullptr },  34 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  1" , nullptr },  35 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  2" , nullptr },  36 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 2" , nullptr },  37 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  2" , nullptr },  38 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 2" , nullptr },  39 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  2" , nullptr },  40 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  2" , nullptr },  41 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 2" , nullptr },  42 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  2" , nullptr },  43 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 2" , nullptr },  44 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  2" , nullptr },  45 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 2" , nullptr },  46 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  2" , nullptr },  47 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  3" , nullptr },  48 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 3" , nullptr },  49 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  3" , nullptr },  50 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 3" , nullptr },  51 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  3" , nullptr },  52 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  3" , nullptr },  53 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 3" , nullptr },  54 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  3" , nullptr },  55 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 3" , nullptr },  56 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  3" , nullptr },  57 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 3" , nullptr },  58 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  3" , nullptr },  59 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  4" , nullptr },  60 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 4" , nullptr },  61 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  4" , nullptr },  62 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 4" , nullptr },  63 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  4" , nullptr },  64 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  4" , nullptr },  65 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 4" , nullptr },  66 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  4" , nullptr },  67 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 4" , nullptr },  68 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  4" , nullptr },  69 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 4" , nullptr },  70 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  4" , nullptr },  71 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  5" , nullptr },  72 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 5" , nullptr },  73 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  5" , nullptr },  74 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 5" , nullptr },  75 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  5" , nullptr },  76 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  5" , nullptr },  77 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 5" , nullptr },  78 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  5" , nullptr },  79 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 5" , nullptr },  80 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  5" , nullptr },  81 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 5" , nullptr },  82 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  5" , nullptr },  83 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  6" , nullptr },  84 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 6" , nullptr },  85 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  6" , nullptr },  86 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 6" , nullptr },  87 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  6" , nullptr },  88 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  6" , nullptr },  89 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 6" , nullptr },  90 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  6" , nullptr },  91 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 6" , nullptr },  92 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  6" , nullptr },  93 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 6" , nullptr },  94 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  6" , nullptr },  95 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  7" , nullptr },  96 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 7" , nullptr },  97 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  7" , nullptr },  98 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 7" , nullptr },  99 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  7" , nullptr }, 100 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  7" , nullptr }, 101 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 7" , nullptr }, 102 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  7" , nullptr }, 103 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 7" , nullptr }, 104 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  7" , nullptr }, 105 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 7" , nullptr }, 106 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  7" , nullptr }, 107 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  8" , nullptr }, 108 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 8" , nullptr }, 109 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  8" , nullptr }, 110 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 8" , nullptr }, 111 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  8" , nullptr }, 112 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  8" , nullptr }, 113 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 8" , nullptr }, 114 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  8" , nullptr }, 115 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 8" , nullptr }, 116 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  8" , nullptr }, 117 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 8" , nullptr }, 118 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  8" , nullptr }, 119 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  9" , nullptr }, 120 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 9" , nullptr }, 121 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  9" , nullptr }, 122 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 9" , nullptr }, 123 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  9" , nullptr }, 124 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  9" , nullptr }, 125 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 9" , nullptr }, 126 , def::mapping::target_t::device),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  9" , nullptr }, 127 , def::mapping::target_t::device),
  MENU_BUILDER(mi_cmap_copy_t         ,    4 , { "Copy from Mapping 2", "マッピング2からコピー" }, def::mapping::target_t::device),
  MENU_BUILDER(mi_cmap_reset_default_t ,   4 , { "Reset to Default", "デフォルトに戻す" }),
  MENU_BUILDER(mi_tree_t              ,   3  , { "Mapping 2(Song)", "マッピング2 (ソング)" }),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Play Button"   , "プレイボタン" }),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 1"      , "ボタン 1"     },  1 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 2"      , "ボタン 2"     },  2 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 3"      , "ボタン 3"     },  3 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 4"      , "ボタン 4"     },  4 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 5"      , "ボタン 5"     },  5 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 6"      , "ボタン 6"     },  6 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 7"      , "ボタン 7"     },  7 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 8"      , "ボタン 8"     },  8 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 9"      , "ボタン 9"     },  9 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 10"     , "ボタン 10"    }, 10 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 11"     , "ボタン 11"    }, 11 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 12"     , "ボタン 12"    }, 12 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 13"     , "ボタン 13"    }, 13 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 14"     , "ボタン 14"    }, 14 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_internal_t       ,     5, { "Button 15"     , "ボタン 15"    }, 15 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Slot Button"   , "スロットボタン" }),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 1"    , "スロットBtn 1" },  1 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 2"    , "スロットBtn 2" },  2 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 3"    , "スロットBtn 3" },  3 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_slotbutton_t     ,     5, { "Slot Btn 4"    , "スロットBtn 4" },  4 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_tree_t              ,    4 , { "Ext Input"     , "拡張入力"     }),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 1"        , "拡張 1"       },   1 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 2"        , "拡張 2"       },   2 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 3"        , "拡張 3"       },   3 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 4"        , "拡張 4"       },   4 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 5"        , "拡張 5"       },   5 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 6"        , "拡張 6"       },   6 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 7"        , "拡張 7"       },   7 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 8"        , "拡張 8"       },   8 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 9"        , "拡張 9"       },   9 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 10"       , "拡張 10"      },  10 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 11"       , "拡張 11"      },  11 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 12"       , "拡張 12"      },  12 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 13"       , "拡張 13"      },  13 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 14"       , "拡張 14"      },  14 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 15"       , "拡張 15"      },  15 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 16"       , "拡張 16"      },  16 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 17"       , "拡張 17"      },  17 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 18"       , "拡張 18"      },  18 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 19"       , "拡張 19"      },  19 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 20"       , "拡張 20"      },  20 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 21"       , "拡張 21"      },  21 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 22"       , "拡張 22"      },  22 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 23"       , "拡張 23"      },  23 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 24"       , "拡張 24"      },  24 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 25"       , "拡張 25"      },  25 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 26"       , "拡張 26"      },  26 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 27"       , "拡張 27"      },  27 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 28"       , "拡張 28"      },  28 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 29"       , "拡張 29"      },  29 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 30"       , "拡張 30"      },  30 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 31"       , "拡張 31"      },  31 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_external_t       ,     5, { " Ext 32"       , "拡張 32"      },  32 - 1, def::mapping::target_t::song),
  MENU_BUILDER(mi_tree_t              ,    4 , { "MIDI Note"     , "MIDI Note"    }),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C#-1" , nullptr },   1 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D -1" , nullptr },   2 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D#-1" , nullptr },   3 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E -1" , nullptr },   4 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F -1" , nullptr },   5 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F#-1" , nullptr },   6 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G -1" , nullptr },   7 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G#-1" , nullptr },   8 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A -1" , nullptr },   9 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A#-1" , nullptr },  10 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B -1" , nullptr },  11 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  0" , nullptr },  12 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 0" , nullptr },  13 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  0" , nullptr },  14 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 0" , nullptr },  15 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  0" , nullptr },  16 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  0" , nullptr },  17 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 0" , nullptr },  18 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  0" , nullptr },  19 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 0" , nullptr },  20 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  0" , nullptr },  21 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 0" , nullptr },  22 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  0" , nullptr },  23 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  1" , nullptr },  24 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 1" , nullptr },  25 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  1" , nullptr },  26 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 1" , nullptr },  27 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  1" , nullptr },  28 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  1" , nullptr },  29 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 1" , nullptr },  30 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  1" , nullptr },  31 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 1" , nullptr },  32 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  1" , nullptr },  33 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 1" , nullptr },  34 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  1" , nullptr },  35 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  2" , nullptr },  36 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 2" , nullptr },  37 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  2" , nullptr },  38 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 2" , nullptr },  39 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  2" , nullptr },  40 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  2" , nullptr },  41 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 2" , nullptr },  42 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  2" , nullptr },  43 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 2" , nullptr },  44 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  2" , nullptr },  45 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 2" , nullptr },  46 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  2" , nullptr },  47 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  3" , nullptr },  48 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 3" , nullptr },  49 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  3" , nullptr },  50 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 3" , nullptr },  51 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  3" , nullptr },  52 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  3" , nullptr },  53 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 3" , nullptr },  54 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  3" , nullptr },  55 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 3" , nullptr },  56 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  3" , nullptr },  57 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 3" , nullptr },  58 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  3" , nullptr },  59 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  4" , nullptr },  60 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 4" , nullptr },  61 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  4" , nullptr },  62 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 4" , nullptr },  63 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  4" , nullptr },  64 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  4" , nullptr },  65 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 4" , nullptr },  66 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  4" , nullptr },  67 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 4" , nullptr },  68 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  4" , nullptr },  69 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 4" , nullptr },  70 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  4" , nullptr },  71 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  5" , nullptr },  72 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 5" , nullptr },  73 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  5" , nullptr },  74 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 5" , nullptr },  75 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  5" , nullptr },  76 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  5" , nullptr },  77 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 5" , nullptr },  78 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  5" , nullptr },  79 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 5" , nullptr },  80 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  5" , nullptr },  81 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 5" , nullptr },  82 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  5" , nullptr },  83 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  6" , nullptr },  84 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 6" , nullptr },  85 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  6" , nullptr },  86 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 6" , nullptr },  87 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  6" , nullptr },  88 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  6" , nullptr },  89 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 6" , nullptr },  90 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  6" , nullptr },  91 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 6" , nullptr },  92 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  6" , nullptr },  93 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 6" , nullptr },  94 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  6" , nullptr },  95 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  7" , nullptr },  96 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 7" , nullptr },  97 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  7" , nullptr },  98 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 7" , nullptr },  99 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  7" , nullptr }, 100 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  7" , nullptr }, 101 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 7" , nullptr }, 102 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  7" , nullptr }, 103 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 7" , nullptr }, 104 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  7" , nullptr }, 105 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 7" , nullptr }, 106 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  7" , nullptr }, 107 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  8" , nullptr }, 108 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 8" , nullptr }, 109 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  8" , nullptr }, 110 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 8" , nullptr }, 111 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  8" , nullptr }, 112 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  8" , nullptr }, 113 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 8" , nullptr }, 114 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  8" , nullptr }, 115 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G# 8" , nullptr }, 116 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A  8" , nullptr }, 117 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  A# 8" , nullptr }, 118 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  B  8" , nullptr }, 119 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C  9" , nullptr }, 120 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  C# 9" , nullptr }, 121 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D  9" , nullptr }, 122 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  D# 9" , nullptr }, 123 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  E  9" , nullptr }, 124 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F  9" , nullptr }, 125 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  F# 9" , nullptr }, 126 , def::mapping::target_t::song),
  MENU_BUILDER(mi_ca_midinote_t       ,     5, { "  G  9" , nullptr }, 127 , def::mapping::target_t::song),
  MENU_BUILDER(mi_cmap_copy_t         ,    4 , { "Copy from Mapping 1", "マッピング1からコピー" }, def::mapping::target_t::song),
  MENU_BUILDER(mi_cmap_delete_t        ,    4, { "Delete Mapping" , "マッピング消去" }, def::mapping::target_t::song),
  MENU_BUILDER(mi_tree_t              ,  2   , { "Velocity"       , "ベロシティ(強弱)"}),
  MENU_BUILDER(mi_imu_velocity_t      ,   3  , { "Device Button"  , "本体ボタン"  }),
  MENU_BUILDER(mi_ext_midi_velocity_t ,   3  , { "External MIDI"  , "外部MIDI入力"}),
  MENU_BUILDER(mi_tree_t              ,  2   , { "Display"        , "表示"        }),
  MENU_BUILDER(mi_lcd_backlight_t     ,   3  , { "Backlight"      , "画面の輝度"  }),
  MENU_BUILDER(mi_led_brightness_t    ,   3  , { "LED Brightness" , "LEDの輝度"   }),
  MENU_BUILDER(mi_detail_view_t       ,   3  , { "Detail View"    , "詳細表示"    }),
  MENU_BUILDER(mi_wave_view_t         ,   3  , { "Wave View"      , "波形表示"    }),
  MENU_BUILDER(mi_language_t          ,  2   , { "Language"       , "言語"        }),
  MENU_BUILDER(mi_guide_sound_t       ,  2   , { "Guide Sound"    , "ガイド音"    }),
  MENU_BUILDER(mi_tree_t              ,  2   , { "Volume"         , "音量"        }),
  MENU_BUILDER(mi_vol_midi_t          ,   3  , { "MIDI Mastervol" , "MIDIマスター音量"}),
  MENU_BUILDER(mi_vol_adcmic_t        ,   3  , { "ADC MicAmp"     , "ADCマイクアンプ" }),
  MENU_BUILDER(mi_system_info_t       ,  2   , { "System Info"       , "システム情報"      }),
  MENU_BUILDER(mi_all_reset_t         ,  2   , { "Reset All Settings", "全設定リセット"    }),
  MENU_BUILDER(mi_manual_qr_t         , 1    , { "Web Manual"     , "Webマニュアル" }),
  nullptr, // end of menu
};
// const size_t menu_system_size = sizeof(menu_system) / sizeof(menu_system[0]) - 1;

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_PART = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_PART)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_part, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_part[] = {
  MENU_BUILDER(mi_tree_t          ,0  , { "Part Setting"   , "パート設定"        }),
  MENU_BUILDER(mi_program_root_t  , 1 , { "Tone"           , "音色"             }),
  MENU_BUILDER(mi_program_category_t, 2, { "Piano"         , "ピアノ"           }, 0),
  MENU_BUILDER(mi_program_category_t, 2, { "Chromatic"     , "鍵盤打楽器"       }, 1),
  MENU_BUILDER(mi_program_category_t, 2, { "Organ"         , "オルガン"         }, 2),
  MENU_BUILDER(mi_program_category_t, 2, { "Guitar"        , "ギター"           }, 3),
  MENU_BUILDER(mi_program_category_t, 2, { "Bass"          , "ベース"           }, 4),
  MENU_BUILDER(mi_program_category_t, 2, { "Strings"       , "ストリングス"     }, 5),
  MENU_BUILDER(mi_program_category_t, 2, { "Ensemble"      , "アンサンブル"     }, 6),
  MENU_BUILDER(mi_program_category_t, 2, { "Brass"         , "ブラス"           }, 7),
  MENU_BUILDER(mi_program_category_t, 2, { "Reed"          , "リード"           }, 8),
  MENU_BUILDER(mi_program_category_t, 2, { "Pipe"          , "パイプ"           }, 9),
  MENU_BUILDER(mi_program_category_t, 2, { "Synth Lead"    , "シンセリード"     }, 10),
  MENU_BUILDER(mi_program_category_t, 2, { "Synth Pad"     , "シンセパッド"     }, 11),
  MENU_BUILDER(mi_program_category_t, 2, { "Synth FX"      , "シンセFX"         }, 12),
  MENU_BUILDER(mi_program_category_t, 2, { "Ethnic"        , "民族楽器"         }, 13),
  MENU_BUILDER(mi_program_category_t, 2, { "Percussive"    , "打楽器"           }, 14),
  MENU_BUILDER(mi_program_category_t, 2, { "Sound FX"      , "効果音"           }, 15),
  MENU_BUILDER(mi_program_category_t, 2, { "Drum"          , "ドラム"           }, 16),
  MENU_BUILDER(mi_octave_t        , 1 , { "Octave"         , "オクターブ"       }),
  MENU_BUILDER(mi_voicing_t       , 1 , { "Voicing"        , "ボイシング"       }),
  MENU_BUILDER(mi_velocity_t      , 1 , { "Velocity"       , "ベロシティ値"     }),
  MENU_BUILDER(mi_partvolume_t    , 1 , { "Volume"         , "音量"            }),
  MENU_BUILDER(mi_loop_length_t   , 1 , { "Loop Length"    , "ループ長"         }),
  MENU_BUILDER(mi_anchor_step_t   , 1 , { "Anchor Step"    , "アンカーステップ" }),
  MENU_BUILDER(mi_stroke_speed_t  , 1 , { "Stroke Speed"   , "ストローク速度"  }),
  MENU_BUILDER(mi_tree_t           , 1 , { "DrumNote"       , "ドラムノート"     }),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch1"         , "ピッチ1"          }, 0),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch2"         , "ピッチ2"          }, 1),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch3"         , "ピッチ3"          }, 2),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch4"         , "ピッチ4"          }, 3),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch5"         , "ピッチ5"          }, 4),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch6"         , "ピッチ6"          }, 5),
  MENU_BUILDER(mi_drum_note_t     ,  2, { "Pitch7"         , "ピッチ7"          }, 6),
  MENU_BUILDER(mi_part_clipboard_t, 1 , { "Copy/Paste"     , "コピー/ペースト"  }),
  MENU_BUILDER(mi_save_arpeggio_t , 1 , { "Save Arpeggio"  , "パターンの保存"  }, def::app::data_type_t::data_arpeggio_users),
  MENU_BUILDER(mi_clear_notes_t   , 1 , { "Clear All Notes", "ノートをクリア"}),
  nullptr, // end of menu
};
// const size_t menu_part_size = sizeof(menu_part) / sizeof(menu_part[0]) - 1;

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_PLAY_MODE = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_PLAY_MODE)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_play_mode, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_play_mode[] = {
  // MENU_BUILDER(mi_play_mode_t   ,0   , { "Select Mode"       , "モード選択"        }),
  MENU_BUILDER(mi_tree_t          ,0   , { "Song"         , "ソング"        }),
  MENU_BUILDER(mi_play_mode_t , 1  , {  "Play Mode"   , "プレイモード"      }),
  MENU_BUILDER(mi_recording_t     , 1  , {  "Recoding"    , "レコーディング"     }),
  nullptr, // end of menu
};

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_SONG_EDIT = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_SONG_EDIT)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_song_edit, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_song_edit[] = {
  MENU_BUILDER(mi_tree_t              ,0  , { "Recording Menu"   , "レコーディングメニュー" }),
  MENU_BUILDER(mi_tree_t              , 1 , { "Jump"             , "ジャンプ"        }),
  MENU_BUILDER(mi_seq_index_t         ,  2, {  "Go to Start"     , "先頭へ移動"      }, 0),
  MENU_BUILDER(mi_seq_index_t         ,  2, {  "Go to End"       , "末尾へ移動"      }, -1),
  MENU_BUILDER(mi_tree_t              , 1 , { "Stretch / Compress", "拡張・圧縮"     }),
  MENU_BUILDER(mi_seq_resize_t        ,  2, {  "Stretch(200%)"   , "200%に拡張"      }, 4),
  MENU_BUILDER(mi_seq_resize_t        ,  2, {  "Compress(50%)"   , "50%に圧縮"       }, 1),
  MENU_BUILDER(mi_clear_seq_t         , 1 , { "Clear After Cursor", "カーソル後をクリア" }),
  MENU_BUILDER(mi_play_mode_t         , 1 , { "Play Mode"        , "プレイモード"    }),
  MENU_BUILDER(mi_exit_recording_t    , 1 , { "Exit Recording"   , "レコーディング終了" }),
  MENU_BUILDER(mi_open_main_menu_t    , 1 , { "More..."          , "詳細設定"        }),
  nullptr, // end of menu
};

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_AUTOSONG = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_AUTOSONG)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_autosong, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_autosong[] = {
  MENU_BUILDER(mi_tree_t              ,0     , { "Guide Play Menu"  , "ガイドプレイメニュー" }),
  MENU_BUILDER(mi_song_autorepeat_t   , 1    , { "Auto Repeat"      , "オートリピート"       }),
  MENU_BUILDER(mi_song_part_operation_t, 1   , { "Part Operation"   , "パート操作"           }),
  MENU_BUILDER(mi_play_mode_t         , 1    , { "Play Mode"        , "プレイモード"         }),
  MENU_BUILDER(mi_open_main_menu_t    , 1    , { "More..."          , "詳細設定"             }),
  nullptr, // end of menu
};

#undef MENU_ID
#undef MENU_BUILDER
static constexpr const size_t START_COUNTER_PART_QUICK_EDIT = __COUNTER__ + 1;
#define MENU_ID (__COUNTER__ - START_COUNTER_PART_QUICK_EDIT)
#define MENU_BUILDER(type, ...) (const type[]){ { def::menu_category_t::menu_part_quick_edit, MENU_ID, __VA_ARGS__ } }

static constexpr menu_item_ptr menu_part_quick_edit[] = {
  MENU_BUILDER(mi_tree_t            ,0  , { "Part Edit"      , "パート編集"   }),
  MENU_BUILDER(mi_program_root_t    , 1 , { "Tone"                 , "音色"            }),
  MENU_BUILDER(mi_program_category_t, 2 , { "Piano"                , "ピアノ"          }, 0),
  MENU_BUILDER(mi_program_category_t, 2 , { "Chromatic"            , "鍵盤打楽器"      }, 1),
  MENU_BUILDER(mi_program_category_t, 2 , { "Organ"                , "オルガン"        }, 2),
  MENU_BUILDER(mi_program_category_t, 2 , { "Guitar"               , "ギター"          }, 3),
  MENU_BUILDER(mi_program_category_t, 2 , { "Bass"                 , "ベース"          }, 4),
  MENU_BUILDER(mi_program_category_t, 2 , { "Strings"              , "ストリングス"    }, 5),
  MENU_BUILDER(mi_program_category_t, 2 , { "Ensemble"             , "アンサンブル"    }, 6),
  MENU_BUILDER(mi_program_category_t, 2 , { "Brass"                , "ブラス"          }, 7),
  MENU_BUILDER(mi_program_category_t, 2 , { "Reed"                 , "リード"          }, 8),
  MENU_BUILDER(mi_program_category_t, 2 , { "Pipe"                 , "パイプ"          }, 9),
  MENU_BUILDER(mi_program_category_t, 2 , { "Synth Lead"           , "シンセリード"    }, 10),
  MENU_BUILDER(mi_program_category_t, 2 , { "Synth Pad"            , "シンセパッド"    }, 11),
  MENU_BUILDER(mi_program_category_t, 2 , { "Synth FX"             , "シンセFX"        }, 12),
  MENU_BUILDER(mi_program_category_t, 2 , { "Ethnic"               , "民族楽器"        }, 13),
  MENU_BUILDER(mi_program_category_t, 2 , { "Percussive"           , "打楽器"          }, 14),
  MENU_BUILDER(mi_program_category_t, 2 , { "Sound FX"             , "効果音"          }, 15),
  MENU_BUILDER(mi_program_category_t, 2 , { "Drum"                 , "ドラム"          }, 16),
  MENU_BUILDER(mi_tree_t            , 1 , { "Pattern"              , "パターン"         }),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "a. Drum"             , nullptr           }, def::app::data_type_t::data_arpeggio_drum ),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "b. Bass"             , nullptr           }, def::app::data_type_t::data_arpeggio_bass ),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "c. Guitar"           , nullptr           }, def::app::data_type_t::data_arpeggio_guitar ),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "d. Piano"            , nullptr           }, def::app::data_type_t::data_arpeggio_piano ),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "e. Other"            , nullptr           }, def::app::data_type_t::data_arpeggio_other ),
  MENU_BUILDER(mi_load_arpeggio_t   ,  2, {  "f. User(SD)"         , nullptr           }, def::app::data_type_t::data_arpeggio_users),
  MENU_BUILDER(mi_partvolume_t      , 1 , { "Volume"               , "音量"            }),
  MENU_BUILDER(mi_partpan_t         , 1 , { "Pan"                  , "左右バランス"     }),
  MENU_BUILDER(mi_octave_t          , 1 , { "Octave"               , "オクターブ"       }),
  MENU_BUILDER(mi_anchor_set_t      , 1 , { "Chord Change Behavior", "コード変更時の動作"}),
  MENU_BUILDER(mi_arpeggio_edit_t   , 1 , { "Edit Pattern"    , "パターン編集"   }),
  MENU_BUILDER(mi_part_clipboard_t  , 1 , { "Copy/Paste"           , "コピー/ペースト"  }),
  MENU_BUILDER(mi_clear_notes_t     , 1 , { "Clear All Notes"      , "ノートをクリア"   }),
  nullptr, // end of menu
};

#undef MENU_ID
#undef MENU_BUILDER

void menu_control_t::openMenu(def::menu_category_t category)
{
  system_registry->menu_status.reset();

  _menu_array = getMenuArray(category);
  bool hasSubMenu = (_menu_array[1] != nullptr);
  system_registry->menu_status.setSelectIndex(0, hasSubMenu ? 1 : 0);
  system_registry->menu_status.setCurrentLevel(0);
  system_registry->menu_status.setCurrentMenuID(0);
  system_registry->menu_status.setMenuCategory( category );

  _category = category;

  system_registry->runtime_info.setGuiFlag_Menu( true );
  if (!hasSubMenu) {
    _menu_array[0]->enter();
  }
}

bool menu_control_t::enter(void)
{
  auto current_level = system_registry->menu_status.getCurrentLevel();
  auto select_index = system_registry->menu_status.getSelectIndex(current_level);
  auto current_menu_id = system_registry->menu_status.getCurrentMenuID();

  if (current_menu_id == select_index) {
    return _menu_array[select_index]->execute();
  }
  return _menu_array[select_index]->enter();
}

bool menu_control_t::exit(void)
{
  auto current_index = system_registry->menu_status.getCurrentMenuID();
  return _menu_array[current_index]->exit();
}

bool menu_control_t::inputNumber(uint8_t number)
{
  auto current_index = system_registry->menu_status.getCurrentMenuID();
  return _menu_array[current_index]->inputNumber(number);
}

bool menu_control_t::inputUpDown(int updown)
{
  auto current_index = system_registry->menu_status.getCurrentMenuID();

  return _menu_array[current_index]->inputUpDown(updown);
}

int menu_control_t::getChildrenMenuIDList(std::vector<uint16_t>* index_list, uint16_t parent_index)
{
  return getSubMenuIndexList(index_list, _menu_array, parent_index);
}

int menu_control_t::getFocusDisplayNumber(void)
{
  auto level = system_registry->menu_status.getCurrentLevel();
  auto focus_index = system_registry->menu_status.getSelectIndex(level);

  // 常にリスト内の位置 + minValue が表示番号
  // getCurrentMenuID() が現在表示中の親（子リストのオーナー）を指す
  auto parent_index = system_registry->menu_status.getCurrentMenuID();
  std::vector<uint16_t> child_list;
  int child_count = getSubMenuIndexList(&child_list, _menu_array, parent_index);
  int min_value = _menu_array[parent_index]->getMinValue();
  for (int i = 0; i < child_count; ++i) {
    if (child_list[i] == focus_index) {
      return i + min_value;
    }
  }
  // 見つからない場合は先頭扱い
  return min_value;
}

bool menu_control_t::queueFocusSound(void)
{
  auto level = system_registry->menu_status.getCurrentLevel();
  auto focus_index = system_registry->menu_status.getSelectIndex(level);
  return _menu_array[focus_index]->onFocus();
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
  assert(menu_id_check(menu_system  , def::menu_category_t::menu_system  ) && "menu_system definition error");
  assert(menu_id_check(menu_part    , def::menu_category_t::menu_part    ) && "menu_part definition error");
  assert(menu_id_check(menu_play_mode , def::menu_category_t::menu_play_mode ) && "menu_play_mode definition error");
  assert(menu_id_check(menu_song_edit , def::menu_category_t::menu_song_edit ) && "menu_song_edit definition error");
  assert(menu_id_check(menu_autosong, def::menu_category_t::menu_autosong) && "menu_autosong definition error");
  assert(menu_id_check(menu_part_quick_edit, def::menu_category_t::menu_part_quick_edit) && "menu_part_quick_edit definition error");
#endif

  switch (category) {
  default:
  case def::menu_category_t::menu_system:  return menu_system;
  case def::menu_category_t::menu_part:    return menu_part;
  case def::menu_category_t::menu_play_mode: return menu_play_mode;
  case def::menu_category_t::menu_song_edit: return menu_song_edit;
  case def::menu_category_t::menu_autosong:return menu_autosong;
  case def::menu_category_t::menu_part_quick_edit: return menu_part_quick_edit;
  }
  return nullptr;
}

//-------------------------------------------------------------------------
