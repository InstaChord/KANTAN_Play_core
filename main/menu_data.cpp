// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "menu_data.hpp"
#include "file_manage.hpp"
#include "sequencer_external.hpp"
#include "task_midi.hpp"
#include "restart_confirmation_state.hpp"

namespace kanplay_ns {
//-------------------------------------------------------------------------
// extern instance
menu_control_t menu_control;

static std::string _title_text_buffer;
static std::string _tmp_filename;
static int _input_number_result;

static menu_item_ptr_array getMenuArray(def::menu_category_t category);

#include "menu_data/menu_data_base.inl"
#include "menu_data/menu_data_system.inl"
#include "menu_data/menu_data_part.inl"
#include "menu_data/menu_data_file.inl"
#include "menu_data/menu_data_midi.inl"
#include "menu_data/menu_data_arrays.inl"

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
