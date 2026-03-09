# Performance FX v2: All Punch-Ins Redesign

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the punch-in + continuous FX split with 32 unified punch-in FX pads — all momentary by default, shift+pad to latch, with per-FX pressure mappings and animated time-based effects.

**Architecture:** Every pad is a punch-in FX. Hold = on, release = off (with fade tail for space FX). Shift+hold = latch (stays on). E1-E4 control the last-touched pad's parameters. E5-E8 are always global (HPF, LPF, Dry/Wet, EQ Bump). Scenes snapshot latched pads + params + globals. Volume knob passes through to host.

**Tech Stack:** C (DSP), JavaScript (UI), ARM64 cross-compilation via Docker

---

## Design: 32 FX Types

### Row 4 — Time/Repeat (Orange) — Pads 0-7
| Pad | FX | On Tap | Pressure | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|---|---|
| 0 | RPT 1/4 | Repeats at 1/4 | Pitch drift on repeats | Division fine | Decay | Filter | Gate |
| 1 | RPT 1/8 | Repeats at 1/8 | Pitch drift | Division fine | Decay | Filter | Gate |
| 2 | RPT 1/16 | Repeats at 1/16 | Pitch drift | Division fine | Decay | Filter | Gate |
| 3 | RPT Trip | Repeats at triplet | Pitch drift | Division fine | Decay | Filter | Gate |
| 4 | Stutter | Rapid micro-repeat | Grain length | Division fine | Decay | Filter | Gate |
| 5 | Scatter | Random slice rearrange | Chaos amount | Slice size | Reverse prob | Filter | — |
| 6 | Reverse | Reverse playback | Playback speed | Length | Filter | — | — |
| 7 | Half-Spd | Half-speed playback | Speed amount | — | Filter | — | — |

### Row 3 — Filter Sweeps (Blue) — Pads 8-15
Animated: a `phase` counter runs 0→1 while held, driving the sweep.

| Pad | FX | On Tap | Pressure | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|---|---|
| 8 | LP Sweep ↓ | Filter starts closing | Sweep speed | Sweep range | Resonance | Start freq | — |
| 9 | HP Sweep ↑ | Filter starts opening | Sweep speed | Sweep range | Resonance | Start freq | — |
| 10 | BP Rise | Bandpass sweeps up | Sweep speed | Width (Q) | Resonance | Start freq | — |
| 11 | BP Fall | Bandpass sweeps down | Sweep speed | Width (Q) | Resonance | Start freq | — |
| 12 | Reso Sweep | Resonant peak sweeps | Sweep speed | Q amount | Sweep range | — | — |
| 13 | Phaser | Phaser sweeps | LFO speed | Depth | Feedback | Stages | — |
| 14 | Flanger | Flanger sweeps | LFO speed | Depth | Feedback | — | — |
| 15 | Auto Filt | LFO-driven filter | LFO speed | Depth | Resonance | Shape (LP/BP/HP) | — |

### Row 2 — Space Throws (Purple) — Pads 16-23
"Throws": audio feeds into effect while held. On release, the tail decays naturally. State (delay buffer, reverb) persists through release fade.

| Pad | FX | On Tap | Pressure | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|---|---|
| 16 | Delay | Delay throw | Feedback amount | Time | Feedback | Filter | — |
| 17 | Ping Pong | Stereo ping pong | Feedback amount | Time | Feedback | Spread | — |
| 18 | Tape Echo | Warm tape delay | Feedback amount | Time (Age) | Wow/Flutter | Feedback | — |
| 19 | Echo Freeze | Infinite feedback delay | — (feedback locked at 1.0) | Time | — | Filter | — |
| 20 | Reverb | Big reverb swell | Decay time | Decay | Damping | Pre-delay | — |
| 21 | Shimmer | Shimmer reverb | Decay time | Decay | Shimmer amt | Damping | — |
| 22 | Dark Verb | Dark/long reverb | Decay time | Decay | Darkness | — | — |
| 23 | Spring | Short spring reverb | Decay time | Decay | Tone | Drip | — |

### Row 1 — Distortion & Rhythm (Pink/Yellow/Green) — Pads 24-31

| Pad | FX | On Tap | Pressure | E1 | E2 | E3 | E4 |
|---|---|---|---|---|---|---|---|
| 24 | Bitcrush | Crushes to ~6 bit | Bit depth (harder=fewer) | Bit depth | — | — | — |
| 25 | Downsample | Sample rate reduce | Hold period | Rate | — | — | — |
| 26 | Tape Stop | Tape slowdown | Decel speed | Decel rate | — | — | — |
| 27 | Vinyl Brake | Slow vinyl stop | Decel speed (slower) | Decel rate | Noise | — | — |
| 28 | Saturate | Warm drive | Drive amount | Drive | Tone | Curve | — |
| 29 | Gate/Duck | Rhythmic gating | Gate depth | Rate | Depth | Shape | — |
| 30 | Tremolo | Amplitude modulation | LFO speed | Rate | Depth | Shape | Stereo |
| 31 | Chorus | Chorus/detune | Depth | Rate | Depth | Feedback | — |

## Design: Globals (E5-E8 always)

| Knob | Param | Range | Default |
|---|---|---|---|
| E5 | High-pass filter | 0-1 (freq) | 0 (off) |
| E6 | Low-pass filter | 0-1 (freq) | 1 (open) |
| E7 | Dry/Wet mix | 0-1 | 1 (full wet) |
| E8 | EQ Bump (bandpass peak) | 0-1 (freq) | 0.5 (centered) |

Master knob: volume passthrough (not claimed).

## Design: Interactions

- **Hold pad** = FX on. Release = FX off (with appropriate tail/fade).
- **Shift+Hold pad** = Latch. FX stays on after release. Pad pulses.
- **Shift+Hold latched pad** = Unlatch. FX fades out.
- **Last-touched pad** gets E1-E4. Touch a new pad = E1-E4 switch to that pad's params.
- **Knob peek**: Touch any knob = overlay shows name + value.
- **16 step buttons** = 16 scenes. Tap = recall. Shift+tap = save.
- **Step sequencer**: Shift+Loop on/off. Play starts. Shift+Jog = division.
- **Undo** = bypass (tap toggle, hold momentary).
- **Back** = deactivate all FX, bypass, save state, exit.
- **Jog click** = tap tempo. **Shift+Jog** = BPM fine adjust.

## Design: Memory Budget

Allocate per-type (not worst-case for all 32):
- Pads 0-7 (repeat): shared capture buffer (4s = ~1.4MB) + per-pad repeat buf (2s each = ~5.6MB)
- Pads 8-15 (filters): SVF state only (~bytes each)
- Pads 16-19 (delay): delay buffer each (4s = ~1.4MB × 4 = ~5.6MB)
- Pads 20-23 (reverb): reverb state each (~80KB × 4 = ~320KB)
- Pads 24-28 (dist): tiny state
- Pads 29 (gate): tiny
- Pads 30 (trem): tiny
- Pads 31 (chorus): mod_delay buffer (1s = ~352KB)

**Total: ~13MB** (down from ~77MB in v1)

## Design: Space FX Throw Behavior

When a space FX (delay/reverb) is held:
1. Audio feeds into the effect (input_mix = 1.0)
2. Output is 100% wet
3. Pressure modulates a per-FX parameter

When released (not latched):
1. Input feed stops (input_mix fades to 0 over ~256 samples)
2. Effect tail continues naturally (delay echoes decay, reverb tail fades)
3. The FX stays "active" (processing) until the tail drops below threshold
4. Then the pad deactivates fully

When latched:
1. Behaves like old continuous mode — input continuously feeds, output is wet

## Design: Exit Fix

On Back button:
1. Unlatch all pads
2. Send deactivate for all 32 pads (triggers fade-outs)
3. Set bypass = 1
4. Save state
5. Call host_exit_module()

---

## Task 1: Header Redesign (`perf_fx_dsp.h`)

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h`

Complete rewrite of types and API. Key changes:
- 32 FX type enum (single enum, not two separate)
- `PFX_NUM_FX 32` replaces `PFX_NUM_PUNCH_IN 16` + `PFX_NUM_CONTINUOUS 16`
- Remove `continuous_t` struct entirely
- Expand `punch_in_t` → `pfx_slot_t` with all needed state per type
- Add `phase` (animation timer), `latched`, `params[4]`, `last_touched`
- Add per-type DSP state (delay, reverb, mod_delay, phaser, compressor) — only allocated for pads that need them
- Simplify `scene_t` and `step_preset_t` to snapshot `pfx_slot_t` latched state + params + globals
- Simplify engine: remove `cont[]`, `active_cont_*`, `morph`
- 4 global params: HPF, LPF, dry_wet, eq_bump_freq
- Add `last_touched_slot` to engine
- Remove `claims_master_knob` concept

**Step 1: Write new header**

Replace the entire header with the v2 design. All types, enums, and API declarations.

**Step 2: Commit**
```bash
git add src/dsp/perf_fx_dsp.h
git commit -m "feat: v2 header - 32 unified punch-in FX architecture"
```

---

## Task 2: DSP Engine Core (`perf_fx_dsp.c`)

**Files:**
- Modify: `src/dsp/perf_fx_dsp.c`

Complete rewrite. Reuses existing DSP algorithms (SVF, delay, reverb, phaser, chorus, etc.) but restructured for unified punch-in architecture.

### Step 1: Engine init/destroy/reset

- Allocate per-type: repeat buffers for pads 0-7, delay buffers for pads 16-19, reverb state for pads 20-23, mod_delay for pad 31
- Shared capture buffer for repeat FX
- 4 global filter states (HPF, LPF, EQ bump)

### Step 2: Slot activate/deactivate/set_pressure/set_param

- `pfx_activate(e, slot, velocity)` — sets active, resets phase, initializes per-type state
- `pfx_deactivate(e, slot)` — starts fade-out (or tail decay for space FX)
- `pfx_set_pressure(e, slot, pressure)` — per-FX pressure mapping
- `pfx_set_param(e, slot, param_idx, value)` — E1-E4 param control
- `pfx_set_latched(e, slot, latched)` — latch/unlatch

### Step 3: Animated filter sweep FX (pads 8-15)

New processing functions for sweep FX. Each has a `phase` that advances per-sample:
- `process_lp_sweep()` — cutoff = lerp(start, 0.05, phase). Pressure = phase speed.
- `process_hp_sweep()` — cutoff = lerp(start, 0.95, phase). Pressure = phase speed.
- `process_bp_rise/fall()` — center freq sweeps. Pressure = speed.
- `process_reso_sweep()` — resonant peak sweeps up. Pressure = speed.
- `process_punch_phaser()` — adapted from cont phaser. Pressure = LFO speed.
- `process_punch_flanger()` — adapted from cont flanger. Pressure = LFO speed.
- `process_punch_auto_filter()` — adapted from cont auto-filter. Pressure = LFO speed.

### Step 4: Space throw FX (pads 16-23)

New processing for delay/reverb throws:
- `process_delay_throw()` — feeds delay while active, tail on release. Pressure = feedback.
- `process_ping_pong_throw()` — stereo cross-feed delay. Pressure = feedback.
- `process_tape_echo_throw()` — tape delay with wow. Pressure = feedback.
- `process_echo_freeze()` — delay with feedback=1.0 while held. On release, feedback drops to 0.95 then decays.
- `process_reverb_throw()` — reverb swell. Pressure = decay time.
- `process_shimmer_throw()` — shimmer reverb. Pressure = decay.
- `process_dark_verb_throw()` — dark reverb. Pressure = decay.
- `process_spring_throw()` — spring reverb. Pressure = decay.

Each space FX has a `tail_active` flag. While tail_active, the FX continues processing even after deactivation. When output drops below threshold (~0.001), tail_active clears.

### Step 5: Distortion/rhythm punch-ins (pads 24-31)

- Pads 24-28: Reuse existing bitcrush, downsample, tape stop + add vinyl brake (slower tape stop), saturator
- Pad 29: Gate/ducker adapted from existing
- Pad 30: Tremolo adapted from existing
- Pad 31: Chorus adapted from existing

### Step 6: Main render loop

Simplified:
1. Read input, apply input gain (just 1.0 default now)
2. Save dry
3. Update capture buffer
4. Process all active punch-in FX (serial chain, includes space tails)
5. Apply 4 globals (HPF, LPF, dry/wet, EQ bump) — always active
6. Step sequencer tick
7. Output gain, soft clip, convert to int16

### Step 7: Scenes (simplified)

- `pfx_scene_save(e, slot)` — save which pads are latched + their params[4] + 4 globals
- `pfx_scene_recall(e, slot)` — unlatch all, then latch saved pads with saved params + globals
- Remove morph entirely

### Step 8: Commit
```bash
git add src/dsp/perf_fx_dsp.c
git commit -m "feat: v2 DSP engine - 32 unified punch-in FX with animated sweeps and space throws"
```

---

## Task 3: Plugin Param Handling (`perf_fx_plugin.c`)

**Files:**
- Modify: `src/dsp/perf_fx_plugin.c`

### Step 1: Rewrite set_param

Remove all `cont_*` handling. Expand `punch_*` to cover all 32 slots:
- `punch_N_on` — activate slot N with velocity
- `punch_N_off` — deactivate slot N
- `punch_N_pressure` — set pressure
- `punch_N_param_M` — set param M (0-3) for slot N
- `punch_N_latch` — set latch state
- 4 globals: `global_hpf`, `global_lpf`, `dry_wet`, `eq_bump`
- Scene/step: same as before but simplified
- `bypass`, `bpm`, `transport_running`, `step_seq_active/division`

### Step 2: Rewrite get_param

- `fx_names` — all 32 FX names as JSON array
- `fx_active` — 32-element active state array
- `fx_latched` — 32-element latch state array
- `fx_param_names_N` — 4 param names for slot N
- Scene/step populated state
- Global params

### Step 3: Rewrite fx_on_midi

Update pad→slot mapping for all 32 pads:
- Row 4 (top): notes 92-99 → slots 0-7
- Row 3: notes 84-91 → slots 8-15
- Row 2: notes 76-83 → slots 16-23
- Row 1 (bottom): notes 68-75 → slots 24-31

### Step 4: Update FX name arrays

32 FX names + 32×4 param name arrays.

### Step 5: Commit
```bash
git add src/dsp/perf_fx_plugin.c
git commit -m "feat: v2 plugin params - 32 slots, simplified globals, space throw support"
```

---

## Task 4: UI Complete Rewrite (`ui.js`)

**Files:**
- Modify: `src/ui.js`

### Step 1: Constants and state

- 32 FX names, 32 colors (bright + dim), 32 pad mappings
- Color by category: orange (repeat), blue (filter), purple (space), pink/yellow/green (dist/rhythm)
- State: `active[32]`, `latched[32]`, `pressure[32]`, `params[32][4]`
- `lastTouched = -1` (which pad E1-E4 controls)
- 4 global knob values (E5-E8: HPF, LPF, Dry/Wet, EQ Bump)
- Remove all continuous FX state, scene mode, morph

### Step 2: Pad handling

- **Note On (velocity > 0)**: `activate(slot, velocity)`. Set `lastTouched = slot`. If shift held, toggle latch.
- **Note Off**: If not latched, `deactivate(slot)`.
- **Aftertouch (0xA0)**: `set_pressure(slot, pressure)`.
- **Shift+Note On**: Toggle latch. If already latched, unlatch + deactivate.

### Step 3: Knob handling

- E1-E4: If `lastTouched >= 0`, control that pad's params[0-3]
- E5-E8: Always control globals (HPF, LPF, Dry/Wet, EQ Bump)
- Master: Pass through to host volume (don't intercept)
- Knob peek: Touch knob → show label + value overlay

### Step 4: LED management

- Progressive init (8 LEDs/frame)
- Active pads: bright category color
- Latched pads: bright color + pulse animation
- Inactive pads: dim category color
- Last-touched pad: White (overrides color while held or while E1-E4 active)
- Step LEDs: scene populated = grey, current = white

### Step 5: Display

Simplified main view:
- Header: `PFX 120.0` + active FX count
- Active FX names (up to ~3 lines)
- Bottom: Current E1-E4 labels (from lastTouched pad) or "Touch a pad"
- Overlay for knob changes, scene recall, etc.

### Step 6: Scene handling

- 16 step buttons = 16 scenes
- Tap = recall (unlatch all, then latch saved pads with saved params)
- Shift+tap = save (snapshot latched pads + params + globals)
- Step sequencer: Shift+Loop toggle, Play start/stop

### Step 7: Exit handling

```javascript
if (d1 === MoveBack && d2 > 0) {
    saveState();
    // Deactivate ALL FX
    for (let i = 0; i < 32; i++) {
        if (active[i] || latched[i]) {
            sendParam(`punch_${i}_off`, '1');
        }
        latched[i] = false;
        active[i] = false;
    }
    sendParam('bypass', '1');
    host_exit_module();
}
```

### Step 8: Persistence

Same structure but for 32 slots: `active[32]`, `latched[32]`, `params[32][4]`, globals, scenes.

### Step 9: Commit
```bash
git add src/ui.js
git commit -m "feat: v2 UI - 32 punch-in pads, shift-latch, per-pad knobs, clean exit"
```

---

## Task 5: Tests (`test_perf_fx.c`)

**Files:**
- Modify: `src/dsp/test_perf_fx.c`

### Step 1: Update tests for v2 API

- Test activate/deactivate for all slot ranges (0-7, 8-15, 16-23, 24-31)
- Test latch behavior
- Test pressure mapping for representative FX (filter sweep, delay throw, bitcrush)
- Test animated phase advances
- Test space FX tail (delay/reverb continues after deactivate)
- Test scene save/recall with latched pads
- Test render passthrough (no FX active)
- Test render with bypass
- Test exit cleanup (all deactivated)

### Step 2: Build and run tests
```bash
gcc -o test_perf_fx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -lm -I src/dsp
./test_perf_fx
```

### Step 3: Commit
```bash
git add src/dsp/test_perf_fx.c
git commit -m "test: v2 tests for 32 unified punch-in FX"
```

---

## Task 6: Module Config + Help

**Files:**
- Modify: `src/module.json`
- Modify: `src/help.json`

### Step 1: module.json

Remove `claims_master_knob: true` so volume knob passes through to host.

### Step 2: help.json

Update getting started guide and all help pages for v2 UX.

### Step 3: Commit
```bash
git add src/module.json src/help.json
git commit -m "docs: v2 module config and help for unified punch-in architecture"
```

---

## Task 7: Build, Deploy, Test

### Step 1: Cross-compile
```bash
./scripts/build.sh
```

### Step 2: Run unit tests
```bash
gcc -o test_perf_fx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -lm -I src/dsp && ./test_perf_fx
```

### Step 3: Deploy
```bash
./scripts/install.sh
```

### Step 4: Hardware test checklist
- [ ] Launch via Shift+Vol+Jog → Overtake → Performance FX
- [ ] All 32 pads light up in category colors
- [ ] Hold any pad → hear FX immediately
- [ ] Aftertouch works (press harder on pad)
- [ ] Filter sweep pads animate over time
- [ ] Space FX (delay/reverb) have audible tails on release
- [ ] Shift+hold = latch (pad pulses, FX stays on)
- [ ] Shift+hold latched = unlatch
- [ ] E1-E4 control last-touched pad params
- [ ] E5-E8 always control globals (HPF, LPF, Dry/Wet, EQ Bump)
- [ ] Touch knob = peek overlay
- [ ] Volume knob controls host volume (not claimed)
- [ ] Step buttons: Shift+tap = save scene, tap = recall
- [ ] Back button: all FX stop, exit clean, Move resumes normally
- [ ] No stuck audio or FX after exit
