// SPDX-License-Identifier: MIT
// Copyright (c) 2026 InstaChord Corp.

#include "sampler_amy_probe.hpp"

#if defined(KANPLAY_SAMPLER) && defined(KANPLAY_AMY_PROBE) \
 && !defined(M5UNIFIED_PC_BUILD)

#include <AMY-Arduino.h>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>

// KANTAN Play already owns MIDI input/output. AMY's ESP32 single-thread path
// polls its UART unconditionally, even when midi=NONE, so provide the host
// hooks explicitly and keep MIDI out of the render benchmark.
extern "C" void run_midi() {}
extern "C" void stop_midi() {}
extern "C" void esp_poll_midi() {}
extern "C" void midi_out(uint8_t*, uint16_t) {}

namespace sampler_ns {
namespace {

constexpr uint16_t probe_max_oscs = 40;
constexpr uint8_t probe_voices = 9;
constexpr size_t probe_blocks = 128;

void probe_log(const char* format, ...)
{
  char buffer[256];
  va_list args;
  va_start(args, format);
  const int result = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  const size_t length = result <= 0
    ? 0u : std::min((size_t)result, sizeof(buffer) - 1u);
  (void)usb_serial_jtag_write_bytes(buffer, length, pdMS_TO_TICKS(100));
}

struct heap_snapshot_t {
  uint32_t internal_free;
  uint32_t internal_largest;
  uint32_t psram_free;
  uint32_t psram_largest;
};

heap_snapshot_t heap_snapshot()
{
  return {
    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
  };
}

void print_heap_delta(const heap_snapshot_t& before, const heap_snapshot_t& after)
{
  probe_log("AMY_PROBE heap internal=%lu (%ld) largest=%lu (%ld) "
            "psram=%lu (%ld) largest=%lu (%ld)\n",
            (unsigned long)after.internal_free,
            (long)after.internal_free - (long)before.internal_free,
            (unsigned long)after.internal_largest,
            (long)after.internal_largest - (long)before.internal_largest,
            (unsigned long)after.psram_free,
            (long)after.psram_free - (long)before.psram_free,
            (unsigned long)after.psram_largest,
            (long)after.psram_largest - (long)before.psram_largest);
}

void set_probe_oscillators(uint16_t active_count, float velocity)
{
  for (uint16_t osc = 0; osc < probe_max_oscs; ++osc) {
    amy_event event = amy_default_event();
    event.osc = osc;
    event.wave = (osc & 1u) ? SAW_DOWN : SINE;
    event.midi_note = 36.0f + (float)(osc % 36u);
    event.velocity = osc < active_count ? velocity : 0.0f;
    amy_add_event(&event);
  }
}

void benchmark_oscillators(uint16_t active_count)
{
  set_probe_oscillators(active_count, 0.18f);
  for (uint8_t i = 0; i < 16; ++i) { (void)amy_update(); }

  std::array<uint32_t, probe_blocks> usec{};
  uint64_t total_usec = 0;
  for (size_t i = 0; i < usec.size(); ++i) {
    const int64_t started = esp_timer_get_time();
    int16_t* block = amy_update();
    const uint32_t elapsed = (uint32_t)(esp_timer_get_time() - started);
    if (block == nullptr) {
      probe_log("AMY_PROBE render returned null at block=%u\n", (unsigned)i);
      return;
    }
    usec[i] = elapsed;
    total_usec += elapsed;
  }
  std::sort(usec.begin(), usec.end());
  const uint32_t block_budget_usec =
    (uint32_t)(((uint64_t)AMY_BLOCK_SIZE * 1000000u) / AMY_SAMPLE_RATE);
  const uint32_t average = (uint32_t)(total_usec / usec.size());
  const uint32_t p95 = usec[(usec.size() * 95u) / 100u];
  const uint32_t maximum = usec.back();
  probe_log("AMY_PROBE oscs=%u avg=%luus p95=%luus max=%luus budget=%luus "
            "p95_load=%lu%% max_load=%lu%%\n",
            (unsigned)active_count, (unsigned long)average,
            (unsigned long)p95, (unsigned long)maximum,
            (unsigned long)block_budget_usec,
            (unsigned long)((uint64_t)p95 * 100u / block_budget_usec),
            (unsigned long)((uint64_t)maximum * 100u / block_budget_usec));
}

} // namespace

void run_amy_startup_probe()
{
  const bool owns_usb_serial = !usb_serial_jtag_is_driver_installed();
  if (owns_usb_serial) {
    usb_serial_jtag_driver_config_t serial_config =
      USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    (void)usb_serial_jtag_driver_install(&serial_config);
  }
  // Give the host enough time to reopen USB Serial/JTAG after flashing. This
  // environment is a hardware benchmark, so startup latency is intentional.
  delay(15000);

  const heap_snapshot_t before = heap_snapshot();
  amy_config_t config = amy_default_config();
  config.features.reverb = 0;
  config.features.echo = 0;
  config.features.chorus = 0;
  config.features.partials = 0;
  config.features.custom = 0;
  config.features.audio_in = 0;
  config.features.default_synths = 0;
  config.features.startup_bleep = 0;
  config.platform.multicore = 0;
  config.platform.multithread = 0;
  config.midi = AMY_MIDI_IS_NONE;
  config.audio = AMY_AUDIO_IS_NONE;
  config.max_oscs = probe_max_oscs;
  config.ks_oscs = 0;
  config.max_sequencer_tags = 32;
  config.max_voices = probe_voices;
  config.max_synths = 3;
  config.max_memory_patches = 4;
  config.overload_threshold = 0;
  // Keep render-critical blocks in internal RAM. Larger control/state pools can
  // live in PSRAM and preserve internal memory for the existing audio task.
  config.ram_caps_events = MALLOC_CAP_SPIRAM;
  config.ram_caps_sysex = MALLOC_CAP_SPIRAM;
  config.ram_caps_synth = MALLOC_CAP_SPIRAM;
  config.ram_caps_delay = MALLOC_CAP_SPIRAM;
  config.ram_caps_sample = MALLOC_CAP_SPIRAM;
  config.ram_caps_block = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  config.ram_caps_fbl = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  probe_log("AMY_PROBE begin sr=%u block=%u oscs=%u voices=%u\n",
            (unsigned)AMY_SAMPLE_RATE, (unsigned)AMY_BLOCK_SIZE,
            (unsigned)config.max_oscs, (unsigned)config.max_voices);
  amy_start(config);
  const heap_snapshot_t initialized = heap_snapshot();
  print_heap_delta(before, initialized);

  for (uint8_t i = 0; i < 8; ++i) { (void)amy_update(); }
  for (const uint16_t count : { 16u, 24u, 32u, 40u }) {
    benchmark_oscillators(count);
  }

  set_probe_oscillators(probe_max_oscs, 0.0f);
  for (uint8_t i = 0; i < 4; ++i) { (void)amy_update(); }
  amy_stop();
  const heap_snapshot_t stopped = heap_snapshot();
  probe_log("AMY_PROBE stopped\n");
  print_heap_delta(before, stopped);
  (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1000));
  if (owns_usb_serial) { (void)usb_serial_jtag_driver_uninstall(); }
}

} // namespace sampler_ns

#endif
