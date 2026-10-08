# Third-party components

SW AUDIO plugins are built with the following libraries. All are fetched at build time (see CMakeLists.txt).

| Component | Use | License |
| --- | --- | --- |
| CLAP SDK (free-audio/clap 1.2.10) | plugin API | MIT |
| clap-wrapper (free-audio/clap-wrapper v0.16.0) | VST3 / AUv2 from the CLAP build | MIT |
| VST3 SDK (Steinberg, fetched by clap-wrapper) | VST3 wrapper | MIT (VST trademark/logo per Steinberg's guidelines) |
| AudioUnitSDK (Apple, macOS only, fetched by clap-wrapper) | AUv2 wrapper | Apache 2.0 |
| doctest 2.4.11 | unit tests only (not shipped) | MIT |
| Monocypher 4.0.3 (vendored in `core/third_party/monocypher/`) | Ed25519 checks of licence files | BSD-2-Clause or CC0-1.0 (dual; `LICENCE.md`) |
