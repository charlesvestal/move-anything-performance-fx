/*
 * Performance FX Module UI — v2 Architecture
 *
 * 32 unified punch-in FX pads (hold=on, release=off)
 * Shift+hold = latch, Shift+hold latched = unlatch
 * Last-touched pad gets E1-E4, E5-E8 always global
 * Step sequencer, scene presets, per-set persistence
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
    'STUTTER', 'SCATTER', 'REVERSE', 'HALF-SPD',
    /* Row 3: Filter Sweeps */
    'LP SWP\u2193', 'HP SWP\u2191', 'BP RISE', 'BP FALL',
    'RESO SW', 'PHASER', 'FLANGER', 'AUTOFLT',
    /* Row 2: Space Throws */
    'DELAY', 'PING', 'TAPE ECH', 'ECH FRZ',
    'REVERB', 'SHIMMER', 'DK VERB', 'SPRING',
    /* Row 1: Distortion/Rhythm */
    'CRUSH', 'DWNSMPL', 'TAPE STP', 'VNL BRK',
    'SATURATE', 'GATE', 'TREMOLO', 'CHORUS'
];

/* Per-FX E1-E4 param labels */
const FX_PARAM_LABELS = [
    /* Row 4: repeats/time */
    ['Div Fine', 'Decay', 'Filter', 'Gate'],       /* RPT 1/4 */
    ['Div Fine', 'Decay', 'Filter', 'Gate'],       /* RPT 1/8 */
    ['Div Fine', 'Decay', 'Filter', 'Gate'],       /* RPT 1/16 */
    ['Div Fine', 'Decay', 'Filter', 'Gate'],       /* RPT TRP */
    ['Grain', 'Div', 'Decay', 'Filter'],           /* STUTTER */
    ['Grain', 'Div', 'Decay', 'Filter'],           /* SCATTER */
    ['Length', 'Fade', 'Filter', '\u2014'],         /* REVERSE */
    ['Speed', 'Fade', 'Filter', '\u2014'],         /* HALF-SPD */
    /* Row 3: filter sweeps */
    ['Speed', 'Range', 'Reso', 'Start'],           /* LP SWP */
    ['Speed', 'Range', 'Reso', 'Start'],           /* HP SWP */
    ['Speed', 'Range', 'Reso', 'Start'],           /* BP RISE */
    ['Speed', 'Range', 'Reso', 'Start'],           /* BP FALL */
    ['Speed', 'Range', 'Reso', 'Start'],           /* RESO SW */
    ['Speed', 'Depth', 'Feedbk', 'Stages'],       /* PHASER */
    ['Speed', 'Depth', 'Feedbk', 'Manual'],        /* FLANGER */
    ['Speed', 'Depth', 'Reso', 'Shape'],           /* AUTOFLT */
    /* Row 2: space throws */
    ['Time', 'Feedbk', 'Filter', '\u2014'],        /* DELAY */
    ['Time', 'Feedbk', 'Filter', '\u2014'],        /* PING */
    ['Time', 'Feedbk', 'Filter', '\u2014'],        /* TAPE ECH */
    ['Time', 'Feedbk', 'Filter', '\u2014'],        /* ECH FRZ */
    ['Decay', 'Damp', 'PreDly', '\u2014'],         /* REVERB */
    ['Decay', 'Damp', 'PreDly', '\u2014'],         /* SHIMMER */
    ['Decay', 'Damp', 'PreDly', '\u2014'],         /* DK VERB */
    ['Decay', 'Damp', 'PreDly', '\u2014'],         /* SPRING */
    /* Row 1: distortion/rhythm */
    ['Bits', '\u2014', '\u2014', '\u2014'],         /* CRUSH */
    ['Rate', '\u2014', '\u2014', '\u2014'],         /* DWNSMPL */
    ['Speed', 'Wow', '\u2014', '\u2014'],           /* TAPE STP */
    ['Speed', 'Noise', '\u2014', '\u2014'],         /* VNL BRK */
    ['Drive', 'Tone', 'Curve', '\u2014'],           /* SATURATE */
    ['Thresh', 'Attack', 'Release', '\u2014'],      /* GATE */
    ['Speed', 'Depth', 'Shape', '\u2014'],          /* TREMOLO */
    ['Rate', 'Depth', 'Feedbk', '\u2014']           /* CHORUS */
];

/* Global param labels (E5-E8) */
const GLOBAL_LABELS = ['HPF', 'LPF', 'D/W', 'Bump'];
const GLOBAL_PARAMS = ['global_hpf', 'global_lpf', 'dry_wet', 'eq_bump'];
const GLOBAL_DEFAULTS = [0.0, 1.0, 1.0, 0.5];

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
/* Row 1 (slots 24-31): Pink for 24-28, Green for 29-31 */
for (let i = 24; i < 32; i++) {
    if (i <= 28) {
        BRIGHT_COLORS.push(BrightPink);
        DIM_COLORS.push(Rose);
    } else {
        BRIGHT_COLORS.push(BrightGreen);
        DIM_COLORS.push(ForestGreen);
    }
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
let lastTouched = -1;  /* slot index with E1-E4 focus */

/* Per-slot E1-E4 param values (0.0-1.0) */
let slotParams = [];
for (let i = 0; i < NUM_SLOTS; i++) {
    slotParams.push([0.5, 0.5, 0.5, 0.5]);
}

/* Global param values (0.0-1.0) */
let globalValues = GLOBAL_DEFAULTS.slice();

/* Track routing */
let trackRouted = [false, false, false, false];

/* Step presets (scenes on step buttons) */
let stepPopulated = new Array(16).fill(false);
let currentStep = -1;

/* Step FX sequencer */
let stepSeqActive = false;
let stepRecordMode = false;
let stepSeqDivision = 0;
let transportRunning = false;

/* Recording */
let isRecording = false;

/* Display overlay */
let overlayText = '';
let overlayParam = '';
let overlayValue = '';
let overlayTimer = 0;
const OVERLAY_DURATION = 66;

/* LED init */
let ledInitPending = true;
let ledInitIndex = 0;

/* BPM and tap tempo */
let bpm = 120.0;
let tapTimes = [];
const TAP_TIMEOUT = 2000;
const TAP_MIN_TAPS = 2;

/* Audio source */
let audioSource = 1; /* 0=LINE_IN, 1=MOVE_MIX, 2=TRACKS */

/* Persistence */
let autosaveCounter = 0;
const AUTOSAVE_INTERVAL = 440;
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
    const state = {
        version: 2,
        fxLatched: fxLatched,
        slotParams: slotParams,
        globalValues: globalValues,
        lastTouched: lastTouched,
        stepPopulated: stepPopulated,
        currentStep: currentStep,
        audioSource: audioSource,
        trackRouted: trackRouted,
        bpm: bpm,
        stepSeqDivision: stepSeqDivision
    };

    const dspState = getParam('state');

    const fullState = {
        ui: state,
        dsp: dspState || ''
    };

    try {
        const path = getStatePath();
        const dir = path.substring(0, path.lastIndexOf('/'));
        host_ensure_dir(dir);
        host_write_file(path, JSON.stringify(fullState));
    } catch (e) {
        /* ignore save errors */
    }
}

function loadState() {
    try {
        const path = getStatePath();
        const raw = host_read_file(path);
        if (!raw) return false;

        const fullState = JSON.parse(raw);
        const state = fullState.ui;
        if (!state || state.version !== 2) return false;

        if (state.fxLatched) fxLatched = state.fxLatched;
        if (state.slotParams) slotParams = state.slotParams;
        if (state.globalValues) globalValues = state.globalValues;
        if (state.lastTouched !== undefined) lastTouched = state.lastTouched;
        if (state.stepPopulated) stepPopulated = state.stepPopulated;
        if (state.currentStep !== undefined) currentStep = state.currentStep;
        if (state.audioSource !== undefined) audioSource = state.audioSource;
        if (state.trackRouted) trackRouted = state.trackRouted;
        if (state.bpm !== undefined) bpm = state.bpm;
        if (state.stepSeqDivision !== undefined) stepSeqDivision = state.stepSeqDivision;

        /* Push restored state to DSP */
        sendParam('bpm', String(bpm));
        sendParam('audio_source', String(audioSource));
        sendParam('step_seq_division', String(stepSeqDivision));

        /* Restore track routing */
        if (state.trackRouted) {
            let mask = 0;
            for (let i = 0; i < 4; i++) {
                if (trackRouted[i]) mask |= (1 << i);
            }
            sendParam('track_mask', String(mask));
        }

        /* Restore global params */
        for (let i = 0; i < 4; i++) {
            sendParam(GLOBAL_PARAMS[i], globalValues[i].toFixed(3));
        }

        /* Restore latched FX */
        for (let i = 0; i < NUM_SLOTS; i++) {
            if (fxLatched[i]) {
                sendParam(`punch_${i}_on`, '100');
                sendParam(`punch_${i}_latch`, '1');
                fxActive[i] = true;
                /* Restore per-slot params */
                for (let j = 0; j < 4; j++) {
                    sendParam(`punch_${i}_param_${j}`, slotParams[i][j].toFixed(3));
                }
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
    if (lastTouched === slot && (fxActive[slot] || fxLatched[slot])) {
        return White;
    }
    if (fxLatched[slot]) {
        return Pulse4th;  /* bright color with pulse animation */
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

    /* Step buttons */
    for (let i = 0; i < 16; i++) {
        let color = Black;
        if (stepPopulated[i]) color = LightGrey;
        if (currentStep === i) color = White;
        leds.push({ note: MoveSteps[i], color });
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
        setButtonLED(MoveLoop, stepSeqActive ? WhiteLedBright : WhiteLedDim);
        setButtonLED(MoveCapture, isRecording ? WhiteLedBright : WhiteLedDim);
        setButtonLED(MoveBack, WhiteLedDim);
        setButtonLED(MoveShift, WhiteLedDim);
        setButtonLED(MoveCopy, WhiteLedDim);
        setButtonLED(MoveDelete, WhiteLedDim);
        setButtonLED(MoveRec, stepRecordMode ? WhiteLedBright : WhiteLedDim);
        setButtonLED(MovePlay, transportRunning ? WhiteLedBright : WhiteLedDim);

        /* Track button LEDs */
        for (let i = 0; i < 4; i++) {
            setButtonLED(TRACK_CCS[i], trackRouted[i] ? WhiteLedBright : WhiteLedDim);
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
    const sourceLabels = ['IN', 'MIX', 'TRK'];
    const sourceLabel = sourceLabels[audioSource] || 'MIX';
    let trackInfo = '';
    if (audioSource === 2) {
        let tracks = [];
        for (let i = 0; i < 4; i++) {
            if (trackRouted[i]) tracks.push(i + 1);
        }
        trackInfo = tracks.length > 0 ? ` ${tracks.join('')}` : '';
    }

    /* Count active FX */
    let activeCount = 0;
    for (let i = 0; i < NUM_SLOTS; i++) {
        if (fxActive[i]) activeCount++;
    }
    print(0, 0, `PFX ${bpm.toFixed(0)} ${sourceLabel}${trackInfo} [${activeCount}]`, 1);
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

    /* Line 4: E1-E4 labels from lastTouched pad */
    if (lastTouched >= 0 && (fxActive[lastTouched] || fxLatched[lastTouched])) {
        const labels = FX_PARAM_LABELS[lastTouched];
        const name = FX_NAMES[lastTouched];
        /* Truncate name to fit */
        const shortName = name.length > 8 ? name.substring(0, 7) + '~' : name;
        print(0, 33, shortName, 1);
        for (let i = 0; i < 4; i++) {
            const lbl = labels[i].substring(0, 5);
            print(50 + i * 20, 33, lbl, 1);
        }
    } else {
        print(0, 33, 'Touch a pad', 1);
    }

    /* Line 5: E5-E8 labels always */
    for (let i = 0; i < 4; i++) {
        print(i * 32, 44, GLOBAL_LABELS[i], 1);
    }

    /* Step seq / record mode indicators */
    if (stepRecordMode) {
        print(105, 0, 'REC', 1);
    } else if (stepSeqActive) {
        print(105, 0, 'SEQ', 1);
    }

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
 * Parameter handling
 * ================================================================ */

function sendParam(key, value) {
    host_module_set_param(key, String(value));
}

function getParam(key) {
    return host_module_get_param(key);
}

function showOverlay(title, param, value) {
    overlayText = title;
    overlayParam = param;
    overlayValue = String(value);
    overlayTimer = OVERLAY_DURATION;

    const parts = [title, param, value].filter(s => s && s.length > 0);
    announce(parts.join(', '));
}

/* ================================================================
 * MIDI input handling
 * ================================================================ */

function handlePadOn(note, velocity) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;

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
            sendParam(`punch_${slot}_on`, String(velocity));
            sendParam(`punch_${slot}_latch`, '1');
            showOverlay(FX_NAMES[slot], 'Latched', '');
        }
    } else {
        /* Normal punch-in: hold = on */
        fxActive[slot] = true;
        sendParam(`punch_${slot}_on`, String(velocity));
    }

    /* Update last touched for E1-E4 focus */
    const prevTouched = lastTouched;
    lastTouched = slot;
    if (prevTouched >= 0 && prevTouched !== slot) {
        refreshPadLED(prevTouched);
    }
    refreshPadLED(slot);
}

function handlePadOff(note) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;

    /* If latched, pad release does nothing */
    if (fxLatched[slot]) return;

    /* Normal release */
    fxActive[slot] = false;
    sendParam(`punch_${slot}_off`, '1');
    refreshPadLED(slot);
}

function handleAftertouch(note, pressure) {
    const slot = NOTE_TO_SLOT[note];
    if (slot === undefined) return;
    if (!fxActive[slot]) return;

    sendParam(`punch_${slot}_pressure`, (pressure / 127.0).toFixed(3));
}

function handleStep(stepIdx, pressed) {
    if (!pressed) return;

    if (shiftHeld) {
        /* Shift+step = save scene */
        sendParam(`scene_save_${stepIdx}`, '1');
        stepPopulated[stepIdx] = true;
        currentStep = stepIdx;
        setLED(MoveSteps[stepIdx], White);
        showOverlay('Scene', `Saved #${stepIdx + 1}`, '');
    } else if (stepRecordMode) {
        /* Step record mode: write step */
        sendParam(`step_save_${stepIdx}`, '1');
        stepPopulated[stepIdx] = true;
        setLED(MoveSteps[stepIdx], BrightRed);
        showOverlay('Step Record', `Written #${stepIdx + 1}`, '');
    } else {
        /* Tap = recall scene */
        if (stepPopulated[stepIdx]) {
            sendParam(`scene_recall_${stepIdx}`, '1');
            if (currentStep >= 0) setLED(MoveSteps[currentStep], stepPopulated[currentStep] ? LightGrey : Black);
            currentStep = stepIdx;
            setLED(MoveSteps[stepIdx], White);
            showOverlay('Scene', `Recalled #${stepIdx + 1}`, '');
            syncFxState();
        }
    }
}

function handleKnob(knobIndex, delta) {
    if (knobIndex < 4) {
        /* E1-E4: per-slot params for lastTouched */
        if (lastTouched < 0 || (!fxActive[lastTouched] && !fxLatched[lastTouched])) {
            showOverlay('E1-E4', 'Touch a pad first', '');
            return;
        }
        const slot = lastTouched;
        let v = slotParams[slot][knobIndex] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        slotParams[slot][knobIndex] = v;
        sendParam(`punch_${slot}_param_${knobIndex}`, v.toFixed(3));
        const label = FX_PARAM_LABELS[slot][knobIndex];
        showOverlay(FX_NAMES[slot], label, v.toFixed(2));
    } else {
        /* E5-E8: global params */
        const gi = knobIndex - 4;
        let v = globalValues[gi] + delta * 0.01;
        v = Math.max(0.0, Math.min(1.0, v));
        globalValues[gi] = v;
        sendParam(GLOBAL_PARAMS[gi], v.toFixed(3));
        showOverlay('Global', GLOBAL_LABELS[gi], v.toFixed(2));
    }
}

function handleKnobPeek(knobNote) {
    /* Capacitive touch notes: 0=E1 .. 7=E8, 8=Master, 9=Jog */
    if (knobNote === 9) return;
    if (knobNote === 8) return; /* Master knob = volume passthrough, no peek */

    if (knobNote >= 0 && knobNote < 4) {
        /* E1-E4: show per-slot param */
        if (lastTouched >= 0 && (fxActive[lastTouched] || fxLatched[lastTouched])) {
            const label = FX_PARAM_LABELS[lastTouched][knobNote];
            const v = slotParams[lastTouched][knobNote];
            showOverlay(FX_NAMES[lastTouched], label, v.toFixed(2));
        } else {
            showOverlay('E1-E4', 'Touch a pad', '');
        }
    } else if (knobNote >= 4 && knobNote < 8) {
        /* E5-E8: show global param */
        const gi = knobNote - 4;
        showOverlay('Global', GLOBAL_LABELS[gi], globalValues[gi].toFixed(2));
    }
}

function handleTrackButton(trackIdx, pressed) {
    if (!pressed) return;

    if (shiftHeld) {
        /* Shift+Track: solo */
        for (let i = 0; i < 4; i++) {
            trackRouted[i] = (i === trackIdx);
        }
    } else {
        trackRouted[trackIdx] = !trackRouted[trackIdx];
    }

    let mask = 0;
    for (let i = 0; i < 4; i++) {
        if (trackRouted[i]) mask |= (1 << i);
    }
    sendParam('track_mask', String(mask));

    if (mask > 0 && audioSource !== 2) {
        audioSource = 2;
        sendParam('audio_source', '2');
    } else if (mask === 0 && audioSource === 2) {
        audioSource = 1;
        sendParam('audio_source', '1');
    }

    for (let i = 0; i < 4; i++) {
        setButtonLED(TRACK_CCS[i], trackRouted[i] ? WhiteLedBright : WhiteLedDim);
    }

    let trackList = [];
    for (let i = 0; i < 4; i++) {
        if (trackRouted[i]) trackList.push(`T${i + 1}`);
    }
    showOverlay('Source', trackList.length > 0 ? `Tracks: ${trackList.join('+')}` : 'Move Mix', '');
}

function handleJogScroll(delta) {
    /* Scroll through active/latched FX to change lastTouched for E1-E4 */
    let activeSlots = [];
    for (let i = 0; i < NUM_SLOTS; i++) {
        if (fxActive[i] || fxLatched[i]) activeSlots.push(i);
    }
    if (activeSlots.length === 0) return;

    let curIdx = activeSlots.indexOf(lastTouched);
    if (curIdx < 0) curIdx = 0;
    curIdx = (curIdx + (delta > 0 ? 1 : -1) + activeSlots.length) % activeSlots.length;

    const prevTouched = lastTouched;
    lastTouched = activeSlots[curIdx];

    if (prevTouched >= 0 && prevTouched !== lastTouched) {
        refreshPadLED(prevTouched);
    }
    refreshPadLED(lastTouched);
    showOverlay(FX_NAMES[lastTouched], 'E1-E4 Focus', '');
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

    try {
        const ltStr = getParam('last_touched');
        if (ltStr) {
            const lt = parseInt(ltStr, 10);
            if (lt >= 0 && lt < NUM_SLOTS) lastTouched = lt;
        }
    } catch (e) { /* ignore */ }
}

function syncStepPopulatedState() {
    try {
        const raw = getParam('step_populated');
        if (raw) {
            const arr = JSON.parse(raw);
            for (let i = 0; i < 16; i++) {
                stepPopulated[i] = arr[i] === 1;
            }
        }
    } catch (e) { /* ignore */ }
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

globalThis.init = function() {
    console.log('Performance FX v2 module initializing');

    detectSetUUID();
    stateLoaded = loadState();

    if (!stateLoaded) {
        /* Fresh start - push defaults */
        sendParam('bpm', String(bpm));
        sendParam('audio_source', String(audioSource));
        for (let i = 0; i < 4; i++) {
            sendParam(GLOBAL_PARAMS[i], GLOBAL_DEFAULTS[i].toFixed(3));
        }
    }

    syncStepPopulatedState();

    ledInitPending = true;
    ledInitIndex = 0;
};

globalThis.tick = function() {
    if (ledInitPending) {
        setupLedBatch();
        return;
    }

    /* Sync step sequencer position from DSP */
    if (stepSeqActive) {
        const raw = getParam('current_step');
        if (raw) {
            const newStep = parseInt(raw, 10);
            if (newStep !== currentStep && newStep >= 0 && newStep < 16) {
                if (currentStep >= 0) setLED(MoveSteps[currentStep], stepPopulated[currentStep] ? LightGrey : Black);
                currentStep = newStep;
                setLED(MoveSteps[currentStep], White);
                syncFxState();
            }
        }
    }

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

    /* Channel aftertouch - broadcast to all active punch-ins */
    if (status === 0xD0) {
        for (let i = 0; i < NUM_SLOTS; i++) {
            if (fxActive[i]) {
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

        /* Loop - toggle step sequencer on/off */
        if (d1 === MoveLoop && d2 > 0) {
            stepSeqActive = !stepSeqActive;
            sendParam('step_seq_active', stepSeqActive ? '1' : '0');
            setButtonLED(MoveLoop, stepSeqActive ? WhiteLedBright : WhiteLedDim);
            showOverlay('Step Seq', stepSeqActive ? 'ON' : 'OFF', '');
            return;
        }

        /* Copy - cycle audio source */
        if (d1 === MoveCopy && d2 > 0) {
            audioSource = (audioSource + 1) % 3;
            sendParam('audio_source', String(audioSource));
            const sourceNames = ['Line In', 'Move Mix', 'Tracks'];
            showOverlay('Source', sourceNames[audioSource], '');

            if (audioSource !== 2) {
                for (let i = 0; i < 4; i++) trackRouted[i] = false;
                sendParam('track_mask', '0');
                for (let i = 0; i < 4; i++) {
                    setButtonLED(TRACK_CCS[i], WhiteLedDim);
                }
            } else {
                for (let i = 0; i < 4; i++) trackRouted[i] = true;
                sendParam('track_mask', '15');
                for (let i = 0; i < 4; i++) {
                    setButtonLED(TRACK_CCS[i], WhiteLedBright);
                }
            }
            return;
        }

        /* Delete - Shift+Delete clears current step */
        if (d1 === MoveDelete && d2 > 0) {
            if (shiftHeld && currentStep >= 0) {
                sendParam(`step_clear_${currentStep}`, '1');
                stepPopulated[currentStep] = false;
                setLED(MoveSteps[currentStep], Black);
                showOverlay('Scene', `Cleared #${currentStep + 1}`, '');
                currentStep = -1;
            }
            return;
        }

        /* Rec - Step record mode */
        if (d1 === MoveRec && d2 > 0) {
            stepRecordMode = !stepRecordMode;
            showOverlay('Step Record', stepRecordMode ? 'ON - press steps' : 'OFF', '');
            setButtonLED(MoveRec, stepRecordMode ? WhiteLedBright : WhiteLedDim);
            return;
        }

        /* Play - transport start/stop */
        if (d1 === MovePlay && d2 > 0) {
            transportRunning = !transportRunning;
            sendParam('transport_running', transportRunning ? '1' : '0');
            setButtonLED(MovePlay, transportRunning ? WhiteLedBright : WhiteLedDim);
            showOverlay('Transport', transportRunning ? 'Running' : 'Stopped', '');
            return;
        }

        /* Capture - record output to WAV */
        if (d1 === MoveCapture && d2 > 0) {
            if (isRecording) {
                host_sampler_stop();
                isRecording = false;
                showOverlay('Record', 'Stopped', '');
                setButtonLED(MoveCapture, WhiteLedDim);
            } else {
                const now = new Date();
                const ts = `${now.getFullYear()}${String(now.getMonth()+1).padStart(2,'0')}${String(now.getDate()).padStart(2,'0')}_${String(now.getHours()).padStart(2,'0')}${String(now.getMinutes()).padStart(2,'0')}${String(now.getSeconds()).padStart(2,'0')}`;
                const path = `/data/UserData/UserLibrary/Recordings/pfx_${ts}.wav`;
                host_sampler_start(path);
                isRecording = true;
                showOverlay('Record', 'Recording...', '');
                setButtonLED(MoveCapture, WhiteLedBright);
            }
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
                /* Shift+Click = cycle audio source */
                audioSource = (audioSource + 1) % 3;
                sendParam('audio_source', String(audioSource));
                const sourceNames = ['Line In', 'Move Mix', 'Tracks'];
                showOverlay('Source', sourceNames[audioSource], '');
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
