# Performance FX Manual

Performance FX is a live audio effects processor for Ableton Move. It provides 32 pressure-sensitive punch-in effects with latching — all controlled from Move's pads, knobs, and buttons.

## Getting Started

Open Performance FX from the **Overtake Modules menu** (Shift+Vol+Jog Click), then select it.

It processes **Move Mix** — the main audio output from Move. Play something on Move, then hold any pad to hear the effect. Press harder for more intensity.

All 32 pads are punch-in FX: hold a pad to activate, release to stop. Shift+hold to latch an FX on.

## FX Layout

### Row 4 — Time/Repeat (Orange)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | RPT 1/4 | Beat repeat, quarter notes |
| 2 | RPT 1/8 | Beat repeat, eighth notes |
| 3 | RPT 1/16 | Beat repeat, sixteenth notes |
| 4 | RPT TRP | Beat repeat, triplet |
| 5 | STUTTER | Glitch stutter |
| 6 | SCATTER | Rhythmic slice rearrangement |
| 7 | REVERSE | Reverse playback |
| 8 | STRETCH | Time-stretch slowdown (Bungee) |

### Row 3 — Filter Sweeps (Blue)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | LP SWP | Low-pass filter sweep down |
| 2 | HP SWP | High-pass filter sweep up |
| 3 | BP RISE | Band-pass filter sweep up |
| 4 | BP FALL | Band-pass filter sweep down |
| 5 | RESO SW | Resonant sweep |
| 6 | PHASER | Phaser |
| 7 | FLANGER | Flanger |
| 8 | AUTOFLT | Auto-filter (LFO-swept SVF) |

### Row 2 — Space/Delay (Purple)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | DLY 1/4 | Delay, quarter note |
| 2 | DLY D8 | Delay, dotted eighth |
| 3 | PP 1/4 | Ping-pong delay, quarter note |
| 4 | PP D8 | Ping-pong delay, dotted eighth |
| 5 | ROOM | Room reverb |
| 6 | HALL | Hall reverb |
| 7 | DK VERB | Dark reverb |
| 8 | SPRING | Spring reverb |

### Row 1 — Distortion/Rhythm (Pink/Green/Yellow)

| Pad | Effect | Description |
|-----|--------|-------------|
| 1 | CRUSH | Bit crusher |
| 2 | DWNSMPL | Sample rate reduction |
| 3 | SATURATE | Saturation/drive |
| 4 | GATE | Rhythmic gate |
| 5 | TREMOLO | Tremolo/autopan |
| 6 | OCT DN | Octave down pitch shift |
| 7 | VINYL | Vinyl noise overlay |
| 8 | VNL BRK | Vinyl brake slowdown |

## Encoders

| Knob | Parameter |
|------|-----------|
| E1 | Repeat Length |
| E2 | Repeat Speed (detent at center = normal) |
| E3 | Repeat Loop on/off (turn right = on, left = off) |
| E4 | Per-FX param 1 (last touched pad) |
| E5 | Per-FX param 2 (last touched pad) |
| E6 | Per-FX param 3 (last touched pad) |
| E7 | Tilt EQ |
| E8 | DJ Filter |

Touch any knob to see its current value on the display. Tap a latched pad to select it for E4-E6 editing.

## Pressure

Each FX responds to pad pressure differently:

- **Repeats**: Pitch drift amount
- **Filter sweeps**: Sweep speed / resonance
- **Phaser/Flanger**: Depth
- **Delays**: Feedback amount
- **Reverbs**: Decay length
- **Bitcrush**: Bit depth
- **Saturate**: Drive amount
- **Tremolo**: Depth
- **Gate**: Gate threshold

## Latching

Any FX can be latched (stays on after release):

- **Shift+Hold pad**: Latch the FX (pad pulses)
- **Shift+Hold latched pad**: Unlatch it
- **Hold pad then press Shift**: Also latches/unlatches

Multiple FX can be latched simultaneously for layered effects.

## Buttons

| Button | Action |
|--------|--------|
| **Undo** (tap) | Toggle bypass on/off |
| **Undo** (hold) | Momentary bypass (bypassed while held) |
| **Jog turn** | Coarse BPM adjustment (+/- 1 BPM) |
| **Shift+Jog turn** | Fine BPM adjustment (+/- 0.5 BPM) |
| **Back** | Exit |

## Tempo

BPM range: 20-300. Adjust with jog wheel turn (coarse) or Shift+Jog turn (fine).

The display header shows current BPM and active FX count.
