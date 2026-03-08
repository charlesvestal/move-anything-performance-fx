# CLAUDE.md

Instructions for Claude Code when working with this repository.

## Project Overview

Performance FX is a live audio effects processor tool module for [Move Anything](https://github.com/charlesvestal/move-anything) on Ableton Move hardware. It provides 32 audio effects (16 momentary punch-in + 16 continuous), scene snapshots, step FX sequencer, and per-track Link Audio processing.

Accessed via the Tools menu (Shift+Vol+Step13).

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

All 32 effects are implemented in pure C with no external DSP libraries:

**Punch-in FX** (momentary, pressure-sensitive):
- Beat repeats (1/4, 1/8, 1/16, triplet), stutter, scatter, reverse, half-speed
- LP/HP/BP filter sweeps, resonant peak, bitcrush, sample rate reduce, tape stop, ducker

**Continuous FX** (toggled, max 3 simultaneous):
- Delays: standard, ping-pong, tape echo, granular cloud
- Reverbs: plate, dark, spring, shimmer
- Modulation: chorus, phaser, flanger, lo-fi, ring mod
- Dynamics: compressor, saturator, freeze

Each continuous FX has 8 parameters mapped to the 8 encoders when selected.

### Audio Sources

The module reads audio from three possible sources:
- **Line In** — Direct from Move's audio input
- **Move Mix** — Move's main stereo output (from host mapped memory)
- **Tracks** — Per-track audio via Link Audio shared memory (`/move-track-audio`)

Track source requires Link Audio enabled in the host and the shim writing to the shared memory segment. The plugin gracefully handles the shm being unavailable.

### UI Module

`ui.js` is a standalone JavaScript module (~1600 lines) following the Move Anything tool module pattern:

- Exports `init()`, `tick()`, `onMidiMessageInternal()` via `globalThis`
- Uses shared utilities from `move-anything/src/shared/` (constants, display, input filter)
- Communicates with DSP via `host_module_set_param()` / `host_module_get_param()`
- State persistence to `/data/UserData/move-anything/set_state/<uuid>/perf_fx_state.json`

### State Persistence

State auto-saves every ~10 seconds and on exit. Per-set persistence: if a Move set is active, state saves under that set's UUID. Includes all FX states, knob values, 16 scenes, 16 step presets, source/track config, and tempo.

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

- **Max 3 continuous FX**: Hardware constraint — more causes audio underruns on the CM4
- **Pressure curves**: Linear, exponential, and switch modes for punch-in intensity mapping
- **Scene morphing**: Crossfade between two scenes over ~2 seconds (per-sample interpolation in DSP)
- **Step FX sequencer**: Advances through populated step presets synced to BPM

## Release

1. Update version in `src/module.json`
2. Commit: `git commit -am "bump version to 0.2.0"`
3. Tag and push: `git tag v0.2.0 && git push --tags`
4. GitHub Actions builds and uploads tarball
5. Add release notes: `gh release edit v0.2.0 --notes "- Changes here"`

## Host Requirements

- **Minimum host version**: 0.8.0 (for Link Audio track shm support)
- **Link Audio**: Optional but needed for per-track source mode. Shim writes to `/move-track-audio` shm.
- The host's `pfx_track_shm.h` and this repo's copy must match.
