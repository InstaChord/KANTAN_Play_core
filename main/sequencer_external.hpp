// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <string>
#include "common_define.hpp"

namespace kanplay_ns { namespace sequencer_external {
void loadPreferredDevice();
void prepareAtBoot();
void service();
void beginScan();
bool back();
size_t select(size_t index);
size_t rowCount();
std::string rowText(size_t index);
std::string statusText();
std::string deviceInfo(size_t index);
bool forgetDevice();
bool restartConnection();
bool changeSource(def::command::external_input_source_t source);
enum class restart_notice_t : uint8_t { idle, saving, restarting };
enum class restart_reason_t : uint8_t { input_source, ble_connection, firmware_update, system };
bool requestRestart(restart_reason_t reason);
restart_notice_t getRestartNotice();
bool restartNoticeActive();
const char* restartNoticeTitle();
const char* restartNoticeTarget();
const char* restartNoticeDetail();
void noteConnectCrash();
// Set only by the Wi-Fi worker; menus read an atomic status snapshot.
enum class wifi_status_t { idle, stopping_ble, settling, ready, failed };
void setWiFiStatus(wifi_status_t status);
wifi_status_t getWiFiStatus();
const char* wifiStatusText();
}}
