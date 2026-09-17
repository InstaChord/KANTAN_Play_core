#!/usr/bin/env python3
"""Regression checks for the built-in Voice one-shot preset collection."""

from pathlib import Path
import wave


ROOT = Path(__file__).resolve().parents[1]
VOICE = ROOT / "docs/Sample_Sound/Voice"
SAMPLES = ROOT / "main/sampler/sampler_samples.hpp"

FILES = {
    "v_1.wav": "VOICE 1",
    "v_2.wav": "VOICE 2",
    "v_3.wav": "VOICE 3",
    "v_4.wav": "VOICE 4",
    "v_Go.wav": "GO",
    "v_Ha.wav": "HA",
    "v_Hey.wav": "HEY",
    "v_Yeah.wav": "YEAH",
    "v_jp_Hai.wav": "HAI",
}


def main() -> None:
    source = SAMPLES.read_text()
    total_bytes = 0
    for filename, label in FILES.items():
        path = VOICE / filename
        assert path.is_file(), path
        with wave.open(str(path), "rb") as audio:
            assert audio.getnchannels() == 1, filename
            assert audio.getsampwidth() == 2, filename
            assert audio.getframerate() == 32000, filename
            assert 0 < audio.getnframes() <= 32000, filename
        total_bytes += path.stat().st_size
        assert f'"Voice/{filename}"' in source, filename
        assert f'{{ "{label}"' in source, label

    assert total_bytes == 188514, total_bytes
    assert total_bytes < 200 * 1024
    assert source.count("sample_category_t::voice") >= len(FILES)
    print("PASS: 9 valid 32 kHz mono Voice presets, 188514-byte embedded budget")


if __name__ == "__main__":
    main()
