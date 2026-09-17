#!/usr/bin/env python3
"""Test shared builtin PCM lifetime and independently restored tone metadata."""
import pathlib
import subprocess
import tempfile
import binascii
import struct
from test_sampler_pcm_render import ROOT, function
from migrate_ktsynth_v2 import chunks, riff

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
synth_layer_slot_t sampler_pool_t::synth_layer2[sampler_pool_t::synth_source_count];
uint8_t sampler_pool_t::synth_layer_count[sampler_pool_t::synth_source_count] = { 1, 1, 1 };
static sample_asset_t sampler_assets[sampler_pool_t::asset_capacity];
static void report_import_progress(uint32_t) {}
static void build_waveform_cache(sample_slot_t&) {}
'''
HARNESS = r'''
}
int main(int argc, char** argv) {
  assert(argc == 5);
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
  std::ifstream layered_stream(argv[2], std::ios::binary);
  std::vector<uint8_t> layered((std::istreambuf_iterator<char>(layered_stream)), {});
  assert(sampler_pool_t::loadSynthKtSynth(0, "Layered", layered.data(), layered.size()));
  assert(sampler_pool_t::synth_layer_count[0] == 2);
  auto* primary_asset = sampler_pool_t::synth_source[0].asset;
  auto* colour_asset = sampler_pool_t::synth_layer2[0].asset;
  assert(primary_asset && colour_asset && primary_asset != colour_asset);
  assert(sampler_pool_t::shareSynth(1, 0, "Layered", layered.data(), layered.size()));
  assert(sampler_pool_t::synth_layer_count[1] == 2);
  assert(sampler_pool_t::synth_source[1].asset == primary_asset);
  assert(sampler_pool_t::synth_layer2[1].asset == colour_asset);
  assert(primary_asset->references == 2 && colour_asset->references == 2);
  sampler_pool_t::eraseSynth(0);
  sampler_pool_t::eraseSynth(1);
  assert(sampler_pool_t::usedBytes() == 0);
  std::ifstream shared_pcm_stream(argv[3], std::ios::binary);
  std::vector<uint8_t> shared_pcm((std::istreambuf_iterator<char>(shared_pcm_stream)), {});
  assert(sampler_pool_t::loadSynthKtSynth(0, "Shared PCM", shared_pcm.data(), shared_pcm.size()));
  assert(sampler_pool_t::synth_layer_count[0] == 2);
  auto& shared_primary = sampler_pool_t::synth_source[0];
  auto& shared_colour = sampler_pool_t::synth_layer2[0];
  assert(shared_primary.asset == shared_colour.asset && shared_primary.pcm == shared_colour.pcm);
  assert(shared_primary.asset->references == 2);
  assert(sampler_pool_t::usedBytes() == shared_primary.asset->bytes());
  assert(shared_colour.synth_delay_100us == 7 && shared_colour.synth_hold_ms == 3
      && shared_colour.synth_decay_ms == 900 && shared_colour.synth_sustain_level_q15 == 8192);
  sampler_pool_t::eraseSynth(0);
  assert(sampler_pool_t::usedBytes() == 0);
  std::ifstream saw_stream(argv[4], std::ios::binary);
  std::vector<uint8_t> saw((std::istreambuf_iterator<char>(saw_stream)), {});
  assert(sampler_pool_t::loadSynthKtSynth(0, "Saw Lead", saw.data(), saw.size()));
  assert(sampler_pool_t::synth_layer_count[0] == 2);
  assert(sampler_pool_t::synth_source[0].asset == sampler_pool_t::synth_layer2[0].asset);
  assert(sampler_pool_t::synth_source[0].synth_tune_cents == -19);
  assert(sampler_pool_t::synth_layer2[0].synth_tune_cents == -27);
  assert(sampler_pool_t::synth_source[0].synth_delay_100us == 10);
  assert(sampler_pool_t::synth_layer2[0].synth_delay_100us == 3);
  sampler_pool_t::eraseSynth(0);
  assert(sampler_pool_t::usedBytes() == 0);
  puts("PASS: KTS2 one/two-layer assets parse, share independently, reject CRC damage and release cleanly");
}
'''


def make_layered(blob: bytes, share_pcm: bool = False) -> bytes:
    parts = chunks(blob)
    metadata = bytearray(next(payload for kind, payload in parts if kind == b"KNTN"))
    pcm = next(payload for kind, payload in parts if kind == b"data")
    metadata[20] = 2
    metadata[72:120] = metadata[24:72]
    metadata[72 + 38] = 0 if share_pcm else 1
    struct.pack_into("<4H", metadata, 72 + 40, 7, 3, 900, 8192)
    struct.pack_into("<I", metadata, 16, 0)
    crc = binascii.crc32(metadata)
    crc = binascii.crc32(pcm, crc)
    if not share_pcm:
        crc = binascii.crc32(pcm, crc)
    crc &= 0xFFFFFFFF
    struct.pack_into("<I", metadata, 16, crc)
    output = []
    for kind, payload in parts:
        output.append((kind, bytes(metadata) if kind == b"KNTN" else payload))
        if kind == b"data" and not share_pcm:
            output.append((b"KT2D", pcm))
    return riff(output)


def main():
    source = (ROOT / "main/sampler/sampler_pool.cpp").read_text()
    signatures = ["static int16_t* pool_alloc(", "static void pool_free(",
                  "static sample_asset_t* pool_create_asset(",
                  "static void pool_retain_asset(", "static void pool_release_asset(",
                  "static void initialize_asset_sample_slot(",
                  "size_t sampler_pool_t::usedBytes(", "size_t sampler_pool_t::freeBytes(",
                  "static uint32_t remap_ktsynth_frame(",
                  "static size_t replaceable_slot_bytes(", "static void erase_synth_source_slot(",
                  "static size_t replaceable_synth_bytes(", "static void erase_synth_layer_slot(",
                  "static int16_t resampled_ktsynth_frame(",
                  "static uint16_t ktsynth_tune_scale_q12(",
                  "static void apply_ktsynth_layer(sample_slot_t&",
                  "static void apply_ktsynth_layer(synth_layer_slot_t&",
                  "static bool load_synth_ktsynth_slot(", "bool sampler_pool_t::loadSynthKtSynth(",
                  "bool sampler_pool_t::shareSynth(", "void sampler_pool_t::eraseSynth("]
    with tempfile.TemporaryDirectory(prefix="sampler-shared-test-") as directory:
        path = pathlib.Path(directory)
        harness = path / "test.cpp"
        harness.write_text(PREAMBLE + "\n".join(function(source, s) for s in signatures) + HARNESS)
        binary = path / "test"
        source_asset = ROOT / "docs/Sample_Sound/KANTAN_Synth/Alto_Sax_Alto_Sax-D4.ktsynth"
        layered_asset = path / "layered.ktsynth"
        layered_asset.write_bytes(make_layered(source_asset.read_bytes()))
        shared_pcm_asset = path / "shared-pcm.ktsynth"
        shared_pcm_asset.write_bytes(make_layered(source_asset.read_bytes(), True))
        subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT), str(harness), "-o", str(binary)], check=True)
        saw_asset = ROOT / "docs/Sample_Sound/KANTAN_Synth/Saw_Lead-F_4.ktsynth"
        subprocess.run([str(binary), str(source_asset), str(layered_asset),
                        str(shared_pcm_asset), str(saw_asset)], check=True)


if __name__ == "__main__":
    main()
