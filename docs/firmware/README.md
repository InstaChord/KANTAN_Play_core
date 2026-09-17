# Firmware OTA Catalog

`catalog.json` is the OTA firmware catalog published by GitHub Pages.
The device downloads this file and selects a firmware by `app`, `channel`, and board name.

## Channels

- `stable`: public stable release in `InstaChord/KANTAN_Play_core`
- `beta`: public beta release in `InstaChord/KANTAN_Play_core`
- `developer`: developer-only release in `u1nagata/KANTAN_Play_core`

The `developer` channel is shown on the device only after Developer Mode is enabled.

Current OTA build artifacts are kept in the repository root at `ota_bin/`. Historical
versioned binaries are kept separately in [archive/firmware](../../archive/firmware/README.md).

## Public Beta Release Flow

1. Build the OTA binary for CoreS3.
2. Create a Git tag in `InstaChord/KANTAN_Play_core`, for example `v0.9.1`.
3. Open GitHub Releases and create a new release from that tag.
4. Enable **Set as a pre-release**.
5. Upload `KANTAN_Play_CoreS3_OTA.bin` as a release asset.
6. Update the `beta` entry in `catalog.json`:
   - `version`: for example `0.9.1`
   - `url.cores3`: the matching release asset URL
7. Publish GitHub Pages.

## Stable Release Flow

1. Create a normal GitHub Release in `InstaChord/KANTAN_Play_core`.
2. Do not mark it as a pre-release.
3. Upload `KANTAN_Play_CoreS3_OTA.bin`.
4. Update the `stable` `version` in `catalog.json`.

The stable URL can keep using `/releases/latest/download/...` as long as the asset name stays stable.
