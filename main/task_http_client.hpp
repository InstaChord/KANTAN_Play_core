// SPDX-License-Identifier: MIT
// Copyright (c) 2025 InstaChord Corp.

#ifndef KANPLAY_TASK_HTTP_CLIENT_HPP
#define KANPLAY_TASK_HTTP_CLIENT_HPP

#include <stdint.h>

/*
task_http_client は外部サーバにhttp接続したり、OTAファームウェア更新を実施するタスクです。
*/

namespace kanplay_ns {
//-------------------------------------------------------------------------
class task_http_client_t {
public:
  bool start(void);
  void exec_ota(const char* json_url);
  void exec_ota(const char* json_url, const char* app_id,
                uint8_t major, uint8_t minor, uint8_t patch);
  void exec_catalog_check(const char* json_url, const char* app_id,
                          uint8_t major, uint8_t minor, uint8_t patch);
  void exec_connectivity_check(const char* url);
  enum request_t {
    request_none,
    request_ota,
    request_catalog_check,
    request_connectivity_check,
  };
private:
  static void task_func(task_http_client_t* me);
  request_t _request = request_none;
  const char* _ota_json_url = nullptr;
  const char* _connectivity_url = nullptr;
  const char* _catalog_app_id = nullptr;
  uint8_t _catalog_version_major = 0;
  uint8_t _catalog_version_minor = 0;
  uint8_t _catalog_version_patch = 0;
};

//-------------------------------------------------------------------------
}; // namespace kanplay_ns

#endif
