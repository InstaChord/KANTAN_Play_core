// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_OPERATOR_HPP
#define KANPLAY_TASK_OPERATOR_HPP

#include "system_registry.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
class task_operator_t {
public:
  void start(void);
private:
  registry_t::history_code_t _history_code = 0;
  // 前回発動したコマンド

  static constexpr const size_t max_command_history = 4;
  def::command::command_param_t _command_history[max_command_history];
  // uint16_t _prev_command = 0;
  static void task_func(task_operator_t* me);
  void commandProccessor(const def::command::command_param_t& command_param, const bool is_pressed);

  void procChordModifier(const def::command::command_param_t& command_param, const bool is_pressed);
  void procChordMinorSwap(const def::command::command_param_t& command_param, const bool is_pressed);
  void procChordSemitone(const def::command::command_param_t& command_param, const bool is_pressed);
  void procChordBassDegree(const def::command::command_param_t& command_param, const bool is_pressed);
  void procChordBassSemitone(const def::command::command_param_t& command_param, const bool is_pressed);
  void procEditFunction(const def::command::command_param_t& command_param);
  void procMelodyEditFunction(const def::command::command_param_t& command_param);
  void enterMelodyEdit(void);
  void setSlotIndex(uint8_t slot_index);

  void changeCommandMapping(void);

  void changeSubbuttonMapping(const uint32_t *map);

  void afterMenuClose(void);
  void syncButtonColor(void);

  uint8_t _modifier_press_order[8];
  uint8_t _bass_degree_press_order[8];
  bool _mapping_switch_used = false;
  struct melody_undo_t {
    uint16_t step;
    system_registry_t::melody_event_t event;
  };
  static constexpr size_t max_melody_undo = 32;
  melody_undo_t _melody_undo[max_melody_undo];
  size_t _melody_undo_count = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
