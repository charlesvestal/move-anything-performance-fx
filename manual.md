# Performance FX Manual

Performance FX is a live audio effects processor for Ableton Move. It provides pressure-sensitive punch-in effects, toggle-based continuous effects, scene snapshots, and a step FX sequencer — all controlled from Move's pads, knobs, and buttons.

## Getting Started

Open Performance FX from the **Tools menu** (Shift+Vol+Step13).

By default, it processes **Move Mix** — the main audio output from Move. You can switch to Line In (mic/instrument) or per-track audio via Link Audio.

The pad grid is divided into two zones:
- **Top two rows** (rows 3-4): Punch-in FX — hold to activate, release to stop
- **Bottom two rows** (rows 1-2): Continuous FX — tap to toggle on/off

## Audio Sources

Cycle sources with **Shift+Jog click**:

| Source | Description |
|--------|-------------|
| **Line In** | External mic or instrument input |
| **Move Mix** | Move's main stereo output |
| **Tracks** | Individual tracks via Link Audio (requires Link Audio enabled) |

### Track Selection (Tracks Mode)

When in Tracks mode, use the **track buttons** (1-4) to select which tracks to process:
- **Tap track**: Toggle that track on/off
- **Shift+Track**: Solo that track (mute all others)

Pressing any track button automatically switches to Tracks mode. The display header shows active tracks (e.g., `TRK 1234`).

Link Audio must be enabled in `features.json` for Tracks mode to work.

## Punch-in FX (Top Two Rows)

Hold a pad to activate the effect. Release to stop. **Pressure controls effect depth** — press harder for more intensity.

### Row 4 (Time-Based)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | RPT 1/4 | Beat repeat, quarter notes |
| 2 | RPT 1/8 | Beat repeat, eighth notes |
| 3 | RPT 1/16 | Beat repeat, sixteenth notes |
| 4 | RPT RP | Beat repeat, ramping |
| 5 | STUTTER | Glitch stutter |
| 6 | SCATTER | Rhythmic scatter |
| 7 | REVERSE | Reverse playback |
| 8 | HALF SP | Half-speed slowdown |

### Row 3 (Filter/Distortion)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | LP FLT | Low-pass filter sweep |
| 2 | HP FLT | High-pass filter sweep |
| 3 | BP FLT | Band-pass filter sweep |
| 4 | RESO PK | Resonant peak |
| 5 | CRUSH | Bit crusher |
| 6 | SR RED | Sample rate reduction |
| 7 | TAPE ST | Tape stop slowdown |
| 8 | DUCKER | Rhythmic ducking |

## Continuous FX (Bottom Two Rows)

Tap a pad to toggle the effect on/off. **Maximum 3 continuous FX can be active at once.** If you try to activate a 4th, a "Max FX" message appears.

### Row 2 (Delay/Reverb)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | DELAY | Standard delay |
| 2 | PING | Ping-pong delay |
| 3 | TAPE | Tape delay with wow/flutter |
| 4 | CLOUD | Granular cloud delay |
| 5 | PLATE | Plate reverb |
| 6 | DARK | Dark reverb |
| 7 | SPRING | Spring reverb |
| 8 | SHIMMER | Shimmer reverb |

### Row 1 (Modulation/Dynamics)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | CHORUS | Chorus |
| 2 | PHASER | Phaser |
| 3 | FLANGE | Flanger |
| 4 | LOFI | Lo-fi degradation |
| 5 | COMP | Compressor |
| 6 | SATUR | Saturation/drive |
| 7 | RING | Ring modulator |
| 8 | FREEZE | Spectral freeze |

### Selecting and Editing FX

When a continuous FX is active, its pad lights up. One active FX can be **selected** (shown with a white pad) — all 8 knobs then control that FX's parameters.

- **Tap inactive FX**: Activates it and selects it
- **Tap active but unselected FX**: Selects it (without toggling)
- **Tap active and selected FX**: Deselects it (returns knobs to global)
- **Shift+Tap active FX**: Deactivates it
- **Jog wheel turn**: Scrolls selection between active FX
- **Master knob (Vol)**: Controls the selected FX's mix level

## Encoders (Knobs 1-8)

### Global Mode (No FX Selected)

| Knob | Parameter |
|------|-----------|
| E1 | Dry/Wet mix |
| E2 | Input Gain |
| E3 | Low-pass Filter |
| E4 | High-pass Filter |
| E5 | EQ Low |
| E6 | EQ Mid |
| E7 | EQ High |
| E8 | Output Gain |

### FX Edit Mode (FX Selected)

| Knob | Parameter |
|------|-----------|
| E1-E4 | Primary FX parameters |
| E5-E8 | Secondary FX parameters |
| Master | FX mix level |

## Buttons

### Bypass (Undo Button)

- **Tap**: Toggle bypass on/off
- **Hold**: Momentary bypass (bypassed while held, restores on release)

### Scene Mode (Loop Button)

- **Tap Loop**: Toggle between FX mode and Scene mode (bottom rows switch function)

### Step FX Sequencer (Shift+Loop)

- **Shift+Loop**: Toggle the step FX sequencer on/off

### Transport

- **Play**: Start/stop the internal transport (for step sequencer and tempo-synced FX)
- **Rec**: Enter step record mode

### Ducker Rate (Copy Button)

- **Tap Copy** (in FX mode): Cycle ducker subdivision — 1/4, 1/8, 1/16

### Scene Operations (Copy/Delete in Scene Mode)

- **Copy+Pad**: Save current FX state to that scene slot
- **Delete+Pad**: Clear a scene slot
- **Shift+Delete**: Clear a step preset

### Recording

- **Capture**: Record the processed output to a WAV file

### Exit

- **Back**: Save state and exit to Tools menu

## Tempo

- **Jog click**: Tap tempo (2+ taps to calculate BPM)
- **Shift+Jog turn**: Fine BPM adjustment

Tempo affects beat repeat divisions, ducker timing, and the step FX sequencer.

The display header shows the current BPM (e.g., `PFX 120`).

## Scenes

16 scene slots store complete snapshots of all continuous FX states and global parameters.

1. Press **Loop** to enter Scene mode (bottom pad rows switch to scene recall)
2. **Tap a pad** to recall that scene
3. **Copy+Pad** to save the current state to a scene slot
4. **Delete+Pad** to clear a scene

### Scene Morphing

Crossfade smoothly between two scenes:
1. **Shift+Pad A** to set the morph start
2. **Shift+Pad B** to set the morph target
3. The system crossfades from A to B over approximately 2 seconds

## Step Presets

The 16 step buttons along the bottom of Move store FX state snapshots.

- **Tap step**: Recall that step's preset
- **Shift+Step**: Save current state to that step
- **Shift+Delete**: Clear a step preset

### Step FX Sequencer

The step sequencer automatically plays through step presets in time with the transport:

1. Save FX states to step buttons
2. Press **Shift+Loop** to enable the sequencer
3. Press **Play** to start the transport
4. The sequencer advances through populated steps in sync

**Change division** with **Shift+Jog turn** while the sequencer is active:
- 1/4 note
- 1/8 note
- 1/16 note
- 1 bar

## Persistence

State saves automatically every ~10 seconds and when you exit.

If a Move set is active, state is saved under that set's UUID — each set gets its own Performance FX configuration. Without a set, state is saved globally.

Saved data includes:
- All continuous FX states and knob values
- All 16 scenes
- All 16 step presets
- Audio source and track routing
- Tempo
- Bypass state
