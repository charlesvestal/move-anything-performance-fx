/*
 * Performance FX Module UI — v2 Architecture
 *
 * 32 unified punch-in FX pads (hold=on, release=off)
 * Shift+hold = latch, Shift+hold latched = unlatch
 * E1: RPT Length  E2: RPT Speed  E3: RPT on/off
 * E4-E6: per-slot FX params (last touched pad)
 * E7: Tilt EQ  E8: DJ Filter
 */

import {
    Black, White, LightGrey, DarkGrey,
    BrightRed, OrangeRed, Bright, VividYellow,
    BrightGreen, ForestGreen, NeonGreen, TealGreen, Cyan,
    AzureBlue, RoyalBlue, Navy,
    BlueViolet, Violet, Purple, ElectricViolet,
    HotMagenta, NeonPink, Rose, BrightPink,
    Ochre, BurntOrange, Mustard,
    MintGreen, PaleCyan, SkyBlue, LightBlue,
    Lilac, Lime,
    MidiNoteOn, MidiNoteOff, MidiCC, MidiPolyAftertouch,
    MoveShift, MoveBack, MoveMainButton, MoveMainKnob,
    MoveKnob1, MoveKnob2, MoveKnob3, MoveKnob4,
    MoveKnob5, MoveKnob6, MoveKnob7, MoveKnob8,
    MoveMaster, MovePads, MoveSteps,
    MoveCapture, MoveUndo, MoveLoop, MoveCopy, MoveDelete,
    MovePlay, MoveRec,
    MoveRow1, MoveRow2, MoveRow3, MoveRow4,
    WhiteLedOff, WhiteLedDim, WhiteLedMedium, WhiteLedBright,
    Pulse4th
} from '/data/UserData/move-anything/shared/constants.mjs';

import {
    isCapacitiveTouchMessage, isNoiseMessage,
    setLED, setButtonLED, decodeDelta
} from '/data/UserData/move-anything/shared/input_filter.mjs';

import { announce } from '/data/UserData/move-anything/shared/screen_reader.mjs';

/* ================================================================
 * Constants
 * ================================================================ */

const SCREEN_W = 128;
const SCREEN_H = 64;
const LEDS_PER_FRAME = 8;
const NUM_SLOTS = 32;

/* Pad note → slot index mapping
 * Row 4 (top):    92-99  → slots 0-7
 * Row 3:          84-91  → slots 8-15
 * Row 2:          76-83  → slots 16-23
 * Row 1 (bottom): 68-75  → slots 24-31
 */
const PAD_NOTES = [
    92, 93, 94, 95, 96, 97, 98, 99,   /* Row 4: slots 0-7 */
    84, 85, 86, 87, 88, 89, 90, 91,   /* Row 3: slots 8-15 */
    76, 77, 78, 79, 80, 81, 82, 83,   /* Row 2: slots 16-23 */
    68, 69, 70, 71, 72, 73, 74, 75    /* Row 1: slots 24-31 */
];

/* Build reverse lookup: note → slot index */
const NOTE_TO_SLOT = {};
for (let i = 0; i < NUM_SLOTS; i++) {
    NOTE_TO_SLOT[PAD_NOTES[i]] = i;
}

/* Track buttons */
const TRACK_CCS = [MoveRow1, MoveRow2, MoveRow3, MoveRow4];

/* FX Names (32) */
const FX_NAMES = [
    /* Row 4: Time/Repeat */
    'RPT 1/4', 'RPT 1/8', 'RPT 1/16', 'RPT TRP',
    'STUTTER', 'SCATTER', 'REVERSE', 'STRETCH',
    /* Row 3: Filter Sweeps */
    'LP SWP\u2193', 'HP SWP\u2191', 'BP RISE', 'BP FALL',
    'RESO SW', 'PHASER', 'FLANGER', 'AUTOFLT',
    /* Row 2: Space Throws */
    'DLY 1/4', 'DLY D8', 'PP 1/4', 'PP D8',
    'ROOM', 'HALL', 'DK VERB', 'SPRING',
    /* Row 1: Distortion/Rhythm */
    'CRUSH', 'DWNSMPL', 'SATURATE', 'GATE',
    'TREMOLO', 'OCT DN', 'VINYL', 'VNL BRK'
];

/* Per-slot param names (E1-E3, indexed by slot) */
const SLOT_PARAM_NAMES = [
    /* Row 4: Time/Repeat (Speed is on E5 global) */
    ['Filter', 'Gate',   '---'],
    ['Filter', 'Gate',   '---'],
    ['Filter', 'Gate',   '---'],
    ['Filter', 'Gate',   '---'],
    ['Filter', 'Gate',   '---'],
    ['Pattern','Gate',   'Revrse'],
    ['Feedbk', 'Filter', 'Mix'],
    ['Tone',   'WowFlt', 'Mix'],
    /* Row 3: Filter Sweeps */
    ['Speed',  'Reso',   'Depth'],
    ['Speed',  'Reso',   'Depth'],
    ['Speed',  'Reso',   'Width'],
    ['Speed',  'Reso',   'Width'],
    ['Speed',  'Reso',   'Mix'],
    ['Depth',  'Feedbk', 'Mix'],
    ['Depth',  'Feedbk', 'Mix'],
    ['Depth',  'Center', 'Reso'],
    /* Row 2: Space Throws */
    ['Feedbk', 'Tone',   'Level'],
    ['Feedbk', 'Tone',   'Level'],
    ['Feedbk', 'Tone',   'Level'],
    ['Feedbk', 'Tone',   'Level'],
    ['Time',   'Tone',   'Level'],
    ['Time',   'Tone',   'Level'],
    ['Time',   'Dark',   'Level'],
    ['Time',   'Tone',   'Level'],
    /* Row 1: Distortion & Rhythm */
    ['Filter', 'Tone',   'Mix'],
    ['Filter', 'Tone',   'Mix'],
    ['Drive',  'Tone',   'Mix'],
    ['Speed',  'Shape',  'Depth'],
    ['Rate',   'Depth',  'Wave'],
    ['Tone',   'WowFlt', 'Mix'],
    ['Noise',  'WowFlt', 'Tone'],
    ['Speed',  'Tone',   'Mix']
];

/* Global param labels (E4-E8) */
const GLOBAL_LABELS = ['Length', 'Speed', 'Loop', 'Tilt', 'DJ Flt'];
const GLOBAL_KEYS = ['repeat_rate', 'repeat_speed', 'rpt_toggle', 'tilt_eq', 'dj_filter'];
const GLOBAL_DEFAULTS = [0.5, 0.5, 0.0, 0.5, 0.5];
const NUM_GLOBALS = 5;

/* LED color mapping per slot */
const BRIGHT_COLORS = [];
const DIM_COLORS = [];

/* Row 4 (slots 0-7): Orange */
for (let i = 0; i < 8; i++) {
    BRIGHT_COLORS.push(OrangeRed);
    DIM_COLORS.push(Ochre);
}
/* Row 3 (slots 8-15): Blue */
for (let i = 0; i < 8; i++) {
    BRIGHT_COLORS.push(AzureBlue);
    DIM_COLORS.push(RoyalBlue);
}
/* Row 2 (slots 16-23): Purple */
for (let i = 0; i < 8; i++) {
    BRIGHT_COLORS.push(ElectricViolet);
    DIM_COLORS.push(Violet);
}
/* Row 1 (slots 24-31): grouped by function */
/* Crush/Downsample/Saturate (24-26): Pink */
for (let i = 0; i < 3; i++) {
    BRIGHT_COLORS.push(BrightPink);
    DIM_COLORS.push(Rose);
}
/* Gate/Tremolo (27-28): Green */
for (let i = 0; i < 2; i++) {
    BRIGHT_COLORS.push(BrightGreen);
    DIM_COLORS.push(ForestGreen);
}
/* Pitch Down/Vinyl Sim/Vinyl Brake (29-31): Yellow */
for (let i = 0; i < 3; i++) {
    BRIGHT_COLORS.push(VividYellow);
    DIM_COLORS.push(Mustard);
}

/* Persistence paths */
const STATE_DIR = '/data/UserData/move-anything/perf_fx_state';

/* ================================================================
 * State
 * ================================================================ */

let shiftHeld = false;
let bypassed = false;
let undoHeld = false;
let undoWasBypassed = false;

/* FX state */
let fxActive = new Array(NUM_SLOTS).fill(false);
let fxLatched = new Array(NUM_SLOTS).fill(false);
let fxHeld = new Array(NUM_SLOTS).fill(false); /* physically held (finger on pad) */
/* Per-slot param values (3 per slot, 0.0-1.0)
 * Repeat slots (0-7): filter=center, gate=off, unused
 * All others: 0.5 matches pre-knob behavior */
let slotParams = [];
for (let i = 0; i < NUM_SLOTS; i++) {
    if (i < 8) slotParams.push([0.5, 0.0, 0.5]);  /* repeat: gate off */
    else slotParams.push([0.5, 0.5, 0.5]);          /* others: neutral */
}
/* Last touched slot for E1-E3 mapping */
let lastTouchedSlot = -1;
/* Last repeat slot used (for step button toggle) */
let lastRepeatSlot = 0; /* default to RPT 1/4 (slot 0) */
/* Global param values (0.0-1.0) */
let globalValues = GLOBAL_DEFAULTS.slice();

/* Track routing */
let trackRouted = [false, false, false, false];

/* Display overlay */
let overlayText = '';
let overlayParam = '';
let overlayValue = '';
let overlayTimer = 0;
const OVERLAY_DURATION = 330;

/* Throttle screen reader announce to prevent D-Bus flood on rapid knob turns */
let lastAnnounceTime = 0;
const ANNOUNCE_THROTTLE_MS = 150;

/* LED init */
let ledInitPending = true;
let ledInitIndex = 0;

/* BPM and tap tempo */
let bpm = 120.0;
let tapTimes = [];
const TAP_TIMEOUT = 2000;
const TAP_MIN_TAPS = 2;

/* Audio source */
let audioSource = 1; /* Always Move Mix */

/* Persistence */
let autosaveCounter = 0;
const AUTOSAVE_INTERVAL = 2200;
let currentSetUUID = '';
let stateLoaded = false;

/* ================================================================
 * Persistence
 * ================================================================ */

function getStatePath() {
    if (currentSetUUID) {
        return `/data/UserData/move-anything/set_state/${currentSetUUID}/perf_fx_state.json`;
    }
    return `${STATE_DIR}/perf_fx_state.json`;
}

function saveState() {
    /* State persistence disabled for now */
}

function loadState() {
    try {
        const path = getStatePath();
        const raw = host_read_file(path);
        if (!raw) return false;

        const fullState = JSON.parse(raw);
        const state = fullState.ui;
        if (!state || ![2, 3, 4, 5, 6, 7, 8].includes(state.version)) return false;

        if (state.fxLatched) fxLatched = state.fxLatched;
        if (state.version >= 8) {
            /* v8: gate defaults to 0, speed detent */
            if (state.globalValues && state.globalValues.length >= NUM_GLOBALS) {
                globalValues = state.globalValues.slice(0, NUM_GLOBALS);
            }
            if (state.slotParams && state.slotParams.length >= NUM_SLOTS) {
                slotParams = state.slotParams;
            }
            if (state.lastTouchedSlot !== undefined) {
                lastTouchedSlot = state.lastTouchedSlot;
            }
            if (state.lastRepeatSlot !== undefined) {
                lastRepeatSlot = state.lastRepeatSlot;
            }
        } else {
            /* v2-v7: old rate system, old defaults, or old gate defaults — reset */
            globalValues = GLOBAL_DEFAULTS.slice();
        }
        /* audioSource always 1 (Move Mix), trackRouted unused */
        if (state.bpm !== undefined) bpm = state.bpm;

        /* Push restored state to DSP */
        sendParam('bpm', String(bpm));
        sendParam('audio_source', '1'); /* Always Move Mix */

        /* Restore global params (skip rpt_toggle which is UI-only) */
        for (let i = 0; i < NUM_GLOBALS; i++) {
            if (GLOBAL_KEYS[i] === 'rpt_toggle') continue;
            sendParam(GLOBAL_KEYS[i], globalValues[i].toFixed(3));
        }

        /* Restore per-slot params */
        for (let i = 0; i < NUM_SLOTS; i++) {
            for (let j = 0; j < 3; j++) {
                sendParam(`punch_${i}_param_${j}`, slotParams[i][j].toFixed(3));
            }
        }

        /* Restore latched FX */
        for (let i = 0; i < NUM_SLOTS; i++) {
            if (fxLatched[i]) {
                sendParam(`punch_${i}_on`, '0.700');
                sendParam(`punch_${i}_latch`, '1');
                fxActive[i] = true;
            }
        }

        /* Restore DSP state if available */
        if (fullState.dsp) {
            sendParam('restore_state', fullState.dsp);
        }

        return true;
    } catch (e) {
        return false;
    }
}

function detectSetUUID() {
    try {
        const raw = host_read_file('/data/UserData/move-anything/active_set.txt');
        if (raw) {
            const lines = raw.split('\n');
            if (lines[0] && lines[0].length > 8) {
                currentSetUUID = lines[0].trim();
            }
        }
    } catch (e) {
        /* ignore */
    }
}

/* ================================================================
 * Tap Tempo
 * ================================================================ */

function handleTapTempo() {
    const now = Date.now();

    if (tapTimes.length > 0 && (now - tapTimes[tapTimes.length - 1]) > TAP_TIMEOUT) {
        tapTimes = [];
    }

    tapTimes.push(now);
    if (tapTimes.length > 8) tapTimes.shift();

    if (tapTimes.length >= TAP_MIN_TAPS) {
        let totalInterval = 0;
        for (let i = 1; i < tapTimes.length; i++) {
            totalInterval += tapTimes[i] - tapTimes[i - 1];
        }
        const avgInterval = totalInterval / (tapTimes.length - 1);
        const tapBpm = 60000.0 / avgInterval;

        if (tapBpm >= 20 && tapBpm <= 300) {
            bpm = Math.round(tapBpm * 10) / 10;
            sendParam('bpm', bpm.toFixed(1));
            showOverlay('Tap Tempo', `${bpm.toFixed(1)} BPM`, (bpm / 300).toFixed(2));
        }
    } else {
        showOverlay('Tap Tempo', 'Tap again...', '');
    }
}

/* ================================================================
 * LED Management
 * ================================================================ */

function getPadColor(slot) {
    if (fxLatched[slot]) {
        return BrightRed;
    }
    if (fxActive[slot]) {
        return BRIGHT_COLORS[slot];
    }
    return DIM_COLORS[slot];
}

function buildLedList() {
    const leds = [];

    /* All 32 pads */
    for (let i = 0; i < NUM_SLOTS; i++) {
        leds.push({
            note: PAD_NOTES[i],
            color: getPadColor(i)
        });
    }

    /* Step buttons (unused — all off) */
    for (let i = 0; i < 16; i++) {
        leds.push({ note: MoveSteps[i], color: Black });
    }

    return leds;
}

function setupLedBatch() {
    const leds = buildLedList();
    const start = ledInitIndex;
    const end = Math.min(start + LEDS_PER_FRAME, leds.length);

    for (let i = start; i < end; i++) {
        setLED(leds[i].note, leds[i].color);
    }

    ledInitIndex = end;
    if (ledInitIndex >= leds.length) {
        ledInitPending = false;
        /* Button LEDs */
        setButtonLED(MoveUndo, bypassed ? WhiteLedBright : WhiteLedDim);
        setButtonLED(MoveBack, WhiteLedDim);
        setButtonLED(MoveShift, WhiteLedDim);

        /* Track button LEDs */
        for (let i = 0; i < 4; i++) {
            setButtonLED(TRACK_CCS[i], WhiteLedDim);
        }
    }
}

function refreshPadLED(slot) {
    if (slot >= 0 && slot < NUM_SLOTS) {
        setLED(PAD_NOTES[slot], getPadColor(slot));
    }
}

function refreshAllPadLEDs() {
    for (let i = 0; i < NUM_SLOTS; i++) {
        setLED(PAD_NOTES[i], getPadColor(i));
    }
}

/* ================================================================
 * Display
 * ================================================================ */

function drawMainView() {
    clear_screen();

    /* Line 1: header */
    let activeCount = 0;
    for (let i = 0; i < NUM_SLOTS; i++) {
        if (fxActive[i]) activeCount++;
    }
    print(0, 0, `PFX ${bpm.toFixed(0)} [${activeCount}]`, 1);
    draw_line(0, 9, SCREEN_W, 9, 1);

    /* Lines 2-3: names of active/latched FX */
    let activeLine1 = '';
    let activeLine2 = '';
    for (let i = 0; i < NUM_SLOTS; i++) {
        if (fxActive[i] || fxLatched[i]) {
            const name = FX_NAMES[i];
            const tag = fxLatched[i] ? '*' : '';
            const entry = name + tag;
            if (activeLine1.length === 0) {
                activeLine1 = entry;
            } else if (activeLine1.length + entry.length + 1 <= 21) {
                activeLine1 += ' ' + entry;
            } else if (activeLine2.length === 0) {
                activeLine2 = entry;
            } else if (activeLine2.length + entry.length + 1 <= 21) {
                activeLine2 += ' ' + entry;
            }
        }
    }
    if (activeLine1.length > 0) {
        print(0, 12, activeLine1, 1);
    } else {
        print(0, 12, 'No FX active', 1);
    }
    if (activeLine2.length > 0) {
        print(0, 21, activeLine2, 1);
    }

    /* Separator */
    draw_line(0, 30, SCREEN_W, 30, 1);

    /* Line 4: E1-E3 (RPT controls) + E4 (per-slot param 1) */
    const timeLabel = getTimeLabel(globalValues[0]);
    print(0, 33, timeLabel, 1);     /* E1: RPT Length */
    const spd = globalValues[1];
    print(32, 33, spd < 0.48 ? 'Slow' : spd > 0.52 ? 'Fast' : 'Nrml', 1); /* E2: Speed */
    const rptOn = fxActive[lastRepeatSlot] || fxLatched[lastRepeatSlot];
    print(64, 33, rptOn ? 'LP*' : 'Loop', 1);  /* E3: Loop SW */
    /* E4: first per-slot param */
    if (lastTouchedSlot >= 0 && lastTouchedSlot < NUM_SLOTS) {
        print(96, 33, SLOT_PARAM_NAMES[lastTouchedSlot][0], 1);
    } else {
        print(96, 33, '---', 1);
    }

    /* Line 5: E5-E8 labels */
    if (lastTouchedSlot >= 0 && lastTouchedSlot < NUM_SLOTS) {
        print(0, 44, SLOT_PARAM_NAMES[lastTouchedSlot][1], 1);  /* E5 */
        print(32, 44, SLOT_PARAM_NAMES[lastTouchedSlot][2], 1); /* E6 */
    } else {
        print(0, 44, '---', 1);
        print(32, 44, '---', 1);
    }
    print(64, 44, 'Tilt', 1);       /* E7: Tilt EQ */
    print(96, 44, 'DJ', 1);         /* E8: DJ Filter */

    /* Bypass overlay */
    if (bypassed) {
        draw_rect(30, 16, 68, 14, 1);
        fill_rect(31, 17, 66, 12, 0);
        print(38, 19, 'BYPASSED', 1);
    }
}

function drawOverlay() {
    if (overlayTimer <= 0) return;

    clear_screen();
    print(0, 0, overlayText, 1);
    draw_line(0, 10, SCREEN_W, 10, 1);

    print(0, 16, overlayParam, 1);

    let numVal = parseFloat(overlayValue);
    if (!isNaN(numVal)) {
        let barWidth = Math.floor(numVal * 110);
        if (barWidth < 0) barWidth = 0;
        if (barWidth > 110) barWidth = 110;
        fill_rect(8, 30, barWidth, 10, 1);
        draw_rect(8, 30, 110, 10, 1);
        let pct = Math.round(numVal * 100);
        print(50, 45, `${pct}%`, 1);
    } else {
        print(0, 30, overlayValue, 1);
    }

    overlayTimer--;
}

/* ================================================================
 * Rate label helper (maps 0..1 to musical division name)
 * ================================================================ */

function resetRepeatKnobs(slot) {
    /* Reset filter to center (bypass), gate to off, speed to normal */
    slotParams[slot][0] = 0.5;  /* filter = center */
    slotParams[slot][1] = 0.0;  /* gate = off */
    sendParam(`punch_${slot}_param_0`, '0.500');
    sendParam(`punch_${slot}_param_1`, '0.000');
    globalValues[1] = 0.5;      /* speed = normal */
    sendParam('repeat_speed', '0.500');
}

function bpmSyncedRate(slot) {
    /* Convert BPM-synced beat division to rate01 position (free seconds).
     * Matches DSP: seconds = 2.0 * 0.006^rate01
     * Inverse: rate01 = ln(seconds/2.0) / ln(0.006) */
    const beatSec = 60.0 / (bpm > 20 ? bpm : 120);
    const divMap = [1.0, 0.5, 0.25, 2.0/3.0, 0.125]; /* 1/4, 1/8, 1/16, trip, 1/32 */
    const seconds = beatSec * (divMap[slot] || 0.5);
    if (seconds >= 2.0) return 0.0;
    if (seconds <= 0.012) return 1.0;
    return Math.log(seconds / 2.0) / Math.log(0.006);
}

function getTimeLabel(rate01) {
    /* Matches DSP: seconds = 2.0 * 0.006^rate01 */
    const seconds = 2.0 * Math.pow(0.006, rate01);
    if (seconds >= 1.0) return seconds.toFixed(1) + 's';
    const ms = Math.round(seconds * 1000);
    return ms + 'ms';
}

/* ================================================================
 * Parameter handling
 * ================================================================ */

/* ---- Param queue for overtake mode ----
 * In overtake mode, shadow_set_param is fire-and-forget into a single
 * shared memory slot. Rapid calls within the same tick clobber each other.
 * Queue non-critical params and drain them 1 per tick.
 * Critical params (on/off/latch) use the blocking variant. */
const paramQueue = [];
const PARAMS_PER_TICK = 2;  /* drain up to 2 queued params per tick */

function sendParam(key, value) {
    const v = String(value);
    /* Critical: note on/off/latch must be delivered immediately */
    if (key.endsWith('_on') || key.endsWith('_off') || key.endsWith('_latch')) {
        if (typeof host_module_set_param_blocking === 'function') {
            host_module_set_param_blocking(key, v, 50);
        } else {
            host_module_set_param(key, v);
        }
        return;
    }
    /* Non-critical: queue and deduplicate (keep latest value per key) */
    const existing = paramQueue.findIndex(p => p[0] === key);
    if (existing >= 0) {
        paramQueue[existing][1] = v;
    } else {
        paramQueue.push([key, v]);
    }
}

function drainParamQueue() {
    let sent = 0;
    while (paramQueue.length > 0 && sent < PARAMS_PER_TICK) {
        const [key, value] = paramQueue.shift();
        host_module_set_param(key, value);
        sent++;
    }
}

function getParam(key) {
    return host_module_get_param(key);
}

function showOverlay(title, param, value) {
    overlayText = title;
    overlayParam = param;
    overlayValue = String(value);
    overlayTimer = OVERLAY_DURATION;

    const now = Date.now();
    if (now - lastAnnounceTime >= ANNOUNCE_THROTTLE_MS) {
        lastAnnounceTime = now;
        const parts = [title, param, value].filter(s => s && s.length > 0);
        announce(parts.join(', '));
    }
}

/* ================================================================
 * MIDI input handling
 * ================================================================ */

function handlePadOn(note, velocity) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;

    const velNorm = (velocity / 127.0).toFixed(3);

    if (shiftHeld) {
        /* Shift+hold = latch toggle */
        if (fxLatched[slot]) {
            /* Unlatch */
            fxLatched[slot] = false;
            fxActive[slot] = false;
            sendParam(`punch_${slot}_latch`, '0');
            sendParam(`punch_${slot}_off`, '1');
            showOverlay(FX_NAMES[slot], 'Unlatched', '');
        } else {
            /* Latch on */
            fxLatched[slot] = true;
            fxActive[slot] = true;
            lastTouchedSlot = slot;
            if (slot <= 4) {
                lastRepeatSlot = slot;
                globalValues[0] = bpmSyncedRate(slot);
                resetRepeatKnobs(slot);
            }
            sendParam(`punch_${slot}_on`, velNorm);
            sendParam(`punch_${slot}_latch`, '1');
            showOverlay(FX_NAMES[slot], 'Latched', '');
        }
    } else {
        /* Normal tap on latched pad = select for knob editing (don't unlatch) */
        if (fxLatched[slot]) {
            lastTouchedSlot = slot;
            const names = SLOT_PARAM_NAMES[slot];
            showOverlay(FX_NAMES[slot], `${names[0]} | ${names[1]} | ${names[2]}`, '');
            refreshPadLED(slot);
            return;
        }
        /* Normal punch-in: hold = on.
         * If already active (missed note-off), deactivate first. */
        if (fxActive[slot]) {
            sendParam(`punch_${slot}_off`, '1');
        }
        fxActive[slot] = true;
        fxHeld[slot] = true;
        lastTouchedSlot = slot;
        if (slot <= 4) {
            lastRepeatSlot = slot;
            globalValues[0] = bpmSyncedRate(slot);
            resetRepeatKnobs(slot);
        }
        sendParam(`punch_${slot}_on`, velNorm);
    }

    refreshPadLED(slot);
}

function handlePadOff(note) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;

    fxHeld[slot] = false;

    /* If latched, pad release does nothing */
    if (fxLatched[slot]) return;

    /* Normal release */
    fxActive[slot] = false;
    sendParam(`punch_${slot}_off`, '1');
    refreshPadLED(slot);
}

/* Per-slot pressure throttle so simultaneous pad presses don't starve each other */
const lastPressureTime = new Array(NUM_SLOTS).fill(0);
const PRESSURE_THROTTLE_MS = 30; /* Don't send pressure faster than ~33Hz */

function handleAftertouch(note, pressure) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;
    if (!fxActive[slot]) return;

    /* Per-slot throttle — each pad has its own timer */
    const now = Date.now();
    if (now - lastPressureTime[slot] < PRESSURE_THROTTLE_MS) return;
    lastPressureTime[slot] = now;

    sendParam(`punch_${slot}_pressure`, (pressure / 127.0).toFixed(3));
}

function handleStep(stepIdx, pressed) {
    /* Step buttons unused — reserved for future */
}

function handleKnob(knobIndex, delta) {
    if (knobIndex === 0) {
        /* E1: RPT Length — free seconds */
        let v = globalValues[0] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        globalValues[0] = v;
        sendParam('repeat_rate', v.toFixed(3));
        const timeLabel = getTimeLabel(v);
        showOverlay('Repeat', `Length: ${timeLabel}`, v.toFixed(2));
    } else if (knobIndex === 1) {
        /* E2: RPT Speed — detent around 0.5 (normal) */
        let v = globalValues[1] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        /* Snap to 0.5 when crossing through the detent zone */
        if (v >= 0.49 && v <= 0.51) v = 0.5;
        globalValues[1] = v;
        sendParam('repeat_speed', v.toFixed(3));
        const label = v < 0.49 ? 'Slow' : v > 0.51 ? 'Fast' : 'Normal';
        showOverlay('Repeat', `Speed: ${label}`, v.toFixed(2));
    } else if (knobIndex === 2) {
        /* E3: RPT on/off — turn right = on, turn left = off */
        const slot = lastRepeatSlot;
        if (delta > 0 && !fxActive[slot]) {
            fxLatched[slot] = true;
            fxActive[slot] = true;
            lastTouchedSlot = slot;
            sendParam(`punch_${slot}_on`, '0.700');
            sendParam(`punch_${slot}_latch`, '1');
            globalValues[2] = 1.0;
            showOverlay(FX_NAMES[slot], 'Loop ON', '1.00');
        } else if (delta < 0 && fxActive[slot]) {
            fxLatched[slot] = false;
            fxActive[slot] = false;
            sendParam(`punch_${slot}_latch`, '0');
            sendParam(`punch_${slot}_off`, '1');
            globalValues[2] = 0.0;
            showOverlay(FX_NAMES[slot], 'Loop OFF', '0.00');
        }
        refreshPadLED(slot);
    } else if (knobIndex >= 3 && knobIndex <= 5) {
        /* E4-E6: per-slot params for last touched pad */
        if (lastTouchedSlot < 0 || lastTouchedSlot >= NUM_SLOTS) {
            showOverlay('No FX', 'Tap a pad first', '');
            return;
        }
        const slot = lastTouchedSlot;
        const pi = knobIndex - 3;
        let v = slotParams[slot][pi] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        slotParams[slot][pi] = v;
        sendParam(`punch_${slot}_param_${pi}`, v.toFixed(3));
        showOverlay(FX_NAMES[slot], SLOT_PARAM_NAMES[slot][pi], v.toFixed(2));
    } else if (knobIndex === 6) {
        /* E7: Tilt EQ */
        let v = globalValues[3] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        globalValues[3] = v;
        sendParam('tilt_eq', v.toFixed(3));
        showOverlay('Global', 'Tilt', v.toFixed(2));
    } else if (knobIndex === 7) {
        /* E8: DJ Filter */
        let v = globalValues[4] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        globalValues[4] = v;
        sendParam('dj_filter', v.toFixed(3));
        showOverlay('Global', 'DJ Flt', v.toFixed(2));
    }
}

function handleKnobPeek(knobNote) {
    /* Capacitive touch notes: 0=E1 .. 7=E8, 8=Master, 9=Jog */
    if (knobNote === 9) return;
    if (knobNote === 8) return; /* Master knob = volume passthrough, no peek */

    if (knobNote === 0) {
        /* E1: RPT Length */
        const timeLabel = getTimeLabel(globalValues[0]);
        showOverlay('Repeat', `Length: ${timeLabel}`, globalValues[0].toFixed(2));
    } else if (knobNote === 1) {
        /* E2: RPT Speed */
        showOverlay('Repeat', 'Speed', globalValues[1].toFixed(2));
    } else if (knobNote === 2) {
        /* E3: RPT on/off */
        const rptActive = fxActive[lastRepeatSlot] || fxLatched[lastRepeatSlot];
        showOverlay(FX_NAMES[lastRepeatSlot], rptActive ? 'Loop ON' : 'Loop OFF', rptActive ? '1.00' : '0.00');
    } else if (knobNote >= 3 && knobNote <= 5) {
        /* E4-E6: per-slot params */
        const pi = knobNote - 3;
        if (lastTouchedSlot >= 0 && lastTouchedSlot < NUM_SLOTS) {
            const slot = lastTouchedSlot;
            showOverlay(FX_NAMES[slot], SLOT_PARAM_NAMES[slot][pi],
                       slotParams[slot][pi].toFixed(2));
        } else {
            showOverlay('No FX', 'Tap a pad first', '');
        }
    } else if (knobNote === 6) {
        /* E7: Tilt EQ */
        showOverlay('Global', 'Tilt', globalValues[3].toFixed(2));
    } else if (knobNote === 7) {
        /* E8: DJ Filter */
        showOverlay('Global', 'DJ Flt', globalValues[4].toFixed(2));
    }
}

function handleTrackButton(trackIdx, pressed) {
    /* Track buttons unused — always Move Mix */
}

function handleJogScroll(delta) {
    /* Jog scroll adjusts BPM in coarse steps */
    bpm = Math.max(20, Math.min(300, bpm + delta * 1.0));
    sendParam('bpm', bpm.toFixed(1));
    showOverlay('Tempo', `${bpm.toFixed(1)} BPM`, (bpm / 300).toFixed(2));
}

function syncFxState() {
    try {
        const activeStr = getParam('fx_active');
        if (activeStr) {
            const active = JSON.parse(activeStr);
            for (let i = 0; i < NUM_SLOTS; i++) {
                fxActive[i] = active[i] === 1;
            }
        }
    } catch (e) { /* ignore */ }

    try {
        const latchedStr = getParam('fx_latched');
        if (latchedStr) {
            const latched = JSON.parse(latchedStr);
            for (let i = 0; i < NUM_SLOTS; i++) {
                fxLatched[i] = latched[i] === 1;
            }
        }
    } catch (e) { /* ignore */ }

}


/* ================================================================
 * Lifecycle
 * ================================================================ */

globalThis.init = function() {
    console.log('Performance FX v2 module initializing');

    /* State persistence disabled — always start fresh */
    sendParam('bpm', String(bpm));
    sendParam('audio_source', '1');
    for (let i = 0; i < NUM_GLOBALS; i++) {
        if (GLOBAL_KEYS[i] === 'rpt_toggle') continue;
        sendParam(GLOBAL_KEYS[i], GLOBAL_DEFAULTS[i].toFixed(3));
    }

    ledInitPending = true;
    ledInitIndex = 0;
};

globalThis.tick = function() {
    if (ledInitPending) {
        setupLedBatch();
        return;
    }

    /* Drain queued params (pressure, knob values, etc.) */
    drainParamQueue();

    /* Autosave */
    autosaveCounter++;
    if (autosaveCounter >= AUTOSAVE_INTERVAL) {
        autosaveCounter = 0;
        saveState();
    }

    /* Render display */
    if (overlayTimer > 0) {
        drawOverlay();
    } else {
        drawMainView();
    }
    host_flush_display();
};

globalThis.onMidiMessageInternal = function(data) {
    const status = data[0] & 0xF0;
    const d1 = data[1];
    const d2 = data[2];

    /* Filter clock and sysex noise, but NOT aftertouch or capacitive touch */
    if (data[0] === 0xF8 || data[0] === 0xF0 || data[0] === 0xF7) return;

    /* Capacitive touch on knobs (notes 0-9) = knob peek */
    if (status === 0x90 && d1 < 10 && d2 > 0) {
        handleKnobPeek(d1);
        return;
    }
    if (status === 0x80 && d1 < 10) return;

    /* Polyphonic aftertouch - pad pressure */
    if (status === 0xA0) {
        handleAftertouch(d1, d2);
        return;
    }

    /* Channel aftertouch - broadcast to all active punch-ins (throttled) */
    if (status === 0xD0) {
        const now = Date.now();
        for (let i = 0; i < NUM_SLOTS; i++) {
            if (fxActive[i] && now - lastPressureTime[i] >= PRESSURE_THROTTLE_MS) {
                lastPressureTime[i] = now;
                sendParam(`punch_${i}_pressure`, (d1 / 127.0).toFixed(3));
            }
        }
        return;
    }

    /* Note On */
    if (status === 0x90) {
        if (d2 > 0) {
            if (d1 >= 68 && d1 <= 99) {
                handlePadOn(d1, d2);
                return;
            }
            if (d1 >= 16 && d1 <= 31) {
                handleStep(d1 - 16, true);
                return;
            }
        } else {
            if (d1 >= 68 && d1 <= 99) {
                handlePadOff(d1);
                return;
            }
        }
    }

    /* Note Off */
    if (status === 0x80) {
        if (d1 >= 68 && d1 <= 99) {
            handlePadOff(d1);
        }
        return;
    }

    /* CC Messages */
    if (status === 0xB0) {
        /* Shift */
        if (d1 === MoveShift) {
            shiftHeld = d2 > 0;
            /* Shift pressed while holding pads → latch/unlatch them */
            if (shiftHeld) {
                for (let i = 0; i < NUM_SLOTS; i++) {
                    if (fxHeld[i]) {
                        if (fxLatched[i]) {
                            /* Already latched: unlatch (will release on pad-off) */
                            fxLatched[i] = false;
                            sendParam(`punch_${i}_latch`, '0');
                            showOverlay(FX_NAMES[i], 'Unlatched', '');
                        } else {
                            /* Not latched: latch it */
                            fxLatched[i] = true;
                            sendParam(`punch_${i}_latch`, '1');
                            showOverlay(FX_NAMES[i], 'Latched', '');
                        }
                        refreshPadLED(i);
                    }
                }
            }
            return;
        }

        /* Back - CLEAN EXIT */
        if (d1 === MoveBack && d2 > 0) {
            saveState();
            for (let i = 0; i < NUM_SLOTS; i++) {
                sendParam(`punch_${i}_off`, '1');
                sendParam(`punch_${i}_latch`, '0');
            }
            sendParam('bypass', '1');
            host_exit_module();
            return;
        }

        /* Undo - Bypass (tap=toggle, hold=momentary) */
        if (d1 === MoveUndo) {
            if (d2 > 0) {
                undoHeld = true;
                undoWasBypassed = bypassed;
                if (!bypassed) {
                    bypassed = true;
                    sendParam('bypass', '1');
                    setButtonLED(MoveUndo, WhiteLedBright);
                    showOverlay('FX', 'BYPASSED', '');
                } else {
                    bypassed = false;
                    sendParam('bypass', '0');
                    setButtonLED(MoveUndo, WhiteLedDim);
                    showOverlay('FX', 'ACTIVE', '');
                }
            } else {
                if (undoHeld && bypassed && !undoWasBypassed) {
                    bypassed = false;
                    sendParam('bypass', '0');
                    setButtonLED(MoveUndo, WhiteLedDim);
                }
                undoHeld = false;
            }
            return;
        }

        /* Copy - unused */
        if (d1 === MoveCopy && d2 > 0) {
            return;
        }

        /* Jog wheel turn */
        if (d1 === MoveMainKnob) {
            const delta = decodeDelta(d2);
            if (shiftHeld) {
                /* Shift+Turn = BPM fine */
                bpm = Math.max(20, Math.min(300, bpm + delta * 0.5));
                sendParam('bpm', bpm.toFixed(1));
                showOverlay('Tempo', `${bpm.toFixed(1)} BPM`, (bpm / 300).toFixed(2));
            } else {
                /* Turn = scroll through active/latched FX */
                handleJogScroll(delta);
            }
            return;
        }

        /* Jog click */
        if (d1 === MoveMainButton && d2 > 0) {
            if (shiftHeld) {
                /* Shift+Click = unused */
            } else {
                /* Click = tap tempo */
                handleTapTempo();
            }
            return;
        }

        /* Knobs E1-E8 */
        if (d1 >= MoveKnob1 && d1 <= MoveKnob8) {
            const knobIdx = d1 - MoveKnob1;
            const delta = decodeDelta(d2);
            handleKnob(knobIdx, delta);
            return;
        }

        /* Master knob - DO NOT intercept CC 79, let it pass through for volume */

        /* Track buttons */
        for (let i = 0; i < 4; i++) {
            if (d1 === TRACK_CCS[i]) {
                handleTrackButton(i, d2 > 0);
                return;
            }
        }
    }
};

globalThis.onMidiMessageExternal = function(data) {
    /* Pass external MIDI through for potential future use */
};
