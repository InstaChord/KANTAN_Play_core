# Development Documentation

This directory holds the developer-facing documentation for the two firmware variants.

## KANTAN Play core

- [Architecture](./core/architecture.md): source layout, build environments, and runtime design
- [HTTP API](./core/api.md): browser and SD-card data API
- [Song format](./core/song-format.md): song and progression data format
- [MIDI module guide](./core/midi-module.md): shared MIDI driver and BLE/UART/USB transports
- [Wi-Fi API module guide](./core/wifi-api-module.md): Web API layer used by the Wi-Fi task

## KANTAN Sampler

- [Product specification](./sampler/product-spec.md): manuals, advertising, Web copy, interaction, and UI source of truth
- [Manual content plan](./sampler/manual-content-plan.md): beginner onboarding order, manual routes, and required visual references
- [Manual image briefs](./sampler/manual-image-briefs.md): shot list, required UI state, and asset filenames for manual images
- [Program specification](./sampler/program-spec.md): implementation and data model
- [Preset library production guide](./sampler/preset-library-production-guide.md): built-in and official Web Library selection, capacity, and production workflow
- [Preset Project structure guide](./sampler/preset-project-structure-guide.md): Sample Kit, Beat Kit, Beat Pattern, synth, Key/Scale, and Project packaging
- [Beat Pattern production guide](./sampler/beat-pattern-production-guide.md): MIDI note mapping, timing, velocity, limits, and Work production workflow
- [Beat part test plan](./sampler/beat-part-test-plan.md): ordered hardware verification for Audio/Pattern Beat integration
- [OneLibrary integration handoff](./sampler/onelibrary-integration-handoff.md): rekordbox SD metadata research, adapter design, synchronization, and staged implementation
- [AMY synth feasibility validation](./sampler/amy-synth-validation.md): pinned dependency, baseline budgets, probe environment, and staged integration plan
- [SoundFont converter interface](./sampler/sf2-converter-interface.md): one-region SF2 conversion contract and KANTAN Sample Synth metadata
- [Development guide](./sampler/development.md): build, install, and development guidance
