// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#if defined(KANPLAY_SAMPLER) && defined(ARDUINO)

#include <M5Unified.h>

#include <esp_heap_caps.h>
#include <esp_http_server.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "../file_manage.hpp"
#include "sampler_web.hpp"
#include "sampler_web_api.hpp"

namespace sampler_ns {
namespace {

struct web_dir_t {
  const char* token;
  const char* path;
  const char* suffix;
  size_t max_bytes;
  const char* content_type;
  bool audio;
};

static constexpr const web_dir_t web_dirs[] = {
  { "samples", "/sampler/samples", nullptr, 3200 * 1024, nullptr, true },
  { "loops",   "/sampler/loops",   nullptr, 1600 * 1024, nullptr, true },
  { "music",   "/sampler/music",   nullptr, 128 * 1024 * 1024, nullptr, true },
  { "kits",    "/sampler/kits",    nullptr, 7 * 1024 * 1024, "application/vnd.instachord.ktkit", false },
  { "projects", "/sampler/projects", ".json", 128 * 1024, "application/json", false },
};

static bool web_sd_mounted = false;

static esp_err_t send_error(httpd_req_t* req, const char* status, const char* message)
{
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  char body[96];
  snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message ? message : "error");
  return httpd_resp_sendstr(req, body);
}

static std::string url_decode(const char* text, size_t length)
{
  std::string out;
  out.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    char c = text[i];
    if (c == '%' && i + 2 < length) {
      unsigned int value = 0;
      if (sscanf(text + i + 1, "%2x", &value) == 1) {
        out.push_back((char)value);
        i += 2;
        continue;
      }
    }
    out.push_back(c == '+' ? ' ' : c);
  }
  return out;
}

static bool valid_relative_path(const std::string& name, const char* suffix, bool allow_empty = false)
{
  if (name.empty()) { return allow_empty; }
  if (name.size() > 120 || name[0] == '/' || name.find('\\') != std::string::npos || name.find("..") != std::string::npos) { return false; }
  for (size_t start = 0; start < name.size();) {
    size_t end = name.find('/', start);
    if (end == std::string::npos) { end = name.size(); }
    if (end == start || name[start] == '.') { return false; }
    start = end + 1;
  }
  if (!suffix || !suffix[0]) { return true; }
  size_t sl = strlen(suffix);
  return name.size() > sl && strcasecmp(name.c_str() + name.size() - sl, suffix) == 0;
}

static bool has_suffix(const std::string& name, const char* suffix)
{
  size_t length = strlen(suffix);
  return name.size() > length && strcasecmp(name.c_str() + name.size() - length, suffix) == 0;
}

static bool is_asset_directory(const web_dir_t& dir, const std::string& name)
{
  return (strcmp(dir.token, "kits") == 0 || strcmp(dir.token, "projects") == 0)
      && has_suffix(name, "_assets");
}

static bool is_mp3_name(const std::string& name) { return has_suffix(name, ".mp3"); }
static bool is_ktsynth_name(const std::string& name) { return has_suffix(name, ".ktsynth"); }
static bool is_ktkit_name(const std::string& name) { return has_suffix(name, ".ktkit"); }
static bool is_midi_name(const std::string& name)
{
  return has_suffix(name, ".mid") || has_suffix(name, ".midi");
}

static bool valid_web_file_name(const web_dir_t& dir, const std::string& name)
{
  if (!valid_relative_path(name, nullptr)) { return false; }
  const bool beat_midi = strcmp(dir.token, "loops") == 0 && is_midi_name(name);
  const bool synth_tone = strcmp(dir.token, "samples") == 0 && is_ktsynth_name(name);
  const bool kit_file = strcmp(dir.token, "kits") == 0
                     && (is_ktkit_name(name) || has_suffix(name, ".json"));
  if (strcmp(dir.token, "kits") == 0) { return kit_file; }
  return dir.audio ? (has_suffix(name, ".wav") || is_mp3_name(name) || beat_midi || synth_tone)
                   : valid_relative_path(name, dir.suffix);
}

static const web_dir_t* parse_dir_and_name(httpd_req_t* req, std::string* name)
{
  static constexpr const char prefix[] = "/api/sampler/files/";
  const char* path = req->uri;
  if (strncmp(path, prefix, sizeof(prefix) - 1) != 0) { return nullptr; }
  path += sizeof(prefix) - 1;
  const char* end = strchr(path, '?');
  size_t path_len = end ? (size_t)(end - path) : strlen(path);
  for (const auto& dir : web_dirs) {
    size_t token_len = strlen(dir.token);
    if (path_len < token_len || memcmp(path, dir.token, token_len) != 0) { continue; }
    if (path_len == token_len) {
      if (name) { name->clear(); }
      return &dir;
    }
    if (path[token_len] != '/') { continue; }
    std::string decoded = url_decode(path + token_len + 1, path_len - token_len - 1);
    if (!valid_web_file_name(dir, decoded)) { return nullptr; }
    if (name) { *name = decoded; }
    return &dir;
  }
  return nullptr;
}

static bool ensure_dirs(bool force_remount = false)
{
  if (force_remount || !web_sd_mounted) {
    // SDの終了・再初期化はHTTPタスクから行わない。メインループ側へ
    // 依頼し、KIT読込みやセッション保存との競合を避ける。
    web_sd_mounted = sampler_web_prepare_storage_operation(true);
    if (!web_sd_mounted) { return false; }
  }
  if (!kanplay_ns::storage_sd.beginStorage()) { return false; }
  kanplay_ns::storage_sd.makeDirectory("/sampler");
  for (const auto& dir : web_dirs) { kanplay_ns::storage_sd.makeDirectory(dir.path); }
  return true;
}

static std::string full_path(const web_dir_t& dir, const std::string& name)
{
  return std::string(dir.path) + "/" + name;
}

static bool is_asset_document(const web_dir_t& dir)
{
  return strcmp(dir.token, "projects") == 0;
}

static void remove_document_assets(const std::string& document_path)
{
  if (!has_suffix(document_path, ".json")) { return; }
  const std::string asset_dir = document_path.substr(0, document_path.size() - 5) + "_assets";
  std::vector<kanplay_ns::file_info_string_t> assets;
  kanplay_ns::storage_sd.getFileList(assets, asset_dir.c_str(), "");
  for (const auto& asset : assets) {
    if (!asset.filename.empty() && asset.filename.find('/') == std::string::npos
     && asset.filename.find("..") == std::string::npos) {
      kanplay_ns::storage_sd.removeFile((asset_dir + "/" + asset.filename).c_str());
    }
  }
  // storage backends that support removing an empty directory accept this;
  // otherwise the hidden, empty asset directory is harmless.
  kanplay_ns::storage_sd.removeFile(asset_dir.c_str());
}

static bool replace_all(std::string& text, const std::string& from, const std::string& to)
{
  if (from.empty()) { return false; }
  bool changed = false;
  for (size_t pos = 0; (pos = text.find(from, pos)) != std::string::npos;) {
    text.replace(pos, from.size(), to);
    pos += to.size();
    changed = true;
  }
  return changed;
}

static bool rename_asset_document(const std::string& source_path, const std::string& target_path)
{
  const int size = kanplay_ns::storage_sd.getFileSize(source_path.c_str());
  if (size <= 0 || size > 128 * 1024) { return false; }

  std::string json((size_t)size, '\0');
  if (kanplay_ns::storage_sd.loadFromFileToMemory(
        source_path.c_str(), (uint8_t*)json.data(), json.size()) != size) { return false; }

  const std::string source_assets = source_path.substr(0, source_path.size() - 5) + "_assets";
  const std::string target_assets = target_path.substr(0, target_path.size() - 5) + "_assets";
  const bool has_assets = replace_all(json, source_assets, target_assets);
  const std::string temporary = target_path + ".rename";
  kanplay_ns::storage_sd.removeFile(temporary.c_str());
  if (kanplay_ns::storage_sd.saveFromMemoryToFile(
        temporary.c_str(), (const uint8_t*)json.data(), json.size()) != (int)json.size()) {
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return false;
  }

  bool assets_moved = false;
  if (has_assets) {
    assets_moved = kanplay_ns::storage_sd.renameFile(source_assets.c_str(), target_assets.c_str());
    if (!assets_moved) {
      kanplay_ns::storage_sd.removeFile(temporary.c_str());
      return false;
    }
  }
  if (!kanplay_ns::storage_sd.renameFile(temporary.c_str(), target_path.c_str())) {
    if (assets_moved) { kanplay_ns::storage_sd.renameFile(target_assets.c_str(), source_assets.c_str()); }
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return false;
  }
  if (!kanplay_ns::storage_sd.removeFile(source_path.c_str())) {
    kanplay_ns::storage_sd.removeFile(target_path.c_str());
    if (assets_moved) { kanplay_ns::storage_sd.renameFile(target_assets.c_str(), source_assets.c_str()); }
    return false;
  }
  return true;
}

static bool query_path(httpd_req_t* req, std::string& out)
{
  out.clear();
  size_t length = httpd_req_get_url_query_len(req);
  if (length == 0) { return true; }
  std::vector<char> query(length + 1, 0);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) { return false; }
  char raw[128] = {};
  if (httpd_query_key_value(query.data(), "path", raw, sizeof(raw)) != ESP_OK) { return true; }
  out = url_decode(raw, strlen(raw));
  return valid_relative_path(out, nullptr, true);
}

static esp_err_t list_files(httpd_req_t* req, const web_dir_t& dir)
{
  if (!ensure_dirs()) { return send_error(req, "503 Service Unavailable", "SD card unavailable"); }
  std::string relative;
  if (!query_path(req, relative)) { return send_error(req, "400 Bad Request", "invalid folder path"); }
  std::string current = full_path(dir, relative);
  std::vector<kanplay_ns::file_info_string_t> files;
  kanplay_ns::storage_sd.getFileList(files, current.c_str(), "");
  httpd_resp_set_type(req, "application/json");
  char header[180];
  snprintf(header, sizeof(header), "{\"path\":\"%s\",\"files\":[", relative.c_str());
  httpd_resp_sendstr_chunk(req, header);
  bool first = true;
  for (const auto& file : files) {
    if (!valid_web_file_name(dir, file.filename)) { continue; }
    char item[180];
    snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"size\":%u}", first ? "" : ",", file.filename.c_str(), (unsigned)file.filesize);
    httpd_resp_sendstr_chunk(req, item);
    first = false;
  }
  httpd_resp_sendstr_chunk(req, "]}");
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static const web_dir_t* parse_folder_dir(httpd_req_t* req)
{
  static constexpr const char prefix[] = "/api/sampler/folders/";
  const char* token = req->uri;
  if (strncmp(token, prefix, sizeof(prefix) - 1) != 0) { return nullptr; }
  token += sizeof(prefix) - 1;
  const char* end = strchr(token, '?');
  size_t len = end ? (size_t)(end - token) : strlen(token);
  for (const auto& dir : web_dirs) {
    if (strlen(dir.token) == len && memcmp(dir.token, token, len) == 0) { return &dir; }
  }
  return nullptr;
}

static esp_err_t response_folders(httpd_req_t* req)
{
  sampler_web_note_client_access();
  // 一覧取得は演奏PCMへ触れないため、プレビューを止めずに返す。
  // フォルダ作成だけはSDへ書き込む前にオーディオを停止する。
  if (req->method == HTTP_POST && !sampler_web_prepare_storage_operation()) {
    return send_error(req, "503 Service Unavailable", "audio stop timeout");
  }
  const auto* dir = parse_folder_dir(req);
  if (!dir) { return send_error(req, "404 Not Found", "unknown sampler directory"); }
  if (!ensure_dirs()) { return send_error(req, "503 Service Unavailable", "SD card unavailable"); }
  std::string relative;
  if (!query_path(req, relative)) { return send_error(req, "400 Bad Request", "invalid folder path"); }
  if (req->method == HTTP_POST) {
    size_t length = httpd_req_get_url_query_len(req);
    std::vector<char> query(length + 1, 0);
    char raw[96] = {};
    if (length == 0 || httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK
     || httpd_query_key_value(query.data(), "name", raw, sizeof(raw)) != ESP_OK) { return send_error(req, "400 Bad Request", "folder name required"); }
    std::string name = url_decode(raw, strlen(raw));
    if (!valid_relative_path(name, nullptr) || name.find('/') != std::string::npos) { return send_error(req, "400 Bad Request", "invalid folder name"); }
    std::string created = full_path(*dir, relative);
    created += "/" + name;
    if (!kanplay_ns::storage_sd.makeDirectory(created.c_str())) { return send_error(req, "409 Conflict", "folder create failed"); }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
  }
  std::vector<kanplay_ns::file_info_string_t> folders;
  std::string current = full_path(*dir, relative);
  kanplay_ns::storage_sd.getDirectoryList(folders, current.c_str());
  httpd_resp_set_type(req, "application/json");
  char header[180];
  snprintf(header, sizeof(header), "{\"path\":\"%s\",\"folders\":[", relative.c_str());
  httpd_resp_sendstr_chunk(req, header);
  bool first = true;
  for (size_t i = 0; i < folders.size(); ++i) {
    // Asset folders belong to their JSON Project/Kit and are not user-facing
    // navigation entries. Showing them invites accidental reorganization that
    // would break the saved document's audio references.
    if (is_asset_directory(*dir, folders[i].filename)) { continue; }
    char item[140];
    snprintf(item, sizeof(item), "%s\"%s\"", first ? "" : ",", folders[i].filename.c_str());
    httpd_resp_sendstr_chunk(req, item);
    first = false;
  }
  httpd_resp_sendstr_chunk(req, "]}");
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t get_file(httpd_req_t* req, const web_dir_t& dir, const std::string& name)
{
  if (!ensure_dirs()) { return send_error(req, "503 Service Unavailable", "SD card unavailable"); }
  std::string path = full_path(dir, name);
  int size = kanplay_ns::storage_sd.getFileSize(path.c_str());
  if (size <= 0 || (size_t)size > dir.max_bytes) { return send_error(req, "404 Not Found", "file not found"); }
  kanplay_ns::storage_read_stream_t stream;
  if (!kanplay_ns::storage_sd.openReadStream(path.c_str(), &stream)) {
    return send_error(req, "500 Internal Server Error", "read failed");
  }
  httpd_resp_set_type(req, is_ktkit_name(name) ? "application/vnd.instachord.ktkit"
    : has_suffix(name, ".json") ? "application/json" : dir.audio
    ? (is_mp3_name(name) ? "audio/mpeg"
       : is_midi_name(name) ? "audio/midi"
       : is_ktsynth_name(name) ? "application/vnd.instachord.ktsynth"
       : "audio/wav")
    : dir.content_type);
  httpd_resp_set_hdr(req, "Content-Disposition", name.c_str());
  auto* data = (uint8_t*)heap_caps_malloc(16 * 1024, MALLOC_CAP_SPIRAM);
  if (!data) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return send_error(req, "500 Internal Server Error", "memory unavailable");
  }
  esp_err_t result = ESP_OK;
  while (stream.position < stream.size) {
    const int len = kanplay_ns::storage_sd.readStream(&stream, data, 16 * 1024);
    if (len <= 0 || httpd_resp_send_chunk(req, (const char*)data, len) != ESP_OK) {
      result = ESP_FAIL;
      break;
    }
  }
  kanplay_ns::storage_sd.closeReadStream(&stream);
  if (result == ESP_OK) { result = httpd_resp_send_chunk(req, nullptr, 0); }
  free(data);
  return result;
}

static esp_err_t put_file(httpd_req_t* req, const web_dir_t& dir, const std::string& name)
{
  if (!ensure_dirs()) { return send_error(req, "503 Service Unavailable", "SD card unavailable"); }
  if (is_ktsynth_name(name) && (size_t)req->content_len > 2u * 1024u * 1024u) {
    return send_error(req, "413 Payload Too Large", "KANTAN Synth tone too large");
  }
  if (req->content_len == 0 || (size_t)req->content_len > dir.max_bytes) { return send_error(req, "413 Payload Too Large", "file too large"); }
  std::string path = full_path(dir, name);
  std::string temporary = path + ".upload";
  std::string backup = path + ".backup";
  kanplay_ns::storage_sd.removeFile(temporary.c_str());
  kanplay_ns::storage_sd.removeFile(backup.c_str());

  const size_t buffer_size = std::min<size_t>((size_t)req->content_len, 32 * 1024);
  auto* data = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  if (!data) { return send_error(req, "500 Internal Server Error", "memory unavailable"); }

  size_t received = 0;
  bool first_chunk = true;
  while (received < (size_t)req->content_len) {
    size_t wanted = std::min<size_t>(buffer_size, (size_t)req->content_len - received);
    int got = HTTPD_SOCK_ERR_TIMEOUT;
    for (int retry = 0; retry < 4 && got == HTTPD_SOCK_ERR_TIMEOUT; ++retry) {
      got = httpd_req_recv(req, (char*)data, wanted);
    }
    if (got <= 0) {
      free(data);
      kanplay_ns::storage_sd.removeFile(temporary.c_str());
      return send_error(req, "400 Bad Request", "upload interrupted");
    }

    size_t chunk_size = (size_t)got;
    const size_t header_size = is_ktkit_name(name) ? 8 : dir.audio ? 12 : 1;
    int header_retries = 0;
    while (first_chunk && chunk_size < header_size && received + chunk_size < (size_t)req->content_len) {
      int extra = httpd_req_recv(req, (char*)data + chunk_size,
                                 std::min<size_t>(buffer_size - chunk_size,
                                                  (size_t)req->content_len - received - chunk_size));
      if (extra == HTTPD_SOCK_ERR_TIMEOUT && ++header_retries < 4) { continue; }
      if (extra <= 0) {
        free(data);
        return send_error(req, "400 Bad Request", "upload interrupted");
      }
      chunk_size += (size_t)extra;
    }

    if (first_chunk) {
      bool valid = true;
      if (dir.audio) {
        if (is_midi_name(name)) {
          valid = chunk_size >= 4 && memcmp(data, "MThd", 4) == 0;
        } else if (is_mp3_name(name)) {
          valid = chunk_size >= 3 && (memcmp(data, "ID3", 3) == 0
            || (data[0] == 0xff && (data[1] & 0xe0) == 0xe0));
        } else {
          valid = chunk_size >= 12 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0;
        }
      } else if (is_ktkit_name(name)) {
        static constexpr uint8_t magic[8] = {'K','T','K','I','T','\r','\n',0x1a};
        valid = chunk_size >= sizeof(magic) && memcmp(data, magic, sizeof(magic)) == 0;
      } else if (has_suffix(name, ".json")) {
        size_t offset = 0;
        while (offset < chunk_size && std::isspace((unsigned char)data[offset])) { ++offset; }
        valid = offset < chunk_size && data[offset] == '{';
      }
      if (!valid) {
        free(data);
        return send_error(req, "400 Bad Request", "invalid file format");
      }
    }

    int written = first_chunk
      ? kanplay_ns::storage_sd.saveFromMemoryToFile(temporary.c_str(), data, chunk_size)
      : kanplay_ns::storage_sd.appendFromMemoryToFile(temporary.c_str(), data, chunk_size);
    if (written != (int)chunk_size && ensure_dirs(true)) {
      // 受信済みチャンクはPSRAMに残っているので、SDだけを
      // 再マウントして一度再試行できる。ブラウザの再送は不要。
      written = first_chunk
        ? kanplay_ns::storage_sd.saveFromMemoryToFile(temporary.c_str(), data, chunk_size)
        : kanplay_ns::storage_sd.appendFromMemoryToFile(temporary.c_str(), data, chunk_size);
    }
    if (written != (int)chunk_size) {
      free(data);
      kanplay_ns::storage_sd.removeFile(temporary.c_str());
      return send_error(req, "500 Internal Server Error", "save failed");
    }
    first_chunk = false;
    received += chunk_size;
  }
  free(data);

  const bool replacing = kanplay_ns::storage_sd.getFileSize(path.c_str()) >= 0;
  if (replacing && !kanplay_ns::storage_sd.renameFile(path.c_str(), backup.c_str())) {
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return send_error(req, "500 Internal Server Error", "replace failed");
  }
  if (!kanplay_ns::storage_sd.renameFile(temporary.c_str(), path.c_str())) {
    if (replacing) { kanplay_ns::storage_sd.renameFile(backup.c_str(), path.c_str()); }
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return send_error(req, "500 Internal Server Error", "save failed");
  }
  if (replacing) { kanplay_ns::storage_sd.removeFile(backup.c_str()); }

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
}

static esp_err_t delete_file(httpd_req_t* req, const web_dir_t& dir, const std::string& name)
{
  if (!ensure_dirs()) { return send_error(req, "503 Service Unavailable", "SD card unavailable"); }
  std::string path = full_path(dir, name);
  if (!kanplay_ns::storage_sd.removeFile(path.c_str())) { return send_error(req, "404 Not Found", "delete failed"); }
  // Remove the document first. If that fails, its assets remain intact and
  // the saved Kit/Project can still be loaded. A failed asset cleanup after a
  // successful delete leaves only harmless orphan data, never broken JSON.
  if ((is_asset_document(dir) || (strcmp(dir.token, "kits") == 0 && has_suffix(name, ".json")))) {
    remove_document_assets(path);
  }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
}

static esp_err_t rename_file(httpd_req_t* req, const web_dir_t& dir, const std::string& name)
{
  size_t query_len = httpd_req_get_url_query_len(req);
  if (query_len == 0 || query_len > 256) { return send_error(req, "400 Bad Request", "new name required"); }
  std::vector<char> query(query_len + 1, 0);
  if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) { return send_error(req, "400 Bad Request", "invalid query"); }
  char target_raw[128] = {};
  if (httpd_query_key_value(query.data(), "to", target_raw, sizeof(target_raw)) != ESP_OK) { return send_error(req, "400 Bad Request", "new name required"); }
  std::string target = url_decode(target_raw, strlen(target_raw));
  if (!valid_web_file_name(dir, target) || target == name || !ensure_dirs()) { return send_error(req, "400 Bad Request", "invalid file name"); }
  if (strcmp(dir.token, "kits") == 0 && is_ktkit_name(name) != is_ktkit_name(target)) {
    return send_error(req, "400 Bad Request", "kit format extension cannot be changed");
  }
  std::string source_path = full_path(dir, name);
  std::string target_path = full_path(dir, target);
  if (kanplay_ns::storage_sd.getFileSize(target_path.c_str()) >= 0) { return send_error(req, "409 Conflict", "file already exists"); }
  const bool legacy_asset_document = is_asset_document(dir)
    || (strcmp(dir.token, "kits") == 0 && has_suffix(name, ".json"));
  const bool renamed = legacy_asset_document
    ? rename_asset_document(source_path, target_path)
    : kanplay_ns::storage_sd.renameFile(source_path.c_str(), target_path.c_str());
  if (!renamed) { return send_error(req, "500 Internal Server Error", "rename failed"); }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"result\":\"ok\"}");
}

static esp_err_t response_files(httpd_req_t* req)
{
  sampler_web_note_client_access();
  std::string name;
  const auto* dir = parse_dir_and_name(req, &name);
  if (!dir) { return send_error(req, "404 Not Found", "unknown sampler directory"); }
  // GETは一覧・ダウンロードだけなので、PSRAM再生を止める必要はない。
  // SDを書き換える操作に限り、既存仕様どおり先にオーディオを止める。
  if (req->method != HTTP_GET && !sampler_web_prepare_storage_operation()) {
    return send_error(req, "503 Service Unavailable", "audio stop timeout");
  }
  if (name.empty()) { return req->method == HTTP_GET ? list_files(req, *dir) : send_error(req, "405 Method Not Allowed", "file name required"); }
  if (req->method == HTTP_GET) { return get_file(req, *dir, name); }
  if (req->method == HTTP_PUT) { return put_file(req, *dir, name); }
  if (req->method == HTTP_DELETE) { return delete_file(req, *dir, name); }
  if (req->method == HTTP_POST) { return rename_file(req, *dir, name); }
  return send_error(req, "405 Method Not Allowed", "method not allowed");
}

static esp_err_t response_state(httpd_req_t* req)
{
  sampler_web_note_client_access();
  std::string json;
  if (!sampler_web_export_state(json)) { return send_error(req, "500 Internal Server Error", "state unavailable"); }
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.c_str(), json.size());
}

static esp_err_t response_audio(httpd_req_t* req)
{
  sampler_web_note_client_access();
  static constexpr const char prefix[] = "/api/sampler/audio/";
  const char* key = req->uri + sizeof(prefix) - 1;
  bool background = strcmp(key, "background.wav") == 0;
  uint8_t pad = 0;
  if (!background && (sscanf(key, "pad/%hhu.wav", &pad) != 1 || pad >= 12)) {
    return send_error(req, "404 Not Found", "unknown audio");
  }
  sampler_web_audio_t audio;
  if (!sampler_web_get_audio(background, pad, audio)) { return send_error(req, "404 Not Found", "audio unavailable"); }
  struct wav_header_t {
    char riff[4]; uint32_t size; char wave[4]; char fmt[4]; uint32_t fmt_size;
    uint16_t format; uint16_t channels; uint32_t rate; uint32_t byte_rate;
    uint16_t align; uint16_t bits; char data[4]; uint32_t data_size;
  } __attribute__((packed));
  const uint32_t bytes = audio.frames * sizeof(int16_t);
  wav_header_t header = {{'R','I','F','F'}, 36 + bytes, {'W','A','V','E'}, {'f','m','t',' '}, 16,
    1, 1, audio.sample_rate, audio.sample_rate * 2, 2, 16, {'d','a','t','a'}, bytes};
  httpd_resp_set_type(req, "audio/wav");
  httpd_resp_send_chunk(req, (const char*)&header, sizeof(header));
  const uint8_t* data = (const uint8_t*)audio.pcm;
  for (uint32_t offset = 0; offset < bytes; ) {
    size_t chunk = std::min<uint32_t>(4096, bytes - offset);
    if (httpd_resp_send_chunk(req, (const char*)data + offset, chunk) != ESP_OK) { break; }
    offset += chunk;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t response_command(httpd_req_t* req)
{
  sampler_web_note_client_access();
  if (req->content_len == 0 || req->content_len > 32 * 1024) { return send_error(req, "413 Payload Too Large", "command too large"); }
  std::vector<uint8_t> body(req->content_len);
  size_t read = 0;
  while (read < body.size()) {
    int got = httpd_req_recv(req, (char*)body.data() + read, body.size() - read);
    if (got <= 0) { return send_error(req, "400 Bad Request", "request failed"); }
    read += got;
  }
  if (!sampler_web_enqueue_command(body.data(), body.size())) { return send_error(req, "409 Conflict", "sampler is busy"); }
  httpd_resp_set_status(req, "202 Accepted");
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"result\":\"queued\"}");
}

static constexpr const httpd_uri uri_table[] = {
  { "/api/sampler/files/*", HTTP_GET,    response_files,   nullptr, false, false, nullptr },
  { "/api/sampler/files/*", HTTP_PUT,    response_files,   nullptr, false, false, nullptr },
  { "/api/sampler/files/*", HTTP_DELETE, response_files,   nullptr, false, false, nullptr },
  { "/api/sampler/files/*", HTTP_POST,   response_files,   nullptr, false, false, nullptr },
  { "/api/sampler/folders/*", HTTP_GET,  response_folders, nullptr, false, false, nullptr },
  { "/api/sampler/folders/*", HTTP_POST, response_folders, nullptr, false, false, nullptr },
  { "/api/sampler/state",   HTTP_GET,    response_state,   nullptr, false, false, nullptr },
  { "/api/sampler/audio/*", HTTP_GET,    response_audio,   nullptr, false, false, nullptr },
  { "/api/sampler/command", HTTP_POST,   response_command, nullptr, false, false, nullptr },
};

} // namespace

void sampler_web_api_register_uris(httpd_handle_t server)
{
  if (!server) { return; }
  web_sd_mounted = false;
  for (const auto& uri : uri_table) { httpd_register_uri_handler(server, &uri); }
}

void sampler_web_api_unregister_uris(httpd_handle_t server)
{
  if (!server) { return; }
  for (const auto& uri : uri_table) { httpd_unregister_uri_handler(server, uri.uri, uri.method); }
  web_sd_mounted = false;
}

} // namespace sampler_ns

#endif
