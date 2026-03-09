# CLAUDE.md

Instructions for Claude Code when working with this repository.

## Project Overview

Performance FX is a live audio effects processor overtake module for [Move Anything](https://github.com/charlesvestal/move-anything) on Ableton Move hardware. It provides 32 pressure-sensitive punch-in audio effects with latching, scene snapshots, and a step FX sequencer.

Accessed via the Overtake Modules menu (Shift+Vol+Jog Click).

## Architecture

```
src/
  module.json           # Module metadata (tool, API v2)
  ui.js                 # JavaScript UI (pads, knobs, scenes, display)
  help.json             # On-device help text
  dsp/
    perf_fx_dsp.h       # DSP engine types and API
    perf_fx_dsp.c       # DSP engine implementation (all 32 effects)
    perf_fx_plugin.c    # Plugin API v2 wrapper (params, MIDI, rendering)
    test_perf_fx.c      # Unit tests (compile & run on host)
    plugin_api_v1.h     # Vendored from move-anything (host API ABI)
    pfx_track_shm.h     # Vendored from move-anything (Link Audio shm ABI)
scripts/
  build.sh              # Cross-compile via Docker
  install.sh            # Deploy to Move device
  Dockerfile            # ARM64 cross-compilation environment
```

### Vendored Headers

Two headers are vendored from `move-anything/src/host/`:

- **`plugin_api_v1.h`** — Host/plugin ABI. Defines `host_api_v1_t`, `plugin_api_v2_t`. Source of truth: `move-anything/src/host/plugin_api_v1.h`
- **`pfx_track_shm.h`** — Shared memory layout for per-track Link Audio audio. Source of truth: `move-anything/src/host/pfx_track_shm.h`

These are stable ABI contracts. If the host changes them, both repos must be updated.

### DSP Engine

All 32 effects are implemented in C with Bungee (C++ time-stretch library) for the stretch FX:

**Row 4 — Time/Repeat**: RPT 1/4, RPT 1/8, RPT 1/16, RPT Triplet, Stutter, Scatter, Reverse, Stretch
**Row 3 — Filter Sweeps**: LP Sweep, HP Sweep, BP Rise, BP Fall, Reso Sweep, Phaser, Flanger, AutoFilter
**Row 2 — Space/Delay**: Delay 1/4, Delay D8, PingPong 1/4, PingPong D8, Room, Hall, Dark Verb, Spring
**Row 1 — Distortion/Rhythm**: Crush, Downsample, Saturate, Gate, Tremolo, Octave Down, Vinyl, Vinyl Break

All FX are punch-in (hold=on, release=off) with pressure sensitivity and Shift+hold latching.

### UI Module

`ui.js` is a standalone JavaScript module following the Move Anything overtake module pattern:

- Exports `init()`, `tick()`, `onMidiMessageInternal()` via `globalThis`
- Uses shared utilities from `move-anything/src/shared/` (constants, display, input filter)
- Communicates with DSP via `host_module_set_param()` / `host_module_get_param()`
- Always starts fresh (no state persistence)

## Build Commands

```bash
./scripts/build.sh      # Build for ARM64 via Docker
./scripts/install.sh    # Deploy to Move
```

The build compiles `perf_fx_plugin.c` and `perf_fx_dsp.c` together into `dsp.so`, linked with `-lm -lrt` (math + POSIX shared memory).

### Running Tests Locally

```bash
cc -o test_pfx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -Isrc/dsp -lm && ./test_pfx
```

## Code Style

- **C**: Snake_case. Prefix engine functions with `pfx_`. Log with `pfx:` prefix.
- **JavaScript**: Follows Move Anything module conventions. Host functions are `snake_case`.
- **Parameters**: Keys are `snake_case` (e.g., `cont_5_param_2`, `track_mask`, `dry_wet`).

## Key Design Decisions

- **All punch-in**: Every FX is hold=on, release=off for live performance feel
- **Pressure curves**: Linear, exponential, and switch modes for punch-in intensity mapping
- **Immediate rate changes**: Repeat rate knob takes effect immediately (no waiting for loop quantum)
- **No state persistence**: Always starts fresh — removed to simplify

## Release

1. Update version in `src/module.json`
2. Commit: `git commit -am "bump version to 0.2.0"`
3. Tag and push: `git tag v0.2.0 && git push --tags`
4. GitHub Actions builds and uploads tarball
5. Add release notes: `gh release edit v0.2.0 --notes "- Changes here"`

## Host Requirements

- **Minimum host version**: 0.7.10
- Uses `audio_fx_api_v2` interface (available since early host versions)
