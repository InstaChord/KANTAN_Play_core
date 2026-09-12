// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#include "sampler_ktkit.hpp"

#if defined(KANPLAY_SAMPLER)

#include <algorithm>
#include <cstring>

#include "../file_manage.hpp"

namespace sampler_ns {
namespace {

static constexpr uint8_t ktkit_magic[8] = {'K','T','K','I','T','\r','\n',0x1a};
static constexpr uint16_t ktkit_header_size = 36;
static constexpr uint16_t ktkit_asset_entry_size = 28;

static uint16_t read_u16(const uint8_t* p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t* p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

static void write_u16(uint8_t* p, uint16_t value)
{
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void write_u32(uint8_t* p, uint32_t value)
{
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
  p[2] = (uint8_t)(value >> 16);
  p[3] = (uint8_t)(value >> 24);
}

static bool stream_exact(kanplay_ns::storage_read_stream_t& stream,
                         uint8_t* data, size_t size)
{
  size_t done = 0;
  while (done < size) {
    const int read = kanplay_ns::storage_sd.readStream(&stream, data + done, size - done);
    if (read <= 0) { return false; }
    done += (size_t)read;
  }
  return true;
}

static bool append_exact(const char* path, const uint8_t* data, size_t size)
{
  return kanplay_ns::storage_sd.appendFromMemoryToFile(path, data, size) == (int)size;
}

static bool validate_internal(const char* path, ktkit_kind_t expected_kind,
                              uint32_t asset_budget, ktkit_package_t& package,
                              bool validate_crc)
{
  package = {};
  if (!path || !kanplay_ns::storage_sd.beginStorage()) { return false; }
  const int file_size_signed = kanplay_ns::storage_sd.getFileSize(path);
  if (file_size_signed < (int)ktkit_header_size
   || (uint32_t)file_size_signed > ktkit_max_file_bytes) { return false; }
  const uint32_t file_size = (uint32_t)file_size_signed;

  kanplay_ns::storage_read_stream_t stream;
  if (!kanplay_ns::storage_sd.openReadStream(path, &stream)) { return false; }
  uint8_t header[ktkit_header_size] = {};
  bool ok = stream_exact(stream, header, sizeof(header));
  if (!ok || memcmp(header, ktkit_magic, sizeof(ktkit_magic)) != 0
   || read_u16(header + 8) != ktkit_format_version
   || read_u16(header + 10) != ktkit_header_size
   || header[12] != (uint8_t)expected_kind) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return false;
  }
  const uint16_t asset_count = read_u16(header + 14);
  const uint32_t manifest_size = read_u32(header + 16);
  const uint32_t table_size = read_u32(header + 20);
  const uint32_t payload_offset = read_u32(header + 24);
  const uint32_t declared_size = read_u32(header + 28);
  const uint32_t declared_crc = read_u32(header + 32);
  const uint64_t expected_payload_offset = (uint64_t)ktkit_header_size
                                         + manifest_size + table_size;
  if (manifest_size == 0 || manifest_size > ktkit_max_manifest_bytes
   || asset_count > 32 || table_size != (uint32_t)asset_count * ktkit_asset_entry_size
   || expected_payload_offset != payload_offset || declared_size != file_size
   || payload_offset > file_size) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return false;
  }

  package.kind = expected_kind;
  package.manifest.resize(manifest_size);
  if (!stream_exact(stream, reinterpret_cast<uint8_t*>(package.manifest.data()), manifest_size)) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return false;
  }
  uint32_t body_crc = ktkit_crc32(reinterpret_cast<const uint8_t*>(package.manifest.data()),
                                  package.manifest.size());
  std::vector<uint8_t> table(table_size);
  if (table_size && !stream_exact(stream, table.data(), table.size())) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return false;
  }
  if (table_size) { body_crc = ktkit_crc32(table.data(), table.size(), body_crc); }

  uint64_t summed_bytes = 0;
  uint32_t previous_end = payload_offset;
  package.assets.reserve(asset_count);
  for (uint16_t i = 0; i < asset_count; ++i) {
    const uint8_t* entry = table.data() + i * ktkit_asset_entry_size;
    ktkit_asset_info_t asset;
    asset.id = read_u32(entry);
    asset.offset = read_u32(entry + 4);
    asset.size = read_u32(entry + 8);
    asset.crc32 = read_u32(entry + 12);
    asset.sample_rate = read_u32(entry + 16);
    asset.frames = read_u32(entry + 20);
    const uint16_t format = read_u16(entry + 24);
    if (asset.id == 0 || asset.size == 0 || format != 1
     || asset.sample_rate < 8000 || asset.sample_rate > 96000
     || asset.frames == 0 || asset.frames > UINT32_MAX / sizeof(int16_t)
     || asset.size != asset.frames * sizeof(int16_t)
     || asset.offset != previous_end || (uint64_t)asset.offset + asset.size > file_size) {
      kanplay_ns::storage_sd.closeReadStream(&stream);
      return false;
    }
    for (const auto& prior : package.assets) {
      if (prior.id == asset.id) {
        kanplay_ns::storage_sd.closeReadStream(&stream);
        return false;
      }
    }
    summed_bytes += asset.size;
    if (summed_bytes > asset_budget) {
      kanplay_ns::storage_sd.closeReadStream(&stream);
      return false;
    }
    previous_end = asset.offset + asset.size;
    package.assets.push_back(asset);
  }
  if (previous_end != file_size) {
    kanplay_ns::storage_sd.closeReadStream(&stream);
    return false;
  }

  uint8_t buffer[4096];
  for (const auto& asset : package.assets) {
    if (!kanplay_ns::storage_sd.seekStream(&stream, asset.offset)) { ok = false; break; }
    uint32_t remaining = asset.size;
    uint32_t asset_crc = 0xffffffffu;
    while (remaining) {
      const size_t chunk = std::min<size_t>(remaining, sizeof(buffer));
      if (!stream_exact(stream, buffer, chunk)) { ok = false; break; }
      asset_crc = ktkit_crc32(buffer, chunk, asset_crc);
      body_crc = ktkit_crc32(buffer, chunk, body_crc);
      remaining -= (uint32_t)chunk;
    }
    if (!ok || (asset_crc ^ 0xffffffffu) != asset.crc32) { ok = false; break; }
  }
  kanplay_ns::storage_sd.closeReadStream(&stream);
  if (!ok || (validate_crc && (body_crc ^ 0xffffffffu) != declared_crc)) { return false; }
  package.total_asset_bytes = (uint32_t)summed_bytes;
  return true;
}

} // namespace

uint32_t ktkit_crc32(const uint8_t* data, size_t size, uint32_t state)
{
  if (!data) { return state; }
  uint32_t crc = state;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return crc;
}

bool ktkit_write_atomic(const char* path, ktkit_kind_t kind,
                        const uint8_t* manifest, size_t manifest_size,
                        const std::vector<ktkit_asset_source_t>& assets)
{
  if (!path || !manifest || manifest_size == 0
   || manifest_size > ktkit_max_manifest_bytes || assets.size() > 32
   || !kanplay_ns::storage_sd.beginStorage()) { return false; }
  const std::string temporary = std::string(path) + ".tmp";
  const std::string backup = std::string(path) + ".bak";
  kanplay_ns::storage_sd.removeFile(temporary.c_str());
  kanplay_ns::storage_sd.removeFile(backup.c_str());

  const uint32_t table_size = (uint32_t)assets.size() * ktkit_asset_entry_size;
  const uint32_t payload_offset = ktkit_header_size + (uint32_t)manifest_size + table_size;
  uint64_t total_size64 = payload_offset;
  for (size_t index = 0; index < assets.size(); ++index) {
    const auto& asset = assets[index];
    if (asset.id == 0 || !asset.data || asset.size == 0 || asset.frames == 0
     || asset.size != asset.frames * sizeof(int16_t)) { return false; }
    for (size_t prior = 0; prior < index; ++prior) {
      if (assets[prior].id == asset.id) { return false; }
    }
    total_size64 += asset.size;
  }
  if (total_size64 > ktkit_max_file_bytes) { return false; }
  const uint32_t total_size = (uint32_t)total_size64;

  std::vector<uint8_t> table(table_size, 0);
  uint32_t offset = payload_offset;
  for (size_t i = 0; i < assets.size(); ++i) {
    const auto& asset = assets[i];
    uint8_t* entry = table.data() + i * ktkit_asset_entry_size;
    write_u32(entry, asset.id);
    write_u32(entry + 4, offset);
    write_u32(entry + 8, asset.size);
    write_u32(entry + 12, ktkit_crc32(asset.data, asset.size) ^ 0xffffffffu);
    write_u32(entry + 16, asset.sample_rate);
    write_u32(entry + 20, asset.frames);
    write_u16(entry + 24, 1); // signed PCM16 little-endian, mono
    offset += asset.size;
  }

  uint32_t body_crc = ktkit_crc32(manifest, manifest_size);
  if (!table.empty()) { body_crc = ktkit_crc32(table.data(), table.size(), body_crc); }
  for (const auto& asset : assets) { body_crc = ktkit_crc32(asset.data, asset.size, body_crc); }
  body_crc ^= 0xffffffffu;

  uint8_t header[ktkit_header_size] = {};
  memcpy(header, ktkit_magic, sizeof(ktkit_magic));
  write_u16(header + 8, ktkit_format_version);
  write_u16(header + 10, ktkit_header_size);
  header[12] = (uint8_t)kind;
  write_u16(header + 14, (uint16_t)assets.size());
  write_u32(header + 16, (uint32_t)manifest_size);
  write_u32(header + 20, table_size);
  write_u32(header + 24, payload_offset);
  write_u32(header + 28, total_size);
  write_u32(header + 32, body_crc);

  bool ok = kanplay_ns::storage_sd.saveFromMemoryToFile(
              temporary.c_str(), header, sizeof(header)) == (int)sizeof(header)
         && append_exact(temporary.c_str(), manifest, manifest_size)
         && (table.empty() || append_exact(temporary.c_str(), table.data(), table.size()));
  for (const auto& asset : assets) {
    if (!ok) { break; }
    const uint8_t* cursor = asset.data;
    uint32_t remaining = asset.size;
    while (remaining) {
      const size_t chunk = std::min<size_t>(remaining, 8192);
      if (!append_exact(temporary.c_str(), cursor, chunk)) { ok = false; break; }
      cursor += chunk;
      remaining -= (uint32_t)chunk;
    }
  }
  ktkit_package_t verified;
  ok = ok && validate_internal(temporary.c_str(), kind, UINT32_MAX, verified, true);
  if (!ok) {
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return false;
  }

  const bool replacing = kanplay_ns::storage_sd.getFileSize(path) >= 0;
  if (replacing && !kanplay_ns::storage_sd.renameFile(path, backup.c_str())) {
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return false;
  }
  if (!kanplay_ns::storage_sd.renameFile(temporary.c_str(), path)) {
    if (replacing) { kanplay_ns::storage_sd.renameFile(backup.c_str(), path); }
    kanplay_ns::storage_sd.removeFile(temporary.c_str());
    return false;
  }
  if (replacing) { kanplay_ns::storage_sd.removeFile(backup.c_str()); }
  return true;
}

bool ktkit_open_validate(const char* path, ktkit_kind_t expected_kind,
                         uint32_t asset_budget, ktkit_package_t& package)
{
  return validate_internal(path, expected_kind, asset_budget, package, true);
}

bool ktkit_read_asset(const char* path, const ktkit_asset_info_t& asset,
                      uint8_t* destination, size_t capacity)
{
  if (!path || !destination || capacity < asset.size
   || !kanplay_ns::storage_sd.beginStorage()) { return false; }
  kanplay_ns::storage_read_stream_t stream;
  if (!kanplay_ns::storage_sd.openReadStream(path, &stream)) { return false; }
  const bool ok = asset.offset <= stream.size
               && (uint64_t)asset.offset + asset.size <= stream.size
               && kanplay_ns::storage_sd.seekStream(&stream, asset.offset)
               && stream_exact(stream, destination, asset.size)
               && (ktkit_crc32(destination, asset.size) ^ 0xffffffffu) == asset.crc32;
  kanplay_ns::storage_sd.closeReadStream(&stream);
  return ok;
}

const ktkit_asset_info_t* ktkit_find_asset(const ktkit_package_t& package,
                                           uint32_t id)
{
  for (const auto& asset : package.assets) {
    if (asset.id == id) { return &asset; }
  }
  return nullptr;
}

} // namespace sampler_ns

#endif
