# KTKIT file format

`.ktkit` is the portable, self-contained Kit package used by KANTAN Sampler.
It deliberately does not replace the Project format: a Project stores a whole
performance, while a KTKIT stores either the twelve Sampler Pad sounds or the
twelve Pattern Beat Pad sounds.

## Version 1 layout

All integer fields are unsigned little-endian. Offsets are absolute file
offsets. Sections and assets are contiguous, so a reader can validate every
range without decompressing or loading the complete file.

| Header offset | Size | Field |
|---:|---:|---|
| 0 | 8 | magic `KTKIT\r\n\x1a` |
| 8 | 2 | container format version (`1`) |
| 10 | 2 | header size (`36`) |
| 12 | 1 | Kit kind (`1` Sampler, `2` Beat) |
| 13 | 1 | flags, reserved (`0`) |
| 14 | 2 | asset count |
| 16 | 4 | JSON manifest size |
| 20 | 4 | asset table size |
| 24 | 4 | binary payload offset |
| 28 | 4 | complete file size |
| 32 | 4 | CRC32 of manifest + table + payload |

The UTF-8 JSON manifest follows the header. It contains `formatVersion`, a
`kind` of `sample-kit` or `beat-kit`, Pad settings, and either `builtinId` or
`assetId` for every populated Pad. Sampler slice Pads may additionally specify
`assetOffsetFrames` and `frames` so several Pads can share one stored PCM asset.

Each 28-byte asset-table entry contains, in order: asset ID, absolute payload
offset, byte size, asset CRC32, sample rate, frame count, payload format, and a
reserved field. Payload format `1` is signed mono PCM16 little-endian. A given
PCM payload is emitted once even when several Pads reference it.

Built-in sounds use a stable `builtinId` and have no binary payload. User WAV,
recording, imported MP3, chopped material, and other RAM-resident user sounds
are stored as their decoded PCM, making the package independent of the original
SD file and decoder version.

## Validation and atomicity

Readers reject an unknown magic, container or manifest version, wrong Kit kind,
duplicate Pad or asset ID, non-contiguous/out-of-range section, inconsistent
PCM metadata, CRC failure, or a package whose decoded assets exceed the target
pool budget. All structural and CRC checks complete before the current Kit is
cleared. An unexpected allocation failure during Sampler Kit application uses
a temporary self-contained rollback package.

Writers create `<name>.ktkit.tmp`, finish and re-open it for complete validation,
then replace the destination with rename/backup semantics. Therefore power loss
or an interrupted write cannot turn the previous destination into a partially
valid package.

## Compatibility and future extension

- New Kit saves use `.ktkit`. Existing Kit JSON remains load-only; saving it
  again creates `.ktkit`.
- `Default_Kit.ktkit` is preferred. `Default_Kit.json` is still accepted when
  the new default is absent.
- Project and LittleFS Resume remain their existing JSON formats.
- Future format changes must increment the container version when header/table
  semantics change. Additive manifest keys may be introduced within a version
  only when old readers can safely ignore them. New payload encodings require a
  new asset `format` value and must preserve streaming range/CRC validation.
