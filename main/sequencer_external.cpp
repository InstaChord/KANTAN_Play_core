// SPDX-License-Identifier: MIT
#include "sequencer_external.hpp"
#include "ble_selection_state.hpp"
#include "task_midi.hpp"
#include "system_registry.hpp"
#include "file_manage.hpp"
#include <M5Unified.h>
#include <atomic>
#include <mutex>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#if !defined(M5UNIFIED_PC_BUILD)
#include <esp_attr.h>
#else
#define RTC_DATA_ATTR
#endif

namespace kanplay_ns { namespace sequencer_external {
namespace {
using phase_t = ble_selection_state_t::phase_t;
std::mutex view_mutex;
ble_selection_state_t view;
task_midi_t midi; // Handle to the shared transports; never starts another task.
task_midi_t::ble_scan_device_t devices[12];
std::atomic<bool> crash_blocked{false};
std::atomic<wifi_status_t> wifi_status{wifi_status_t::idle};
std::atomic<restart_notice_t> restart_notice{restart_notice_t::idle};
std::atomic<uint8_t> restart_target{uint8_t(def::command::external_input_off)};
std::atomic<restart_reason_t> restart_reason{restart_reason_t::input_source};
uint32_t restart_not_before_msec = 0;
RTC_DATA_ATTR uint32_t input_restart_marker = 0;
constexpr uint32_t input_restart_magic = 0x534551A0u;
bool host_disabled_on_boot = false;
const char* failure = "No BLE devices / Retry";
const char* text(const char* en, const char* ja) { return localize_text_t{en, ja}.get(); }
const char* sourceText(def::command::external_input_source_t source) {
  switch (source) {
  case def::command::external_input_usb_midi_host: return text("USB MIDI Controller", "USB MIDIコントローラー");
  case def::command::external_input_usb_midi_device: return text("USB MIDI Computer", "USB MIDIコンピューター");
  case def::command::external_input_ble_midi: return "BLE MIDI";
  default: return text("Off", "オフ");
  }
}
void scheduleRestart(def::command::external_input_source_t target, restart_reason_t reason) {
  restart_target.store(uint8_t(target), std::memory_order_relaxed);
  restart_reason.store(reason, std::memory_order_relaxed);
  restart_not_before_msec = M5.millis() + 1800;
  restart_notice.store(restart_notice_t::restarting, std::memory_order_release);
}

// Small, versioned peer record, as in Sampler. Do not serialize the song or
// allocate a large JSON document during BLE pairing. Sampler's file is untouched.
struct peer_record_t {
  uint32_t magic;
  char address[18];
  char name[24];
  uint8_t reserved[2];
  uint32_t checksum;
};
#if defined(M5UNIFIED_PC_BUILD)
constexpr const char* peer_path = "sequencer_ble.bin";
constexpr const char* peer_temp = "sequencer_ble.tmp";
#else
constexpr const char* peer_path = "/littlefs/sequencer_ble.bin";
constexpr const char* peer_temp = "/littlefs/sequencer_ble.tmp";
#endif
bool readPeer(const char* path, peer_record_t& record) {
  const int fd = ::open(path, O_RDONLY);
  if (fd < 0) { return false; }
  const auto length = ::read(fd, &record, sizeof(record));
  uint8_t extra;
  const bool exact = length == sizeof(record) && ::read(fd, &extra, 1) == 0;
  ::close(fd);
  return exact;
}
uint32_t checksum(const peer_record_t& data) {
  uint32_t hash = 2166136261u;
  const auto* p = reinterpret_cast<const uint8_t*>(&data);
  for (size_t i = 0; i < offsetof(peer_record_t, checksum); ++i) { hash = (hash ^ p[i]) * 16777619u; }
  return hash;
}
bool savePeer(const char* address, const char* name, bool fresh_pairing = false) {
  peer_record_t data{};
  data.magic = 0x53424C01u;
  snprintf(data.address, sizeof(data.address), "%s", address);
  snprintf(data.name, sizeof(data.name), "%s", name);
  data.reserved[0] = fresh_pairing ? 1 : 0;
  data.checksum = checksum(data);
  // Write/verify a temporary file, then rename: failed writes keep the old peer.
  if (!storage_littlefs.beginStorage()) { return false; }
  const int fd = ::open(peer_temp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) { return false; }
  const bool written = ::write(fd, &data, sizeof(data)) == sizeof(data);
  const bool synced = ::fsync(fd) == 0;
  const bool closed = ::close(fd) == 0;
  if (!written || !synced || !closed) { return false; }
  peer_record_t readback{};
  if (!readPeer(peer_temp, readback) || memcmp(&data, &readback, sizeof(data))) { return false; }
  return ::rename(peer_temp, peer_path) == 0;
}
bool available() {
  return system_registry->midi_port_setting.getExternalInputSource() == def::command::external_input_ble_midi
      && !task_midi_t::isBLESuspendedForWiFi();
}
void startScan() {
  if (!available()) { view.phase = phase_t::blocked; return; }
  // Re-entering the page must not overwrite a scan/connection in flight.
  if (view.phase == phase_t::scanning || view.phase == phase_t::connecting) { return; }
  if (crash_blocked.exchange(false)) {
    system_registry->midi_port_setting.setBLEMIDI(def::command::midi_input);
  }
  view.scan(M5.millis());
  failure = "No BLE devices / Retry";
  midi.requestBLEMidiScan();
}
}

void prepareAtBoot() {
  auto& ports = system_registry->midi_port_setting;
  const auto source = ports.getExternalInputSource();
  const bool intentional = input_restart_marker == (input_restart_magic ^ uint32_t(source));
  input_restart_marker = 0;
  if (source == def::command::external_input_usb_midi_host && !intentional) {
    // Match Sampler: ordinary boot returns USB-C to PC/charging. Only the
    // menu's deliberate save/restart handoff may enable the host stack.
    ports.setExternalInputSource(def::command::external_input_off);
    host_disabled_on_boot = true;
  }
  ports.applyExternalInputSourceAtBoot();
}

void loadPreferredDevice() {
  peer_record_t data{};
  if (!storage_littlefs.beginStorage()
      || !readPeer(peer_path, data)
      || data.magic != 0x53424C01u || checksum(data) != data.checksum
      || data.address[17] || data.name[23] || data.reserved[0] > 1) { return; }
  // Consume the reset request before connecting. Bond operations are then
  // performed by the MIDI owner after BLE is initialized, not by the UI.
  if (data.reserved[0] && !savePeer(data.address, data.name)) { return; }
  midi.setBLEMidiPreferredDevice(data.address, data.name, data.reserved[0] != 0);
}
void noteConnectCrash() { crash_blocked.store(true); }
restart_notice_t getRestartNotice() { return restart_notice.load(std::memory_order_acquire); }
bool restartNoticeActive() { return getRestartNotice() != restart_notice_t::idle; }
bool requestRestart(restart_reason_t reason) {
  if (restartNoticeActive()) { return false; }
  scheduleRestart(system_registry->midi_port_setting.getExternalInputSource(), reason);
  return true;
}
const char* restartNoticeTitle() {
  switch (restart_reason.load(std::memory_order_relaxed)) {
  case restart_reason_t::ble_connection: return text("RESETTING BLE CONNECTION", "BLE接続をリセット");
  case restart_reason_t::firmware_update: return text("UPDATE COMPLETE", "更新が完了しました");
  case restart_reason_t::system: return text("RESTARTING DEVICE", "本体を再起動します");
  default: return text("APPLYING INPUT", "入力を切り替えます");
  }
}
const char* restartNoticeTarget() {
  switch (restart_reason.load(std::memory_order_relaxed)) {
  case restart_reason_t::ble_connection:
    return text("The saved device will be paired again", "保存機器を再接続します");
  case restart_reason_t::firmware_update:
    return text("The new firmware is ready", "新しいファームを起動します");
  case restart_reason_t::system:
    return text("Your settings have been saved", "設定は保存されています");
  default:
    break;
  }
  return sourceText(static_cast<def::command::external_input_source_t>(
      restart_target.load(std::memory_order_relaxed)));
}
const char* restartNoticeDetail() {
  return getRestartNotice() == restart_notice_t::saving
      ? text("Saving settings...", "設定を保存しています...")
      : text("Settings saved", "設定を保存しました");
}
void setWiFiStatus(wifi_status_t status) { wifi_status.store(status); }
wifi_status_t getWiFiStatus() { return wifi_status.load(); }
const char* wifiStatusText() {
  switch (getWiFiStatus()) {
  case wifi_status_t::stopping_ble: return text("Stopping BLE...", "BLE停止中...");
  case wifi_status_t::settling: return text("Preparing Wi-Fi...", "Wi-Fi準備中...");
  case wifi_status_t::failed: return text("BLE stop timed out. Retry", "BLE停止失敗。再試行して下さい");
  default: return "";
  }
}
void service() {
  if (getRestartNotice() == restart_notice_t::restarting
      && int32_t(M5.millis() - restart_not_before_msec) >= 0) {
    system_registry->runtime_info.setPowerOff(def::command::system_control_t::sc_reset);
  }
  std::lock_guard<std::mutex> lock(view_mutex);
  if (task_midi_t::isBLESuspendedForWiFi()
      && (view.phase == phase_t::scanning || view.phase == phase_t::connecting || view.phase == phase_t::confirm || view.phase == phase_t::list)) {
    view.phase = phase_t::blocked;
  }
  if (view.phase == phase_t::scanning) {
    auto state = midi.getBLEMidiScanState();
    if (state == task_midi_t::ble_scan_state_t::ready) {
      view.scanReady(midi.getBLEMidiScanDevices(devices, 12));
    } else if (state == task_midi_t::ble_scan_state_t::failed) {
      failure = "BLE scan failed / Retry"; view.phase = phase_t::failed;
    }
  }
  bool central = false;
  midi.getBLEMidiConnectionDiagnostic(&central, nullptr, nullptr);
  auto previous = view.phase;
  view.service(M5.millis(), central);
  if (view.phase == phase_t::connected && !central) {
    view.phase = phase_t::failed;
    failure = "Disconnected / Retry";
  } else if (view.phase == phase_t::failed && previous != phase_t::failed) {
    failure = previous == phase_t::connecting ? "Connection failed / Retry" : "Scan timed out / Retry";
    midi.cancelBLEMidiScan();
  }
}
void beginScan() { std::lock_guard<std::mutex> lock(view_mutex); startScan(); }
bool back() {
  std::lock_guard<std::mutex> lock(view_mutex);
  bool leave = view.back();
  if (leave) { midi.cancelBLEMidiScan(); }
  return leave;
}
size_t select(size_t index) {
  std::lock_guard<std::mutex> lock(view_mutex);
  if (view.choose(index)) { return 1; } // Cancel is the default, never Allow.
  if (view.phase == phase_t::confirm) {
    if (index == 1) { view.back(); return view.selected; }
    if (index == 2 && available()) {
      auto& device = devices[view.selected];
      if (!savePeer(device.address, device.name)) { view.phase = phase_t::save_failed; return 0; }
      midi.setBLEMidiPreferredDevice(device.address, device.name, true, device.address_type);
      view.connect(M5.millis());
      return 0;
    }
  } else if (view.phase == phase_t::failed || view.phase == phase_t::save_failed) { startScan(); return 0; }
  return index;
}
size_t rowCount() {
  std::lock_guard<std::mutex> lock(view_mutex);
  return view.phase == phase_t::list ? view.count : view.phase == phase_t::confirm ? 3 : 1;
}
std::string rowText(size_t index) {
  std::lock_guard<std::mutex> lock(view_mutex);
  if (view.phase == phase_t::list) {
    if (index >= view.count) { return ""; }
    const auto& device = devices[index];
    char label[80];
    // Address tail disambiguates identically named controllers.
    snprintf(label, sizeof(label), "%.18s [%s] %d", device.name[0] ? device.name : "BLE device", device.address + 12, (int)device.rssi);
    return label;
  }
  if (view.phase == phase_t::confirm) {
    if (index == 1) { return text("Cancel", "キャンセル"); }
    if (index == 2) { return text("Allow Connection", "接続を許可"); }
    const auto& device = devices[view.selected];
    return device.name[0] ? device.name : device.address;
  }
  switch (view.phase) {
  case phase_t::scanning: return text("Scanning BLE... / Back: Cancel", "BLE検索中... 戻る:中止");
  case phase_t::connecting: return text("Connecting...", "接続中...");
  case phase_t::connected: return text("BLE connected", "BLE接続完了");
  case phase_t::failed:
    if (!strcmp(failure, "Connection failed / Retry")) { return text(failure, "接続失敗 / 再試行"); }
    if (!strcmp(failure, "BLE scan failed / Retry")) { return text(failure, "BLE検索失敗 / 再試行"); }
    if (!strcmp(failure, "Scan timed out / Retry")) { return text(failure, "検索時間切れ / 再試行"); }
    if (!strcmp(failure, "Disconnected / Retry")) { return text(failure, "切断されました / 再試行"); }
    return text(failure, "機器なし / 再試行");
  case phase_t::save_failed: return text("Save failed / Retry", "保存失敗 / 再試行");
  case phase_t::blocked: return task_midi_t::isBLESuspendedForWiFi()
      ? text("Restart to use BLE", "BLEを使うには再起動して下さい")
      : text("Select BLE MIDI first", "入力ソースをBLE MIDIにして下さい");
  case phase_t::forgotten: return text("Device forgotten", "接続先を解除しました");
  default: return "";
  }
}
std::string statusText() {
  if (task_midi_t::isBLESuspendedForWiFi()) { return text("Off (Wi-Fi)", "停止中(Wi-Fi)"); }
  if (crash_blocked.load()) { return text("Retry Scan & Connect", "再検索して下さい"); }
  bool central = false, peripheral = false;
  midi.getBLEMidiConnectionDiagnostic(&central, &peripheral, nullptr);
  if (central || peripheral) { return text("Connected", "接続済み"); }
  if (system_registry->runtime_info.getMidiPortStateBLE() == def::command::midiport_info_t::mp_connecting) {
    return text("Connecting...", "接続中...");
  }
  return system_registry->runtime_info.getMidiPortStateBLE() == def::command::midiport_info_t::mp_off
    ? text("Off", "オフ") : text("Not connected", "未接続");
}
std::string deviceInfo(size_t index) {
  char buffer[96], address[18]{}, name[24]{};
  midi.getBLEMidiPreferredDevice(address, sizeof(address), name, sizeof(name));
  switch (index) {
  case 0: return std::string("BLE: ") + statusText();
  case 1: return std::string("Saved: ") + (name[0] ? name : "--");
  case 2: return address[0] ? address : "--";
  case 3: snprintf(buffer, sizeof(buffer), "MIDI packets: %lu", (unsigned long)midi.getBLEMidiPacketCount()); return buffer;
  case 4: return host_disabled_on_boot ? "USB: Off (normal boot)"
      : midi.isUSBStackReady() ? "USB: Ready" : midi.isUSBStarted() ? "USB: Starting" : "USB: Off";
  case 5: {
    uint16_t vendor = 0, product = 0; bool seen = false, is_midi = false;
    midi.getUSBHostDiagnostic(&vendor, &product, nullptr, nullptr, nullptr, &seen, &is_midi, nullptr, nullptr, nullptr);
    snprintf(buffer, sizeof(buffer), "USB %04X:%04X %s", vendor, product, seen ? is_midi ? "MIDI" : "Detected" : "No device"); return buffer;
  }
  default: return "";
  }
}
bool forgetDevice() {
  std::lock_guard<std::mutex> lock(view_mutex);
  if (!savePeer("", "")) { view.phase = phase_t::save_failed; return false; }
  midi.forgetBLEMidiPreferredDevice();
  view.phase = phase_t::forgotten;
  return true;
}
bool changeSource(def::command::external_input_source_t source) {
  if (restartNoticeActive()) { return false; }
  restart_target.store(uint8_t(source), std::memory_order_relaxed);
  restart_reason.store(restart_reason_t::input_source, std::memory_order_relaxed);
  restart_notice.store(restart_notice_t::saving, std::memory_order_release);
  auto& ports = system_registry->midi_port_setting;
  auto old = ports.getExternalInputSource();
  ports.setExternalInputSource(source); // Stage only; no live transport change.
  if (!system_registry->save()) {
    ports.setExternalInputSource(old);
    restart_notice.store(restart_notice_t::idle, std::memory_order_release);
    return false;
  }
  input_restart_marker = input_restart_magic ^ uint32_t(source);
  scheduleRestart(source, restart_reason_t::input_source);
  return true;
}
bool restartConnection() {
  if (restartNoticeActive()) { return false; }
  restart_reason.store(restart_reason_t::ble_connection, std::memory_order_relaxed);
  restart_notice.store(restart_notice_t::saving, std::memory_order_release);
  // A clean restart also works after Wi-Fi, without rebuilding two radio
  // controllers in a fragmented heap. Keep the selected peer, clear its bond.
  char address[18]{}, name[24]{};
  midi.getBLEMidiPreferredDevice(address, sizeof(address), name, sizeof(name));
  if (!system_registry->save() || !savePeer(address, name, true)) {
    restart_notice.store(restart_notice_t::idle, std::memory_order_release);
    std::lock_guard<std::mutex> lock(view_mutex);
    view.phase = phase_t::save_failed;
    return false;
  }
  scheduleRestart(system_registry->midi_port_setting.getExternalInputSource(), restart_reason_t::ble_connection);
  return true;
}
}}
