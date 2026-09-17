// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <memory>

#include "gui.hpp"
#include "resource_icon.hpp"

#include "system_registry.hpp"
#include "file_manage.hpp"
#include "menu_data.hpp"
#include "sequencer_external.hpp"

#if CORE_DEBUG_LEVEL > 3
// #define DEBUG_GUI
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------
// extern instance
gui_t gui;

static M5GFX* _gfx;

void gui_t::startWrite(void) { _gfx->startWrite(); }
void gui_t::endWrite(void) { _gfx->endWrite(); }


static constexpr const int32_t header_height = 24;
static constexpr const int32_t main_btns_height = 96;
static constexpr const int32_t sub_btns_height = 32;
static constexpr const size_t menu_header_height = 32;
static constexpr const int32_t disp_width = 240;
static constexpr const int32_t disp_height = 320;
static constexpr const int32_t main_area_width = disp_width;
static constexpr const int32_t main_area_height = disp_height - header_height - main_btns_height - sub_btns_height;

#include "gui/gui_base.inl"
#include "gui/gui_popup.inl"
#include "gui/gui_info.inl"
#include "gui/gui_buttons.inl"
#include "gui/gui_partinfo.inl"
#include "gui/gui_melody.inl"
#include "gui/gui_progression.inl"
#include "gui/gui_menu.inl"
#include "gui/gui_misc.inl"

//-------------------------------------------------------------------------
}; // namespace kanplay_ns
