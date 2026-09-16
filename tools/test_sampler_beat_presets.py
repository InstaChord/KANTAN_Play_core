#!/usr/bin/env python3
"""Static acceptance checks for the built-in Sampler Beat presets."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "main/sampler/sampler_app.cpp").read_text()
WEB = (ROOT / "docs/sampler-ui/app.js").read_text()
INDEX = (ROOT / "docs/sampler-ui/index.html").read_text()
WIFI = (ROOT / "main/task_wifi.cpp").read_text()


def block(pattern: str, text: str = SOURCE) -> str:
    match = re.search(pattern, text, re.S)
    assert match, f"missing source block: {pattern}"
    return match.group(1)


def hits(name: str) -> list[tuple[int, int, int]]:
    body = block(
        rf"static constexpr beat_pattern_hit_t builtin_{name}\[\]\s*=\s*\{{(.*?)\n\}};"
    )
    return [tuple(map(int, values)) for values in re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", body
    )]


def quoted_sources(array_name: str) -> list[list[str]]:
    bodies = re.findall(
        rf"static constexpr beat_sound_t {array_name}\[def::pad::pad_count\]\s*=\s*\{{(.*?)\n\}};",
        SOURCE,
        re.S,
    )
    assert bodies, f"missing Kit array: {array_name}"
    return [re.findall(r'\{\s*"([^"]+)"', body) for body in bodies]


metadata = block(
    r"static constexpr builtin_beat_pattern_t builtin_beat_patterns\[\]\s*=\s*\{(.*?)\n\};"
)
presets = re.findall(r'\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(\d+)\s*\}', metadata)
expected = [
    ("DISCO", "116"),
    ("POP", "100"),
    ("ROCK", "120"),
    ("HOUSE", "124"),
    ("HIP HOP", "88"),
    ("BREAK", "110"),
    ("FUNK", "104"),
    ("REGGAE", "82"),
]
assert presets == expected, f"unexpected preset order or tempo: {presets}"

for token, _bpm in presets:
    slug = token.lower().replace(" ", "")
    base = hits(f"pattern_{slug}")
    fill = hits(f"fill_{slug}")
    assert base, f"{token}: empty pattern"
    assert any(order == 0 for order, _tick, _velocity in base), f"{token}: no Kick"
    assert any(order == 1 for order, _tick, _velocity in base), f"{token}: no Snare"
    assert any(order in (7, 8, 10, 11) for order, _tick, _velocity in base), (
        f"{token}: no hat, shaker, or cymbal pulse"
    )
    assert len(base) * 2 + len(fill) <= 128, f"{token}: too many two-bar events"
    for section, values in (("base", base), ("fill", fill)):
        assert len({(order, tick) for order, tick, _velocity in values}) == len(values), (
            f"{token}: duplicate {section} hit"
        )
        for order, tick, velocity in values:
            assert 0 <= order < 12, f"{token}: Pad order out of range"
            assert 0 <= tick < 64, f"{token}: tick out of range"
            assert 1 <= velocity <= 127, f"{token}: velocity out of range"
    assert not ({(o, t) for o, t, _v in base} & {(o, t) for o, t, _v in fill}), (
        f"{token}: second-bar fill duplicates its base groove"
    )

midi_body = block(
    r"static constexpr uint8_t beat_midi_note_map\[def::pad::pad_count\]\s*=\s*\{(.*?)\n\};"
)
midi_notes = [int(value) for value in re.findall(r"\b\d+\b", re.sub(r"//.*", "", midi_body))]
assert midi_notes == [36, 40, 37, 39, 41, 43, 45, 42, 70, 49, 51, 46]

label_body = block(
    r"static constexpr const char\* beat_pad_labels\[def::pad::pad_count\]\s*=\s*\{(.*?)\n\};"
)
labels = re.findall(r'"([^"]+)"', label_body)
assert labels[-4:] == ["SHAKER", "CRASH", "RIDE", "HH-O"]

for array_name in ("acoustic_beat_sounds", "chiptune_beat_sounds", "dance_beat_sounds"):
    for sources in quoted_sources(array_name):
        assert len(sources) == 12, f"{array_name}: expected 12 sounds"
        roles = sources[-4:]
        assert "SHAKER" in roles[0]
        assert "CRASH" in roles[1]
        assert "RIDE" in roles[2]
        assert "HAT" in roles[3]
        assert not any("COWBELL" in source for source in roles)

assert "static uint8_t pending_beat_pattern = beat_preset_disco;" in SOURCE
assert "return beat_preset_disco;" in SOURCE
assert SOURCE.count("load_builtin_beat_pattern(beat_preset_disco)") >= 2
assert "beat_preset_disco," in SOURCE

for token, _bpm in presets:
    assert f"pattern:{token}" in WEB, f"Web preview is missing {token}"
assert "name:'DISCO PATTERN'" in WEB
assert "app.js?v=104-kts2-layer2" in INDEX
assert "app.js?v=104-kts2-layer2" in WIFI

print("PASS: 8 simple Beat presets, fixed Crash/Ride roles, legacy migration, and DISCO defaults")
