#!/usr/bin/env python3
"""Pack one or two mono PCM16 WAV files into the unified KTS2 container."""

from __future__ import annotations

import argparse
import binascii
import json
import struct
import wave
from pathlib import Path

from migrate_ktsynth_v2 import riff


def read_pcm(path: Path) -> tuple[int, int, bytes]:
    with wave.open(str(path), "rb") as source:
        if source.getnchannels() != 1 or source.getsampwidth() != 2 or source.getcomptype() != "NONE":
            raise ValueError(f"{path}: requires uncompressed PCM16 mono WAV")
        rate = source.getframerate()
        frames = source.getnframes()
        pcm = source.readframes(frames)
    if not 8000 <= rate <= 48000 or frames < 16 or frames > rate * 20:
        raise ValueError(f"{path}: rate must be 8-48kHz and length 16 frames-20 seconds")
    return rate, frames, pcm


def descriptor(layer: dict, base: Path, index: int,
               built: list[tuple[bytes, bytes, int, int, int]]) -> tuple[bytes, bytes, int, int, int]:
    pcm_source = int(layer.get("pcmSourceLayer", index))
    if pcm_source < 0 or pcm_source > index:
        raise ValueError(f"layer {index}: pcmSourceLayer must reference this or an earlier layer")
    wav_path = (base / layer["wav"]).resolve() if "wav" in layer else None
    if pcm_source < index:
        if wav_path is not None:
            raise ValueError(f"layer {index}: shared PCM must not include wav")
        rate, frames, pcm = built[pcm_source][2], struct.unpack_from("<I", built[pcm_source][0], 4)[0], built[pcm_source][1]
    elif wav_path is not None:
        rate, frames, pcm = read_pcm(wav_path)
    else:
        raise ValueError(f"layer {index}: own PCM requires wav")
    start = int(layer.get("startFrame", 0))
    end = int(layer.get("endFrame", frames))
    loop_start = int(layer.get("loopStartFrame", 0))
    loop_end = int(layer.get("loopEndFrame", 0))
    crossfade = int(layer.get("loopCrossfadeFrames", 0))
    sustain = 1 if layer.get("sustainMode", "off") == "loop" else 0
    attack = int(layer.get("attackMs", 0))
    release = int(layer.get("releaseMs", 120))
    delay_100us = int(round(float(layer.get("delayMs", 0)) * 10.0))
    hold = int(layer.get("holdMs", 0))
    decay = int(layer.get("decayMs", 0))
    sustain_level = int(layer.get("sustainLevelQ15", 32768))
    tune = int(layer.get("tuneCents", 0))
    gain = int(layer.get("defaultGainQ8", 256 if index == 0 else 0))
    root = int(layer.get("rootNote", 60))
    if not 0 <= start < end <= frames:
        raise ValueError(f"layer {index}: invalid Start/End")
    if sustain:
        if not start <= loop_start < loop_end <= end or crossfade > (loop_end - loop_start) // 4:
            raise ValueError(f"layer {index}: invalid loop")
    elif loop_start or loop_end or crossfade:
        raise ValueError(f"layer {index}: loop coordinates require sustainMode=loop")
    if not (0 <= crossfade <= 65535 and 0 <= attack <= 5000 and 10 <= release <= 10000
            and 0 <= delay_100us <= 65535 and 0 <= hold <= 5000 and 0 <= decay <= 60000
            and 0 <= sustain_level <= 32768 and -100 <= tune <= 100
            and 0 <= gain <= 512 and 0 <= root <= 127):
        raise ValueError(f"layer {index}: metadata out of range")
    packed = struct.pack("<7IHHhHBBBB4H", rate, frames, start, end, loop_start,
                         loop_end, crossfade, attack, release, tune, gain,
                         root, sustain, pcm_source, 0, delay_100us, hold, decay,
                         sustain_level)
    return packed, pcm, rate, gain, pcm_source


def build(manifest_path: Path) -> bytes:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    layers = manifest.get("layers", [])
    if not 1 <= len(layers) <= 2:
        raise ValueError("layers must contain one or two entries")
    name = str(manifest.get("name", manifest_path.stem)).encode("utf-8")
    if len(name) > 63:
        raise ValueError("UTF-8 name exceeds 63 bytes")
    built = []
    for index, layer in enumerate(layers):
        built.append(descriptor(layer, manifest_path.parent, index, built))
    if sum(item[3] for item in built) > 512:
        raise ValueError("combined defaultGainQ8 exceeds 512")
    header = bytearray(128)
    struct.pack_into("<4sHHHHIIB3x", header, 0, b"KTS2", 2, 1, 128,
                     len(name), 0, 0, len(built))
    for index, item in enumerate(built):
        header[24 + index * 48:72 + index * 48] = item[0]
    metadata = header + name
    crc = binascii.crc32(metadata)
    for index, (_, pcm, _, _, pcm_source) in enumerate(built):
        if pcm_source == index:
            crc = binascii.crc32(pcm, crc)
    struct.pack_into("<I", metadata, 16, crc & 0xFFFFFFFF)
    fmt = struct.pack("<HHIIHH", 1, 1, built[0][2], built[0][2] * 2, 2, 16)
    parts = [(b"fmt ", fmt), (b"KNTN", bytes(metadata)), (b"data", built[0][1])]
    if len(built) == 2 and built[1][4] == 1:
        parts.append((b"KT2D", built[1][1]))
    output = riff(parts)
    if len(output) > 2 * 1024 * 1024:
        raise ValueError("KTS2 file exceeds 2 MiB")
    return output


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_bytes(build(args.manifest))
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
