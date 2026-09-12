// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#ifndef KANTAN_SAMPLER_KTKIT_HPP
#define KANTAN_SAMPLER_KTKIT_HPP

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace sampler_ns {

enum class ktkit_kind_t : uint8_t {
  sampler = 1,
  beat = 2,
};

struct ktkit_asset_source_t {
  uint32_t id = 0;
  const uint8_t* data = nullptr;
  uint32_t size = 0;
  uint32_t sample_rate = 0;
  uint32_t frames = 0;
};

struct ktkit_asset_info_t {
  uint32_t id = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
  uint32_t crc32 = 0;
  uint32_t sample_rate = 0;
  uint32_t frames = 0;
};

struct ktkit_package_t {
  ktkit_kind_t kind = ktkit_kind_t::sampler;
  std::string manifest;
  std::vector<ktkit_asset_info_t> assets;
  uint32_t total_asset_bytes = 0;
};

static constexpr uint16_t ktkit_format_version = 1;
static constexpr uint32_t ktkit_max_manifest_bytes = 128 * 1024;
static constexpr uint32_t ktkit_max_file_bytes = 7 * 1024 * 1024;

uint32_t ktkit_crc32(const uint8_t* data, size_t size, uint32_t state = 0xffffffffu);

bool ktkit_write_atomic(const char* path, ktkit_kind_t kind,
                        const uint8_t* manifest, size_t manifest_size,
                        const std::vector<ktkit_asset_source_t>& assets);

bool ktkit_open_validate(const char* path, ktkit_kind_t expected_kind,
                         uint32_t asset_budget, ktkit_package_t& package);

bool ktkit_read_asset(const char* path, const ktkit_asset_info_t& asset,
                      uint8_t* destination, size_t capacity);

const ktkit_asset_info_t* ktkit_find_asset(const ktkit_package_t& package,
                                           uint32_t id);

} // namespace sampler_ns

#endif
