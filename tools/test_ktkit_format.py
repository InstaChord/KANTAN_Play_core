#!/usr/bin/env python3
"""Host-side compatibility and corruption tests for KTKIT v1."""

import json
import struct
import unittest
import wave
import zlib
from io import BytesIO

MAGIC = b"KTKIT\r\n\x1a"
HEADER = struct.Struct("<8sHHBBHIIIII")
ENTRY = struct.Struct("<IIIIIIHH")
VERSION = 1


def user_wav_pcm():
    pcm = struct.pack("<64h", *[int((i - 32) * 700) for i in range(64)])
    stream = BytesIO()
    with wave.open(stream, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(32000)
        wav.writeframes(pcm)
    stream.seek(0)
    with wave.open(stream, "rb") as wav:
        return wav.readframes(wav.getnframes()), wav.getframerate(), wav.getnframes()


def build_package(kind, manifest, assets):
    manifest_bytes = json.dumps(manifest, separators=(",", ":")).encode()
    table_size = len(assets) * ENTRY.size
    payload_offset = HEADER.size + len(manifest_bytes) + table_size
    table = bytearray()
    payload = bytearray()
    offset = payload_offset
    for asset_id, pcm, rate, frames in assets:
        table += ENTRY.pack(asset_id, offset, len(pcm), zlib.crc32(pcm), rate, frames, 1, 0)
        payload += pcm
        offset += len(pcm)
    body = manifest_bytes + table + payload
    return HEADER.pack(MAGIC, VERSION, HEADER.size, kind, 0, len(assets),
                       len(manifest_bytes), table_size, payload_offset,
                       HEADER.size + len(body), zlib.crc32(body)) + body


def parse_package(data, expected_kind, budget):
    if len(data) < HEADER.size:
        raise ValueError("short")
    magic, version, header_size, kind, _flags, count, manifest_size, table_size, payload_offset, total_size, body_crc = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or header_size != HEADER.size or kind != expected_kind:
        raise ValueError("header")
    if manifest_size <= 0 or table_size != count * ENTRY.size:
        raise ValueError("length")
    if payload_offset != HEADER.size + manifest_size + table_size or total_size != len(data):
        raise ValueError("bounds")
    if zlib.crc32(data[HEADER.size:]) != body_crc:
        raise ValueError("container crc")
    manifest_end = HEADER.size + manifest_size
    manifest = json.loads(data[HEADER.size:manifest_end])
    offset = payload_offset
    total_assets = 0
    assets = {}
    for index in range(count):
        values = ENTRY.unpack_from(data, manifest_end + index * ENTRY.size)
        asset_id, asset_offset, size, crc, rate, frames, fmt, _ = values
        if not asset_id or asset_id in assets or asset_offset != offset or size != frames * 2 or fmt != 1:
            raise ValueError("asset table")
        end = asset_offset + size
        if end > len(data) or zlib.crc32(data[asset_offset:end]) != crc:
            raise ValueError("asset crc")
        assets[asset_id] = data[asset_offset:end]
        offset = end
        total_assets += size
    if offset != len(data) or total_assets > budget:
        raise ValueError("capacity")
    return manifest, assets


class KtkitFormatTest(unittest.TestCase):
    def setUp(self):
        self.pcm, self.rate, self.frames = user_wav_pcm()
        self.sample_manifest = {
            "formatVersion": 1,
            "kind": "sample-kit",
            "sampler": {"volume": 100},
            "samples": [
                {"internalPad": 0, "name": "Built-in Kick", "builtinId": "KICK 808"},
                {"internalPad": 1, "name": "User WAV", "assetId": 1, "frames": self.frames},
                {"internalPad": 2, "name": "Shared WAV", "assetId": 1, "frames": self.frames},
            ],
        }
        self.sample = build_package(1, self.sample_manifest,
                                    [(1, self.pcm, self.rate, self.frames)])
        self.beat_manifest = {
            "formatVersion": 1,
            "kind": "beat-kit",
            "drumKit": "dance",
            "pads": [
                {"internalPad": 0, "name": "Beat WAV", "assetId": 1},
                {"internalPad": 1, "name": "Shared Beat WAV", "assetId": 1},
            ],
        }
        self.beat = build_package(2, self.beat_manifest,
                                  [(1, self.pcm, self.rate, self.frames)])

    def test_sampler_round_trip_builtin_user_wav_and_deduplication(self):
        manifest, assets = parse_package(self.sample, 1, 5 * 1024 * 1024)
        self.assertEqual(manifest, self.sample_manifest)
        self.assertEqual(assets[1], self.pcm)
        self.assertEqual(len(assets), 1)
        self.assertEqual([p.get("assetId") for p in manifest["samples"]], [None, 1, 1])

    def test_beat_round_trip_and_deduplication(self):
        manifest, assets = parse_package(self.beat, 2, 1536 * 1024)
        self.assertEqual(manifest, self.beat_manifest)
        self.assertEqual(len(assets), 1)

    def assert_rejected_without_replacing_current(self, package, expected_kind, budget):
        current = {"kit": "unchanged"}
        try:
            parsed = parse_package(package, expected_kind, budget)
        except (ValueError, json.JSONDecodeError):
            parsed = None
        if parsed is not None:
            current = {"kit": "replaced"}
        self.assertEqual(current, {"kit": "unchanged"})

    def test_corrupt_magic_version_length_crc_and_capacity_preserve_current(self):
        cases = []
        bad = bytearray(self.sample); bad[0] ^= 1; cases.append((bytes(bad), 5 * 1024 * 1024))
        bad = bytearray(self.sample); bad[8:10] = struct.pack("<H", 99); cases.append((bytes(bad), 5 * 1024 * 1024))
        bad = bytearray(self.sample); bad[16:20] = struct.pack("<I", 0x7fffffff); cases.append((bytes(bad), 5 * 1024 * 1024))
        bad = bytearray(self.sample); bad[-1] ^= 1; cases.append((bytes(bad), 5 * 1024 * 1024))
        cases.append((self.sample, len(self.pcm) - 1))
        for package, budget in cases:
            with self.subTest(budget=budget, byte=len(package)):
                self.assert_rejected_without_replacing_current(package, 1, budget)

    def test_wrong_kit_kind_is_rejected(self):
        self.assert_rejected_without_replacing_current(self.beat, 1, 5 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
