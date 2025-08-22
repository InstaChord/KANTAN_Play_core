// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_KANTANPLAY_HPP
#define KANPLAY_TASK_KANTANPLAY_HPP

#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
class task_kantanplay_t {
public:
  void start(void);
private:
  registry_t::history_code_t _player_command_history_code = 0;
  static void task_func(task_kantanplay_t* me);
  bool commandProccessor(void);

/*
////

// オモテ拍の流れ

手動演奏(全部手動)   /   手動演奏(ウラ自動)   /   自動演奏(外部パルス)   /   自動演奏(Songテンポ)
      ↓                       ↓                      ↓                       ↓
  [ユーザーが Degree ボタンを押して chord_degreeコマンドが発行、 procChordDegree が実行される ]
      ↓                       ↓
  [ 上記トリガで chord_beat 発行 ]    [ 外部から chord_beat 発行 ]
      ↓                       ↓                      ↓
  [                 procChordBeat 関数開始                 ]   [ autoProc 関数でタイミング判定 ]
      ↓                       ↓                      ↓　                      ↓
  [        chordBeat 関数を実行 { chordStepAdvance 関数 / chordStepPlay 関数 }             ]




自動演奏（内部テンポ）
  オモテのタイミングをテンポ情報に依存して自動化しステップ進めるコマンドを定期的に発行する

自動演奏（外部パルス）
  ステップ進めるコマンドに依存して待機

手動演奏 (ウラ拍自動)・手動演奏 (オモテウラ手動)

Degree操作コマンド {
  押した時 {
    次回の演奏条件を確定 (度数・コード種・スワップ・半音を確定する)
    if (手動のとき) {
      ステップ進めるコマンド発行
    }
    if (自動演奏 (内部テンポ) がwait状態とき) {
      自動演奏 (内部テンポ) をrunning状態に変更
      オートオモテ拍の次回発動時間を0に
    }
  }
  離した時 {
    if (オモテウラ手動のとき) {
      ステップ進めるコマンド発行
    }
  }
}


オートオモテ拍の時間に達した時 {
  if (自動演奏(内部テンポ)) {
    ステップ進めるコマンド発行
  }
}

ステップ進むコマンド来た時 {
}

*/
  struct chord_option_t
  {
    uint8_t degree;
    uint8_t bass_degree;
    // int8_t semitone_shift;
    // int8_t bass_semitone_shift;
    // bool minor_swap;
    // 比較演算子オペレータ

    bool operator==(const chord_option_t& rhs) const
    {
      return rhs.degree == degree
          && rhs.bass_degree == bass_degree
          // && rhs.semitone_shift == semitone_shift
          // && rhs.bass_semitone_shift == bass_semitone_shift
          // && rhs.minor_swap == minor_swap;
          ;
    }
    bool operator!=(const chord_option_t& rhs) const
    {
      return !(*this == rhs);
    }
  };
  
  // 次回のオモテ拍から適用されるオプション (自動演奏で先行入力した場合)
  chord_option_t _next_option;
  
  // 現在の演奏オプション (演奏中の状態)
  chord_option_t _current_option;

  // 自動演奏(オモテ拍)が次回発動するまでの残り時間 (usec)
  int32_t _auto_play_onbeat_remain_usec = -1;

  // 自動演奏(ウラ拍)が次回発動するまでの残り時間 (usec)
  int32_t _auto_play_offbeat_remain_usec = -1;

  // 自動演奏(ウラ拍)の間隔時間 (usec) ※スイングに対応するため2つ用意する
  int32_t _auto_play_offbeat_cycle_usec[2] = { 0, };

  // 自動演奏時のユーザーによるオンビート操作の遅延許容の残り時間 (usec)
  int32_t _auto_play_input_tolerating_remain_usec = -1;

  // オンビート演奏間の経過時間 (usec)
  int32_t _reactive_onbeat_cycle_usec = -1;

  // 最新のオンビート演奏時点の時間情報 (usec)
  uint32_t _reactive_onbeat_usec = 0;

  // ステップのオン・オフ進行状況保持用 0==オンビート , 1~3==オフビート位置
  uint8_t _current_beat_index = 0;

  // オモテ拍のタイミングで確定した演奏中のスロット番号
  uint8_t _current_slot_index;

  // オモテ拍のタイミングで確定したメジャー・マイナースワップ
  bool _minor_swap;

  // オモテ拍のタイミングで確定した半音上げ下げ
  int8_t _semitone_shift;

  // オモテ拍のタイミングで確定した半音上げ下げ(オンコード用)
  int8_t _bass_semitone_shift;

  // 現在のサステインの状態
  def::play::sustain_state_t _sustain_state;

  // Degreeボタンコマンドの処理
  void procChordDegree(const def::command::command_param_t& command_param, const bool is_pressed);

  // ステップ進むコマンドの処理 (外部パルス信号等)
  void procChordBeat(const def::command::command_param_t& command_param, const bool is_pressed);

  void chordBeat(const bool on_beat);
  void chordStepAdvance(bool disable_note_off = false);
  void chordStepPlay(void);
  int32_t calcSwing_x100(void);
  int32_t calcStepAdvance(const bool on_beat);
  void updateOffbeatTiming(void);
  void setOnbeatCycle(int32_t usec = -1);
  int32_t getOnbeatCycle(void);
  int32_t getOnbeatCycleBySongTempo(void);
  uint32_t autoProc(void);
  uint32_t chordProc(void);
  void sustainProc(void);
  void setSustain(bool sustain_on);

  void chordStepReset(void);
  void chordNoteOff(int part);
  void resetStepAndMute(void);
  void resetStep(void);
  void allPartsNoteOff(void);
  void procSoundEffect(const def::command::command_param_t& command_param, const bool is_pressed);
  void procNoteButton(const def::command::command_param_t& command_param, const bool is_pressed);
  void procDrumButton(const def::command::command_param_t& command_param, const bool is_pressed);
  void procChordStepResetRequest(const def::command::command_param_t& command_param, const bool is_pressed);
  void procPlayEffect(const def::command::command_param_t& command_param, const bool is_pressed);

  void setPitchManage(uint8_t part, uint8_t pitch, uint8_t midi_ch, uint8_t note_number, int8_t velocity, int32_t press_usec, int32_t release_usec);

  struct midi_pitch_manage_t
  {
    int32_t press_usec;
    int32_t release_usec;
    uint8_t midi_ch;
    uint8_t note_number;
    uint8_t velocity;
  };
  // ピッチごとの演奏情報 (履歴を最大2個分持てるようにする)
  // 履歴の配列は 0 が古い。max_manage_history - 1 が最新
  static constexpr const size_t max_manage_history = 3;
  midi_pitch_manage_t _midi_pitch_manage[def::app::max_chord_part][def::app::max_pitch_with_drum][max_manage_history];
  int32_t checkOtherPitchNote(int part, int pitch, int midi_ch, int note_number);

  struct midi_note_manage_t
  {
    uint8_t midi_ch = 0;
    uint8_t note_number = 0;
  };
  midi_note_manage_t _midi_note_manage[def::hw::max_main_button];

  // registry_t::history_code_t _slot_index;
  uint32_t _prev_usec = 0;
  uint32_t _current_usec = 0;

  // 自動でアルペジエータが先頭に戻るまでのタイムアウト残り時間(マイクロ秒)
  int32_t _arpeggio_reset_remain_usec = -1;

  // 演奏時のベロシティ
  uint8_t _press_velocity;

  bool _step_reset_request = false;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
