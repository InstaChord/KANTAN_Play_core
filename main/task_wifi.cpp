// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include <string.h>

#include "task_wifi.hpp"

#include "task_http_client.hpp"

#include "system_registry.hpp"

#include "task_wifi/task_wifi_api.hpp"
#if defined(KANPLAY_SAMPLER)
  #include "sampler/sampler_define.hpp"
  #include "sampler/sampler_web_api.hpp"
#endif

#if defined (M5UNIFIED_PC_BUILD)
namespace kanplay_ns {

void task_wifi_t::task_func(task_wifi_t* me)
{
  uint8_t counter;
  for (;;) {
    ++counter;
    M5.delay(256);
    auto mode = system_registry->wifi_control.getWifiMode();
    auto op = system_registry->wifi_control.getOperation();

    auto sta_info = def::command::wifi_sta_info_t::wsi_off;
    auto ap_info = def::command::wifi_ap_info_t::wai_off;

    bool sta = false;
    bool ap = false;
    if (mode == def::command::wifi_mode_t::wifi_enable_sta) {
      sta_info = (def::command::wifi_sta_info_t)((3 & (counter >> 3))+1);
    }
    if (op == def::command::wifi_operation_t::wfop_setup_ap) {
      sta_info = def::command::wifi_sta_info_t::wsi_waiting;
      ap_info = (counter & 0x10)
              ? def::command::wifi_ap_info_t::wai_enabled
              : def::command::wifi_ap_info_t::wai_waiting;
      system_registry->runtime_info.setWiFiStationCount((counter & 0x10) ? 1 : 0);
    }
    if (op == def::command::wifi_operation_t::wfop_setup_wps) {
      sta_info = def::command::wifi_sta_info_t::wsi_waiting;
    }
    if (op == def::command::wifi_operation_t::wfop_ota_begin) {
      system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_ota_progress);
    }
    if (op == def::command::wifi_operation_t::wfop_ota_progress) {
      sta_info = (def::command::wifi_sta_info_t)((3 & (counter >> 3))+1);
      int progress = counter & 0x7f;
      if (progress > 101) { progress = 101; }
      system_registry->runtime_info.setWiFiOtaProgress(progress);
    }
    if (op == def::command::wifi_operation_t::wfop_connectivity_check_begin) {
      system_registry->runtime_info.setWiFiConnectivity(
        def::command::wifi_connectivity_state_t::online);
      system_registry->wifi_control.setOperation(
        def::command::wifi_operation_t::wfop_disable);
    }
    system_registry->runtime_info.setWiFiSTAInfo(sta_info);
    system_registry->runtime_info.setWiFiAPInfo(ap_info);
  }
}

bool task_wifi_t::start(void) {
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "wifi", this);
  return thread != nullptr;
}

bool task_wifi_t::stop(void) { return true; }

bool task_wifi_t::hasSavedSTAConfig(void) { return false; }
bool task_wifi_t::getSavedSTASSID(char* out, size_t out_size)
{
  if (out && out_size) { out[0] = 0; }
  return false;
}
};
#else

#include <esp_wifi.h>
#include <esp_wps.h>
#include <esp_http_server.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <nvs.h>

#include <esp_crt_bundle.h>

#include <mdns.h>
#include <lwip/sockets.h>

#if __has_include (<esp_sntp.h>)
  #include <esp_sntp.h>
  #define SNTP_ENABLED 1
#elif __has_include (<sntp.h>)
  #include <sntp.h>
  #define SNTP_ENABLED 1
#endif



// ファイルインポートマクロ
#define IMPORT_FILE(section, filename, symbol) \
extern const char symbol[], sizeof_##symbol[]; \
asm (\
  ".section " #section "\n"\
  ".balign 4\n"\
  ".global " #symbol "\n"\
  #symbol ":\n"\
  ".incbin \"" filename "\"\n"\
  ".global sizeof_" #symbol "\n"\
  ".set sizeof_" #symbol ", . - " #symbol "\n"\
  ".balign 4\n"\
  ".section \".text\"\n")

IMPORT_FILE(.rodata, "incbin/html/wifi.html", html_wifi);

// ブラウザUIの配信元URL。platformio.ini の build_flags で上書き可能。
// 通常モードと developer mode で別アカウントの GitHub Pages を参照する。
#ifndef KANPLAY_UI_BASE
  #define KANPLAY_UI_BASE     "https://instachord.github.io/KANTAN_Play_core/ui"
#endif
#ifndef KANPLAY_UI_BASE_DEV
  #define KANPLAY_UI_BASE_DEV "https://ainyan03.github.io/KANTAN_Play_core/ui"
#endif
#ifndef KANPLAY_SAMPLER_UI_BASE
  #define KANPLAY_SAMPLER_UI_BASE "https://u1nagata.github.io/KANTAN_Play_core/sampler-ui"
#endif

namespace kanplay_ns {
//-------------------------------------------------------------------------

// --- WiFi state tracking ---
// イベントハンドラが他タスクから書く軽量フラグ類は wifi_state_t に入れず
// ファイルスコープ static のまま残す（task_wifi_info との競合回避のため）。
enum wifi_sta_state_t : uint8_t {
  STA_STOPPED,
  STA_IDLE,
  STA_CONNECTED,
  STA_DISCONNECTED,
};
static volatile wifi_sta_state_t _sta_state = STA_STOPPED;
// IP_EVENT_STA_GOT_IP はIP設定完了を示すが、DNS/HTTPSの最初の要求は直後に
// 失敗するアクセスポイントがある。OTA開始前だけ短く安定化時間を設ける。
static volatile uint32_t _sta_connected_ms = 0;
static volatile bool _ap_started = false;
static volatile int _ap_station_count = 0;
// Keep AP-only resources stable until the setup page has actually answered
// its first request. STA netif creation and scanning are deferred until then.
static volatile bool _setup_http_seen = false;
static volatile uint32_t _ap_start_requested_ms = 0;
static volatile uint8_t _ap_start_attempts = 0;
// -2=idle, -1=scanning, >=0=results ready to be consumed by task loop
static volatile int _scan_status = -2;
// POST /wifi で接続試行を開始した時刻(ms)。0 は未試行。
// grace 期間内の STA_DISCONNECTED は "connecting" として扱い、誤った「接続失敗」表示を抑止する。
static volatile uint32_t _connect_start_ms = 0;
static constexpr uint32_t CONNECT_GRACE_MS = 10000;
// 直近の STA_DISCONNECTED イベントの reason コード (0 = 未発生)
static volatile uint16_t _last_disconnect_reason = 0;
// 直近で esp_wifi_connect() を発行した時刻。切断後の再接続間隔制御に使う。
static volatile uint32_t _last_connect_attempt_ms = 0;
static constexpr uint32_t STA_RECONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
// OTA はHTTPS証明書の検証を行う。IP取得直後はDNSとSNTPの初期化がまだ
// 終わっていないため、外部サーバへ出る前にだけ十分な安定化時間を取る。
static constexpr uint32_t OTA_NETWORK_SETTLE_MS = 5000;

// Wi-Fi 起動時刻からの経過時間を計測するための基準時刻 (タイミングログ用)
static volatile uint32_t _setup_t0_ms = 0;


// --- SSID scan cache ---
static constexpr uint8_t SSID_CACHE_MAX = 16;
static constexpr uint32_t SSID_CACHE_TTL_MS = 90000; // 90秒見えなかったエントリは捨てる
struct ssid_cache_entry_t {
  char ssid[33];
  int8_t rssi;
  uint32_t last_seen_ms;
};

// --- Dynamically allocated Wi-Fi runtime state ---
// Wi-Fi が有効な間だけ確保し、完全無効化時に解放する。
// これにより WiFi ドライバ本体 (esp_wifi_deinit) と付随リソースが返却される。
struct wifi_state_t {
  // SSID cache
  ssid_cache_entry_t ssid_cache[SSID_CACHE_MAX] = {};
  volatile uint8_t ssid_cache_count = 0;
  SemaphoreHandle_t ssid_cache_mutex = nullptr;
  volatile uint32_t last_scan_done_ms = 0;

  // Wi-Fi driver / netif
  esp_netif_t* sta_netif = nullptr;
  esp_netif_t* ap_netif = nullptr;
  bool wifi_started = false;
  bool wps_enabled = false;

  // Captive portal DNS
  int dns_sock = -1;
  uint32_t dns_target_ip = 0;

  // HTTP server
  httpd_handle_t http_server = nullptr;
  bool mdns_started = false;
};
static wifi_state_t* _ws = nullptr;

static bool wifi_sta_config_has_ssid(const wifi_config_t& cfg) {
  return cfg.sta.ssid[0] != 0;
}

static void wifi_save_sta_config(const wifi_config_t& cfg) {
  if (!wifi_sta_config_has_ssid(cfg)) return;
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open("kanplay_wifi", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    M5.Log.printf("[wifi] nvs open failed: 0x%x %s\r\n", err, esp_err_to_name(err));
    return;
  }
  nvs_set_str(handle, "ssid", (const char*)cfg.sta.ssid);
  nvs_set_str(handle, "pass", (const char*)cfg.sta.password);
  nvs_commit(handle);
  nvs_close(handle);
}

static bool wifi_load_sta_config(wifi_config_t* cfg) {
  if (!cfg) return false;
  memset(cfg, 0, sizeof(*cfg));
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open("kanplay_wifi", NVS_READONLY, &handle);
  if (err != ESP_OK) {
    return false;
  }
  size_t ssid_len = sizeof(cfg->sta.ssid);
  err = nvs_get_str(handle, "ssid", (char*)cfg->sta.ssid, &ssid_len);
  if (err != ESP_OK || cfg->sta.ssid[0] == 0) {
    nvs_close(handle);
    return false;
  }
  size_t pass_len = sizeof(cfg->sta.password);
  err = nvs_get_str(handle, "pass", (char*)cfg->sta.password, &pass_len);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    nvs_close(handle);
    return false;
  }
  nvs_close(handle);
  return true;
}

bool task_wifi_t::hasSavedSTAConfig(void)
{
  wifi_config_t cfg = {};
  return wifi_load_sta_config(&cfg);
}

bool task_wifi_t::getSavedSTASSID(char* out, size_t out_size)
{
  if (!out || out_size == 0) { return false; }
  out[0] = 0;
  wifi_config_t cfg = {};
  if (!wifi_load_sta_config(&cfg)) { return false; }
  snprintf(out, out_size, "%s", (const char*)cfg.sta.ssid);
  return out[0] != 0;
}

static bool wifi_ensure_sta_config(const char* context) {
  wifi_config_t cfg = {};
  // The setup page stores credentials in our NVS namespace.  Always prefer
  // that explicit user choice over ESP-IDF's older flash-stored STA config.
  // Otherwise a previous network can silently overwrite a newly submitted
  // SSID while the radio is being restarted.
  if (wifi_load_sta_config(&cfg)) {
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    M5.Log.printf("[wifi] apply saved sta config: %s err=0x%x %s\r\n",
                  context ? context : "-", err, esp_err_to_name(err));
    return err == ESP_OK;
  }

  esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
  if (err == ESP_OK && wifi_sta_config_has_ssid(cfg)) {
    wifi_save_sta_config(cfg);
    return true;
  }

  M5.Log.printf("[wifi] no saved ssid: %s\r\n", context ? context : "-");
  return false;
}

static void wifi_connect_sta(const char* context) {
  if (!_ws || !_ws->wifi_started) return;
  uint32_t now = M5.millis();
  _last_connect_attempt_ms = now ? now : 1;
  if (!wifi_ensure_sta_config(context)) {
    _sta_state = STA_DISCONNECTED;
    _last_disconnect_reason = WIFI_REASON_NO_AP_FOUND;
    return;
  }
  _last_disconnect_reason = 0;
  _sta_state = STA_IDLE;
  M5.Log.printf("[wifi] connect start: %s\r\n", context ? context : "-");
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    M5_LOGW("[wifi] esp_wifi_connect failed (%s): 0x%x %s",
            context ? context : "-", err, esp_err_to_name(err));
    M5.Log.printf("[wifi] connect call failed: 0x%x %s\r\n", err, esp_err_to_name(err));
  }
}

static void dns_server_stop() {
  if (!_ws) return;
  if (_ws->dns_sock >= 0) {
    close(_ws->dns_sock);
    _ws->dns_sock = -1;
  }
}

static void dns_server_start(uint32_t ip) {
  if (!_ws) return;
  dns_server_stop();
  _ws->dns_target_ip = ip;
  _ws->dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (_ws->dns_sock < 0) return;
  int flags = fcntl(_ws->dns_sock, F_GETFL, 0);
  fcntl(_ws->dns_sock, F_SETFL, flags | O_NONBLOCK);
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = INADDR_ANY;
  bind(_ws->dns_sock, (struct sockaddr*)&addr, sizeof(addr));
}

static void dns_server_process() {
  if (!_ws || _ws->dns_sock < 0) return;
  static constexpr int DNS_MAX_QUERY = 512;
  static constexpr int DNS_ANSWER_SIZE = 16; // A record answer: 2+2+2+4+2+4=16 bytes
  uint8_t buf[DNS_MAX_QUERY + DNS_ANSWER_SIZE];
  struct sockaddr_in client = {};
  socklen_t client_len = sizeof(client);
  int len = recvfrom(_ws->dns_sock, buf, DNS_MAX_QUERY, 0, (struct sockaddr*)&client, &client_len);
  if (len < 12 || len > DNS_MAX_QUERY) return;
  // Build DNS response: set QR=1, keep RD, set ANCOUNT=1
  buf[2] = 0x80 | (buf[2] & 0x01);
  buf[3] = 0x00;
  buf[6] = 0; buf[7] = 1;  // ANCOUNT = 1
  buf[8] = 0; buf[9] = 0;  // NSCOUNT = 0
  buf[10] = 0; buf[11] = 0; // ARCOUNT = 0
  // Append A record answer
  int pos = len;
  buf[pos++] = 0xC0; buf[pos++] = 0x0C; // name pointer to question
  buf[pos++] = 0x00; buf[pos++] = 0x01; // type A
  buf[pos++] = 0x00; buf[pos++] = 0x01; // class IN
  buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x3C; // TTL 60s
  buf[pos++] = 0x00; buf[pos++] = 0x04; // RDLENGTH 4
  memcpy(&buf[pos], &_ws->dns_target_ip, 4); pos += 4;
  sendto(_ws->dns_sock, buf, pos, 0, (struct sockaddr*)&client, client_len);
}

// --- WiFi initialization ---
static TaskHandle_t _wifi_task_handle = nullptr;
static TaskHandle_t _wifi_info_task_handle = nullptr;
static volatile bool _wifi_task_stop_requested = false;
static volatile bool _wifi_info_task_stop_requested = false;

static bool wpsStart() {
  if (!_ws) return false;
  esp_wps_config_t config = {};
  config.wps_type = WPS_TYPE_PBC;
  strncpy(config.factory_info.manufacturer, "ESPRESSIF", sizeof(config.factory_info.manufacturer) - 1);
  strncpy(config.factory_info.model_number, CONFIG_IDF_TARGET, sizeof(config.factory_info.model_number) - 1);
  strncpy(config.factory_info.model_name, "ESPRESSIF IOT", sizeof(config.factory_info.model_name) - 1);
  strncpy(config.factory_info.device_name, "ESP DEVICE", sizeof(config.factory_info.device_name) - 1);
  strncpy(config.pin, "00000000", sizeof(config.pin) - 1);

  esp_err_t err = esp_wifi_wps_enable(&config);
  if (err != ESP_OK) {
    M5_LOGE("WPS Enable Failed: 0x%x: %s", err, esp_err_to_name(err));
    return false;
  }
  err = esp_wifi_wps_start(0);
  if (err != ESP_OK) {
    M5_LOGE("WPS Start Failed: 0x%x: %s", err, esp_err_to_name(err));
    return false;
  }
  _ws->wps_enabled = true;
  return true;
}

static void wpsStop() {
  if (!_ws) return;
  _ws->wps_enabled = false;
  esp_err_t err = esp_wifi_wps_disable();
  if (err != ESP_OK) {
    M5_LOGE("WPS Disable Failed: 0x%x: %s", err, esp_err_to_name(err));
  }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
      _sta_state = STA_IDLE;
      _sta_connected_ms = 0;
      M5.Log.printf("[wifi] sta start\r\n");
      break;
    case WIFI_EVENT_STA_STOP:
      _sta_state = STA_STOPPED;
      _sta_connected_ms = 0;
      M5.Log.printf("[wifi] sta stop\r\n");
      break;
    case WIFI_EVENT_STA_DISCONNECTED:
      {
        auto* evt = (wifi_event_sta_disconnected_t*)event_data;
        _last_disconnect_reason = evt ? evt->reason : 0;
        _sta_state = STA_DISCONNECTED;
        _sta_connected_ms = 0;
        M5.Log.printf("[wifi] sta disconnected: reason=%u\r\n", (unsigned)_last_disconnect_reason);
      }
      break;
    case WIFI_EVENT_AP_START:
      _ap_started = true;
      _setup_http_seen = false;
      _ap_start_attempts = 0;
      // Publish AP readiness from the event itself. The low-priority info
      // task can be delayed while AMY and the Wi-Fi driver are busy, which
      // otherwise leaves the UI on PREPARING WI-FI even though the AP is up.
      system_registry->runtime_info.setWiFiStationCount(_ap_station_count);
      system_registry->runtime_info.setWiFiAPInfo(
        def::command::wifi_ap_info_t::wai_waiting);
      M5.Log.printf("[wifi] ap start\r\n");
      M5_LOGI("[wifi-timing] t=+%lu WIFI_EVENT_AP_START",
              (unsigned long)(M5.millis() - _setup_t0_ms));
      break;
    case WIFI_EVENT_AP_STOP:
      _ap_started = false;
      _ap_station_count = 0;
      _setup_http_seen = false;
      system_registry->runtime_info.setWiFiStationCount(0);
      system_registry->runtime_info.setWiFiAPInfo(
        def::command::wifi_ap_info_t::wai_off);
      M5.Log.printf("[wifi] ap stop\r\n");
      break;
    case WIFI_EVENT_AP_STACONNECTED:
      _ap_station_count = _ap_station_count + 1;
      system_registry->runtime_info.setWiFiStationCount(_ap_station_count);
      system_registry->runtime_info.setWiFiAPInfo(
        def::command::wifi_ap_info_t::wai_enabled);
      M5.Log.printf("[wifi] ap client connected: count=%d\r\n", _ap_station_count);
      M5_LOGI("[wifi-timing] t=+%lu WIFI_EVENT_AP_STACONNECTED (count=%d)",
              (unsigned long)(M5.millis() - _setup_t0_ms), _ap_station_count);
      M5.Log.printf("WIFI_AUTH connected free=%u largest=%u\r\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      // Wake the worker immediately so HTTP/DNS can be created only after
      // this authentication event, without waiting for its periodic tick.
      if (_wifi_task_handle) { xTaskNotifyGive(_wifi_task_handle); }
      break;
    case WIFI_EVENT_AP_STADISCONNECTED:
      if (_ap_station_count > 0) _ap_station_count = _ap_station_count - 1;
      system_registry->runtime_info.setWiFiStationCount(_ap_station_count);
      system_registry->runtime_info.setWiFiAPInfo(
        _ap_station_count > 0
          ? def::command::wifi_ap_info_t::wai_enabled
          : def::command::wifi_ap_info_t::wai_waiting);
      M5.Log.printf("[wifi] ap client disconnected: count=%d\r\n", _ap_station_count);
      if (_wifi_task_handle) { xTaskNotifyGive(_wifi_task_handle); }
      break;
    case WIFI_EVENT_SCAN_DONE:
      {
        uint16_t count = 0;
        esp_wifi_scan_get_ap_num(&count);
        _scan_status = (int)count;
        if (_wifi_task_handle) {
          // タスク側で records 取得＆キャッシュ更新させる
          xTaskNotifyGive(_wifi_task_handle);
        }
      }
      break;
    case WIFI_EVENT_STA_WPS_ER_SUCCESS:
      {
        auto* evt = (wifi_event_sta_wps_er_success_t*)event_data;
        if (evt && evt->ap_cred_cnt > 0) {
          wifi_config_t sta_config = {};
          memcpy(sta_config.sta.ssid, evt->ap_cred[0].ssid, sizeof(sta_config.sta.ssid));
          memcpy(sta_config.sta.password, evt->ap_cred[0].passphrase, sizeof(sta_config.sta.password));
          esp_wifi_set_config(WIFI_IF_STA, &sta_config);
          wifi_save_sta_config(sta_config);
        }
        wpsStop();
        system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
        system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_enable_sta);
      }
      break;
    case WIFI_EVENT_STA_WPS_ER_FAILED:
    case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
      wpsStop();
      if (system_registry->wifi_control.getOperation() == def::command::wifi_operation_t::wfop_setup_wps) {
        wpsStart();
      }
      break;
    default:
      break;
    }
  } else if (event_base == IP_EVENT) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
      _sta_state = STA_CONNECTED;
      _sta_connected_ms = M5.millis();
      _last_disconnect_reason = 0;
      // Publish connectivity immediately. The low-priority signal-strength
      // task may run later under AMY load, which previously let the File
      // Editor deadline expire just after DHCP succeeded.
      system_registry->runtime_info.setWiFiSTAInfo(
        def::command::wifi_sta_info_t::wsi_signal_1);
      auto* evt = (ip_event_got_ip_t*)event_data;
      if (evt) {
        M5.Log.printf("[wifi] got ip: " IPSTR "\r\n", IP2STR(&evt->ip_info.ip));
      } else {
        M5.Log.printf("[wifi] got ip\r\n");
      }
    } else if (event_id == IP_EVENT_AP_STAIPASSIGNED) {
      auto* evt = (ip_event_ap_staipassigned_t*)event_data;
      if (evt) {
        M5.Log.printf("[wifi] ap assigned ip: " IPSTR "\r\n", IP2STR(&evt->ip));
      } else {
        M5.Log.printf("[wifi] ap assigned ip\r\n");
      }
    }
  }

  if (_wifi_info_task_handle) {
    xTaskNotify(_wifi_info_task_handle, event_id, eSetValueWithOverwrite);
  }
}

// esp_netif_init / esp_event_loop_create_default はプロセスで一度だけ行う。
// (esp_netif_deinit は IDF 側で未サポートのため再 init 不可)
static bool _netif_inited = false;
static void ensure_netif_subsystem() {
  if (_netif_inited) return;
  _netif_inited = true;
  esp_netif_init();
  esp_event_loop_create_default();
}

// Wi-Fi 有効化に伴い wifi_state_t を確保し、ドライバを初期化する。
static bool wifi_state_create() {
  if (_ws) return true;
  _setup_t0_ms = M5.millis();
  M5_LOGI("[wifi-timing] t=0 wifi_state_create begin");
  ensure_netif_subsystem();

  _ws = new wifi_state_t();
  _ws->ssid_cache_mutex = xSemaphoreCreateMutex();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  // KANTAN Playでは高帯域WiFiは不要(OTAと設定UIのみ)なのでバッファを削減
  cfg.static_rx_buf_num = 4;    // default 8 → 4 (DMA内部RAM、1.6KB/個 → 約6KB節約)
  cfg.dynamic_rx_buf_num = 16;  // default 32 → 16 (PSRAM)
  cfg.rx_ba_win = 4;            // default 6 → 4 (内部RAM、約3KB節約)
  esp_err_t init_err = esp_wifi_init(&cfg);
  if (init_err != ESP_OK) {
    M5_LOGE("[wifi] esp_wifi_init failed: 0x%x", init_err);
    if (_ws->ssid_cache_mutex) {
      vSemaphoreDelete(_ws->ssid_cache_mutex);
    }
    delete _ws;
    _ws = nullptr;
    return false;
  }
  esp_wifi_set_storage(WIFI_STORAGE_FLASH);

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr);

  M5_LOGI("[wifi-timing] t=+%lu wifi_state_create end (driver initialized)",
          (unsigned long)(M5.millis() - _setup_t0_ms));
  return true;
}

// 無線通信を停止する。ESP-IDFのWi-Fi driver/default netif をFile EditorやOTAの
// セッションごとに deinit/destroy すると、イベントハンドラとTCP接続の後始末が
// 競合し、次回起動時にクラッシュすることがある。ドライバは一度だけ初期化して
// 保持し、演奏中は radio を止めるだけにする。
// 呼び出し元(task_func)は事前に HTTP/mDNS/DNS サーバを停止してから呼ぶこと。
static void wifi_state_stop() {
  if (!_ws) return;

  // Wi-Fi停止だけなら、次回のesp_wifi_startで同じnetif/イベントハンドラを
  // 安全に再利用できる。Wi-Fiスタックの確保量は少し残るが、通信負荷はゼロになる。
  if (_ws->wifi_started) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    _ws->wifi_started = false;
  }

  // 参照されうる軽量 static の初期化
  _sta_state = STA_STOPPED;
  _ap_started = false;
  _ap_station_count = 0;
  _ap_start_requested_ms = 0;
  _ap_start_attempts = 0;
  _last_disconnect_reason = 0;
  _last_connect_attempt_ms = 0;
  _scan_status = -2;
}

// netifは使用するモードのものだけ作成する
static void wifi_ensure_sta_netif() {
  if (_ws && !_ws->sta_netif) {
    _ws->sta_netif = esp_netif_create_default_wifi_sta();
  }
}
static void wifi_ensure_ap_netif() {
  if (_ws && !_ws->ap_netif) {
    _ws->ap_netif = esp_netif_create_default_wifi_ap();
  }
}


static esp_err_t response_redirect(httpd_req_t *req, const char *location)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t response_top_handler(httpd_req_t *req)
{
  _setup_http_seen = true;
  M5.Log.printf("[wifi] http GET /\r\n");
  auto operation = system_registry->wifi_control.getOperation();
  if (operation == def::command::wifi_operation_t::wfop_setup_ap) {
    return response_redirect(req, "/wifi");
  }
  return response_redirect(req, "/main");
}

static esp_err_t response_wifi_handler(httpd_req_t *req)
{
  _setup_http_seen = true;
  M5.Log.printf("[wifi] http GET /wifi\r\n");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html_wifi, (uint32_t)sizeof_html_wifi);
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

static esp_err_t response_main_handler(httpd_req_t *req)
{
#if defined(KANPLAY_SAMPLER)
  const char* base = KANPLAY_SAMPLER_UI_BASE;
#else
  // developer mode ON のときだけ別ホストへ向ける (アカウント切替で UI 検証用)
  const char* base = system_registry->runtime_info.getDeveloperMode()
                   ? KANPLAY_UI_BASE_DEV
                   : KANPLAY_UI_BASE;
#endif
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req,
    "<!doctype html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>"
#if defined(KANPLAY_SAMPLER)
    "KANTAN Sampler"
#else
    "KANTAN Play"
#endif
    "</title>"
    "<link rel=\"stylesheet\" href=\"");
  httpd_resp_sendstr_chunk(req, base);
  httpd_resp_sendstr_chunk(req,
    "/app.css?v=104-kts2-layer2\"></head><body>"
    // sampler-ui/index.htmlと同じ最小シェルを本体側で返す。CSS/JSはGitHub
    // Pagesから読み、APIだけを本体のlocation.originへ向ける。
#if defined(KANPLAY_SAMPLER)
    "<main><header><h1>KANTAN Sampler Editor</h1>"
    "<span id=\"status\">Connecting…</span><button id=\"refresh\" title=\"Refresh\">Refresh</button>"
    "</header><nav class=\"tabs\" aria-label=\"Editor views\">"
    "<button class=\"tab active\" data-view=\"sample-view\">Sample</button>"
    "<button class=\"tab\" data-view=\"beat-view\">Beat</button>"
    "<button class=\"tab\" data-view=\"kit-view\">Kit</button>"
    "<button class=\"tab\" data-view=\"project-view\">Project</button>"
    "<button class=\"tab\" data-view=\"music-view\">Music</button>"
    "<button class=\"tab\" data-view=\"synth-view\">Synth</button>"
    "</nav><section id=\"sample-view\" class=\"view active\"></section>"
    "<section id=\"beat-view\" class=\"view\"></section>"
    "<section id=\"kit-view\" class=\"view\"></section>"
    "<section id=\"project-view\" class=\"view\"></section>"
    "<section id=\"music-view\" class=\"view\"></section>"
    "<section id=\"synth-view\" class=\"view\"></section></main>"
#else
    "<div id=\"app\" style=\"font-family:sans-serif;padding:16px\">Loading…</div>"
#endif
    "<script>window.KANPLAY={api:location.origin};</script>"
    "<script type=\"module\" src=\"");
  httpd_resp_sendstr_chunk(req, base);
  httpd_resp_sendstr_chunk(req, "/app.js?v=105-edit-synth-ui\"></script></body></html>");
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

static esp_err_t response_ctrl_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");

  struct btnctrl_t {
    def::command::command_param_t command;
    const char* keycode_array;
  };
  using namespace def::command;
  static constexpr const btnctrl_t btnctrl_table[] = {
    {{internal_button,11}, "qQ7"}, {{internal_button,12}, "wW8"}, {{internal_button,13}, "eE9"}, {{internal_button,14}, "rR" }, {{internal_button,15}, "tT"},
    {{internal_button, 6}, "aA4"}, {{internal_button, 7}, "sS5"}, {{internal_button, 8}, "dD6"}, {{internal_button, 9}, "fF" }, {{internal_button,10}, "gG"},
    {{internal_button, 1}, "zZ1"}, {{internal_button, 2}, "xX2"}, {{internal_button, 3}, "cC3"}, {{internal_button, 4}, "vV0"}, {{internal_button, 5}, "bB"},
  };

  httpd_resp_sendstr_chunk(req,
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">\n"
    "<script>\n"
    "let ct={"
  );

  char linebuf[16];
  for (auto& btn : btnctrl_table) {
    int i = 0;
    do {
      snprintf(linebuf, sizeof(linebuf), "'%c':%d,", btn.keycode_array[i], btn.command.raw);
      httpd_resp_sendstr_chunk(req, linebuf);
    } while (btn.keycode_array[++i]);
    httpd_resp_sendstr_chunk(req, "\n");
  }
  httpd_resp_sendstr_chunk(req,
    "};\n"
    "const ws=new WebSocket('/ws');"
    "document.addEventListener('keydown',function(e){ if(!e.repeat&&e.key in ct){ ws.send('cmd=p'+ct[e.key]); } });\n"
    "document.addEventListener('keyup',function(e){ if(!e.repeat&&e.key in ct){ ws.send('cmd=r'+ct[e.key]); } });\n"
    "</script>\n"
    "</head><body>KEYBOARD CONTROL</body></html>\n"
  );
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

// スキャン結果（wifi_ap_record_t）を SSID キャッシュへマージする。
// ・キャッシュはクリアせず、既存エントリに追記・更新する
// ・同一 SSID は最新の RSSI と last_seen_ms に更新
// ・TTL を超えたエントリは捨てる
// ・溢れた場合は (1) 期限切れ (2) 最弱 RSSI の順に捨てる
// ・最後に RSSI 降順で整列
static void ssid_cache_merge(const wifi_ap_record_t* recs, uint16_t count)
{
  if (!_ws || !_ws->ssid_cache_mutex) return;
  uint32_t now = M5.millis();
  if (now == 0) now = 1;
  xSemaphoreTake(_ws->ssid_cache_mutex, portMAX_DELAY);
  auto* cache = _ws->ssid_cache;

  // 1) TTL 経過エントリを除去（前詰め）
  uint8_t w = 0;
  for (uint8_t r = 0; r < _ws->ssid_cache_count; ++r) {
    if ((now - cache[r].last_seen_ms) < SSID_CACHE_TTL_MS) {
      if (w != r) cache[w] = cache[r];
      ++w;
    }
  }
  _ws->ssid_cache_count = w;

  // 2) 新規スキャン結果をマージ
  for (uint16_t i = 0; i < count; ++i) {
    const char* ssid = (const char*)recs[i].ssid;
    if (ssid[0] == 0) continue; // 非公開SSIDはスキップ
    int8_t rssi = recs[i].rssi;

    bool dup = false;
    for (uint8_t j = 0; j < _ws->ssid_cache_count; ++j) {
      if (strncmp(cache[j].ssid, ssid, 32) == 0) {
        cache[j].rssi = rssi;
        cache[j].last_seen_ms = now;
        dup = true;
        break;
      }
    }
    if (dup) continue;

    if (_ws->ssid_cache_count < SSID_CACHE_MAX) {
      auto& e = cache[_ws->ssid_cache_count];
      strncpy(e.ssid, ssid, 32);
      e.ssid[32] = 0;
      e.rssi = rssi;
      e.last_seen_ms = now;
      _ws->ssid_cache_count = _ws->ssid_cache_count + 1;
    } else {
      // 最弱 RSSI のエントリを探して、より強ければ置換
      uint8_t min_idx = 0;
      for (uint8_t j = 1; j < SSID_CACHE_MAX; ++j) {
        if (cache[j].rssi < cache[min_idx].rssi) min_idx = j;
      }
      if (rssi > cache[min_idx].rssi) {
        auto& e = cache[min_idx];
        strncpy(e.ssid, ssid, 32);
        e.ssid[32] = 0;
        e.rssi = rssi;
        e.last_seen_ms = now;
      }
    }
  }

  // 3) RSSI 降順に挿入ソート
  for (uint8_t i = 1; i < _ws->ssid_cache_count; ++i) {
    ssid_cache_entry_t key = cache[i];
    int j = (int)i - 1;
    while (j >= 0 && cache[j].rssi < key.rssi) {
      cache[j + 1] = cache[j];
      --j;
    }
    cache[j + 1] = key;
  }
  xSemaphoreGive(_ws->ssid_cache_mutex);
}

static void ssid_cache_clear(void)
{
  if (!_ws || !_ws->ssid_cache_mutex) return;
  xSemaphoreTake(_ws->ssid_cache_mutex, portMAX_DELAY);
  _ws->ssid_cache_count = 0;
  xSemaphoreGive(_ws->ssid_cache_mutex);
}

static esp_err_t response_ssid_handler(httpd_req_t *req) {
  _setup_http_seen = true;
  // その時点で持っているキャッシュを即時返却する（待機しない）。
  // 継続的な再スキャンはタスク側で走っているので、呼び出すたびに最新が返る。
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr_chunk(req, "{\"ssids\":[");

  if (_ws && _ws->ssid_cache_mutex) {
    xSemaphoreTake(_ws->ssid_cache_mutex, portMAX_DELAY);
    for (uint8_t i = 0; i < _ws->ssid_cache_count; ++i) {
      // JSON 用に " と \ のみ最低限エスケープ
      const char* s = _ws->ssid_cache[i].ssid;
      httpd_resp_sendstr_chunk(req, (i == 0) ? "\"" : ",\"");
      const char* p = s;
      const char* start = p;
      while (*p) {
        if (*p == '"' || *p == '\\') {
          if (p > start) httpd_resp_send_chunk(req, start, p - start);
          httpd_resp_sendstr_chunk(req, "\\");
          httpd_resp_send_chunk(req, p, 1);
          start = p + 1;
        }
        ++p;
      }
      if (p > start) httpd_resp_send_chunk(req, start, p - start);
      httpd_resp_sendstr_chunk(req, "\"");
    }
    xSemaphoreGive(_ws->ssid_cache_mutex);
  }

  httpd_resp_sendstr_chunk(req, "]}");
  httpd_resp_sendstr_chunk(req, nullptr);
  return ESP_OK;
}

static std::string url_decode(const std::string& str) {
  std::string decoded;
  for (int i = 0; i < str.length(); i++) {
    char ch = str[i];
    if (ch == '%') {
      int ii;
      sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
      ch = static_cast<char>(ii);
      i += 2;
    } else if (ch == '+') {
      ch = ' ';
    }
    decoded += ch;
  }
  return decoded;
}

static esp_err_t response_post_wifi_handler(httpd_req_t *req) {
  const int len = req->content_len;
  std::string ssid, password;
  esp_err_t res = ESP_ERR_INVALID_ARG;
  {
    if (len <= 0 || len > 256) {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid Wi-Fi form");
      return ESP_FAIL;
    }
    std::vector<char> res_buf (len+1, 0);
    int received = 0;
    while (received < len) {
      int ret = httpd_req_recv(req, res_buf.data() + received, len - received);
      if (ret <= 0) {
        return ESP_FAIL;
      }
      received += ret;
    }
    std::vector<char> buf (len+1, 0);
    memset(buf.data(), 0, buf.size());
    res = httpd_query_key_value(res_buf.data(), "s", buf.data(), buf.size());
    if (res == ESP_OK) {
      ssid = url_decode(buf.data());
      memset(buf.data(), 0, buf.size());
      res = httpd_query_key_value(res_buf.data(), "p", buf.data(), buf.size());
      if (res == ESP_OK) {
        password = url_decode(buf.data());
      }
    }
  }

  if (res == ESP_OK && !ssid.empty()) {
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid.c_str(), sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, password.c_str(), sizeof(sta_config.sta.password) - 1);

    // The HTTP handler only persists credentials.  Then it closes the setup
    // AP and lets task_func bring up a clean STA-only connection.  On some AP
    // channels an APSTA handover can fail even with valid credentials.
    wifi_save_sta_config(sta_config);
    wifi_config_t saved_config = {};
    if (!wifi_load_sta_config(&saved_config)
     || strncmp((const char*)saved_config.sta.ssid, (const char*)sta_config.sta.ssid,
                sizeof(sta_config.sta.ssid)) != 0
     || strncmp((const char*)saved_config.sta.password, (const char*)sta_config.sta.password,
                sizeof(sta_config.sta.password)) != 0) {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wi-Fi configuration could not be saved");
      return ESP_FAIL;
    }

    // Return a final page before taking the setup AP down.  Redirecting back
    // to /wifi races the STA-only transition and can leave a stale failure
    // state visible in the phone browser.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
      "<!DOCTYPE html><html><head><meta charset=UTF-8>"
      "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
      "</head><body style=\"font-family:sans-serif;text-align:center;padding:12vw 8vw\">"
      "<h2>Wi-Fi settings saved</h2><p>The device is connecting.</p>"
      "<p>You can return to KANTAN Sampler.</p></body></html>");
    uint32_t now = M5.millis();
    _connect_start_ms = now ? now : 1;
    _last_disconnect_reason = 0;
    _sta_state = STA_IDLE;
    system_registry->wifi_control.setWifiMode(
      def::command::wifi_mode_t::wifi_enable_sta);
    system_registry->wifi_control.setWebServerMode(
      def::command::webserver_mode_t::ws_disable);
    system_registry->wifi_control.setOperation(
      def::command::wifi_operation_t::wfop_disable);
    return ESP_OK;
  }
  httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID is required");
  return ESP_FAIL;
}

static esp_err_t response_status_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/json");

  const char* state_str = "unknown";
  switch (_sta_state) {
    case STA_STOPPED:   state_str = "stopped"; break;
    case STA_IDLE:      state_str = "connecting"; break;
    case STA_CONNECTED: state_str = "connected"; break;
    case STA_DISCONNECTED:
    default: {
      // esp_wifi_disconnect() issued just before a new setup attempt can emit
      // a delayed disconnect event.  Do not turn that old event into a false
      // password error while the fresh STA connection is still being made.
      uint32_t start = _connect_start_ms;
      uint32_t now = M5.millis();
      if (start != 0 && (now - start) < STA_CONNECT_TIMEOUT_MS) {
        state_str = "connecting";
      } else {
        state_str = "failed";
      }
      break;
    }
  }

  char ssid_buf[66] = {}; // 32文字 × エスケープ最大 2倍 + 終端
  {
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
      const uint8_t* src = cfg.sta.ssid;
      size_t j = 0;
      for (size_t i = 0; i < 32 && src[i] && j + 2 < sizeof(ssid_buf); ++i) {
        char c = (char)src[i];
        if (c == '"' || c == '\\') ssid_buf[j++] = '\\';
        ssid_buf[j++] = c;
      }
      ssid_buf[j] = 0;
    }
  }

  char ip_buf[16] = "";
  if (_ws && _ws->sta_netif) {
    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_get_ip_info(_ws->sta_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
      snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ip_info.ip));
    }
  }

  // attempted: 一度でも POST /wifi で接続試行を開始したかどうか
  const char* attempted = (_connect_start_ms != 0) ? "true" : "false";

  char out[192];
  int n = snprintf(out, sizeof(out),
    "{\"attempted\":%s,\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
    attempted, state_str, ssid_buf, ip_buf);
  if (n < 0) n = 0;
  httpd_resp_send(req, out, n);
  return ESP_OK;
}

static esp_err_t response_done_handler(httpd_req_t *req) {
  // A phone may keep the setup page alive and resend /done when the later
  // File Editor server appears at the same address. Only Setup AP owns this
  // endpoint; otherwise the stale request used to shut down File Editor as
  // soon as its STA connection completed.
  if (system_registry->wifi_control.getOperation()
        != def::command::wifi_operation_t::wfop_setup_ap) {
    M5.Log.printf("[wifi] ignored stale POST /done\r\n");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ignored");
    return ESP_OK;
  }
  // Setup 完了後は STA 接続だけを残し、設定用 Web サーバは停止する
  system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_enable_sta);
  system_registry->wifi_control.setWebServerMode(def::command::webserver_mode_t::ws_disable);
  system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req,
    "<!DOCTYPE html><html><head><meta charset=UTF-8></head>"
    "<body style=\"font-family:sans-serif;text-align:center;padding:10vw\">"
    "<h2>Setup finished</h2></body></html>");
  return ESP_OK;
}


static esp_err_t response_ws_handler(httpd_req_t *req)
{
  if (req->method == HTTP_GET) {
    return ESP_OK;
  }
  httpd_ws_frame_t ws_pkt;
  uint8_t *buf = nullptr;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    return ret;
  }
  if (ws_pkt.len) {
    buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
    if (buf == nullptr) {
      return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      free(buf);
      return ret;
    }
    if (memcmp(buf, "cmd=", 4) == 0) {
      bool press = (buf[4] == 'p');
      def::command::command_param_t cmd;
      cmd.raw = atoi((const char*)&buf[5]);
      system_registry->operator_command.addQueue(cmd, press);
    }
  }
  free(buf);
  return ret;
}

static constexpr const httpd_uri uri_table[] = {
  { "/"    , HTTP_GET , response_top_handler      , nullptr, false, false, nullptr },
  { "/wifi", HTTP_GET , response_wifi_handler     , nullptr, false, false, nullptr },
  { "/main", HTTP_GET , response_main_handler     , nullptr, false, false, nullptr },
  { "/ctrl", HTTP_GET , response_ctrl_handler     , nullptr, false, false, nullptr },
  { "/ssid"   , HTTP_GET , response_ssid_handler     , nullptr, false, false, nullptr },
  { "/status" , HTTP_GET , response_status_handler   , nullptr, false, false, nullptr },
  { "/wifi"   , HTTP_POST, response_post_wifi_handler, nullptr, false, false, nullptr },
  { "/done"   , HTTP_POST, response_done_handler     , nullptr, false, false, nullptr },
  { "/ws"     , HTTP_GET , response_ws_handler       , nullptr,  true, false, nullptr },
  // /api/* 系は task_wifi/task_wifi_api.cpp で別途登録
};

static httpd_handle_t start_webserver(void)
{
  httpd_handle_t server = NULL;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // /api/files/* など `*` を含むパターンを使うためにワイルドカード一致を有効化
  config.uri_match_fn = httpd_uri_match_wildcard;
  // uri_table[] の数に応じてハンドラ上限を引き上げる
  config.max_uri_handlers = 32;

  M5.Log.printf("[wifi] http start: free=%u largest=%u\r\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const esp_err_t start_err = httpd_start(&server, &config);
  if (start_err == ESP_OK) {
    M5.Log.printf("[wifi] http server ready\r\n");

    for (auto& uri : uri_table) {
      httpd_register_uri_handler(server, &uri);
    }
    task_wifi_api_register_uris(server);
#if defined(KANPLAY_SAMPLER)
    sampler_ns::sampler_web_api_register_uris(server);
#endif

    return server;
  }

  M5.Log.printf("[wifi] http start failed: err=0x%x %s free=%u largest=%u\r\n",
                start_err, esp_err_to_name(start_err),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return nullptr;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
  if (server) {
#if defined(KANPLAY_SAMPLER)
    sampler_ns::sampler_web_api_unregister_uris(server);
#endif
    task_wifi_api_unregister_uris(server);
    for (auto& uri : uri_table) {
      httpd_unregister_uri(server, uri.uri);
    }
    return httpd_stop(server);
  }
  return ESP_OK;
}

static constexpr const size_t http_port = 80;

static void log_wifi_web_memory(const char* phase)
{
  M5.Log.printf("WIFI_WEB %s free=%u largest=%u\r\n",
                phase ? phase : "-",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static bool wifi_web_services_active()
{
  return _ws && (_ws->http_server != nullptr || _ws->dns_sock >= 0
              || _ws->mdns_started);
}

static bool start_wifi_web_services(bool setup_ap)
{
  if (!_ws) { return false; }
  if (_ws->http_server != nullptr) { return true; }

  log_wifi_web_memory(setup_ap ? "setup-start-before" : "file-start-before");
  _ws->http_server = start_webserver();
  if (_ws->http_server == nullptr) {
    log_wifi_web_memory("start-failed");
    return false;
  }

  if (setup_ap) {
    // Captive DNS is useful only after WPA authentication has completed. It
    // deliberately follows HTTP startup and mDNS is never created for AP.
    if (_ws->ap_netif) {
      esp_netif_ip_info_t ip_info;
      if (esp_netif_get_ip_info(_ws->ap_netif, &ip_info) == ESP_OK) {
        dns_server_start(ip_info.ip.addr);
      }
    }
  } else {
    dns_server_stop();
    if (!_ws->mdns_started) {
      const esp_err_t mdns_err = mdns_init();
      _ws->mdns_started = mdns_err == ESP_OK;
      if (_ws->mdns_started) {
        mdns_hostname_set(def::app::wifi_mdns);
        mdns_service_add(nullptr, "_http", "_tcp", http_port, nullptr, 0);
      } else {
        M5.Log.printf("[wifi] mDNS skipped: 0x%x %s\r\n",
                      mdns_err, esp_err_to_name(mdns_err));
      }
    }
  }
  log_wifi_web_memory(setup_ap ? "setup-start-after" : "file-start-after");
  return true;
}

static void stop_wifi_web_services(const char* reason)
{
  if (!_ws || !wifi_web_services_active()) { return; }
  M5.Log.printf("[wifi] web services stop: %s\r\n", reason ? reason : "-");
  log_wifi_web_memory("stop-before");

  // Keep this order stable: server tasks/sockets first, then name services,
  // and only then may the caller stop the radio.
  if (_ws->http_server) {
    const esp_err_t err = stop_webserver(_ws->http_server);
    _ws->http_server = nullptr;
    M5.Log.printf("[wifi] http stop: 0x%x %s\r\n", err, esp_err_to_name(err));
    M5.delay(16);
  }
  dns_server_stop();
  if (_ws->mdns_started) {
    mdns_free();
    _ws->mdns_started = false;
  }
  // httpd and lwIP release their worker allocations asynchronously relative
  // to the control registry. Give them a scheduling window before radio stop.
  M5.delay(32);
  log_wifi_web_memory("stop-after");
}

static void task_wifi_info(void*) {
  bool ntp_sync = false;
  bool sntp_inited = false;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, 1000);
    if (_wifi_info_task_stop_requested) { break; }
    {
      def::command::wifi_sta_info_t wifi_sta_info = def::command::wifi_sta_info_t::wsi_error;
      switch (_sta_state) {
      case STA_CONNECTED:
        {
          // SNTP初期化はWiFi接続確立後に行う（TCP/IPスタックの初期化が必要なため）
          if (!sntp_inited) {
            sntp_inited = true;
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, def::ntp::server1);
            esp_sntp_setservername(1, def::ntp::server2);
            esp_sntp_setservername(2, def::ntp::server3);
            esp_sntp_init();
          }
          wifi_ap_record_t ap_info;
          if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            int rssi = ((ap_info.rssi + 127) >> 5) + 1;
            if (rssi < 0) rssi = 0;
            wifi_sta_info = (def::command::wifi_sta_info_t)rssi;
          }
          if (!ntp_sync) {
            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
              ntp_sync = true;
              system_registry->runtime_info.setSntpSync(true);
            }
          }
        }
        break;
      case STA_STOPPED:
        wifi_sta_info = def::command::wifi_sta_info_t::wsi_off;
        break;
      case STA_IDLE:
      case STA_DISCONNECTED:
        wifi_sta_info = def::command::wifi_sta_info_t::wsi_waiting;
        break;
      }
      system_registry->runtime_info.setWiFiSTAInfo(wifi_sta_info);
    }
    {
      def::command::wifi_ap_info_t wifi_ap_info = def::command::wifi_ap_info_t::wai_off;
      if (_ap_started) {
        if (_ap_station_count > 0) {
          wifi_ap_info = def::command::wifi_ap_info_t::wai_enabled;
        } else {
          wifi_ap_info = def::command::wifi_ap_info_t::wai_waiting;
        }
        system_registry->runtime_info.setWiFiStationCount(_ap_station_count);
      }
      system_registry->runtime_info.setWiFiAPInfo(wifi_ap_info);
    }
  }
  _wifi_info_task_handle = nullptr;
  vTaskDelete(nullptr);
}

bool task_wifi_t::start(void)
{
#if defined (M5UNIFIED_PC_BUILD)
  auto thread = SDL_CreateThread((SDL_ThreadFunction)task_func, "wifi", this);
#else
  // Make start() idempotent so deferred Wi-Fi startup can create these tasks
  // after BLE has released its controller memory. Wi-Fi/driver call paths keep
  // their stacks in internal RAM; PSRAM stacks destabilize low-level locks.
  BaseType_t info_result = _wifi_info_task_handle ? pdPASS : pdFAIL;
  BaseType_t wifi_result = _wifi_task_handle ? pdPASS : pdFAIL;
  if (_wifi_info_task_handle == nullptr) {
    _wifi_info_task_stop_requested = false;
    info_result = xTaskCreatePinnedToCore(
      task_wifi_info, "wi", 2048, this, 0, &_wifi_info_task_handle,
      def::system::task_cpu_wifi);
  }
  if (_wifi_task_handle == nullptr) {
    _wifi_task_stop_requested = false;
    wifi_result = xTaskCreatePinnedToCore(
      (TaskFunction_t)task_func, "wifi", 4096, this,
      def::system::task_priority_wifi, &_wifi_task_handle,
      def::system::task_cpu_wifi);
  }
  M5.Log.printf("[wifi] task start info=%ld worker=%ld info_handle=%p worker_handle=%p\r\n",
                (long)info_result, (long)wifi_result,
                _wifi_info_task_handle, _wifi_task_handle);
  if (_wifi_task_handle != nullptr) {
    system_registry->wifi_control.setNotifyTaskHandle(_wifi_task_handle);
  }
  return _wifi_info_task_handle != nullptr && _wifi_task_handle != nullptr;
#endif
}

bool task_wifi_t::stop(void)
{
#if !defined (M5UNIFIED_PC_BUILD)
  if (_wifi_info_task_handle == nullptr && _wifi_task_handle == nullptr) {
    return true;
  }

  // The caller waits until task_func has fully stopped the radio/server and
  // published both runtime states as off. Each worker must delete itself;
  // deleting it externally while it leaves ulTaskNotifyTake can corrupt the
  // task stack allocation on ESP32-S3.
  system_registry->wifi_control.setNotifyTaskHandle(nullptr);
  _wifi_info_task_stop_requested = true;
  _wifi_task_stop_requested = true;
  if (_wifi_info_task_handle != nullptr) { xTaskNotifyGive(_wifi_info_task_handle); }
  if (_wifi_task_handle != nullptr) { xTaskNotifyGive(_wifi_task_handle); }
  const uint32_t deadline = M5.millis() + 250;
  while ((_wifi_info_task_handle != nullptr || _wifi_task_handle != nullptr)
      && (int32_t)(M5.millis() - deadline) < 0) {
    M5.delay(1);
  }
  const bool stopped = _wifi_info_task_handle == nullptr
                    && _wifi_task_handle == nullptr;
  M5.Log.printf("[wifi] tasks stop complete=%u\r\n", (unsigned)stopped);
  return stopped;
#else
  return true;
#endif
}

// -----------------------------------------------------------------------------
// task_func 内部で使う低レベルヘルパー群
// -----------------------------------------------------------------------------

// AP にステーションが接続した後、SSID スキャンを有効にするために
// AP-only モードで立ち上がっている Wi-Fi を APSTA モードへ昇格させる。
// 昇格は 1 度だけ発生する想定で、以降の呼び出しは何もしない。
static void maybe_upgrade_to_apsta_for_scan()
{
  if (!_ws || !_ws->wifi_started) return;
  wifi_mode_t cur_mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&cur_mode) != ESP_OK) return;
  if (cur_mode == WIFI_MODE_APSTA || cur_mode == WIFI_MODE_STA) return;

  wifi_ensure_sta_netif();
  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  M5_LOGI("[wifi] upgrade AP->APSTA err=0x%x (t=+%lu)",
          err, (unsigned long)(M5.millis() - _setup_t0_ms));
}

// 完了したスキャン結果をキャッシュに取り込み、必要なら次のスキャンをキックする。
// 呼び出し側は「スキャンすべき状況か」の判定 (goal + station count 等) を済ませてから呼ぶ。
static void scan_tick()
{
  if (!_ws) return;

  // (1) 完了したスキャンの取り込み
  if (_scan_status >= 0) {
    uint16_t count = (uint16_t)_scan_status;
    if (count > 0) {
      wifi_ap_record_t* recs = (wifi_ap_record_t*)malloc(count * sizeof(wifi_ap_record_t));
      if (recs) {
        if (esp_wifi_scan_get_ap_records(&count, recs) == ESP_OK) {
          ssid_cache_merge(recs, count);
        }
        free(recs);
      }
    } else {
      // 0件完了: キャッシュは温存（前回結果を捨てない）
      esp_wifi_clear_ap_list();
    }
    _scan_status = -2;
    uint32_t now = M5.millis();
    _ws->last_scan_done_ms = now ? now : 1; // 0 は「未実施」を意味するので避ける
  }

  // (2) 次のスキャンを間隔に従ってキック
  if (_scan_status == -2) {
    uint32_t now = M5.millis();
    uint32_t interval = (_ws->ssid_cache_count == 0) ? 2500 : 12000;
    bool due = (_ws->last_scan_done_ms == 0)
             || ((now - _ws->last_scan_done_ms) >= interval);
    if (due) {
      esp_err_t err = esp_wifi_scan_start(nullptr, false);
      if (err == ESP_OK) {
        _scan_status = -1;
      } else {
        // 失敗時は次 tick で再試行。last_scan_done_ms を進めて連打を防ぐ。
        _ws->last_scan_done_ms = now ? now : 1;
      }
    }
  }
}

// setup_ap (AP モード) の AP 設定を反映する。呼び出しは esp_wifi_start の前。
static void apply_ap_config()
{
  wifi_config_t ap_config = {};
  strncpy((char*)ap_config.ap.ssid, def::app::wifi_ap_ssid, sizeof(ap_config.ap.ssid) - 1);
  strncpy((char*)ap_config.ap.password, def::app::wifi_ap_pass, sizeof(ap_config.ap.password) - 1);
  ap_config.ap.ssid_len = strlen(def::app::wifi_ap_ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
  // Setup is a one-phone operation. Reserving four WPA stations wastes scarce
  // internal RAM and can make the authentication timer allocation fail.
  ap_config.ap.max_connection = 1;
  const esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  M5.Log.printf("[wifi] ap config: err=0x%x %s auth=%d max=%u\r\n",
                err, esp_err_to_name(err), (int)ap_config.ap.authmode,
                (unsigned)ap_config.ap.max_connection);
}

void task_wifi_t::task_func(task_wifi_t* me)
{
  task_http_client_t task_http_client;
  // The HTTP/TLS worker is not used by Setup AP. Keep its 10 KB stack
  // unallocated until an OTA/catalog/connectivity request actually needs it;
  // WPA authentication has a stricter internal-RAM requirement. Each exec
  // method handles task-allocation failure without notifying a null task.
  bool update_check_started = false;
  uint32_t update_check_deadline = 0;
  bool ota_connect_started = false;
  uint32_t ota_connect_deadline = 0;
  uint32_t http_server_retry_not_before = 0;
  // 無線ドライバの起動に失敗した場合、同じ要求のままでも一度破棄して再試行する。
  // 失敗した個体がAPなしの状態で固定されないよう、試行間隔は1秒に抑える。
  bool radio_reconfigure_pending = false;
  uint32_t radio_reconfigure_not_before = 0;

  // Wi-Fi サブシステムの「望む状態」を 1 つの値で表現するビットフラグ集合。
  // システムレジストリ (wifi_mode / operation / webserver_mode) の組み合わせから
  // compute_from_registry() でこの構造体が計算され、task_func はこれと直前の値の
  // 差分に基づいて必要な起動/停止アクションを発行する。
  struct wifi_goal_t {
    union {
      struct {
        uint8_t ap_enabled   : 1; // AP インタフェースを立ち上げる
        uint8_t sta_enabled  : 1; // STA インタフェースを立ち上げ外部 AP へ接続する
        uint8_t http_server  : 1; // HTTP サーバ (設定 UI) を起動する
        uint8_t ssid_scan    : 1; // setup UI 向けに SSID スキャンリストを保持する
        uint8_t wps          : 1; // WPS プッシュボタン接続モード
      };
      uint8_t raw = 0;
    };
    // レジストリの 3 つの入力値から望む状態を組み立てる。
    // 各入力値は独立に足し算されるため、上位の指定 (operation や webserver_mode) は
    // 下位 (wifi_mode) が指定したフラグを解除しない。
    void compute_from_registry(def::command::wifi_mode_t m,
                               def::command::wifi_operation_t o,
                               def::command::webserver_mode_t s) {
      raw = 0;

      // (1) 基本のインタフェース構成
      switch (m) {
      default:
      case def::command::wifi_mode_t::wifi_disable:                               break;
      case def::command::wifi_mode_t::wifi_enable_sta:    sta_enabled = 1;        break;
      case def::command::wifi_mode_t::wifi_enable_ap:     ap_enabled = 1;         break;
      case def::command::wifi_mode_t::wifi_enable_sta_ap: ap_enabled = sta_enabled = 1; break;
      }

      // (2) 現在走っているオペレーション (セットアップ / OTA 等) に必要な構成を追加
      switch (o) {
      default:
      case def::command::wifi_operation_t::wfop_disable:
        break;
      case def::command::wifi_operation_t::wfop_setup_ap:
        // スマホ向け設定モード。ここでは希望状態だけを表す。実際のHTTP、
        // DNS、SSID scanはWPA認証完了イベント後まで遅延する。
        ap_enabled = 1;
        ssid_scan  = 1;
        http_server = 1;
        break;
      case def::command::wifi_operation_t::wfop_setup_wps:
        wps = 1;
        break;
      case def::command::wifi_operation_t::wfop_ota_begin:
      case def::command::wifi_operation_t::wfop_ota_progress:
        // OTA 取得のため STA 接続が必要
        sta_enabled = 1;
        break;
      case def::command::wifi_operation_t::wfop_update_check_begin:
      case def::command::wifi_operation_t::wfop_update_check_progress:
        // 起動時の更新確認は STA のみ。HTTPサーバは起動しない。
        sta_enabled = 1;
        break;
      case def::command::wifi_operation_t::wfop_connectivity_check_begin:
      case def::command::wifi_operation_t::wfop_connectivity_check_progress:
        sta_enabled = 1;
        break;
      case def::command::wifi_operation_t::wfop_web_filer:
        // Web ファイラー: STA 接続 + HTTP サーバのみ (AP は立てない)
        sta_enabled = 1;
        http_server = 1;
        break;
      }

      // (3) 独立した Web サーバ希望 (デバッグ/コントロール UI など)
      switch (s) {
      default: break;
      case def::command::webserver_mode_t::ws_enable:
        ap_enabled = sta_enabled = http_server = 1;
        break;
      }
    }
  };
  wifi_goal_t goal;

  for (;;) {
#if defined (M5UNIFIED_PC_BUILD)
    M5.delay(2048);
#else

    // -------- ウェイト待ち: AP/HTTPサーバ動作中は DNS を細かく捌くため短周期で回す --------
    int wait = 1024;
    if (goal.ap_enabled || goal.http_server) {
      dns_server_process();
      wait = 4;
    }
    taskYIELD();
    ulTaskNotifyTake(pdTRUE, wait);
    if (_wifi_task_stop_requested) { break; }

    // -------- レジストリから新しい goal を計算 --------
    auto mode = system_registry->wifi_control.getWifiMode();
    auto op = system_registry->wifi_control.getOperation();
    auto webserver_mode = system_registry->wifi_control.getWebServerMode();
    wifi_goal_t prev_goal = goal;
    goal.compute_from_registry(mode, op, webserver_mode);
    const bool radio_requested = goal.ap_enabled || goal.sta_enabled || goal.wps;
    const bool setup_ap_waiting_for_auth =
      op == def::command::wifi_operation_t::wfop_setup_ap
      && goal.ap_enabled && _ap_station_count == 0;
    const bool file_editor_waiting_for_sta =
      op == def::command::wifi_operation_t::wfop_web_filer
      && goal.sta_enabled && _sta_state != STA_CONNECTED;
    const bool web_services_may_start = goal.http_server
      && !setup_ap_waiting_for_auth && !file_editor_waiting_for_sta;
    if (!radio_requested) {
      radio_reconfigure_pending = false;
      radio_reconfigure_not_before = 0;
    }
    if (!goal.http_server) { http_server_retry_not_before = 0; }
    const bool retry_radio = radio_reconfigure_pending && radio_requested
                          && (int32_t)(M5.millis() - radio_reconfigure_not_before) >= 0;

    if (op != def::command::wifi_operation_t::wfop_update_check_begin
     && op != def::command::wifi_operation_t::wfop_update_check_progress) {
      update_check_started = false;
      update_check_deadline = 0;
    }
    if (op != def::command::wifi_operation_t::wfop_ota_begin) {
      ota_connect_started = false;
      ota_connect_deadline = 0;
    }

    // A Setup AP which has no authenticated station must retain the maximum
    // contiguous arena for the next WPA handshake. This also handles a phone
    // disconnecting and later reconnecting without restarting the AP.
    if (!web_services_may_start && wifi_web_services_active()) {
      stop_wifi_web_services(setup_ap_waiting_for_auth
        ? "waiting for AP authentication" : "web service no longer requested");
      http_server_retry_not_before = 0;
    }

    // =============================================================================
    // goal が変化した場合、差分に沿って Wi-Fi サブシステムの構成を切り替える。
    // 3 段階に分けて実行する:
    //   Phase 1: 消失するサブシステムの停止 (WPS/HTTP サーバ/スキャン)
    //   Phase 2: ラジオ (AP/STA/WPS) の再構成 or 完全無効化
    //   Phase 3: 新たに必要になったサブシステムの起動 (HTTP サーバ/DNS/mDNS 等)
    // =============================================================================
    if (prev_goal.raw != goal.raw || retry_radio) {
      if (retry_radio) { radio_reconfigure_pending = false; }

      // ---- Phase 1: 消失するサブシステムの停止 ------------------------------
      if (!goal.wps && _ws && _ws->wps_enabled) {
        wpsStop();
      }
      const bool radio_topology_change =
           prev_goal.ap_enabled  != goal.ap_enabled
        || prev_goal.sta_enabled != goal.sta_enabled
        || prev_goal.wps         != goal.wps
        || retry_radio;
      if (radio_topology_change && wifi_web_services_active()) {
        stop_wifi_web_services("radio reconfigure");
      }
      if (prev_goal.ssid_scan && !goal.ssid_scan) {
        esp_wifi_clear_ap_list();
        _scan_status = -2;
        if (_ws) _ws->last_scan_done_ms = 0;
        ssid_cache_clear();
      }

      // ---- Phase 2: ラジオ (AP/STA/WPS) の再構成 ------------------------------
      // AP/STA/WPS のいずれかのフラグが変化した場合のみ Wi-Fi ドライバを作り直す。
      if (prev_goal.ap_enabled  != goal.ap_enabled
       || prev_goal.sta_enabled != goal.sta_enabled
       || prev_goal.wps         != goal.wps
       || retry_radio) {

        // ソフト遷移: AP は継続、WPS も変化なしで、STA フラグだけが変化したケース。
        // この場合は esp_wifi_stop/start を挟まず、AP ビーコンを途切れさせないまま
        // STA インタフェースの on/off だけを行う。(主な用途: setup_ap で端末が
        // ESP の AP に繋がった後、STA 接続が成立してそのまま webserver_mode に
        // 遷移する場合に、スマホの AP 接続を切らないため。)
        bool soft_sta_transition =
             _ws && _ws->wifi_started
          && prev_goal.ap_enabled && goal.ap_enabled
          && !prev_goal.wps && !goal.wps
          && prev_goal.sta_enabled != goal.sta_enabled;

        if (soft_sta_transition) {
          if (goal.sta_enabled) {
            // STA 有効化: 必要なら APSTA モードに昇格し、未接続なら connect
            wifi_ensure_sta_netif();
            wifi_mode_t cur_mode = WIFI_MODE_NULL;
            if (esp_wifi_get_mode(&cur_mode) == ESP_OK && cur_mode != WIFI_MODE_APSTA) {
              esp_wifi_set_mode(WIFI_MODE_APSTA);
            }
            if (_sta_state != STA_CONNECTED) {
              wifi_connect_sta("soft transition");
            }
          } else {
            // STA 無効化: disconnect のみ。AP を維持したいのでモード (APSTA) は
            // そのまま残す。次回 AP を落とす遷移が来たときに完全再構成する。
            esp_wifi_disconnect();
          }
        } else {
          // 通常遷移: stop/start を伴う完全再構成
          if (prev_goal.sta_enabled && !goal.sta_enabled) {
            esp_wifi_disconnect();
          }
          M5.delay(16);

          const bool radio_off = !goal.ap_enabled && !goal.sta_enabled && !goal.wps;
          if (radio_off) {
          // 完全無効化: ドライバと wifi_state_t を解放して heap を返却する
          wifi_state_stop();
          system_registry->task_status.setSuspend(
            system_registry_t::reg_task_status_t::bitindex_t::TASK_WIFI);
        } else {
          system_registry->task_status.setWorking(
            system_registry_t::reg_task_status_t::bitindex_t::TASK_WIFI);

          // Wi-Fi 有効化: 必要なら wifi_state_t を作成してドライバ初期化
          if (!wifi_state_create()) {
            radio_reconfigure_pending = true;
            radio_reconfigure_not_before = M5.millis() + 1000;
            continue;
          }

          if (_ws->wifi_started) {
            esp_wifi_stop();
            _ws->wifi_started = false;
          }

          // 起動時のモード決定ルール:
          //   AP が必要で STA も必要 → APSTA
          //   AP だけ必要 (SSIDスキャンも) → AP-only で立ち上げ、後でステーション
          //     接続を受けてから APSTA へ昇格する (maybe_upgrade_to_apsta_for_scan)
          //   STA のみ → STA
          wifi_mode_t new_mode;
          if (goal.ap_enabled) {
            new_mode = goal.sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
          } else {
            new_mode = WIFI_MODE_STA;
          }

          // 必要な netif だけ作成 (不要な netif のメモリ確保を避ける)
          if (new_mode == WIFI_MODE_STA || new_mode == WIFI_MODE_APSTA) {
            wifi_ensure_sta_netif();
          }
          if (new_mode == WIFI_MODE_AP || new_mode == WIFI_MODE_APSTA) {
            wifi_ensure_ap_netif();
          }
          esp_wifi_set_mode(new_mode);

          if (goal.ap_enabled) {
            apply_ap_config();
          }

          esp_err_t start_err = esp_wifi_start();
          _ws->wifi_started = start_err == ESP_OK;
          if (start_err != ESP_OK) {
            M5_LOGE("[wifi] esp_wifi_start failed: 0x%x", start_err);
            // 失敗したドライバ状態を残さず、同じ要求で完全初期化し直す。
            wifi_state_stop();
            radio_reconfigure_pending = true;
            radio_reconfigure_not_before = M5.millis() + 1000;
            continue;
          } else {
            _ap_start_requested_ms = goal.ap_enabled ? M5.millis() : 0;
            _ap_start_attempts = goal.ap_enabled ? 1 : 0;
            M5_LOGI("[wifi-timing] t=+%lu esp_wifi_start (mode=%d)",
                    (unsigned long)(M5.millis() - _setup_t0_ms), (int)new_mode);
          }

          if (goal.wps && !_ws->wps_enabled) {
            M5.delay(16);
            wpsStart();
          }
          if (goal.sta_enabled) {
            M5.delay(16);
            wifi_connect_sta("start");
          }
        }
        } // end else (通常遷移)
      }

      // ---- Phase 3: 新しく必要になったサブシステムの起動 ----------------------
      if ((!prev_goal.ssid_scan && goal.ssid_scan) || retry_radio) {
        // 初回スキャンはこの後の scan_tick() に任せる (直前のドライバ再起動と競合しない)
        _scan_status = -2;
        if (_ws) _ws->last_scan_done_ms = 0;
      }
    }

    // Start HTTP only after the connection prerequisite is real: AP client
    // authentication for Setup, or STA association for File Editor. A
    // transient shortage retries only these services without disturbing the
    // connected radio.
    if (web_services_may_start && _ws && _ws->wifi_started
     && _ws->http_server == nullptr
     && (http_server_retry_not_before == 0
      || (int32_t)(M5.millis() - http_server_retry_not_before) >= 0)) {
      M5.delay(16);
      if (!start_wifi_web_services(goal.ap_enabled)) {
        http_server_retry_not_before = M5.millis() + 1000;
      } else {
        http_server_retry_not_before = 0;
      }
    }

    // =============================================================================
    // 定常時の処理 (goal 変化の有無に関わらず毎 tick 実行する必要があるもの)
    // =============================================================================

    // setup_ap 中に STA 接続が成立したら setup を終了する。
    // 完了後は STA 接続だけを残し、AP/HTTP サーバは停止する。
    // (ユーザが /done を押さなくても自動で遷移する)
    if (op == def::command::wifi_operation_t::wfop_setup_ap
        && _sta_state == STA_CONNECTED) {
      system_registry->wifi_control.setWifiMode(
        def::command::wifi_mode_t::wifi_enable_sta);
      system_registry->wifi_control.setWebServerMode(
        def::command::webserver_mode_t::ws_disable);
      system_registry->wifi_control.setOperation(
        def::command::wifi_operation_t::wfop_disable);
    }

    // STA が必要なモードでは、接続失敗後に一定間隔で再試行する。
    // 電波が弱い時は AUTH_FAIL や HANDSHAKE_TIMEOUT に見えることもあるため、
    // reason の種類では止めず、Web server ON / Song Manager / OTA が自力復帰できるようにする。
    if (goal.sta_enabled && _ws && _ws->wifi_started && _sta_state != STA_CONNECTED) {
      uint32_t now = M5.millis();
      uint32_t last = _last_connect_attempt_ms;
      bool retry = false;
      if (_sta_state == STA_DISCONNECTED) {
        retry = last == 0 || (now - last) >= STA_RECONNECT_INTERVAL_MS;
      } else if (_sta_state == STA_IDLE && last != 0) {
        retry = (now - last) >= STA_CONNECT_TIMEOUT_MS;
        if (retry) {
          M5.Log.printf("[wifi] connect timeout: disconnect before retry\r\n");
          esp_wifi_disconnect();
        }
      }
      if (retry) {
        M5.Log.printf("[wifi] reconnect retry: state=%u reason=%u\r\n",
                      (unsigned)_sta_state, (unsigned)_last_disconnect_reason);
        wifi_connect_sta("retry");
      }
    }

    // AP開始イベントが届かない場合、セットアップ用SSIDが実際には送信されない。
    // HTTPサーバを作り直さず無線部だけを再起動し、スマートフォンの接続を復旧する。
    if (goal.ap_enabled && _ws && _ws->wifi_started && !_ap_started
     && _ap_start_requested_ms != 0
     && M5.millis() - _ap_start_requested_ms >= 2500) {
      if (_ap_start_attempts < 3) {
        _ap_start_attempts = _ap_start_attempts + 1;
        M5_LOGE("[wifi] AP start timeout, retry %u", (unsigned)_ap_start_attempts);
        esp_wifi_stop();
        _ws->wifi_started = false;
        M5.delay(24);
        wifi_mode_t retry_mode = goal.sta_enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
        esp_wifi_set_mode(retry_mode);
        apply_ap_config();
        esp_err_t retry_err = esp_wifi_start();
        _ws->wifi_started = retry_err == ESP_OK;
        _ap_start_requested_ms = M5.millis();
        if (retry_err != ESP_OK) {
          M5_LOGE("[wifi] AP retry failed: 0x%x", retry_err);
        }
      } else {
        // 同じ要求のままドライバを完全再初期化して、個体ごとの一時的な
        // AP起動失敗から自動復帰する。
        _ap_start_requested_ms = 0;
        wifi_state_stop();
        radio_reconfigure_pending = true;
        radio_reconfigure_not_before = M5.millis() + 1000;
      }
    }

    // Web ファイラー: STA 接続が確立したら QR を接続済み表示に切り替える
    if (op == def::command::wifi_operation_t::wfop_web_filer) {
      auto qrtype = (_sta_state == STA_CONNECTED)
                    ? def::qrcode_type_t::QRCODE_URL_DEVICE
                    : def::qrcode_type_t::QRCODE_URL_DEVICE_NO_WIFI;
      if (system_registry->popup_qr.getQRCodeType() != qrtype) {
        system_registry->popup_qr.setQRCodeType(qrtype);
      }
    }

    // OTA 開始要求 → STA 接続が確立次第 http client で取得開始
    if (op == def::command::wifi_operation_t::wfop_ota_begin) {
      uint32_t now = M5.millis();
      if (!ota_connect_started) {
        ota_connect_started = true;
        ota_connect_deadline = now + STA_CONNECT_TIMEOUT_MS;
      }
      system_registry->runtime_info.setWiFiOtaProgress(
        def::command::wifi_ota_state_t::ota_connecting);
      if (_sta_state == STA_CONNECTED
       && (M5.millis() - _sta_connected_ms) >= OTA_NETWORK_SETTLE_MS) {
        ota_connect_started = false;
        ota_connect_deadline = 0;
        system_registry->wifi_control.setOperation(
          def::command::wifi_operation_t::wfop_ota_progress);
#if defined(KANPLAY_SAMPLER)
        task_http_client.exec_ota(sampler_ns::def::app::url_ota_catalog, "sampler",
          sampler_ns::def::app::app_version_major,
          sampler_ns::def::app::app_version_minor,
          sampler_ns::def::app::app_version_patch);
#else
        task_http_client.exec_ota(def::app::url_ota_catalog);
#endif
      } else if ((int32_t)(now - ota_connect_deadline) >= 0) {
        M5_LOGW("[wifi] OTA connection timed out");
        system_registry->runtime_info.setWiFiOtaProgress(
#if defined(KANPLAY_SAMPLER)
          def::command::wifi_ota_state_t::ota_wifi_connection_error);
#else
          def::command::wifi_ota_state_t::ota_connection_error);
#endif
        system_registry->wifi_control.setOperation(
          def::command::wifi_operation_t::wfop_disable);
        system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
        ota_connect_started = false;
        ota_connect_deadline = 0;
      }
    }

#if defined(KANPLAY_SAMPLER)
    if (op == def::command::wifi_operation_t::wfop_update_check_begin) {
      uint32_t now = M5.millis();
      if (!update_check_started) {
        update_check_started = true;
        update_check_deadline = now + STA_CONNECT_TIMEOUT_MS;
        system_registry->runtime_info.setWiFiOtaProgress(
          def::command::wifi_ota_state_t::ota_connecting);
      }
      if (_sta_state == STA_CONNECTED
       && (M5.millis() - _sta_connected_ms) >= OTA_NETWORK_SETTLE_MS) {
        system_registry->wifi_control.setOperation(
          def::command::wifi_operation_t::wfop_update_check_progress);
        task_http_client.exec_catalog_check(sampler_ns::def::app::url_ota_catalog, "sampler",
          sampler_ns::def::app::app_version_major,
          sampler_ns::def::app::app_version_minor,
          sampler_ns::def::app::app_version_patch);
      } else if ((int32_t)(now - update_check_deadline) >= 0) {
        system_registry->runtime_info.setWiFiOtaProgress(
#if defined(KANPLAY_SAMPLER)
          def::command::wifi_ota_state_t::ota_wifi_connection_error);
#else
          def::command::wifi_ota_state_t::ota_connection_error);
#endif
        system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
        system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
      }
    }
#endif

#if defined(KANPLAY_SAMPLER)
    if (op == def::command::wifi_operation_t::wfop_connectivity_check_begin
     && _sta_state == STA_CONNECTED
     && (M5.millis() - _sta_connected_ms) >= OTA_NETWORK_SETTLE_MS) {
      system_registry->wifi_control.setOperation(
        def::command::wifi_operation_t::wfop_connectivity_check_progress);
      task_http_client.exec_connectivity_check(sampler_ns::def::app::url_ota_catalog);
    }
#endif

    // SSID スキャン: ステーションが接続してから初めて動かす。
    //   - ステーション接続までは AP の radio を占有させない (接続高速化)
    //   - ステーション接続を受けたら AP-only → APSTA に昇格して scan 解禁
    if (goal.ssid_scan && _ws && _ws->wifi_started
     && _ap_station_count > 0 && _setup_http_seen) {
      maybe_upgrade_to_apsta_for_scan();
      scan_tick();
    }

#endif
  }
#if !defined(M5UNIFIED_PC_BUILD)
  // stop() can interrupt any operation. Funnel that path through the same
  // ordered, idempotent teardown used by normal disable/cancel/timeout.
  stop_wifi_web_services("worker stop");
  wifi_state_stop();
  system_registry->runtime_info.setWiFiStationCount(0);
  system_registry->runtime_info.setWiFiSTAInfo(
    def::command::wifi_sta_info_t::wsi_off);
  system_registry->runtime_info.setWiFiAPInfo(
    def::command::wifi_ap_info_t::wai_off);
  log_wifi_web_memory("worker-stopped");
#endif
  _wifi_task_handle = nullptr;
  vTaskDelete(nullptr);
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
