// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#if 1

#include "midi_transport_ble.hpp"

#if __has_include(<esp_bt.h>)

#include "../system_registry.hpp"

#include <M5Unified.h>

#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <deque>
#include <vector>

#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp32-hal-bt.h>
#include <mutex>

#define MIDI_SERVICE_UUID         "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHARACTERISTIC_UUID  "7772e5db-3868-4112-a1a9-f2669d106bf3"

namespace midi_driver {

static std::mutex mutex_rx;
static std::mutex mutex_central_selection;

//----------------------------------------------------------------

static MIDI_Transport_BLE* _instance = nullptr;

// peripheral BLE client
static BLEClient* _pClient = nullptr;
static volatile uint32_t _client_release_not_before_msec = 0;
// Bluedroid's encrypted GATT discovery needs a contiguous internal-RAM block.
// Keep this block occupied while the rest of the app starts and while scanning
// and pairing run, then release it immediately before service discovery. Free
// heap alone is not sufficient here: AMY/UI allocations can leave plenty of
// bytes split into blocks too small for the GATT database request.
static void* _gatt_connect_reserve = nullptr;
static void* _gatt_discovery_reserve = nullptr;
static constexpr size_t gatt_phase_reserve_bytes = 4 * 1024;

static BLEServer *pServer = nullptr;
static BLEService *pService = nullptr;
static BLEAdvertising *pAdvertising = nullptr;
static BLECharacteristic *pCharacteristic = nullptr;
static int _conn_id = -1;
// static std::deque<std::vector<uint8_t> > _rx_queue;
static std::vector<uint8_t> _rx_data;
// BLE-MIDI notifications are normally only a few bytes and are drained by a
// dedicated subtask. A 1 KB std::vector reserve needlessly consumed scarce
// internal RAM during M-VAVE's encryption handshake.
static constexpr size_t rx_data_reserve = 384;
static uint8_t _rx_running_status = 0;
static volatile uint32_t _rx_packet_count = 0;
static volatile uint8_t _last_rx_length = 0;
static uint8_t _last_rx_data[6] = {};
static char _central_device_name[24] = {};
static char _central_device_address[18] = {};
static char _peripheral_device_address[18] = {};
static uint8_t _central_midi_properties = 0;
static bool _central_is_m_vave = false;
static uint8_t _subscription_attempts = 0;
static uint32_t _subscription_next_retry_msec = 0;
static uint32_t _subscription_rx_packet_base = 0;
static bool _central_wake_pending = false;
static volatile uint8_t _m_vave_auth_state = 0;
static volatile bool _m_vave_pairing_pending = false;
static bool _m_vave_resuming_bond = false;
static uint8_t _m_vave_cccd_value = 0xFF;
static volatile uint8_t _local_notify_registration_status = 0xFF;
static MIDI_Transport_BLE::scan_device_t _central_scan_devices[
  MIDI_Transport_BLE::max_scan_devices];
static size_t _central_scan_device_count = 0;
static MIDI_Transport_BLE::scan_state_t _central_scan_state =
  MIDI_Transport_BLE::scan_state_t::idle;
static bool _central_scan_requested = false;
static bool _central_selection_active = false;
static bool _force_fresh_pairing_once = false;
static int8_t _preferred_address_type_once = -1;
static bool _central_disconnect_requested = false;
static uint8_t _central_reconnect_failures = 0;
static uint8_t _previous_connect_crash_stage = 0;
static uint16_t _previous_connect_free_internal_kb = 0;
static uint16_t _previous_connect_largest_internal_kb = 0;
static uint16_t _previous_connect_midi_stack_kb = 0;
static uint16_t _previous_connect_callback_stack_kb = 0;
static bool _auto_reconnect_suppressed = false;
static uint32_t _connect_guard_clear_msec = 0;
static volatile bool _gatt_connect_in_progress = false;
#if !defined(M5UNIFIED_PC_BUILD)
static constexpr uint32_t connect_guard_magic_value = 0x424C4547u;
RTC_NOINIT_ATTR static uint32_t connect_guard_magic;
RTC_NOINIT_ATTR static uint32_t connect_guard_stage;
RTC_NOINIT_ATTR static uint32_t connect_guard_check;
RTC_NOINIT_ATTR static uint32_t connect_guard_free_internal;
RTC_NOINIT_ATTR static uint32_t connect_guard_largest_internal;
RTC_NOINIT_ATTR static uint32_t connect_guard_midi_stack;
RTC_NOINIT_ATTR static uint32_t connect_guard_callback_stack;
#endif
static char _preferred_central_address[18] = {};
static char _preferred_central_name[24] = {};

static void ensure_gatt_discovery_reserve(void)
{
#if !defined(M5UNIFIED_PC_BUILD)
  if (_gatt_connect_reserve == nullptr) {
    _gatt_connect_reserve = heap_caps_malloc(
      gatt_phase_reserve_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (_gatt_discovery_reserve == nullptr) {
    _gatt_discovery_reserve = heap_caps_malloc(
      gatt_phase_reserve_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#endif
}

static void release_gatt_connect_reserve(void)
{
#if !defined(M5UNIFIED_PC_BUILD)
  if (_gatt_connect_reserve != nullptr) {
    heap_caps_free(_gatt_connect_reserve);
    _gatt_connect_reserve = nullptr;
  }
#endif
}

static void release_gatt_discovery_reserve(void)
{
#if !defined(M5UNIFIED_PC_BUILD)
  if (_gatt_discovery_reserve != nullptr) {
    heap_caps_free(_gatt_discovery_reserve);
    _gatt_discovery_reserve = nullptr;
  }
  printf("BLE_GATT_ARENA free=%u largest=%u\n",
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#endif
}

static void release_all_gatt_reserves(void)
{
  release_gatt_connect_reserve();
  release_gatt_discovery_reserve();
}

static void mark_connect_stage(uint8_t stage)
{
#if !defined(M5UNIFIED_PC_BUILD)
  connect_guard_stage = stage;
  connect_guard_free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  connect_guard_largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t stack_bytes = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  if (stage == 5) { connect_guard_midi_stack = stack_bytes; }
  if (stage >= 11 && stage <= 13) { connect_guard_callback_stack = stack_bytes; }
  connect_guard_check = connect_guard_magic_value ^ stage ^ 0x6D3A91C5u;
  connect_guard_magic = connect_guard_magic_value;
#else
  (void)stage;
#endif
}

static void mark_connect_callback_stage(uint8_t stage)
{
#if !defined(M5UNIFIED_PC_BUILD)
  // Bluedroid invokes this while BLEClient::connect() is waiting on its own
  // registration/open transaction. Walking every heap region from inside
  // that callback can contend with the allocator used by the same event.
  // Keep callback breadcrumbs allocation-free; F/L/M remain the values
  // captured immediately before connect(), while B records callback stack.
  connect_guard_stage = stage;
  connect_guard_callback_stack =
    uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
  connect_guard_check = connect_guard_magic_value ^ stage ^ 0x6D3A91C5u;
  connect_guard_magic = connect_guard_magic_value;
#else
  (void)stage;
#endif
}

static void clear_connect_stage(void)
{
#if !defined(M5UNIFIED_PC_BUILD)
  connect_guard_magic = 0;
  connect_guard_stage = 0;
  connect_guard_check = 0;
#endif
  _connect_guard_clear_msec = 0;
}

#if defined(CONFIG_BLUEDROID_ENABLED)
static bool pairing_is_authorized(void)
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  // Outbound connections are authorized by the device-selection confirmation.
  // Keep the existing inbound BLE MIDI path available for phones that connect
  // to KANTAN Play as the central; this callback does not expose peer direction.
  return !_central_selection_active;
}

class SamplerSecurityCallbacks final : public BLESecurityCallbacks {
public:
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return pairing_is_authorized(); }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    printf("BLE_AUTH success=%u reason=%u type=%u\n",
           result.success ? 1u : 0u, (unsigned)result.fail_reason,
           (unsigned)result.addr_type);
    // A resolvable private address may be replaced by the identity address in
    // AUTH_CMPL.  Only one outbound M-VAVE pairing can be pending, so matching
    // the transient advertising address here would discard a valid result.
    if (_m_vave_pairing_pending) {
      _m_vave_auth_state = result.success ? 2 : 3;
      _m_vave_pairing_pending = false;
    }
  }
  bool onConfirmPIN(uint32_t) override { return pairing_is_authorized(); }
};

static SamplerSecurityCallbacks sampler_security_callbacks;
static BLESecurity sampler_security;

static void configure_ble_bonding(void)
{
  // CoreS3 has no numeric-entry UI.  Just Works bonding is appropriate for a
  // local MIDI controller and lets controllers retain KANTAN as an authorized peer.
  sampler_security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  sampler_security.setCapability(ESP_IO_CAP_NONE);
  sampler_security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sampler_security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEDevice::setSecurityCallbacks(&sampler_security_callbacks);
}

static bool ble_address_is_bonded(BLEAddress address)
{
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) { return false; }
  std::vector<esp_ble_bond_dev_t> devices((size_t)count);
  if (esp_ble_get_bond_device_list(&count, devices.data()) != ESP_OK) { return false; }
  for (int i = 0; i < count; ++i) {
    if (memcmp(devices[(size_t)i].bd_addr, address.getNative(), sizeof(esp_bd_addr_t)) == 0) {
      return true;
    }
  }
  return false;
}

static void prepare_m_vave_pairing(BLEAddress address)
{
  _m_vave_resuming_bond = ble_address_is_bonded(address);
  _m_vave_auth_state = 1;
  _m_vave_pairing_pending = true;
  // Keep BLEDevice's global encryption level disabled here. Arduino BLE calls
  // esp_ble_set_encryption() from both its device and client CONNECT handlers;
  // that duplicate request can reset the S3 before the OPEN event while AMY
  // is active. The caller starts SMP exactly once after connect() returns.
  BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)0);
}

static bool wait_for_m_vave_pairing(BLEClient* client)
{
  // A new bond reports AUTH_CMPL.  Reconnection with a saved LTK can complete
  // encryption without another pairing-complete event, so only wait briefly
  // for an explicit failure in that case.
  const uint32_t deadline = M5.millis() + (_m_vave_resuming_bond ? 1500 : 10000);
  while (_m_vave_pairing_pending && client != nullptr && client->isConnected()
      && (int32_t)(M5.millis() - deadline) < 0) {
    M5.delay(10);
  }
  const bool authenticated = _m_vave_auth_state == 2
                          || (_m_vave_resuming_bond && _m_vave_auth_state == 1
                              && client != nullptr && client->isConnected());
  if (authenticated) { _m_vave_auth_state = 2; }
  _m_vave_pairing_pending = false;
  _m_vave_resuming_bond = false;
  return authenticated;
}
#endif
static uint8_t _central_subscription = 0;

static void note_received_packet(const uint8_t* data, size_t length)
{
  ++_rx_packet_count;
  // A delivered MIDI packet is the first reliable proof that the complete
  // GATT/CCCD path is healthy. Short-lived links must not reset reconnect
  // backoff, otherwise a controller that is still pairing can exhaust the
  // Bluedroid internal heap by reconnecting continuously.
  _central_reconnect_failures = 0;
  const size_t copied = std::min<size_t>(length, sizeof(_last_rx_data));
  for (size_t i = 0; i < copied; ++i) { _last_rx_data[i] = data[i]; }
  _last_rx_length = copied;
}

static bool is_m_vave_name(const String& name)
{
  String upper_name = name;
  upper_name.toUpperCase();
  return upper_name.indexOf("M-VAVE") >= 0
      || upper_name.indexOf("SMC-PAD") >= 0
      || upper_name.indexOf("SMC PAD") >= 0;
}

// InstaChordと直結時のCharacteristic
static BLERemoteCharacteristic* remotecharacteristic = nullptr;
static uint16_t _mtu_size = 23;
#if defined(CONFIG_BLUEDROID_ENABLED)
static volatile bool _native_service_search_active = false;
static volatile bool _native_service_search_complete = false;
static volatile bool _native_service_found = false;
static volatile esp_gatt_status_t _native_service_search_status = ESP_GATT_ERROR;
static uint16_t _native_service_start_handle = 0;
static uint16_t _native_service_end_handle = 0;
static uint16_t _native_midi_char_handle = 0;
static uint16_t _native_cccd_handle = 0;
static esp_gatt_if_t _native_gatt_if = ESP_GATT_IF_NONE;
static uint16_t _native_conn_id = 0;
static esp_bd_addr_t _native_peer_address = {};
static bool _native_peer_address_valid = false;
static volatile bool _native_midi_read_complete = false;
static volatile esp_gatt_status_t _native_midi_read_status = ESP_GATT_ERROR;
static volatile uint16_t _native_midi_read_length = 0;
static volatile bool _native_cccd_read_complete = false;
static volatile esp_gatt_status_t _native_cccd_read_status = ESP_GATT_ERROR;
static volatile uint16_t _native_cccd_read_value = 0xFFFF;
static volatile bool _native_cccd_write_complete = false;
static volatile esp_gatt_status_t _native_cccd_write_status = ESP_GATT_ERROR;
#endif

// static constexpr const size_t _tx_queue_size = 4;
// static int _tx_queue_index = 0;
// static std::vector<uint8_t> _tx_queue[_tx_queue_size];

#if defined (CONFIG_BLUEDROID_ENABLED)
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    _conn_id = pServer->getConnId();
    snprintf(_peripheral_device_address, sizeof(_peripheral_device_address), "%s",
             BLEAddress(param->connect.remote_bda).toString().c_str());
    _instance->setPeripheralConnected(true);
// printf("BLE MIDI Connected.\n");
// pServer->updatePeerMTU(_conn_id, _mtu_size);
    // M5.Lcd.printf("BLE MIDI Connected. MTU:%d\n", _mtu_size);
  };
  void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
    _conn_id = -1;
    _peripheral_device_address[0] = 0;
    _instance->setPeripheralConnected(false);
// printf("BLE MIDI Disconnect.\n");
  }
  void onMtuChanged(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override {
// printf("BLE onMtuChanged : %d\n", param->mtu.mtu);
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic, esp_ble_gatts_cb_param_t *param) override {
    note_received_packet(pCharacteristic->getData(), pCharacteristic->getLength());
    if (_instance->decodeReceive(pCharacteristic->getData(), pCharacteristic->getLength())) {
      // GATT callback is invoked from the Bluetooth host task, not an ISR.
      _instance->execTaskNotify();
    }
    // printf("onWrite called.\n");
    // fflush(stdout);
  }
  // void onRead(BLECharacteristic *pCharacteristic) override {
  //   printf("onRead called.\n");
  // }
  // void onNotify(BLECharacteristic *pCharacteristic) override {
  //   printf("onNotify called.\n");
  // }
  // void onStatus(BLECharacteristic *pCharacteristic, Status s, uint32_t code) override {
  //   if (s == SUCCESS_NOTIFY || s == SUCCESS_INDICATE) {
  //     // ESP_LOGV("BLE", "onStatus: success");
  //     printf("onStatus: success\n");
  //   } else {
  //     // ESP_LOGE("BLE", "onStatus: error %d, code %d", s, code);
  //     printf("onStatus: error %d, code %d", s, code);
  //   }
  //   fflush(stdout);
  // }
};
#endif

#if defined (CONFIG_NIMBLE_ENABLED)
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
    _conn_id = pServer->getConnId();
    _instance->setPeripheralConnected(true);
  };
  void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
    _conn_id = -1;
    _instance->setPeripheralConnected(false);
  }
  void onMtuChanged(BLEServer *pServer, ble_gap_conn_desc *desc, uint16_t mtu) override {
  }
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override {
    note_received_packet(pCharacteristic->getData(), pCharacteristic->getLength());
    if (_instance->decodeReceive(pCharacteristic->getData(), pCharacteristic->getLength())) {
      // GATT callback is invoked from the Bluetooth host task, not an ISR.
      _instance->execTaskNotify();
    }
  }
};
#endif

static MyServerCallbacks myServerCallbacks;

bool MIDI_Transport_BLE::decodeReceive(const uint8_t* data, size_t length)
{
  if (length < 2) { return false; }
  // BLE-MIDIは先頭のtimestamp highに続き、各メッセージのtimestamp lowと
  // MIDIデータが並ぶ。Running Statusを含む標準パケットに加え、timestampを
  // 省略する機器も受け入れる。後者は一般的なBLE MIDIホストが寛容に扱うため、
  // コントローラ互換性のためにここでもフォールバックする。
  // Decode directly into the reserved shared buffer. Allocating a temporary
  // vector for every BLE packet fragments scarce internal RAM when AMY and UI
  // caches are active together.
  std::lock_guard<std::mutex> lock(mutex_rx);
  const size_t size_before = _rx_data.size();
  size_t i = 1; // 先頭はtimestamp high
  while (i < length) {
    // BLE-MIDI timestamp-low is any byte with bit 7 set (80-FF), not only
    // 80-BF.  A timestamp is unambiguous when followed by a status byte, or
    // by data while a running status is active.  This also keeps compatibility
    // with controllers that omit timestamp-low before an explicit status.
    const bool high_bit = (data[i] & 0x80) != 0;
    const bool next_is_status = i + 1 < length && (data[i + 1] & 0x80) != 0;
    const bool timestamp_before_running = high_bit && _rx_running_status >= 0x80
      && i + 1 < length && !next_is_status;
    if (high_bit && (next_is_status || timestamp_before_running)) { ++i; }
    if (i >= length) { break; }
    uint8_t status = _rx_running_status;
    if (data[i] & 0x80) {
      status = data[i++];
      if (status < 0xF0) { _rx_running_status = status; }
    }
    if (status < 0x80) { break; }

    uint8_t data_count = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : 2;
    // リアルタイムメッセージは演奏入力へ渡さず、Running Statusも維持する。
    if (status >= 0xF8) { continue; }
    if (status >= 0xF0 || i + data_count > length) { break; }
    if ((data[i] & 0x80) || (data_count == 2 && (data[i + 1] & 0x80))) { break; }
    // The Sampler consumes external Note and CC only. Controllers can emit
    // aftertouch, pitch bend and program traffic continuously; forwarding it
    // through two queues delays the Note edge without affecting the result.
#if defined(KANPLAY_SAMPLER)
    const uint8_t message_type = status & 0xF0;
    if (message_type == 0x80 || message_type == 0x90 || message_type == 0xB0) {
      _rx_data.push_back(status);
      _rx_data.insert(_rx_data.end(), data + i, data + i + data_count);
    }
#else
    _rx_data.push_back(status);
    _rx_data.insert(_rx_data.end(), data + i, data + i + data_count);
#endif
    i += data_count;
  }
  return _rx_data.size() != size_before;
}

static void publish_scan_results(std::vector<BLEAdvertisedDevice>& devices)
{
  BLEUUID service_uuid(MIDI_SERVICE_UUID);
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  _central_scan_device_count = 0;
  for (auto& device : devices) {
    const String address = device.getAddress().toString().c_str();
    bool duplicate = false;
    for (size_t i = 0; i < _central_scan_device_count; ++i) {
      if (!strcmp(_central_scan_devices[i].address, address.c_str())) {
        duplicate = true;
        break;
      }
    }
    if (duplicate || _central_scan_device_count >= MIDI_Transport_BLE::max_scan_devices) { continue; }
    auto& out = _central_scan_devices[_central_scan_device_count++];
    const String name = device.getName();
    snprintf(out.name, sizeof(out.name), "%s",
             name.length() ? name.c_str() : address.c_str());
    snprintf(out.address, sizeof(out.address), "%s", address.c_str());
    out.rssi = (int8_t)std::max(-127, std::min(20, device.getRSSI()));
    out.address_type = (uint8_t)device.getAddressType();
    out.advertises_midi = device.haveServiceUUID()
                       && device.isAdvertisingService(service_uuid);
  }
  _central_scan_state = MIDI_Transport_BLE::scan_state_t::ready;
}

static std::vector<BLEAdvertisedDevice> ble_scan(void)
{
  std::vector<BLEAdvertisedDevice> foundMidiDevices;
  BLEScan* pBLEScan = BLEDevice::getScan();
  if (pBLEScan == nullptr) {
    std::lock_guard<std::mutex> lock(mutex_central_selection);
    _central_scan_state = MIDI_Transport_BLE::scan_state_t::failed;
    return foundMidiDevices;
  }

  BLEUUID serviceUUID(MIDI_SERVICE_UUID);

  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  {
    std::lock_guard<std::mutex> lock(mutex_central_selection);
    _central_scan_state = MIDI_Transport_BLE::scan_state_t::scanning;
  }
  auto foundDevices = pBLEScan->start(1);
  ESP_LOGV("BLE", "Found %d BLE device(s)", foundDevices->getCount());
  for (int i=0; i < foundDevices->getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices->getDevice(i);
    auto deviceStr = "name = \"" + device.getName() + "\", address = "  + device.getAddress().toString();
    String name = device.getName();
    // M-VAVE SMC-PAD variants omit the MIDI UUID from advertising on some
    // firmware revisions.  Prefer their advertised name before probing other
    // generic BLE devices in the room.
    const bool m_vave = is_m_vave_name(name);
    if (m_vave) {
      foundMidiDevices.insert(foundMidiDevices.begin(), device);
      continue;
    }
    if (device.haveServiceUUID() && device.isAdvertisingService(serviceUUID)) {
      ESP_LOGV("BLE", " - BLE MIDI device : %s", deviceStr.c_str());
      // Prefer peripherals that explicitly advertise the MIDI service.
      foundMidiDevices.insert(foundMidiDevices.begin(), device);
    }
    else {
      ESP_LOGV("BLE", " - Other type of BLE device : %s", deviceStr.c_str());
      // Several BLE MIDI controllers (including some M-VAVE firmware) omit
      // the MIDI service UUID from advertising data.  They are still valid
      // candidates; the GATT service check after connection is authoritative.
      if (device.getName().length()) {
        foundMidiDevices.push_back(device);
      }
    }
  }
  ESP_LOGV("BLE", "Total of BLE MIDI devices : %d", foundMidiDevices.size());
  publish_scan_results(foundMidiDevices);
  pBLEScan->clearResults();
  return foundMidiDevices;
;
/*
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  pBLEScan->clearResults();
  foundMidiDevices.clear();
  BLEScanResults foundDevices = pBLEScan->start(3);
  pBLEScan->clearResults();
  pBLEScan->setAdvertisedDeviceCallbacks(&adv_cb);
  pBLEScan->start(60, true);
*/
}



MIDI_Transport_BLE::~MIDI_Transport_BLE()
{
  end();
}

bool MIDI_Transport_BLE::begin(void)
{
  _is_begin = false;
#if !defined(M5UNIFIED_PC_BUILD)
  if (connect_guard_magic == connect_guard_magic_value
   && connect_guard_stage > 0 && connect_guard_stage <= 15
   && connect_guard_check
        == (connect_guard_magic_value ^ connect_guard_stage ^ 0x6D3A91C5u)) {
    _previous_connect_crash_stage = (uint8_t)connect_guard_stage;
    _previous_connect_free_internal_kb = (uint16_t)std::min<uint32_t>(65535, connect_guard_free_internal / 1024);
    _previous_connect_largest_internal_kb = (uint16_t)std::min<uint32_t>(65535, connect_guard_largest_internal / 1024);
    _previous_connect_midi_stack_kb = (uint16_t)std::min<uint32_t>(65535, connect_guard_midi_stack / 1024);
    _previous_connect_callback_stack_kb = (uint16_t)std::min<uint32_t>(65535, connect_guard_callback_stack / 1024);
    _auto_reconnect_suppressed = true;
  }
#endif
  clear_connect_stage();
  return true;
}

uint8_t MIDI_Transport_BLE::consumePreviousConnectCrashStage(uint16_t* free_internal_kb,
                                                             uint16_t* largest_internal_kb,
                                                             uint16_t* midi_stack_kb,
                                                             uint16_t* callback_stack_kb)
{
  const uint8_t stage = _previous_connect_crash_stage;
  if (free_internal_kb) { *free_internal_kb = _previous_connect_free_internal_kb; }
  if (largest_internal_kb) { *largest_internal_kb = _previous_connect_largest_internal_kb; }
  if (midi_stack_kb) { *midi_stack_kb = _previous_connect_midi_stack_kb; }
  if (callback_stack_kb) { *callback_stack_kb = _previous_connect_callback_stack_kb; }
  _previous_connect_crash_stage = 0;
  return stage;
}

void MIDI_Transport_BLE::end(void)
{
  if (_is_begin) {
    _is_begin = false;
    if (_conn_id >= 0) {
      pServer->disconnect(_conn_id);
      _conn_id = -1;
    }
  }
}

void MIDI_Transport_BLE::addMessage(const uint8_t* data, size_t length)
{
  uint32_t msec = M5.millis();
  int len = length + (_tx_runningStatus != data[0] ? 2 : 0);
  if (_tx_data.size() + len >= _mtu_size - 1) {
    // If the tx_data size exceeds the buffer size, send it immediately
    sendFlush();
  }
  if (_tx_data.empty()) {
    uint32_t msec_high = 0x3F & (msec >> 7);
    _tx_data.push_back(0x80 | msec_high);
    _tx_runningStatus = 0;
  }

  if (_tx_runningStatus != data[0])
  {
    uint32_t msec_low = (msec & 0x7F);
    _tx_data.push_back(0x80 | msec_low);
    _tx_data.push_back(data[0]); // status byte
    _tx_runningStatus = data[0];
  }
  _tx_data.insert(_tx_data.end(), data + 1, data + length);
  if (_tx_data.size() + 4 >= _mtu_size - 3) {
    // If the tx_data size exceeds the buffer size, send it immediately
    sendFlush();
  }
}

bool MIDI_Transport_BLE::sendFlush(void)
{
  bool result = false;
ESP_LOGV("BLE", "sendFlush called, tx_data size: %d", _tx_data.size());
// printf("sendFlush called, tx_data size: %d\n", _tx_data.size());
  auto remote = remotecharacteristic;
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (_native_midi_char_handle != 0 && _native_gatt_if != ESP_GATT_IF_NONE
   && _pClient != nullptr && _pClient->isConnected()) {
    result = true;
    if (!_tx_data.empty()) {
      esp_ble_gattc_write_char(_native_gatt_if, _native_conn_id,
                               _native_midi_char_handle, _tx_data.size(),
                               _tx_data.data(), ESP_GATT_WRITE_TYPE_NO_RSP,
                               ESP_GATT_AUTH_REQ_NONE);
    }
  } else
#endif
  if (remote)
  {
    result = true;
    if (!_tx_data.empty()) {
      remote->writeValue(_tx_data.data(), _tx_data.size(), false);
    }
  } else if (pCharacteristic && _conn_id >= 0) {
    result = true;
    if (!_tx_data.empty()) {
      pCharacteristic->setValue( _tx_data.data(), _tx_data.size());
      pCharacteristic->notify();
    }
  }
  _tx_data.clear();
  _tx_runningStatus = 0;
  return result;
}

std::vector<uint8_t> MIDI_Transport_BLE::read(void)
{
  std::lock_guard<std::mutex> lock(mutex_rx);
  auto rxValueVec = _rx_data;
  _rx_data.clear();
  return rxValueVec;
}

bool MIDI_Transport_BLE::readInto(MIDI_Decoder& decoder)
{
  std::lock_guard<std::mutex> lock(mutex_rx);
  if (_rx_data.empty()) { return false; }
  decoder.addData(_rx_data.data(), _rx_data.size());
  _rx_data.clear();
  return true;
}

uint32_t MIDI_Transport_BLE::getReceivedPacketCount(void) const
{
  return _rx_packet_count;
}

void MIDI_Transport_BLE::getConnectionDiagnostic(bool* central, bool* peripheral, uint8_t* subscription) const
{
  if (central != nullptr) { *central = _central_connected; }
  if (peripheral != nullptr) { *peripheral = _peripheral_connected; }
  if (subscription != nullptr) { *subscription = _central_subscription; }
}

void MIDI_Transport_BLE::getCentralDeviceName(char* name, size_t size) const
{
  if (name == nullptr || size == 0) { return; }
  snprintf(name, size, "%s", _central_device_name);
}

void MIDI_Transport_BLE::getPeerAddresses(char* central, size_t central_size, char* peripheral, size_t peripheral_size) const
{
  if (central != nullptr && central_size != 0) {
    snprintf(central, central_size, "%s", _central_device_address);
  }
  if (peripheral != nullptr && peripheral_size != 0) {
    snprintf(peripheral, peripheral_size, "%s", _peripheral_device_address);
  }
}

uint8_t MIDI_Transport_BLE::getCentralMIDIProperties(void) const
{
  return _central_midi_properties;
}

void MIDI_Transport_BLE::getSecurityDiagnostic(uint8_t* auth_state, uint8_t* cccd_value,
                                               uint8_t* registration_status) const
{
  if (auth_state != nullptr) { *auth_state = _m_vave_auth_state; }
  if (cccd_value != nullptr) { *cccd_value = _m_vave_cccd_value; }
  if (registration_status != nullptr) { *registration_status = _local_notify_registration_status; }
}

bool MIDI_Transport_BLE::clearCentralBond(void)
{
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (_central_device_address[0] == 0) { return false; }
  BLEAddress address{String(_central_device_address)};
  return esp_ble_remove_bond_device(*address.getNative()) == ESP_OK;
#else
  return false;
#endif
}

void MIDI_Transport_BLE::requestCentralScan(void)
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  _central_scan_device_count = 0;
  _central_scan_state = scan_state_t::requested;
  _central_scan_requested = true;
  _central_selection_active = true;
  _central_disconnect_requested = _central_connected || _peripheral_connected;
  _auto_reconnect_suppressed = false;
  _last_central_scan_msec = 0;
}

void MIDI_Transport_BLE::cancelCentralScan(void)
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  _central_scan_requested = false;
  _central_selection_active = false;
  _central_scan_state = scan_state_t::idle;
  _last_central_scan_msec = 0;
}

MIDI_Transport_BLE::scan_state_t MIDI_Transport_BLE::getCentralScanState(void) const
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  return _central_scan_state;
}

size_t MIDI_Transport_BLE::getCentralScanDevices(scan_device_t* devices, size_t capacity) const
{
  if (devices == nullptr || capacity == 0) { return 0; }
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  const size_t count = std::min(capacity, _central_scan_device_count);
  for (size_t i = 0; i < count; ++i) { devices[i] = _central_scan_devices[i]; }
  return count;
}

void MIDI_Transport_BLE::setPreferredCentralDevice(const char* address, const char* name,
                                                   bool force_fresh_pairing,
                                                   int8_t address_type)
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  snprintf(_preferred_central_address, sizeof(_preferred_central_address), "%s", address ? address : "");
  snprintf(_preferred_central_name, sizeof(_preferred_central_name), "%s", name ? name : "");
  _central_scan_requested = false;
  _central_selection_active = false;
  _force_fresh_pairing_once = force_fresh_pairing;
  _preferred_address_type_once = address_type;
  _central_scan_state = scan_state_t::idle;
  // Give the caller time to finish its current UI/menu transaction before a
  // one-second active scan and GATT discovery begin on the MIDI task.
  _last_central_scan_msec = M5.millis();
  _central_reconnect_failures = 0;
}

void MIDI_Transport_BLE::getPreferredCentralDevice(char* address, size_t address_size,
                                                   char* name, size_t name_size) const
{
  std::lock_guard<std::mutex> lock(mutex_central_selection);
  if (address != nullptr && address_size != 0) {
    snprintf(address, address_size, "%s", _preferred_central_address);
  }
  if (name != nullptr && name_size != 0) {
    snprintf(name, name_size, "%s", _preferred_central_name);
  }
}

bool MIDI_Transport_BLE::forgetPreferredCentralDevice(void)
{
  char address[18] = {};
  {
    std::lock_guard<std::mutex> lock(mutex_central_selection);
    snprintf(address, sizeof(address), "%s", _preferred_central_address);
    _preferred_central_address[0] = 0;
    _preferred_central_name[0] = 0;
    _central_scan_requested = false;
    _central_selection_active = false;
    _central_scan_state = scan_state_t::idle;
    _central_disconnect_requested = _central_connected || _peripheral_connected;
  }
#if defined(CONFIG_BLUEDROID_ENABLED)
  if (address[0]) {
    BLEAddress ble_address{String(address)};
    esp_ble_remove_bond_device(*ble_address.getNative());
  }
#endif
  return address[0] != 0;
}

void MIDI_Transport_BLE::getLastReceivedPacket(uint8_t* data, size_t* length) const
{
  const size_t copied = std::min<size_t>(_last_rx_length, sizeof(_last_rx_data));
  if (data != nullptr) {
    for (size_t i = 0; i < copied; ++i) { data[i] = _last_rx_data[i]; }
  }
  if (length != nullptr) { *length = copied; }
}
/*
size_t MIDI_Transport_BLE::read(uint8_t* data, size_t length)
{
  if (_use_rx == false) { return 0; }
  if (_conn_id < 0) { return 0; }
  size_t result = 0;

  while (!_rx_queue.empty())
  {
    std::vector<uint8_t> rxValueVec = _rx_queue.front();
    _rx_queue.pop_front();
    size_t copy_length = std::min(length, rxValueVec.size());
    std::copy(rxValueVec.begin(), rxValueVec.begin() + copy_length, data);
    result += copy_length;
    length -= copy_length;
    data += copy_length;
    if (length == 0) { break; }
  }
  return result;
}
//*/

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  // printf("Notify callback for characteristic %d ", isNotify ? 1 : 0);
  // printf(pBLERemoteCharacteristic->getUUID().toString().c_str());
  // printf(" of data length : %d  data:", length);
  // for (int i = 0; i < length; i++) {
  //   printf("%02x ", pData[i]);
  // }
  // printf("\n");
  // fflush(stdout);
#if !defined(CONFIG_BLUEDROID_ENABLED)
  if (_instance != nullptr && length) {
    note_received_packet(pData, length);
    if (_instance->decodeReceive(pData, length)) {
      // Notifications are delivered on the Bluetooth host task as well.
      _instance->execTaskNotify();
    }
  }
#else
  (void)pBLERemoteCharacteristic;
  (void)pData;
  (void)length;
  (void)isNotify;
#endif
}

#if defined(CONFIG_BLUEDROID_ENABLED)
static void sampler_gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gatt_if,
                                        esp_ble_gattc_cb_param_t* param)
{
  // BLEClient::connect() blocks while Bluedroid reports these events.  Leave a
  // finer RTC breadcrumb so a reset inside that call can be distinguished
  // from an allocation failure before registration.
  if (_gatt_connect_in_progress) {
    if (event == ESP_GATTC_REG_EVT) { mark_connect_callback_stage(11); }
    else if (event == ESP_GATTC_OPEN_EVT) { mark_connect_callback_stage(12); }
    else if (event == ESP_GATTC_CONNECT_EVT) {
      mark_connect_callback_stage(13);
      memcpy(_native_peer_address, param->connect.remote_bda, ESP_BD_ADDR_LEN);
      _native_peer_address_valid = true;
      printf("BLE_LINK addr=%s type=%u interval=%u latency=%u timeout=%u\n",
             BLEAddress(_native_peer_address).toString().c_str(),
             (unsigned)param->connect.ble_addr_type,
             (unsigned)param->connect.conn_params.interval,
             (unsigned)param->connect.conn_params.latency,
             (unsigned)param->connect.conn_params.timeout);
    }
  }
  if (_native_service_search_active && gatt_if == _native_gatt_if) {
    if (event == ESP_GATTC_SEARCH_RES_EVT) {
      BLEUUID uuid(param->search_res.srvc_id.uuid);
      if (uuid.equals(BLEUUID(MIDI_SERVICE_UUID))) {
        _native_service_start_handle = param->search_res.start_handle;
        _native_service_end_handle = param->search_res.end_handle;
        _native_service_found = true;
      }
    } else if (event == ESP_GATTC_SEARCH_CMPL_EVT) {
      _native_service_search_status = param->search_cmpl.status;
      _native_service_search_complete = true;
      _native_service_search_active = false;
    }
  }
  if (event == ESP_GATTC_REG_FOR_NOTIFY_EVT) {
    _local_notify_registration_status = (uint8_t)param->reg_for_notify.status;
    printf("BLE_NOTIFY_REG status=%u\n",
           (unsigned)param->reg_for_notify.status);
    return;
  }
  if (event == ESP_GATTC_READ_CHAR_EVT
   && param->read.handle == _native_midi_char_handle) {
    _native_midi_read_status = param->read.status;
    _native_midi_read_length = param->read.value_len;
    _native_midi_read_complete = true;
    printf("BLE_MIDI_READ status=%u length=%u handle=%u\n",
           (unsigned)param->read.status, (unsigned)param->read.value_len,
           (unsigned)param->read.handle);
    return;
  }
  if (event == ESP_GATTC_READ_DESCR_EVT
   && param->read.handle == _native_cccd_handle) {
    _native_cccd_read_status = param->read.status;
    _native_cccd_read_value = param->read.value_len >= 2
      ? (uint16_t)param->read.value[0] | ((uint16_t)param->read.value[1] << 8)
      : 0xFFFF;
    _native_cccd_read_complete = true;
    printf("BLE_CCCD_READ status=%u length=%u value=%u\n",
           (unsigned)param->read.status, (unsigned)param->read.value_len,
           (unsigned)_native_cccd_read_value);
    return;
  }
  if (event == ESP_GATTC_WRITE_DESCR_EVT
   && param->write.handle == _native_cccd_handle) {
    _native_cccd_write_status = param->write.status;
    _native_cccd_write_complete = true;
    printf("BLE_CCCD status=%u handle=%u\n",
           (unsigned)param->write.status, (unsigned)param->write.handle);
    if (_central_is_m_vave && param->write.status == ESP_GATT_OK) {
      _native_cccd_read_complete = false;
      _native_cccd_read_status = ESP_GATT_ERROR;
      _native_cccd_read_value = 0xFFFF;
      esp_ble_gattc_read_char_descr(
        gatt_if, _native_conn_id, _native_cccd_handle, ESP_GATT_AUTH_REQ_NO_MITM);
    }
    return;
  }
  if (event == ESP_GATTC_DISCONNECT_EVT) {
    printf("BLE_DISCONNECT reason=%u conn=%u\n",
           (unsigned)param->disconnect.reason,
           (unsigned)param->disconnect.conn_id);
  }
  if (event != ESP_GATTC_NOTIFY_EVT || _instance == nullptr
   || param->notify.value == nullptr || param->notify.value_len == 0) { return; }

  // Arduino BLEのBLERemoteCharacteristicはhandleマップの照合に失敗すると
  // 通知自体が届いていてもコールバックを呼ばない。購読済みの中央接続は
  // BLE MIDIのみなので、GATTC通知をここで直接受け取る。
  note_received_packet(param->notify.value, param->notify.value_len);
  if (_instance->decodeReceive(param->notify.value, param->notify.value_len)) {
    _instance->execTaskNotify();
  }
}
#endif

static void service_central_subscription(void)
{
  // Once both the local registration and the remote CCCD readback prove that
  // notifications are armed, repeating either operation only grows the
  // Bluedroid registration table on some IDF builds. Preserve that memory and
  // leave the established subscription untouched while waiting for MIDI.
  const uint16_t expected_cccd = (_central_midi_properties & 0x01) ? 1u : 2u;
  if (_local_notify_registration_status == ESP_GATT_OK
   && _native_cccd_read_complete
   && _native_cccd_read_status == ESP_GATT_OK
   && _native_cccd_read_value == expected_cccd) {
    _subscription_attempts = 4;
    _central_subscription = 1;
    return;
  }
  if (_subscription_attempts >= 4
   || _rx_packet_count != _subscription_rx_packet_base) {
    return;
  }
  const uint32_t now = M5.millis();
  if ((int32_t)(now - _subscription_next_retry_msec) < 0) { return; }

  // M-VAVE can finish address resolution after the first registration request.
  // Re-register with the address reported by the established GATT link before
  // rewriting the remote CCCD. Duplicate registration is explicitly accepted
  // by Bluedroid and does not consume another notification slot.
  if (_native_cccd_handle != 0 && _native_gatt_if != ESP_GATT_IF_NONE) {
    if (_native_peer_address_valid) {
      const esp_err_t register_result = esp_ble_gattc_register_for_notify(
        _native_gatt_if, _native_peer_address, _native_midi_char_handle);
      printf("BLE_NOTIFY_RETRY attempt=%u result=%d addr=%s\n",
             (unsigned)(_subscription_attempts + 1), (int)register_result,
             BLEAddress(_native_peer_address).toString().c_str());
    }
    uint8_t notify_enabled[] = {
      (uint8_t)((_central_midi_properties & 0x01) ? 1 : 2), 0
    };
    const esp_err_t result = esp_ble_gattc_write_char_descr(
      _native_gatt_if, _native_conn_id, _native_cccd_handle,
      sizeof(notify_enabled), notify_enabled, ESP_GATT_WRITE_TYPE_RSP,
      _central_is_m_vave ? ESP_GATT_AUTH_REQ_NO_MITM : ESP_GATT_AUTH_REQ_NONE);
    _m_vave_cccd_value = result == ESP_OK ? notify_enabled[0] : 0xFF;
  }
  ++_subscription_attempts;
  _central_subscription = _subscription_attempts;
  _subscription_next_retry_msec = now + 650 + 250 * _subscription_attempts;
}

void MIDI_Transport_BLE::service(void)
{
  if (_pClient != nullptr && !_pClient->isConnected() && !_connecting
   && (_client_release_not_before_msec == 0
    || (int32_t)(M5.millis() - _client_release_not_before_msec) >= 0)) {
    BLEClient* disconnected = _pClient;
    _pClient = nullptr;
    _client_release_not_before_msec = 0;
    delete disconnected;
    // Rearm the contiguous arena as soon as Bluedroid returns the failed
    // client's allocations, before normal UI work can fragment them again.
    ensure_gatt_discovery_reserve();
  }
  bool disconnect_requested = false;
  bool scan_requested = false;
  bool selection_active = false;
  bool has_preferred_device = false;
  {
    std::lock_guard<std::mutex> lock(mutex_central_selection);
    disconnect_requested = _central_disconnect_requested;
    _central_disconnect_requested = false;
    scan_requested = _central_scan_requested;
    selection_active = _central_selection_active;
    has_preferred_device = _preferred_central_address[0] != 0;
  }
  if (disconnect_requested) {
    if (_pClient != nullptr && _pClient->isConnected()) { _pClient->disconnect(); }
    if (_conn_id >= 0 && pServer != nullptr) { pServer->disconnect(_conn_id); }
    return;
  }
  if (_central_connected) {
    if (_connect_guard_clear_msec != 0
     && (int32_t)(M5.millis() - _connect_guard_clear_msec) >= 0) {
      clear_connect_stage();
    }
    if (_central_wake_pending && _native_midi_char_handle != 0
     && _native_gatt_if != ESP_GATT_IF_NONE && _pClient != nullptr
     && _pClient->isConnected()) {
      // Some M-VAVE firmware does not start forwarding its pad stream until
      // the bidirectional BLE-MIDI characteristic has seen one valid packet.
      // Active Sensing is state-free and inaudible, making it a safe wake-up.
      const uint32_t stamp = M5.millis();
      uint8_t wake_packet[] = {
        (uint8_t)(0x80u | ((stamp >> 7) & 0x3Fu)),
        (uint8_t)(0x80u | (stamp & 0x7Fu)),
        0xFEu
      };
      const esp_err_t wake_result = esp_ble_gattc_write_char(
        _native_gatt_if, _native_conn_id, _native_midi_char_handle,
        sizeof(wake_packet), wake_packet, ESP_GATT_WRITE_TYPE_NO_RSP,
        _central_is_m_vave ? ESP_GATT_AUTH_REQ_NO_MITM : ESP_GATT_AUTH_REQ_NONE);
      printf("BLE_MIDI_WAKE result=%d\n", (int)wake_result);
      _central_wake_pending = false;
    }
    service_central_subscription();
    return;
  }
  if (!(_use_tx || _use_rx) || _central_connected || _peripheral_connected
   || _pClient != nullptr || _connecting) { return; }
  // 手動選択中は要求された1回だけスキャンし、一覧表示中に以前の機器へ
  // 勝手に再接続しない。通常時は保存済み機器だけを探索する。
  if ((selection_active && !scan_requested)
   || (!selection_active && !has_preferred_device)) { return; }
  if (_auto_reconnect_suppressed && !selection_active) { return; }
  if (_central_reconnect_failures >= 3 && !selection_active) {
    // Stop a failing peer from repeatedly registering GATT clients and
    // fragmenting Bluedroid's internal heap. A new Scan & Connect explicitly
    // clears this latch through requestCentralScan().
    _auto_reconnect_suppressed = true;
    return;
  }
  const uint32_t now = M5.millis();
  // A missing saved controller must not force a full active scan every two
  // seconds forever.  Besides wasting power, repeated Bluedroid result-list
  // allocation can collide with UI startup while AMY is resident.
  const uint32_t retry_delay = std::min<uint32_t>(
    30000u, 2000u << std::min<uint8_t>(_central_reconnect_failures, 4u));
  if (now - _last_central_scan_msec < retry_delay) { return; }
  _last_central_scan_msec = now;

  // Reuse the established connection setup path without tearing down the BLE
  // controller or its peripheral advertisement.
  const bool use_tx = _use_tx;
  const bool use_rx = _use_rx;
  _use_tx = false;
  _use_rx = false;
  setUseTxRx(use_tx, use_rx);
  if (_central_connected) {
    _central_reconnect_failures = 0;
  } else if (_central_reconnect_failures < 5) {
    ++_central_reconnect_failures;
  }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    ESP_LOGV("BLE", "ble client: onConnect");
    // printf("ble client: onConnect\n");
    // fflush(stdout);
    // _instance->setCentralConnected(true);
  }

  void onDisconnect(BLEClient* pclient) {
    // connected = false;
    ESP_LOGV("BLE", "ble client: onDisconnect\n");
    // printf("ble client: onDisconnect\n");
    // fflush(stdout);
    remotecharacteristic = nullptr;
#if defined(CONFIG_BLUEDROID_ENABLED)
    _native_service_search_active = false;
    _native_service_search_complete = false;
    _native_service_found = false;
    _native_service_start_handle = 0;
    _native_service_end_handle = 0;
    _native_midi_char_handle = 0;
    _native_cccd_handle = 0;
    _native_midi_read_complete = false;
    _native_midi_read_status = ESP_GATT_ERROR;
    _native_midi_read_length = 0;
    _native_cccd_read_complete = false;
    _native_cccd_read_status = ESP_GATT_ERROR;
    _native_cccd_read_value = 0xFFFF;
    _native_gatt_if = ESP_GATT_IF_NONE;
    _native_conn_id = 0;
    memset(_native_peer_address, 0, sizeof(_native_peer_address));
    _native_peer_address_valid = false;
#endif
    const bool received_since_subscription =
      _rx_packet_count != _subscription_rx_packet_base;
    if (!received_since_subscription && _central_reconnect_failures < 5) {
      ++_central_reconnect_failures;
    }
    _central_device_name[0] = 0;
    _central_device_address[0] = 0;
    _central_midi_properties = 0;
    _central_subscription = 0;
    _central_is_m_vave = false;
    _subscription_attempts = 0;
    _subscription_next_retry_msec = 0;
    _central_wake_pending = false;
    _m_vave_auth_state = 0;
    _m_vave_pairing_pending = false;
    _m_vave_resuming_bond = false;
    _m_vave_cccd_value = 0xFF;
    _local_notify_registration_status = 0xFF;
    _client_release_not_before_msec = M5.millis() + 250;
    // Release an established client later from the MIDI task. Destroying it
    // from inside its own Bluedroid callback can race peer-event dispatch.
    _instance->setCentralConnected(false);
  }
};
static MyClientCallback myClientCallback;

static void defer_failed_client_release(BLEClient* client)
{
  if (client == nullptr) { return; }
  // disconnect() completes on the Bluedroid host task. Keep the object alive
  // until service() observes the disconnected state; deleting it here races
  // the callback and can poison the next controller connection.
  _pClient = client;
  _client_release_not_before_msec = M5.millis() + 250;
  if (client->isConnected()) { client->disconnect(); }
}

void MIDI_Transport_BLE::updateState(void)
{
  _connected = _central_connected || _peripheral_connected;

  auto midiport_info = kanplay_ns::def::command::midiport_info_t::mp_off;

  if (_connecting) {
    midiport_info = kanplay_ns::def::command::midiport_info_t::mp_connecting;
  } else if (_connected) {
    midiport_info = kanplay_ns::def::command::midiport_info_t::mp_connected;
  } else if (_use_tx || _use_rx) {
    midiport_info = kanplay_ns::def::command::midiport_info_t::mp_enabled;
  }
  kanplay_ns::system_registry->runtime_info.setMidiPortStateBLE(midiport_info);
}

void MIDI_Transport_BLE::setCentralConnected(bool connected)
{
  _central_connected = connected;
  updateState();
}

void MIDI_Transport_BLE::setPeripheralConnected(bool connected)
{
  _peripheral_connected = connected;
  updateState();
  auto adv = pAdvertising;
  if (adv != nullptr) {
    if (connected) {
      adv->stop();
    } else {
      adv->start();
    }
  }
}

void MIDI_Transport_BLE::setUseTxRx(bool use_tx, bool use_rx)
{
  _instance = this;
  if (_use_tx == use_tx && _use_rx == use_rx) { return; }

  auto midi_service_uuid = BLEUUID(MIDI_SERVICE_UUID);
  auto midi_characteristic_uuid = BLEUUID(MIDI_CHARACTERISTIC_UUID);

  bool prev_en = _use_tx || _use_rx;
  bool new_en = use_tx || use_rx;
  if (prev_en != new_en) {
    {
      std::lock_guard<std::mutex> lock(mutex_rx);
      _rx_data.clear();
      if (new_en && _rx_data.capacity() < rx_data_reserve) {
        _rx_data.reserve(rx_data_reserve);
      }
    }
    if (new_en) {
      ensure_gatt_discovery_reserve();
      if (!_is_begin) {
        _is_begin = true;
        // BLEDevice::setMTU(_mtu_size);
        BLEDevice::init(_config.device_name);
#if defined(CONFIG_BLUEDROID_ENABLED)
        configure_ble_bonding();
        BLEDevice::setCustomGattcHandler(sampler_gattc_event_handler);
#endif
        // BLEDevice::setMTU(_mtu_size);
#if !defined(KANPLAY_AMY_INTEGRATION)
        // The regular firmware can also act as a BLE MIDI peripheral. The AMY
        // sampler connects to a controller selected by the user and only needs
        // the central/GATT-client path; constructing an unused server consumes
        // several Bluedroid queues in scarce internal RAM.
        pServer = BLEDevice::createServer();
        pServer->setCallbacks(&myServerCallbacks);

        pService = pServer->createService(midi_service_uuid);
        pCharacteristic = pService->createCharacteristic(
                            midi_characteristic_uuid,
                            BLECharacteristic::PROPERTY_READ   |
                            BLECharacteristic::PROPERTY_WRITE_NR|
                            BLECharacteristic::PROPERTY_NOTIFY
                          );
        if (pCharacteristic != nullptr) {
          pCharacteristic->setCallbacks(new MyCallbacks());
          pCharacteristic->addDescriptor(new BLE2902());
          pCharacteristic->setNotifyProperty(true);

          BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
          oAdvertisementData.setFlags(ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
          oAdvertisementData.setCompleteServices(midi_service_uuid);
          oAdvertisementData.setName(_config.device_name);
          pAdvertising = pServer->getAdvertising();
          pAdvertising->setMinPreferred(0x06); // 7.5msec  (6 x 1.25msec)
          pAdvertising->setMaxPreferred(0x0C); // 15.0msec (12 x 1.25msec)
          pAdvertising->setAdvertisementData(oAdvertisementData);
        }
#endif
      }
      if (pService != nullptr) { pService->start(); }
      if (pAdvertising != nullptr) { pAdvertising->start(); }
    } else {
      release_all_gatt_reserves();
      if (_conn_id >= 0 && pServer != nullptr) {
        pServer->disconnect(_conn_id);
        _conn_id = -1;
      }
      if (pAdvertising != nullptr) { pAdvertising->stop(); }
      if (pService != nullptr) { pService->stop(); }
    }

    _instance->setCentralConnected(false);
    if (_pClient != nullptr) {
      remotecharacteristic = nullptr;
      _pClient->disconnect();
      // delete _pClient;
      // _pClient = nullptr;
    }

    if (new_en) {
      char preferred_address[18] = {};
      char preferred_name[24] = {};
      bool selection_active = false;
      bool scan_requested = false;
      bool force_fresh_pairing = false;
      int8_t preferred_address_type = -1;
      {
        std::lock_guard<std::mutex> lock(mutex_central_selection);
        snprintf(preferred_address, sizeof(preferred_address), "%s", _preferred_central_address);
        snprintf(preferred_name, sizeof(preferred_name), "%s", _preferred_central_name);
        selection_active = _central_selection_active;
        scan_requested = _central_scan_requested;
        force_fresh_pairing = _force_fresh_pairing_once;
        preferred_address_type = _preferred_address_type_once;
        _central_scan_requested = false;
        _force_fresh_pairing_once = false;
        _preferred_address_type_once = -1;
      }
      // 手動スキャンは一覧を返すだけ。通常接続では保存済みアドレスと一致する
      // 機器だけを検証し、近くの別コントローラーへ勝手に接続しない。
      const bool have_fresh_selected_address = preferred_address_type >= 0;
      const bool should_scan = scan_requested
                            || (!selection_active && preferred_address[0]
                                && !have_fresh_selected_address);
      if (should_scan) { mark_connect_stage(1); }
      auto foundMidiDevices = should_scan ? ble_scan() : std::vector<BLEAdvertisedDevice>{};
      const bool connect_preferred = !selection_active && preferred_address[0];
      if (connect_preferred
       && (have_fresh_selected_address || !foundMidiDevices.empty())) {
        _connecting = true;
        size_t preferred_name_matches = 0;
        if (preferred_name[0]) {
          for (auto& candidate : foundMidiDevices) {
            if (!strcmp(candidate.getName().c_str(), preferred_name)) { ++preferred_name_matches; }
          }
        }
        // Keep only the lightweight identity needed by esp_ble_gattc_open().
        // Holding every advertised-device object through GATT registration
        // creates an avoidable internal-heap peak when AMY is also resident.
        String selected_address;
        String selected_name;
        esp_ble_addr_type_t selected_address_type = BLE_ADDR_TYPE_PUBLIC;
        if (have_fresh_selected_address) {
          selected_address = preferred_address;
          selected_name = preferred_name;
          selected_address_type = (esp_ble_addr_type_t)preferred_address_type;
        } else for (auto& candidate : foundMidiDevices) {
          const bool address_match = !strcmp(candidate.getAddress().toString().c_str(), preferred_address);
          // Some controllers rotate their private address.  Use the saved name
          // only when it identifies exactly one scan result, avoiding an
          // accidental connection to another same-name device.
          const bool unique_name_match = preferred_name_matches == 1
                                      && !strcmp(candidate.getName().c_str(), preferred_name);
          if (!address_match && !unique_name_match) { continue; }
          selected_address = candidate.getAddress().toString().c_str();
          selected_name = candidate.getName();
          selected_address_type = candidate.getAddressType();
          break;
        }
        std::vector<BLEAdvertisedDevice>().swap(foundMidiDevices);

        if (selected_address.length()) do {
          BLEAddress device_address(selected_address);
          mark_connect_stage(2);
          const bool m_vave = is_m_vave_name(selected_name);
          bool resumed_m_vave_bond = false;
          // M-VAVEはGATT接続だけではMIDI通知を開始せず、Centralからの
          // SMPペアリングと暗号化を必要とする。ボンド鍵は再起動後も保存し、
          // Reset/Forget時だけ削除する。
#if defined(CONFIG_BLUEDROID_ENABLED)
          if (m_vave) {
            mark_connect_stage(3);
            // Manual selection authorizes pairing, but must not unconditionally
            // discard a valid local LTK. M-VAVE may still hold that same key;
            // deleting only our half creates a guaranteed reconnect loop. The
            // failed-authentication path below removes a demonstrably stale key.
            (void)force_fresh_pairing;
            prepare_m_vave_pairing(device_address);
            resumed_m_vave_bond = _m_vave_resuming_bond;
          } else {
            BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)0);
          }
#endif
          mark_connect_stage(4);
          printf("BLE_CONNECT name=%s bonded=%u retry=%u free=%u largest=%u\n",
                 selected_name.c_str(),
#if defined(CONFIG_BLUEDROID_ENABLED)
                 ble_address_is_bonded(device_address) ? 1u : 0u,
#else
                 0u,
#endif
                 (unsigned)_central_reconnect_failures,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
          BLEClient* pClient = BLEDevice::createClient();
          if (pClient == nullptr) {
#if defined(CONFIG_BLUEDROID_ENABLED)
            _m_vave_pairing_pending = false;
            BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)0);
#endif
            break;
          }
          pClient->setClientCallbacks(&myClientCallback);
          mark_connect_stage(5);
          // Registration/open is the first allocation peak. Release only its
          // half now and preserve the other contiguous block for discovery.
          release_gatt_connect_reserve();
          memset(_native_peer_address, 0, sizeof(_native_peer_address));
          _native_peer_address_valid = false;
          _gatt_connect_in_progress = true;
          const bool connected = pClient->connect(device_address, selected_address_type, 12000);
          _gatt_connect_in_progress = false;
          printf("BLE_OPEN connected=%u free=%u largest=%u\n",
                 connected ? 1u : 0u,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
          if (!connected) {
#if defined(CONFIG_BLUEDROID_ENABLED)
            _m_vave_pairing_pending = false;
            BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)0);
#endif
            delete pClient;
            break;
          }
#if defined(CONFIG_BLUEDROID_ENABLED)
          BLEDevice::setEncryptionLevel((esp_ble_sec_act_t)0);
          mark_connect_stage(6);
          if (m_vave) {
            mark_connect_stage(14);
            const esp_err_t encryption_result = esp_ble_set_encryption(
              *device_address.getNative(), ESP_BLE_SEC_ENCRYPT);
            mark_connect_stage(15);
            if (encryption_result != ESP_OK) {
              _m_vave_pairing_pending = false;
              if (resumed_m_vave_bond) {
                esp_ble_remove_bond_device(*device_address.getNative());
              }
              defer_failed_client_release(pClient);
              break;
            }
          }
          if (m_vave && !wait_for_m_vave_pairing(pClient)) {
            // A controller can retain a different peer bond while this unit
            // still has its old LTK. Retrying that stale key only produces an
            // endless connect/disconnect cycle. Forget the local half once so
            // the next bounded reconnect performs a fresh pairing handshake.
            if (resumed_m_vave_bond) {
              esp_ble_remove_bond_device(*device_address.getNative());
            }
            defer_failed_client_release(pClient);
            break;
          }
#endif
          M5.delay(m_vave ? 120 : 16);
          mark_connect_stage(7);
#if defined(CONFIG_BLUEDROID_ENABLED)
          // Pairing has finished. Give the protected contiguous block to the
          // allocation-heavy service/characteristic discovery transaction.
          release_gatt_discovery_reserve();
          // Arduino BLEClient::getService() enumerates and heap-allocates every
          // remote service. M-VAVE exposes enough GATT data for that temporary
          // peak to exhaust CoreS3 internal RAM when AMY is resident. Search
          // only the BLE-MIDI UUID and use IDF's cached handles directly.
          _native_gatt_if = pClient->getGattcIf();
          _native_conn_id = pClient->getConnId();
          _native_service_start_handle = 0;
          _native_service_end_handle = 0;
          _native_service_found = false;
          _native_service_search_complete = false;
          _native_service_search_status = ESP_GATT_ERROR;
          _native_service_search_active = true;
          esp_bt_uuid_t service_filter = *midi_service_uuid.getNative();
          const esp_err_t search_result = esp_ble_gattc_search_service(
            _native_gatt_if, _native_conn_id, &service_filter);
          const uint32_t search_deadline = M5.millis() + 5000;
          while (search_result == ESP_OK && !_native_service_search_complete
              && pClient->isConnected()
              && (int32_t)(M5.millis() - search_deadline) < 0) {
            M5.delay(5);
          }
          _native_service_search_active = false;

          esp_gattc_char_elem_t char_result = {};
          uint16_t char_count = 1;
          bool have_midi_characteristic = _native_service_search_complete
            && _native_service_search_status == ESP_GATT_OK
            && _native_service_found;
          if (have_midi_characteristic) {
            mark_connect_stage(8);
            esp_bt_uuid_t char_uuid = *midi_characteristic_uuid.getNative();
            have_midi_characteristic = esp_ble_gattc_get_char_by_uuid(
              _native_gatt_if, _native_conn_id, _native_service_start_handle,
              _native_service_end_handle, char_uuid, &char_result,
              &char_count) == ESP_GATT_OK && char_count != 0;
          }
          if (have_midi_characteristic) {
            _native_midi_char_handle = char_result.char_handle;
            esp_gattc_descr_elem_t descr_result = {};
            uint16_t descr_count = 1;
            esp_bt_uuid_t cccd_uuid = *BLEUUID((uint16_t)0x2902).getNative();
            if (esp_ble_gattc_get_descr_by_char_handle(
                  _native_gatt_if, _native_conn_id, _native_midi_char_handle,
                  cccd_uuid, &descr_result, &descr_count) == ESP_GATT_OK
             && descr_count != 0) {
              _native_cccd_handle = descr_result.handle;
            }
            _central_midi_properties =
                ((char_result.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ? 0x01 : 0)
              | ((char_result.properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) ? 0x02 : 0)
              | ((char_result.properties & ESP_GATT_CHAR_PROP_BIT_WRITE) ? 0x04 : 0)
              | ((char_result.properties & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) ? 0x08 : 0)
              | (_native_cccd_handle != 0 ? 0x10 : 0);
            printf("BLE_MIDI_CHAR handle=%u cccd=%u properties=0x%02X link=%s\n",
                   (unsigned)_native_midi_char_handle,
                   (unsigned)_native_cccd_handle,
                   (unsigned)_central_midi_properties,
                   _native_peer_address_valid
                     ? BLEAddress(_native_peer_address).toString().c_str()
                     : "unknown");
            _central_is_m_vave = m_vave;
            _subscription_rx_packet_base = _rx_packet_count;

            // Encryption-required BLE-MIDI peripherals may not arm their MIDI
            // I/O path until the central reads the characteristic once after
            // pairing. Apple/CoreMIDI hosts perform this empty read before
            // enabling notifications; M-VAVE follows that stricter sequence.
            // Keep it device-specific so tolerant controllers retain their
            // existing fast connection path.
            if (m_vave) {
              _native_midi_read_complete = false;
              _native_midi_read_status = ESP_GATT_ERROR;
              _native_midi_read_length = 0;
              const esp_err_t read_result = esp_ble_gattc_read_char(
                _native_gatt_if, _native_conn_id, _native_midi_char_handle,
                ESP_GATT_AUTH_REQ_NO_MITM);
              const uint32_t read_deadline = M5.millis() + 1800;
              while (read_result == ESP_OK && !_native_midi_read_complete
                  && pClient->isConnected()
                  && (int32_t)(M5.millis() - read_deadline) < 0) {
                M5.delay(5);
              }
              printf("BLE_MIDI_READ_REQUEST result=%d complete=%u status=%u length=%u\n",
                     (int)read_result, _native_midi_read_complete ? 1u : 0u,
                     (unsigned)_native_midi_read_status,
                     (unsigned)_native_midi_read_length);
              // Some valid BLE-MIDI implementations expose a non-readable
              // characteristic. Notification setup remains the authority in
              // that case, so a failed optional read must not reject the link.
            }
            mark_connect_stage(9);
            _local_notify_registration_status = 0xFF;
            const esp_err_t register_result = esp_ble_gattc_register_for_notify(
              _native_gatt_if,
              _native_peer_address_valid
                ? _native_peer_address : *device_address.getNative(),
              _native_midi_char_handle);
            bool subscription_ready = register_result == ESP_OK
                                   && _native_cccd_handle != 0;
            if (subscription_ready) {
              const uint32_t notify_deadline = M5.millis() + 1500;
              while (_local_notify_registration_status == 0xFF
                  && pClient->isConnected()
                  && (int32_t)(M5.millis() - notify_deadline) < 0) {
                M5.delay(5);
              }
              // A missing completion event is not a rejection. Keep the GATT
              // link and let the bounded CCCD retry service prove delivery.
              if (_local_notify_registration_status != 0xFF) {
                subscription_ready = _local_notify_registration_status == ESP_GATT_OK;
              }
            }
            if (subscription_ready) {
              // SMC-PAD Pocket persists CCCD=1 with its bond, but some
              // firmware revisions do not restart the MIDI transmitter when
              // the next central merely writes the same value again. Force a
              // real disabled -> enabled transition on each new link.
              if (m_vave) {
                uint8_t notify_disabled[] = { 0, 0 };
                _native_cccd_write_complete = false;
                _native_cccd_write_status = ESP_GATT_ERROR;
                const esp_err_t disable_result = esp_ble_gattc_write_char_descr(
                  _native_gatt_if, _native_conn_id, _native_cccd_handle,
                  sizeof(notify_disabled), notify_disabled,
                  ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NO_MITM);
                const uint32_t disable_deadline = M5.millis() + 1200;
                while (disable_result == ESP_OK && !_native_cccd_write_complete
                    && pClient->isConnected()
                    && (int32_t)(M5.millis() - disable_deadline) < 0) {
                  M5.delay(5);
                }
                subscription_ready = disable_result == ESP_OK
                  && (!_native_cccd_write_complete
                   || _native_cccd_write_status == ESP_GATT_OK);
                printf("BLE_CCCD_REARM_OFF result=%d complete=%u status=%u\n",
                       (int)disable_result,
                       _native_cccd_write_complete ? 1u : 0u,
                       (unsigned)_native_cccd_write_status);
                M5.delay(60);
              }
            }
            if (subscription_ready) {
              uint8_t notify_enabled[] = {
                (uint8_t)((char_result.properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) ? 1 : 2), 0
              };
              _native_cccd_write_complete = false;
              _native_cccd_write_status = ESP_GATT_ERROR;
              const esp_err_t cccd_result = esp_ble_gattc_write_char_descr(
                _native_gatt_if, _native_conn_id, _native_cccd_handle,
                sizeof(notify_enabled), notify_enabled, ESP_GATT_WRITE_TYPE_RSP,
                m_vave ? ESP_GATT_AUTH_REQ_NO_MITM : ESP_GATT_AUTH_REQ_NONE);
              const uint32_t cccd_deadline = M5.millis() + 1500;
              while (cccd_result == ESP_OK && !_native_cccd_write_complete
                  && pClient->isConnected()
                  && (int32_t)(M5.millis() - cccd_deadline) < 0) {
                M5.delay(5);
              }
              subscription_ready = cccd_result == ESP_OK;
              if (_native_cccd_write_complete) {
                subscription_ready = _native_cccd_write_status == ESP_GATT_OK;
              }
              _m_vave_cccd_value = subscription_ready ? notify_enabled[0] : 0xFF;
            }
            if (!subscription_ready) {
              if (m_vave && resumed_m_vave_bond) {
                esp_ble_remove_bond_device(*device_address.getNative());
              }
              defer_failed_client_release(pClient);
              _native_midi_char_handle = 0;
              _native_cccd_handle = 0;
              break;
            }
            remotecharacteristic = nullptr;
            _pClient = pClient;
            // Any controller may deliver the completion event late. Retry the
            // descriptor a few times until the first MIDI packet proves that
            // notifications are flowing; M-VAVE additionally benefits after
            // its delayed encryption/bond state becomes active.
            _subscription_attempts = m_vave ? 0 : 4;
            _central_subscription = 1;
            _subscription_next_retry_msec = m_vave ? M5.millis() + 700 : 0;
            snprintf(_central_device_name, sizeof(_central_device_name), "%s",
                     selected_name.length() ? selected_name.c_str() : selected_address.c_str());
            snprintf(_central_device_address, sizeof(_central_device_address), "%s",
                     selected_address.c_str());
            _instance->setCentralConnected(true);
            _central_wake_pending = m_vave;
            mark_connect_stage(10);
            _connect_guard_clear_msec = M5.millis() + 2500;
            break;
          }
#else
          (void)midi_characteristic_uuid;
#endif
          if (m_vave && resumed_m_vave_bond) {
#if defined(CONFIG_BLUEDROID_ENABLED)
            // Service discovery failure immediately after a resumed bond is
            // another stale-key signature. Allow the retry to pair afresh.
            esp_ble_remove_bond_device(*device_address.getNative());
#endif
          }
          defer_failed_client_release(pClient);
        } while (false);
      }
      if (should_scan && !_central_connected) { clear_connect_stage(); }
    } else if (_is_begin) {
      // Wi-Fi AP/STAの開始前にBLEコントローラを完全停止する。単に広告を止める
      // だけでは無線・内部RAMを保持し、Wi-Fi初期化が失敗する個体がある。
      BLEDevice::deinit(false);
      pCharacteristic = nullptr;
      pAdvertising = nullptr;
      pService = nullptr;
      pServer = nullptr;
      remotecharacteristic = nullptr;
      _pClient = nullptr;
      _conn_id = -1;
      _central_is_m_vave = false;
      _subscription_attempts = 0;
      _subscription_next_retry_msec = 0;
      _m_vave_auth_state = 0;
      _m_vave_pairing_pending = false;
      _m_vave_resuming_bond = false;
      _m_vave_cccd_value = 0xFF;
      _local_notify_registration_status = 0xFF;
#if defined(CONFIG_BLUEDROID_ENABLED)
      BLEDevice::setCustomGattcHandler(nullptr);
#endif
      _is_begin = false;
      _tx_data.clear();
      _tx_runningStatus = 0;
    }
  }
  _connecting = false;
  _use_tx = use_tx;
  _use_rx = use_rx;
  updateState();
}

//----------------------------------------------------------------

} // namespace midi_driver

#endif

#endif
