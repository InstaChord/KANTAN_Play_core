// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#include <M5Unified.h>

#include "task_http_client.hpp"

#include "system_registry.hpp"

#if defined (M5UNIFIED_PC_BUILD)
namespace kanplay_ns {

void task_http_client_t::task_func(task_http_client_t* me)
{}

};
#else

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_crt_bundle.h>
#include <string.h>

#define HASH_LEN 32

namespace kanplay_ns {
//-------------------------------------------------------------------------

static char* _http_dst = nullptr;
static size_t _http_dst_remain = 0;
static esp_err_t _http_client_event_handler(esp_http_client_event_t *evt)
{
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (evt->data_len) {
      if (_http_dst_remain < evt->data_len) {
        evt->data_len = _http_dst_remain;
      }
      memcpy(_http_dst, evt->data, evt->data_len);
      _http_dst += evt->data_len;
      _http_dst_remain -= evt->data_len;
    }
  }
  return ESP_OK;
}

static esp_err_t execHttpClient(const char* url, char* data, const size_t length,
                                int* status_out = nullptr, int timeout_ms = 30000)
{
  _http_dst_remain = length;
  _http_dst = data;

  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));
  config.url = url;
  config.event_handler = _http_client_event_handler;
  config.keep_alive_enable = true;
  config.buffer_size = 1024;
  config.buffer_size_tx = 1024;
  // STA接続直後のDNS/TLS確立は既定の5秒を超えることがある。OTAでは最初に
  // このカタログを必ず取るため、バイナリ取得と同じ余裕を与える。
  config.timeout_ms = timeout_ms;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.skip_cert_common_name_check = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);
  int status = 0;
  if (err != ESP_OK) {
    M5_LOGE("HTTP client perform failed: %s (0x%x)", esp_err_to_name(err), err);
  } else {
    status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    M5_LOGI("HTTP status=%d, content_length=%d, received=%d", status, content_len, (int)(_http_dst - data));
  }
  if (status_out) { *status_out = status; }
  esp_http_client_cleanup(client);
  return err;
}

// 初期値として3MBを設定
static size_t ota_content_length = 1024*1024*3; // 3MB
static size_t ota_received_length = 0;

static esp_err_t _http_ota_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        M5_LOGD("HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        M5_LOGD("HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        M5_LOGD("HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        // ヘッダ値のログは出さない（GitHubのLocationヘッダが1KB超でスタック溢れする）
        if (0 == strncmp(evt->header_key, "Content-Length", 14))
        {
          ota_content_length = atoi(evt->header_value);
          M5_LOGV("Content-Length: %d", ota_content_length);
          ota_received_length = 0;
        }
        break;
    case HTTP_EVENT_ON_DATA:
        // M5_LOGD("HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        ota_received_length += evt->data_len;
        if (ota_content_length < ota_received_length) {
          ota_content_length = ota_received_length;
        }
        system_registry->runtime_info.setWiFiOtaProgress(ota_content_length ? ota_received_length * 100 / ota_content_length : 0);
        break;
    case HTTP_EVENT_ON_FINISH:
        M5_LOGD("HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        M5_LOGD("HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        M5_LOGD("HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static TaskHandle_t _httpcl_task_handle = nullptr;

bool task_http_client_t::start(void)
{
  if (_httpcl_task_handle == nullptr) {
    const BaseType_t result = xTaskCreatePinnedToCore(
      (TaskFunction_t)task_func, "httpcl", 10240, this,
      def::system::task_priority_wifi, &_httpcl_task_handle,
      def::system::task_cpu_wifi);
    if (result != pdPASS) { _httpcl_task_handle = nullptr; }
  }
  return _httpcl_task_handle != nullptr;
}

// リダイレクトを手動解決して最終URLを取得する（ヘッダバッファ蓄積を防止）
// CDN等のセッション固有URLは新規接続では無効なため、異なるホストへの
// リダイレクトは追わず、その手前の安定URLを返す。
static bool resolve_redirects(char* url, size_t url_length)
{
  static constexpr size_t PREV_URL_SIZE = 1024;  // PSRAM確保・リポジトリ名やタグ名が長い場合に備える
  char* prev_url = (char*)m5gfx::heap_alloc_psram(PREV_URL_SIZE);
  if (prev_url) { prev_url[0] = '\0'; }
  bool result = false;

  for (int retry = 0; retry < 8; ++retry) {
    esp_http_client_config_t config;
    memset(&config, 0, sizeof(esp_http_client_config_t));
    config.url = url;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.skip_cert_common_name_check = true;
    config.disable_auto_redirect = true;
    config.method = HTTP_METHOD_HEAD;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
      M5_LOGE("resolve_redirects: client init failed");
      break;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    M5_LOGI("resolve_redirects[%d]: status=%d, url=%s", retry, status, url);

    if (err != ESP_OK) {
      M5_LOGE("resolve_redirects: HTTP error %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      break;
    }

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      // リダイレクト前のURLを保存（CDN URLが無効だった場合のフォールバック用）
      if (prev_url) { strncpy(prev_url, url, PREV_URL_SIZE - 1); }

      // Locationヘッダから新しいURLを取得
      esp_http_client_set_redirection(client);
      err = esp_http_client_get_url(client, url, (int)url_length);
      esp_http_client_cleanup(client);
      if (err != ESP_OK) {
        M5_LOGE("resolve_redirects: failed to get redirect URL");
        break;
      }
      M5_LOGI("Redirect to: %s", url);
      continue;
    }

    esp_http_client_cleanup(client);

    if (status == 200) {
      result = true;
      break;
    }

    // 非標準ステータス（CDNがHEADを拒否、セッション固有URLの期限切れ等）
    // リダイレクト前の安定URLにフォールバックし、OTAにリダイレクトを任せる
    if (prev_url && prev_url[0]) {
      strncpy(url, prev_url, url_length);
      url[url_length - 1] = '\0';
      M5_LOGW("resolve_redirects: status=%d, falling back to: %s", status, url);
      result = true;
    } else {
      // フォールバック先がない場合は現在のURLをそのまま使用する
      M5_LOGW("resolve_redirects: status=%d, no fallback available, using current URL", status);
      result = true;
    }
    break;
  }

  m5gfx::heap_free(prev_url);
  return result;
}

static esp_err_t exec_http_ota(const char* binary_url)
{
  M5_LOGI("Starting OTA example task");
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  config.url = binary_url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.event_handler = _http_ota_event_handler;
  config.keep_alive_enable = true;
  config.buffer_size = 2048;       // GitHubのレスポンスヘッダ受信用
  config.buffer_size_tx = 2048;    // CDNリダイレクトURL(JWT込み1.5KB超)のGETリクエスト構築用
  config.timeout_ms = 30000;       // Wi-Fi接続直後のTLS確立と約4MBの更新を待てるようにする
  config.skip_cert_common_name_check = true;

  esp_https_ota_config_t ota_config;
  memset(&ota_config, 0, sizeof(esp_https_ota_config_t));
  ota_received_length = 0;

  ota_config.http_config = &config;

  M5_LOGI("Attempting to download update from %s", config.url);
  return esp_https_ota(&ota_config);
}

static esp_err_t exec_http_ota_with_retry(const char* binary_url)
{
  static constexpr uint8_t ota_attempts = 2;
  esp_err_t result = ESP_FAIL;
  for (uint8_t attempt = 0; attempt < ota_attempts; ++attempt) {
    result = exec_http_ota(binary_url);
    if (result == ESP_OK || attempt + 1 >= ota_attempts) { break; }
    M5_LOGW("OTA binary attempt %u/%u failed: %s; retrying",
            (unsigned)(attempt + 1), (unsigned)ota_attempts,
            esp_err_to_name(result));
    vTaskDelay(pdMS_TO_TICKS(1200));
  }
  return result;
}

static bool binary_url_requires_redirect_resolution(const char* url)
{
  // GitHub Releases uses temporary CDN redirects, but GitHub Pages and Raw
  // URLs are already stable direct binaries.  Avoid a second TLS/HEAD request
  // for the latter: on some APs that preliminary request times out even though
  // the following binary GET would succeed.
  return url != nullptr && strstr(url, "github.com/") != nullptr
      && strstr(url, "/releases/") != nullptr;
}

static const char* get_firmware_channel_name(def::command::firmware_channel_t channel)
{
  switch (channel) {
  default:
  case def::command::firmware_channel_t::stable:    return "stable";
  case def::command::firmware_channel_t::beta:      return "beta";
  case def::command::firmware_channel_t::developer: return "developer";
  }
}

#if defined(KANPLAY_SAMPLER)
static constexpr auto ota_catalog_failure_state = def::command::wifi_ota_state_t::ota_catalog_error;
static constexpr auto ota_no_firmware_state = def::command::wifi_ota_state_t::ota_no_matching_firmware;
#else
static constexpr auto ota_catalog_failure_state = def::command::wifi_ota_state_t::ota_connection_error;
static constexpr auto ota_no_firmware_state = def::command::wifi_ota_state_t::ota_connection_error;
#endif

static bool parse_catalog_version(const char* version, int* major_out, int* minor_out, int* patch_out)
{
  if (version == nullptr || version[0] == '\0') { return false; }
  if (0 == strcmp(version, "latest") || 0 == strcmp(version, "dev")) { return false; }
  if (*version == 'v' || *version == 'V') { ++version; }

  int major = 0;
  int minor = 0;
  int patch = 0;
  if (sscanf(version, "%d.%d.%d", &major, &minor, &patch) != 3) { return false; }
  if (major_out) { *major_out = major; }
  if (minor_out) { *minor_out = minor; }
  if (patch_out) { *patch_out = patch; }
  return true;
}

static int compare_catalog_version(int major, int minor, int patch,
                                   int other_major, int other_minor, int other_patch)
{
  if (major != other_major) { return major > other_major ? 1 : -1; }
  if (minor != other_minor) { return minor > other_minor ? 1 : -1; }
  if (patch != other_patch) { return patch > other_patch ? 1 : -1; }
  return 0;
}

static def::command::wifi_ota_state_t exec_get_catalog_binary_url(const char* catalog_url, char* data, const size_t length,
                                                                   const char* app_id,
                                                                   int current_major, int current_minor, int current_patch)
{
  auto http_err = execHttpClient(catalog_url, data, length);
  if (ESP_OK != http_err) {
    M5_LOGE("OTA catalog request failed: %s (0x%x)", esp_err_to_name(http_err), http_err);
    return ota_catalog_failure_state;
  }

  size_t received = length - _http_dst_remain;
  M5_LOGI("OTA catalog received: %d bytes", (int)received);
  if (received < length) {
    data[received] = '\0';
  } else {
    data[length] = '\0';
  }

  ArduinoJson::JsonDocument json;
  auto error = deserializeJson(json, data);
  data[0] = '\0';
  if (error) {
    M5_LOGE("OTA catalog JSON parse failed: %s", error.c_str());
    return ota_catalog_failure_state;
  }

  auto firmware_array = json["firmware"].as<JsonArray>();
  if (firmware_array.size() == 0) {
    M5_LOGE("OTA catalog firmware array is empty");
    return ota_catalog_failure_state;
  }

#if defined ( CONFIG_IDF_TARGET_ESP32S3 )
  const char* board_name = "cores3";
#else
  const char* board_name = "core2";
#endif
  auto target_channel = system_registry->runtime_info.getFirmwareChannel();
  const char* channel_name = get_firmware_channel_name(target_channel);
  M5_LOGI("OTA catalog target channel=%s board=%s", channel_name, board_name);

#if defined(KANPLAY_SAMPLER)
  // Sampler beta has one public latest stream. Select the greatest numeric
  // version rather than relying on JSON ordering, so an archived or future
  // channel entry can never make an older binary look current.
  const char* sampler_url = nullptr;
  const char* sampler_version = nullptr;
  int sampler_major = -1;
  int sampler_minor = -1;
  int sampler_patch = -1;
#endif

  for (auto item : firmware_array) {
    const char* app = item["app"].as<const char*>();
    const char* channel = item["channel"].as<const char*>();
    const char* version = item["version"].as<const char*>();
    auto url_list = item["url"].as<JsonObject>();
    const char* url = url_list[board_name].as<const char*>();

    if (app == nullptr || app_id == nullptr || 0 != strcmp(app, app_id)) { continue; }
    if (url == nullptr) { continue; }
#if defined(KANPLAY_SAMPLER)
    int candidate_major = 0;
    int candidate_minor = 0;
    int candidate_patch = 0;
    if (!parse_catalog_version(version, &candidate_major, &candidate_minor, &candidate_patch)) { continue; }
    if (sampler_url != nullptr
     && compare_catalog_version(candidate_major, candidate_minor, candidate_patch,
                                sampler_major, sampler_minor, sampler_patch) <= 0) {
      continue;
    }
    sampler_url = url;
    sampler_version = version;
    sampler_major = candidate_major;
    sampler_minor = candidate_minor;
    sampler_patch = candidate_patch;
    continue;
#else
    if (channel == nullptr || 0 != strcmp(channel, channel_name)) { continue; }

    M5_LOGI("OTA catalog matched channel=%s version=%s url=%s",
            channel, version ? version : "(null)", url);
    strncpy(data, url, length);
    data[length] = '\0';
    int catalog_major = 0;
    int catalog_minor = 0;
    int catalog_patch = 0;
    if (target_channel != def::command::firmware_channel_t::developer
     && parse_catalog_version(version, &catalog_major, &catalog_minor, &catalog_patch)
     && compare_catalog_version(catalog_major, catalog_minor, catalog_patch,
                                current_major, current_minor, current_patch) <= 0) {
      return def::command::wifi_ota_state_t::ota_already_up_to_date;
    }
    return def::command::wifi_ota_state_t::ota_update_available;
#endif
  }

#if defined(KANPLAY_SAMPLER)
  if (sampler_url != nullptr) {
    M5_LOGI("OTA catalog latest sampler version=%s url=%s", sampler_version, sampler_url);
    strncpy(data, sampler_url, length);
    data[length] = '\0';
    if (compare_catalog_version(sampler_major, sampler_minor, sampler_patch,
                                current_major, current_minor, current_patch) <= 0) {
      return def::command::wifi_ota_state_t::ota_already_up_to_date;
    }
    return def::command::wifi_ota_state_t::ota_update_available;
  }
#endif

  M5_LOGE("No matching OTA firmware found for channel=%s board=%s", channel_name, board_name);
  return ota_no_firmware_state;
}

// Wi-Fi接続直後や省電力APでは最初のDNS/TLS要求だけ失敗することがある。
// カタログは小さいため、OTA本体の前に限定して短い再試行を行う。
static def::command::wifi_ota_state_t exec_get_catalog_binary_url_with_retry(const char* catalog_url,
                                                                               char* data, const size_t length,
                                                                               const char* app_id,
                                                                               int current_major, int current_minor,
                                                                               int current_patch)
{
  static constexpr uint8_t catalog_attempts = 3;
  for (uint8_t attempt = 0; attempt < catalog_attempts; ++attempt) {
    // GitHub Raw can retain the previous branch object briefly after a push.
    // Give every catalog request a unique URL so update checks never reuse a
    // stale CDN response. The catalog is tiny, so bypassing that cache is less
    // costly than reporting UP TO DATE while a newer firmware is available.
    char fresh_catalog_url[384] = {};
    snprintf(fresh_catalog_url, sizeof(fresh_catalog_url), "%s%c_kp=%08lx",
             catalog_url, strchr(catalog_url, '?') ? '&' : '?',
             (unsigned long)esp_random());
    auto state = exec_get_catalog_binary_url(fresh_catalog_url, data, length, app_id,
                                             current_major, current_minor, current_patch);
    if (state != ota_catalog_failure_state
     || attempt + 1 >= catalog_attempts) {
      return state;
    }
    M5_LOGW("OTA catalog attempt %u/%u failed; retrying", (unsigned)(attempt + 1),
            (unsigned)catalog_attempts);
    vTaskDelay(pdMS_TO_TICKS(700 * (attempt + 1)));
  }
  return ota_catalog_failure_state;
}

static void exec_ota_inner(const char* json_url, const char* app_id,
                           uint8_t major, uint8_t minor, uint8_t patch)
{
  static constexpr const size_t MAX_HTTP_OUTPUT_BUFFER = 1024 * 4;
  auto local_response_buffer = (char*)m5gfx::heap_alloc_psram(MAX_HTTP_OUTPUT_BUFFER + 1);
  if (local_response_buffer == nullptr) {
    M5_LOGE("Failed to allocate PSRAM buffer for OTA");
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connection_error);
    return;
  }

  auto channel = system_registry->runtime_info.getFirmwareChannel();
#if !defined(KANPLAY_SAMPLER)
  if (channel == def::command::firmware_channel_t::developer
   && !system_registry->runtime_info.getDeveloperMode()) {
    M5_LOGE("Developer OTA requested while developer mode is disabled");
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connection_error);
    m5gfx::heap_free(local_response_buffer);
    return;
  }
#endif

  auto state = exec_get_catalog_binary_url_with_retry(json_url, local_response_buffer,
                                                       MAX_HTTP_OUTPUT_BUFFER,
                                                       app_id, major, minor, patch);
  system_registry->runtime_info.setWiFiOtaProgress(state);
  if (state != def::command::wifi_ota_state_t::ota_update_available) {
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
    m5gfx::heap_free(local_response_buffer);
    return;
  }

  // GitHub ReleasesだけはCDN URLを避けるため事前に解決する。サンプラーの
  // GitHub Pages配信は固定URLなので、余分なHEAD要求を行わず直接OTAする。
  if (binary_url_requires_redirect_resolution(local_response_buffer)
   && !resolve_redirects(local_response_buffer, MAX_HTTP_OUTPUT_BUFFER)) {
    M5_LOGE("Failed to resolve OTA binary URL");
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_update_failed);
    m5gfx::heap_free(local_response_buffer);
    return;
  }
  auto ret = exec_http_ota_with_retry(local_response_buffer);
  system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
  system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
  if (ret == ESP_OK) {
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_update_done);
    system_registry->operator_command.addQueue( { def::command::system_control, def::command::system_control_t::sc_reset } );
  } else {
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_update_failed);
    M5_LOGE("Firmware upgrade failed");
  }
  m5gfx::heap_free(local_response_buffer);
}

void task_http_client_t::exec_ota(const char* json_url)
{
  exec_ota(json_url, "kantanplay", def::app::app_version_major,
           def::app::app_version_minor, def::app::app_version_patch);
}

void task_http_client_t::exec_ota(const char* json_url, const char* app_id,
                                  uint8_t major, uint8_t minor, uint8_t patch)
{
  _ota_json_url = json_url;
  _catalog_app_id = app_id;
  _catalog_version_major = major;
  _catalog_version_minor = minor;
  _catalog_version_patch = patch;
  if (!start()) {
    system_registry->runtime_info.setWiFiOtaProgress(
      def::command::wifi_ota_state_t::ota_connection_error);
    system_registry->wifi_control.setOperation(
      def::command::wifi_operation_t::wfop_disable);
    system_registry->wifi_control.setWifiMode(
      def::command::wifi_mode_t::wifi_disable);
    return;
  }
  _request = request_ota;
  xTaskNotify(_httpcl_task_handle, request_ota, eSetValueWithOverwrite);
}

static void exec_catalog_check_inner(const char* json_url, const char* app_id,
                                     uint8_t major, uint8_t minor, uint8_t patch)
{
  static constexpr size_t max_catalog_bytes = 4096;
  auto* data = (char*)m5gfx::heap_alloc_psram(max_catalog_bytes + 1);
  if (data == nullptr) {
    system_registry->runtime_info.setWiFiOtaProgress(def::command::wifi_ota_state_t::ota_connection_error);
  } else {
    auto state = exec_get_catalog_binary_url_with_retry(json_url, data, max_catalog_bytes,
                                                         app_id, major, minor, patch);
    system_registry->runtime_info.setWiFiOtaProgress(state);
    m5gfx::heap_free(data);
  }

  // 更新の有無を調べたら、STAとドライバを必ず解放する。
  if (system_registry->wifi_control.getOperation()
      == def::command::wifi_operation_t::wfop_update_check_progress) {
    system_registry->wifi_control.setOperation(def::command::wifi_operation_t::wfop_disable);
    system_registry->wifi_control.setWifiMode(def::command::wifi_mode_t::wifi_disable);
  }
}

void task_http_client_t::exec_catalog_check(const char* json_url, const char* app_id,
                                            uint8_t major, uint8_t minor, uint8_t patch)
{
  _ota_json_url = json_url;
  _catalog_app_id = app_id;
  _catalog_version_major = major;
  _catalog_version_minor = minor;
  _catalog_version_patch = patch;
  if (!start()) {
    system_registry->runtime_info.setWiFiOtaProgress(
      def::command::wifi_ota_state_t::ota_connection_error);
    system_registry->wifi_control.setOperation(
      def::command::wifi_operation_t::wfop_disable);
    system_registry->wifi_control.setWifiMode(
      def::command::wifi_mode_t::wifi_disable);
    return;
  }
  _request = request_catalog_check;
  xTaskNotify(_httpcl_task_handle, request_catalog_check, eSetValueWithOverwrite);
}

static void exec_connectivity_check_inner(const char* url)
{
  char response[64] = {};
  int status = 0;
  const esp_err_t err = execHttpClient(url, response, sizeof(response) - 1,
                                       &status, 8000);
  const bool online = err == ESP_OK && status >= 200 && status < 400;
  system_registry->runtime_info.setWiFiConnectivity(
    online ? def::command::wifi_connectivity_state_t::online
           : def::command::wifi_connectivity_state_t::offline);
  // Keep STA alive until the sampler UI has consumed the result. It releases
  // Wi-Fi only after drawing the final status, avoiding a result/driver race.
  system_registry->wifi_control.setOperation(
    def::command::wifi_operation_t::wfop_disable);
}

void task_http_client_t::exec_connectivity_check(const char* url)
{
  _connectivity_url = url;
  if (!start()) {
    system_registry->runtime_info.setWiFiConnectivity(
      def::command::wifi_connectivity_state_t::offline);
    system_registry->wifi_control.setOperation(
      def::command::wifi_operation_t::wfop_disable);
    return;
  }
  _request = request_connectivity_check;
  xTaskNotify(_httpcl_task_handle, request_connectivity_check, eSetValueWithOverwrite);
}

void task_http_client_t::task_func(task_http_client_t* me)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    auto request = me->_request;
    me->_request = request_none;
    switch (request) {
    case request_ota:
      exec_ota_inner(me->_ota_json_url, me->_catalog_app_id,
                     me->_catalog_version_major, me->_catalog_version_minor,
                     me->_catalog_version_patch);
      break;

    case request_catalog_check:
      exec_catalog_check_inner(me->_ota_json_url, me->_catalog_app_id,
                               me->_catalog_version_major, me->_catalog_version_minor,
                               me->_catalog_version_patch);
      break;

    case request_connectivity_check:
      exec_connectivity_check_inner(me->_connectivity_url);
      break;

    default:
      break;
    }
  }
}

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
