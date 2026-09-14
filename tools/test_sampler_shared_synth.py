#!/usr/bin/env python3
"""Test shared builtin PCM lifetime and independently restored tone metadata."""
import pathlib
import subprocess
import tempfile
from test_sampler_pcm_render import ROOT, function

PREAMBLE = r'''
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#define M5UNIFIED_PC_BUILD 1
#include "main/sampler/sampler_pool.hpp"
#include "main/sampler/sampler_ktsynth.hpp"
struct { void delay(int) {} } M5;
namespace sampler_ns {
sample_slot_t sampler_pool_t::synth_source[sampler_pool_t::synth_source_count];
static sample_asset_t sampler_assets[sampler_pool_t::asset_capacity];
static void report_import_progress(uint32_t) {}
static void build_waveform_cache(sample_slot_t&) {}
'''
HARNESS = r'''
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream stream(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
  using namespace sampler_ns;
  assert(sampler_pool_t::loadSynthKtSynth(0, "Alto Sax", bytes.data(), bytes.size()));
  auto& source = sampler_pool_t::synth_source[0];
  auto* asset = source.asset;
  const auto authored = source;
  const size_t pcm_bytes = asset->bytes();
  assert(asset->references == 1 && sampler_pool_t::usedBytes() == pcm_bytes);
  source.volume_q8 = 7; source.synth_attack_ms = 999; source.start_frame += 100;
  assert(sampler_pool_t::shareSynth(1, 0, "Alto Sax", bytes.data(), bytes.size()));
  auto& shared = sampler_pool_t::synth_source[1];
  assert(shared.pcm == source.pcm && shared.asset == asset && asset->references == 2);
  assert(shared.volume_q8 == authored.volume_q8 && shared.synth_attack_ms == authored.synth_attack_ms
      && shared.start_frame == authored.start_frame);
  assert(source.volume_q8 == 7 && sampler_pool_t::usedBytes() == pcm_bytes);
  assert(sampler_pool_t::shareSynth(1, 0, "Alto Sax", bytes.data(), bytes.size()));
  assert(asset->references == 2); // replacement by the same asset retains exactly once
  bytes.back() ^= 1;
  assert(!sampler_pool_t::shareSynth(1, 0, "Alto Sax", bytes.data(), bytes.size()));
  assert(shared.asset == asset && asset->references == 2); // invalid import is non-destructive
  sampler_pool_t::eraseSynth(0);
  assert(shared.isValid() && asset->references == 1);
  sampler_pool_t::eraseSynth(1);
  assert(asset->references == 0 && sampler_pool_t::usedBytes() == 0);
  puts("PASS: Alto Sax PCM shared once, independent authored metadata, replacement refcount, CRC failure and final release");
}
'''


def main():
    source = (ROOT / "main/sampler/sampler_pool.cpp").read_text()
    signatures = ["static int16_t* pool_alloc(", "static void pool_free(",
                  "static sample_asset_t* pool_create_asset(",
                  "static void pool_retain_asset(", "static void pool_release_asset(",
                  "static void initialize_asset_sample_slot(",
                  "size_t sampler_pool_t::usedBytes(", "size_t sampler_pool_t::freeBytes(",
                  "static uint32_t remap_ktsynth_frame(",
                  "static size_t replaceable_slot_bytes(", "static void erase_synth_source_slot(",
                  "static bool load_synth_ktsynth_slot(", "bool sampler_pool_t::loadSynthKtSynth(",
                  "bool sampler_pool_t::shareSynth(", "void sampler_pool_t::eraseSynth("]
    with tempfile.TemporaryDirectory(prefix="sampler-shared-test-") as directory:
        path = pathlib.Path(directory)
        harness = path / "test.cpp"
        harness.write_text(PREAMBLE + "\n".join(function(source, s) for s in signatures) + HARNESS)
        binary = path / "test"
        subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT), str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(ROOT / "docs/Sample_Sound/KANTAN_Synth/Alto_Sax_Alto_Sax-D4.ktsynth")], check=True)


if __name__ == "__main__":
    main()
