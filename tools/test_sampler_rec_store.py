#!/usr/bin/env python3
"""Compile real Rec storage functions and compare timing data with v0.8.6."""
import pathlib
import subprocess
import tempfile
from test_sampler_pcm_render import ROOT, function

PREAMBLE = r'''
#include <algorithm>
#include <vector>
#include <stdint.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include "main/sampler/sampler_performance_probe.hpp"
namespace performance_probe = sampler_ns::performance_probe;
enum class performance_page_t { sample, drum, bass, melody, chord, max };
enum class loop_event_type_t { note_on, note_off, pitch_bend_down, pitch_bend_center, pitch_bend_up };
static constexpr uint8_t beat_velocity_default = 100;
static uint8_t sanitize_beat_velocity(uint8_t v) { return v; }
'''
STATE = r'''
static std::vector<loop_event_t> loop_events;
static std::vector<loop_event_t> published;
static std::vector<uint16_t> loop_undo_history[5];
static constexpr size_t loop_event_max = 512;
static uint32_t loop_length_msec = 4000, loop_events_revision = 1, loop_playback_revision = 1;
static bool loop_record_full_notice_pending = false, loop_timeline_cache_valid = true, loop_quantize_enabled = true;
static constexpr uint32_t loop_min_length_ms = 200, loop_live_min_gate_ms = 16;
static int loop_cursor_prev_x = 0, loop_timeline_dirty_span_count = 0;
struct loop_events_guard_t {};
static bool append_loop_playback_snapshot(const loop_event_t& event) { published.push_back(event); return true; }
static bool append_loop_timeline_event(const loop_event_t&) { return true; }
static void advance_loop_events_revision() { ++loop_events_revision; }
static void invalidate_loop_timeline_cache() { loop_timeline_cache_valid = false; }
static unsigned loop_note_off_quantize_steps() { return 32; }
'''


def storage(source, name):
    event = source[source.index("struct loop_event_t {"):source.index("enum class recording_source_t")]
    signatures = ["static bool loop_event_is_pitch_bend(",
                  "static uint32_t loop_note_off_after_note_on(",
                  "static bool normalize_synth_note_off_positions_unlocked(",
                  "static bool loop_note_on_exists_unlocked(",
                  "static bool reserve_loop_event_room_unlocked(",
                  "static void push_loop_event(performance_page_t page"]
    return f"namespace {name} {{\n" + event + STATE + "\n".join(function(source, s) for s in signatures) + "}\n"


HARNESS = r'''
static void compare() {
  assert(reference::loop_events.size() == current::loop_events.size());
  for (size_t i = 0; i < reference::loop_events.size(); ++i) {
    auto a = reference::loop_events[i]; auto b = current::loop_events[i];
    assert(a.page == b.page && a.pad == b.pad && a.type == b.type
        && a.pos_ms == b.pos_ms && a.layer == b.layer && a.velocity == b.velocity);
  }
}
static void push(performance_page_t page, uint8_t pad, loop_event_type_t type, uint32_t pos, uint16_t layer) {
  reference::push_loop_event(page, pad, type, pos, layer);
  current::push_loop_event(page, pad, type, pos, layer);
  compare();
  // Every accepted release is published with its final position. No full
  // snapshot rebuild should be needed just to make a Note Off playable.
  if (!current::published.empty() && type == loop_event_type_t::note_off) {
    assert(current::published.back().pos_ms == current::loop_events.back().pos_ms);
  }
}
int main() {
  using P = performance_page_t; using E = loop_event_type_t;
  for (bool quantized : {false, true}) {
    reference::loop_events.clear(); current::loop_events.clear();
    current::published.clear(); reference::published.clear();
    reference::loop_quantize_enabled = current::loop_quantize_enabled = quantized;
    push(P::chord, 0, E::note_off, 0, 999); // orphan
    for (uint16_t layer = 1; layer <= 255; ++layer) {
      P page = (P)(layer % 5); uint8_t pad = layer % 12;
      uint32_t on = (layer * 123u) % 4000;
      push(page, pad, E::note_on, on, layer);
      push(page, pad, E::note_off, layer % 3 ? on : (on + 125) % 4000, layer);
    }
    push(P::chord, 0, E::note_on, 3999, 300);
    push(P::chord, 0, E::note_off, 3999, 300); // wrap, full capacity
    push(P::melody, 1, E::note_on, 100, 301); // rejected at capacity
  }
  puts("PASS: Rec positions match for quantize on/off, all parts, collapsed gates, loop wrap, orphan Off and full 512-event capacity");
  auto begin = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 10000; ++i) {
    reference::loop_events.resize(510);
    reference::push_loop_event(P::chord, 0, E::note_on, 3999, 300);
    reference::push_loop_event(P::chord, 0, E::note_off, 3999, 300);
  }
  auto middle = std::chrono::steady_clock::now();
  for (unsigned i = 0; i < 10000; ++i) {
    current::loop_events.resize(510);
    current::push_loop_event(P::chord, 0, E::note_on, 3999, 300);
    current::push_loop_event(P::chord, 0, E::note_off, 3999, 300);
  }
  auto end = std::chrono::steady_clock::now();
  double old_time = std::chrono::duration<double, std::micro>(middle - begin).count() / 10000;
  double new_time = std::chrono::duration<double, std::micro>(end - middle).count() / 10000;
  printf("HOST 512-event On/Off store old=%.2fus new=%.2fus ratio=%.3f\n", old_time, new_time, new_time / old_time);
}
'''


def main():
    source = (ROOT / "main/sampler/sampler_app.cpp").read_text()
    baseline = subprocess.check_output(["git", "show", "2418bc88:main/sampler/sampler_app.cpp"], cwd=ROOT, text=True)
    with tempfile.TemporaryDirectory(prefix="sampler-rec-test-") as directory:
        path = pathlib.Path(directory)
        harness = path / "test.cpp"
        harness.write_text(PREAMBLE + storage(baseline, "reference") + storage(source, "current") + HARNESS)
        binary = path / "test"
        subprocess.run(["c++", "-std=c++17", "-O2", "-I", str(ROOT), str(harness), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
