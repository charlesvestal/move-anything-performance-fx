# Move Everything Performance FX

Live punch-in audio effects processor for [Move Everything](https://github.com/charlesvestal/move-anything) on Ableton Move hardware. 32 pressure-sensitive FX pads with latching.

## Prerequisites

- [Move Everything](https://github.com/charlesvestal/move-anything) v0.7.10+ installed on your Ableton Move

## Installation

### Via Module Store (Recommended)

1. Launch Move Everything on your Move
2. Open **Overtake Modules** (Shift+Vol+Jog Click)
3. Select **Module Store**
4. Navigate to **Performance FX** and install

### Manual Installation

```bash
./scripts/build.sh
./scripts/install.sh
```

## Features

- **32 punch-in FX pads** — hold to activate, release to stop
- **Pressure sensitivity** — press harder for more effect intensity
- **Shift+hold latching** — lock FX on, Shift+hold again to unlatch
- **Global EQ** — tilt EQ and DJ filter on dedicated knobs

### FX Layout

| Row | Color  | FX |
|-----|--------|----|
| 4 (top) | Orange | RPT 1/4, RPT 1/8, RPT 1/16, RPT Trip, Stutter, Scatter, Reverse, Stretch |
| 3 | Blue | LP Sweep, HP Sweep, BP Rise, BP Fall, Reso Sweep, Phaser, Flanger, AutoFilter |
| 2 | Purple | Delay 1/4, Delay D8, PingPong 1/4, PingPong D8, Room, Hall, Dark Verb, Spring |
| 1 (bottom) | Pink/Green/Yellow | Crush, Downsample, Saturate, Gate, Tremolo, Octave Down, Vinyl, Vinyl Break |

### Encoders

| Knob | Function |
|------|----------|
| E1 | Repeat Length |
| E2 | Repeat Speed |
| E3 | Repeat Loop on/off |
| E4-E6 | Per-FX params (last touched pad) |
| E7 | Tilt EQ |
| E8 | DJ Filter |

### Controls

| Control | Function |
|---------|----------|
| Pads | Hold = FX on, release = off |
| Pressure | Modulates FX depth per-effect |
| Shift+Pad | Latch/unlatch FX |
| Undo (tap) | Bypass toggle |
| Undo (hold) | Momentary bypass |
| Jog turn | Coarse BPM |
| Shift+Jog turn | Fine BPM |
| Back | Exit |

## Building

Requires Docker for ARM64 cross-compilation:

```bash
./scripts/build.sh      # Build via Docker
./scripts/install.sh    # Deploy to Move
```

## Third-Party Libraries

- **[Bungee](https://github.com/kupix/bungee)** — Time-stretching library (MPL-2.0)
- **[Eigen](https://eigen.tuxfamily.org/)** — Linear algebra (MPL-2.0)
- **[PFFFT](https://bitbucket.org/jpommier/pffft/)** — FFT library (BSD-like, FFTPACK license)
- **[cxxopts](https://github.com/jarro2783/cxxopts)** — Command-line parser (MIT) (build-time dependency of Bungee)
- **RevSC** — Costello/Soundpipe-style FDN reverb (MIT, clean reimplementation)

## License

MIT
