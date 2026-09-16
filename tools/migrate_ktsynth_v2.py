#!/usr/bin/env python3
"""Migrate unpublished single-layer KTS1 assets to the unified KTS2 format."""

from __future__ import annotations

import argparse
import binascii
import struct
from pathlib import Path


def chunks(blob: bytes) -> list[tuple[bytes, bytes]]:
    if blob[:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not RIFF/WAVE")
    result: list[tuple[bytes, bytes]] = []
    offset = 12
    while offset + 8 <= len(blob):
        kind, size = struct.unpack_from("<4sI", blob, offset)
        start = offset + 8
        end = start + size
        if end > len(blob):
            raise ValueError("truncated chunk")
        result.append((kind, blob[start:end]))
        offset = end + (size & 1)
    return result


def riff(parts: list[tuple[bytes, bytes]]) -> bytes:
    body = bytearray(b"WAVE")
    for kind, payload in parts:
        body += struct.pack("<4sI", kind, len(payload)) + payload
        if len(payload) & 1:
            body += b"\0"
    return b"RIFF" + struct.pack("<I", len(body)) + body


def migrate(blob: bytes) -> bytes:
    parts = chunks(blob)
    metadata = next((payload for kind, payload in parts if kind == b"KNTN"), None)
    pcm = next((payload for kind, payload in parts if kind == b"data"), None)
    if metadata is None or pcm is None:
        raise ValueError("KNTN/data chunk missing")
    if metadata[:4] == b"KTS2":
        if len(metadata) < 128 or struct.unpack_from("<H", metadata, 4)[0] != 2:
            raise ValueError("unsupported KANTAN Synth version")
        if struct.unpack_from("<H", metadata, 6)[0] == 1:
            return blob
        upgraded = bytearray(metadata)
        struct.pack_into("<H", upgraded, 6, 1)
        layer_count = upgraded[20]
        if not 1 <= layer_count <= 2:
            raise ValueError("invalid KTS2 layer count")
        for index in range(layer_count):
            descriptor = 24 + index * 48
            upgraded[descriptor + 39] = 0
            struct.pack_into("<4H", upgraded, descriptor + 40, 0, 0, 0, 32768)
        struct.pack_into("<I", upgraded, 16, 0)
        physical_pcm = [pcm]
        layer2 = next((payload for kind, payload in parts if kind == b"KT2D"), None)
        if layer2 is not None:
            physical_pcm.append(layer2)
        crc = binascii.crc32(upgraded)
        for payload in physical_pcm:
            crc = binascii.crc32(payload, crc)
        struct.pack_into("<I", upgraded, 16, crc & 0xFFFFFFFF)
        return riff([(kind, bytes(upgraded) if kind == b"KNTN" else payload)
                     for kind, payload in parts])
    if len(metadata) < 64 or metadata[:4] != b"KTS1" or struct.unpack_from("<H", metadata, 4)[0] != 1:
        raise ValueError("unsupported KANTAN Synth version")
    old_header, name_bytes = struct.unpack_from("<HH", metadata, 8)
    if old_header < 64 or old_header + name_bytes > len(metadata):
        raise ValueError("invalid KTS1 metadata")
    name = metadata[old_header:old_header + name_bytes]
    sample_rate, frames, start, end, loop_start, loop_end, crossfade = struct.unpack_from(
        "<7I", metadata, 16
    )
    attack, release, tune, gain = struct.unpack_from("<HHhH", metadata, 48)
    root, sustain = struct.unpack_from("<BB", metadata, 56)
    header = bytearray(128)
    struct.pack_into("<4sHHHHIIB3x", header, 0, b"KTS2", 2, 1, 128,
                     len(name), 0, 0, 1)
    struct.pack_into("<7IHHhHBBBB4H", header, 24, sample_rate, frames, start, end,
                     loop_start, loop_end, crossfade, attack, release, tune, gain,
                     root, sustain, 0, 0, 0, 0, 0, 32768)
    new_metadata = header + name
    crc = binascii.crc32(new_metadata)
    crc = binascii.crc32(pcm, crc) & 0xFFFFFFFF
    struct.pack_into("<I", new_metadata, 16, crc)
    return riff([(kind, bytes(new_metadata) if kind == b"KNTN" else payload)
                 for kind, payload in parts])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--in-place", action="store_true")
    args = parser.parse_args()
    for path in args.paths:
        converted = migrate(path.read_bytes())
        output = path if args.in_place else path.with_suffix(".kts2.ktsynth")
        output.write_bytes(converted)
        print(f"{path} -> {output}")


if __name__ == "__main__":
    main()
