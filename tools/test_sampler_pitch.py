#!/usr/bin/env python3
"""Verify full-range, high-precision KANTAN Synth pitch ratios."""

import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "main/sampler/sampler_pitch.hpp"

int main() {
  using sampler_ns::sampler_pitch::note_pitch_q16;
  using sampler_ns::sampler_pitch::note_ratio_q16;
  assert(note_ratio_q16(60, 60) == 65536);
  assert(note_ratio_q16(60, 72) == 131072);
  assert(note_ratio_q16(60, 48) == 32768);

  // The two factory tones which exposed the old +/-24-semitone clamp must
  // continue descending below MIDI 56/65 instead of repeating one pitch.
  for (uint8_t root : {uint8_t(80), uint8_t(89)}) {
    uint32_t previous_ratio = 0;
    uint32_t previous_step = 0;
    for (unsigned note = 0; note < 128; ++note) {
      const uint32_t ratio = note_pitch_q16(256, root, (uint8_t)note);
      const uint32_t step = (uint32_t)(((uint64_t)32000 * ratio) / 48000);
      assert(ratio > previous_ratio);
      assert(step > previous_step);
      previous_ratio = ratio;
      previous_step = step;
    }
  }

  // Preserve the Sample Edit pitch multiplier around the new note ratio.
  assert(note_pitch_q16(128, 60, 60) == 32768);
  assert(note_pitch_q16(512, 60, 60) == 131072);
  puts("PASS: Q16 pitch stays continuous across MIDI 0-127 for high-root PCM");
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="sampler-pitch-test-") as directory:
        path = pathlib.Path(directory)
        source = path / "test.cpp"
        binary = path / "test"
        source.write_text(HARNESS)
        subprocess.run(
            ["c++", "-std=c++17", "-O2", "-I", str(ROOT), str(source), "-o", str(binary)],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
