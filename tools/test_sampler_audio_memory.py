#!/usr/bin/env python3
"""Exercise production cache allocation against a bounded fake internal heap."""
import pathlib
import subprocess
import tempfile
from test_sampler_pcm_render import ROOT, function

PREAMBLE = r'''
#include <algorithm>
#include <stdint.h>
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <map>
static constexpr unsigned MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_8BIT = 2;
static size_t available = 200 * 1024, largest = 200 * 1024;
static std::map<void*, size_t> allocations;
static size_t heap_caps_get_free_size(unsigned) { return available; }
static size_t heap_caps_get_largest_free_block(unsigned) { return std::min(largest, available); }
static void* heap_caps_malloc(size_t bytes, unsigned) {
  if (bytes > available || bytes > largest) return nullptr;
  void* ptr = std::malloc(bytes); assert(ptr); allocations[ptr] = bytes; available -= bytes; return ptr;
}
static void fake_free(void* ptr) {
  if (!ptr) return;
  available += allocations.at(ptr); allocations.erase(ptr); std::free(ptr);
}
struct voice_t { bool active = false; uint8_t sustain_cache_slot = 255; };
static voice_t voices[30];
class sampler_audio_t {
public:
  static bool isSynthSustainCacheInUse(uint8_t slot);
  static void primeSynthSustainCache(uint8_t, const int16_t*, uint32_t, uint32_t, uint32_t = 4096, uint32_t = 8192);
  static size_t releaseUnusedSynthSustainCacheMemory();
};
#define free fake_free
'''
HARNESS = r'''
#undef free
static int16_t pcm[20000];
int main() {
  sampler_audio_t::primeSynthSustainCache(0, pcm, 128, 256);
  assert(synth_sustain_cache[0].attack_capacity == 128);
  assert(synth_sustain_cache[0].capacity == 128);
  assert(available == 200 * 1024 - 512); // actual windows, not fixed 24KB
  voices[0] = {true, 0};
  auto* original = synth_sustain_cache[0].pcm;
  sampler_audio_t::primeSynthSustainCache(0, pcm, 256, 1024);
  assert(synth_sustain_cache[0].pcm == original && synth_sustain_cache[0].start == 128);
  assert(sampler_audio_t::releaseUnusedSynthSustainCacheMemory() == 0);
  voices[0].active = false;
  assert(sampler_audio_t::releaseUnusedSynthSustainCacheMemory() == 512);
  // 48KB budget across all six slots, including lazy Sampler caches.
  for (unsigned slot = 0; slot < 6; ++slot) {
    sampler_audio_t::primeSynthSustainCache(slot, pcm, 4096, 12288);
  }
  assert(200 * 1024 - available == 48 * 1024);
  sampler_audio_t::releaseUnusedSynthSustainCacheMemory();
  available = 49 * 1024; largest = 49 * 1024;
  sampler_audio_t::primeSynthSustainCache(0, pcm, 4096, 12288);
  assert(allocations.empty()); // preserve free-heap floor
  available = 100 * 1024; largest = 17 * 1024;
  sampler_audio_t::primeSynthSustainCache(0, pcm, 4096, 12288);
  assert(allocations.empty()); // preserve contiguous allocation headroom
  largest = 100 * 1024;
  sampler_audio_t::primeSynthSustainCache(0, pcm, 0, 256);
  assert(synth_sustain_cache[0].frames == 256 && synth_sustain_cache[0].attack_capacity == 0);
  sampler_audio_t::releaseUnusedSynthSustainCacheMemory();
  assert(allocations.empty());
  puts("PASS: exact cache windows, 48KB total cap, low/fragmented heap fallback, zero-length attack and active-cache lifetime");
}
'''


def main():
    source = (ROOT / "main/sampler/sampler_audio.cpp").read_text()
    cache = source[source.index("static constexpr uint8_t synth_sustain_cache_count"):source.index("static inline int16_t voice_pcm_at")]
    functions = "\n".join(function(source, signature) for signature in [
        "bool sampler_audio_t::isSynthSustainCacheInUse(",
        "void sampler_audio_t::primeSynthSustainCache(",
        "size_t sampler_audio_t::releaseUnusedSynthSustainCacheMemory("])
    with tempfile.TemporaryDirectory(prefix="sampler-memory-test-") as directory:
        path = pathlib.Path(directory)
        harness = path / "test.cpp"
        harness.write_text(PREAMBLE + cache + functions + HARNESS)
        binary = path / "test"
        subprocess.run(["c++", "-std=c++17", "-O2", str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
