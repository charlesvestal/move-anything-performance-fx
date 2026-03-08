/*
 * Performance FX Module UI
 *
 * Live performance audio effects processor with:
 * - 16 pressure-sensitive punch-in FX (top 2 pad rows)
 * - 16 toggleable continuous FX (bottom 2 pad rows)
 * - 8 context-sensitive encoders
 * - 16 step presets
 * - 16 scene snapshots with morphing
 * - Step FX sequencer
 * - Per-set state persistence
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

/* Pad layout: top-to-bottom, left-to-right
 * Row 4 (top):    92 93 94 95 96 97 98 99  -> Punch-in 0-7
 * Row 3:          84 85 86 87 88 89 90 91  -> Punch-in 8-15
 * Row 2:          76 77 78 79 80 81 82 83  -> Continuous 0-7
 * Row 1 (bottom): 68 69 70 71 72 73 74 75  -> Continuous 8-15
 */
const PUNCH_PADS = [92, 93, 94, 95, 96, 97, 98, 99,
                    84, 85, 86, 87, 88, 89, 90, 91];
const CONT_PADS  = [76, 77, 78, 79, 80, 81, 82, 83,
                    68, 69, 70, 71, 72, 73, 74, 75];

/* Track buttons (CCs) */
const TRACK_CCS = [MoveRow1, MoveRow2, MoveRow3, MoveRow4];

/* Punch-in FX colors (by category) */
const PUNCH_COLORS = [
    OrangeRed, OrangeRed, OrangeRed, OrangeRed,  /* Beat repeats: orange */
    BrightRed, BrightRed,                          /* Stutter/Scatter: red */
    Purple, ElectricViolet,                        /* Reverse/HalfSpeed: purple */
    AzureBlue, AzureBlue, AzureBlue, Cyan,        /* Filters: blue */
    BrightPink, HotMagenta,                        /* Crush: pink */
    Violet, BrightGreen                            /* Tape/Ducker: violet/green */
];

/* Punch-in FX dim colors (inactive) */
const PUNCH_DIM_COLORS = [
    BurntOrange, BurntOrange, BurntOrange, BurntOrange,
    67, 67,      /* Brick */
    107, 108,    /* DarkPurple, DarkViolet */
    Navy, Navy, Navy, 87,
    113, 109,    /* Mauve, DeepMagenta */
    108, ForestGreen
];

/* Continuous FX colors (by category) */
const CONT_COLORS = [
    SkyBlue, SkyBlue, SkyBlue, PaleCyan,           /* Delays: sky blue */
    AzureBlue, RoyalBlue, AzureBlue, Lilac,        /* Reverbs: blue */
    MintGreen, BrightGreen, NeonGreen, Ochre,      /* Mod FX: green, Trem/Pan: ochre */
    VividYellow, OrangeRed, HotMagenta, Cyan        /* Comp/Sat/Pitch/Ducker */
];

const CONT_DIM_COLORS = [
    95, 95, 95, 90,
    93, 95, 93, 101,
    80, ForestGreen, 85, 75,
    73, 67, 109, 87
];

/* Punch-in names (short) */
const PUNCH_NAMES = [
    'RPT 1/4', 'RPT 1/8', 'RPT 1/16', 'RPT TRP',
    'STUTTER', 'SCATTER', 'REVERSE', 'HALF-SPD',
    'LP FILT', 'HP FILT', 'BP FILT', 'RESO PK',
    'CRUSH', 'SR RED', 'TAPE STP', 'DUCKER'
];

/* Continuous FX names (short) */
const CONT_NAMES = [
    'DELAY', 'PING', 'TAPE', 'AUTFLT',
    'PLATE', 'DARK', 'SPRING', 'SHIMMER',
    'CHORUS', 'PHASER', 'FLANGE', 'TRMPAN',
    'COMP', 'SATUR', 'PITCH', 'DUCKR'
];

/* Default encoder labels */
const DEFAULT_KNOB_LABELS = ['Dry/Wet', 'In Gain', 'LP Filt', 'HP Filt',
                              'EQ Low', 'EQ Mid', 'EQ High', 'Out Gain'];
const DEFAULT_KNOB_PARAMS = ['dry_wet', 'input_gain', 'global_lp', 'global_hp',
                              'eq_low', 'eq_mid', 'eq_high', 'output_gain'];

/* Continuous FX param labels per type (first 4) */
const CONT_PARAM_LABELS = [
    ['Time', 'Feedbk', 'Filter', 'Mix'],
    ['Time', 'Feedbk', 'Spread', 'Mix'],
    ['Age', 'Wow', 'Feedbk', 'Mix'],
    ['Rate', 'Depth', 'Reso', 'Mix'],
    ['Decay', 'Damp', 'PreDly', 'Mix'],
    ['Decay', 'Dark', 'Mod', 'Mix'],
    ['Decay', 'Tone', 'Drip', 'Mix'],
    ['Decay', 'Pitch', 'Mod', 'Mix'],
    ['Rate', 'Depth', 'Feedbk', 'Mix'],
    ['Rate', 'Depth', 'Feedbk', 'Mix'],
    ['Rate', 'Depth', 'Feedbk', 'Mix'],
    ['Rate', 'Depth', 'Shape', 'Mix'],
    ['Thrsh', 'Ratio', 'Attck', 'Mix'],
    ['Drive', 'Tone', 'Curve', 'Mix'],
    ['Pitch', 'Grain', 'Qual', 'Mix'],
    ['Rate', 'Depth', 'Shape', 'Mix']
];

/* Persistence paths */
const STATE_DIR = '/data/UserData/move-anything/perf_fx_state';
const DEFAULTS_PATH = '/data/UserData/move-anything/perf_fx_defaults.json';

/* ================================================================
 * State
 * ================================================================ */

let shiftHeld = false;
let bypassed = false;
let undoHeld = false; /* for momentary bypass */
let undoWasBypassed = false; /* bypass state before hold */

/* Punch-in state */
let punchActive = new Array(16).fill(false);
let punchPressure = new Array(16).fill(0);

/* Continuous FX state */
let contActive = new Array(16).fill(false);
let selectedCont = -1;  /* Currently selected continuous FX for encoder mapping */

/* Encoder values (0..127) */
let knobValues = [64, 64, 127, 0, 64, 64, 64, 64]; /* Defaults */
let contKnobValues = new Array(16).fill(null).map(() => [64, 64, 64, 38, 64, 64, 64, 64]);

/* Track routing */
let trackRouted = [false, false, false, false];

/* Step presets */
let stepPopulated = new Array(16).fill(false);
let currentStep = -1;

/* Scenes */
let scenePopulated = new Array(16).fill(false);
let sceneMode = false;  /* Bottom pads: false=Continuous FX, true=Scene recall */

/* Scene save/delete modifiers */
let sceneSavePending = false;
let sceneDeletePending = false;
let sceneMorphFirst = -1;

/* Step FX sequencer */
let stepSeqActive = false;
let stepRecordMode = false;
let stepSeqDivision = 0; /* 0=1/4, 1=1/8, 2=1/16, 3=1bar */
let transportRunning = false;

/* Recording */
let isRecording = false;

/* Display overlay */
let overlayText = '';
let overlayParam = '';
let overlayValue = '';
let overlayTimer = 0;
const OVERLAY_DURATION = 66; /* ~1.5 seconds at 44 ticks/sec */

/* LED init */
let ledInitPending = true;
let ledInitIndex = 0;

/* BPM and tap tempo */
let bpm = 120.0;
let tapTimes = []; /* timestamps for tap tempo */
const TAP_TIMEOUT = 2000; /* ms - reset if gap > 2s */
const TAP_MIN_TAPS = 2;

/* Audio source */
let audioSource = 1; /* 0=LINE_IN, 1=MOVE_MIX, 2=TRACKS */

/* Ducker rate */
let duckerRate = 1; /* 0=1/4, 1=1/8, 2=1/16 */

/* Persistence */
let autosaveCounter = 0;
const AUTOSAVE_INTERVAL = 440; /* ~10 seconds at 44 ticks/sec */
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
        version: 1,
        knobValues: knobValues,
        contKnobValues: contKnobValues,
        contActive: contActive,
        selectedCont: selectedCont,
        stepPopulated: stepPopulated,
        scenePopulated: scenePopulated,
        currentStep: currentStep,
        audioSource: audioSource,
        trackRouted: trackRouted,
        duckerRate: duckerRate,
        bpm: bpm
    };

    /* Also ask DSP for full state (scenes + step presets are in DSP memory) */
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
        if (!state || state.version !== 1) return false;

        if (state.knobValues) knobValues = state.knobValues;
        if (state.contKnobValues) contKnobValues = state.contKnobValues;
        if (state.contActive) contActive = state.contActive;
        if (state.selectedCont !== undefined) selectedCont = state.selectedCont;
        if (state.stepPopulated) stepPopulated = state.stepPopulated;
        if (state.scenePopulated) scenePopulated = state.scenePopulated;
        if (state.currentStep !== undefined) currentStep = state.currentStep;
        if (state.audioSource !== undefined) audioSource = state.audioSource;
        if (state.duckerRate !== undefined) duckerRate = state.duckerRate;
        if (state.bpm !== undefined) bpm = state.bpm;
        if (state.trackRouted) {
            trackRouted = state.trackRouted;
            let mask = 0;
            for (let i = 0; i < 4; i++) {
                if (trackRouted[i]) mask |= (1 << i);
            }
            sendParam('track_mask', String(mask));
        }

        /* Push restored state to DSP */
        sendParam('bpm', String(bpm));
        sendParam('audio_source', String(audioSource));
        sendParam('ducker_rate', String(duckerRate));

        /* Restore knob values to DSP */
        for (let i = 0; i < 8; i++) {
            const paramVal = knobValueToParam(i, knobValues[i]);
            sendParam(DEFAULT_KNOB_PARAMS[i], paramVal.toFixed(3));
        }

        /* Restore continuous FX active state and params */
        for (let i = 0; i < 16; i++) {
            if (contActive[i]) {
                sendParam(`cont_${i}_on`, '1');
                for (let j = 0; j < contKnobValues[i].length; j++) {
                    sendParam(`cont_${i}_param_${j}`, (contKnobValues[i][j] / 127.0).toFixed(3));
                }
            }
        }

        return true;
    } catch (e) {
        return false;
    }
}

function loadDefaults() {
    try {
        const raw = host_read_file(DEFAULTS_PATH);
        if (!raw) return;
        const defaults = JSON.parse(raw);
        if (defaults.pressure_curve !== undefined) {
            sendParam('pressure_curve', String(defaults.pressure_curve));
        }
    } catch (e) {
        /* ignore */
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

    /* Reset if too long since last tap */
    if (tapTimes.length > 0 && (now - tapTimes[tapTimes.length - 1]) > TAP_TIMEOUT) {
        tapTimes = [];
    }

    tapTimes.push(now);

    /* Keep last 8 taps */
    if (tapTimes.length > 8) tapTimes.shift();

    if (tapTimes.length >= TAP_MIN_TAPS) {
        /* Calculate average interval */
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

function buildLedList() {
    const leds = [];

    /* Punch-in pads */
    for (let i = 0; i < 16; i++) {
        leds.push({
            note: PUNCH_PADS[i],
            color: punchActive[i] ? PUNCH_COLORS[i] : PUNCH_DIM_COLORS[i]
        });
    }

    /* Bottom pads: continuous FX or scene mode */
    if (!sceneMode) {
        for (let i = 0; i < 16; i++) {
            let color = contActive[i] ? CONT_COLORS[i] : CONT_DIM_COLORS[i];
            if (selectedCont === i) color = White;
            leds.push({ note: CONT_PADS[i], color });
        }
    } else {
        for (let i = 0; i < 16; i++) {
            leds.push({
                note: CONT_PADS[i],
                color: scenePopulated[i] ? getSceneColor(i) : Black
            });
        }
    }

    /* Step buttons */
    for (let i = 0; i < 16; i++) {
        let color = Black;
        if (stepPopulated[i]) color = LightGrey;
        if (currentStep === i) color = White;
        if (stepSeqActive && currentStep === i) color = White;
        leds.push({ note: MoveSteps[i], color });
    }

    return leds;
}

function getSceneColor(idx) {
    /* Color based on scene slot position as proxy for FX category */
    if (idx < 4) return SkyBlue;      /* delay/reverb-ish */
    if (idx < 8) return AzureBlue;    /* reverb-ish */
    if (idx < 12) return BrightGreen; /* modulation */
    return OrangeRed;                  /* distortion/lofi */
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
        /* Set button LEDs */
        setButtonLED(MoveUndo, bypassed ? WhiteLedBright : WhiteLedDim);
        setButtonLED(MoveLoop, sceneMode ? WhiteLedBright : WhiteLedDim);
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

function updateSinglePadLED(padArray, index, color) {
    if (index >= 0 && index < padArray.length) {
        setLED(padArray[index], color);
    }
}

/* ================================================================
 * Display
 * ================================================================ */

function drawMainView() {
    clear_screen();

    /* Header */
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
    print(0, 0, `PFX ${bpm.toFixed(0)} ${sourceLabel}${trackInfo}`, 1);
    draw_line(0, 9, SCREEN_W, 9, 1);

    /* Active punch-ins */
    let punchStr = '';
    for (let i = 0; i < 16; i++) {
        if (punchActive[i]) {
            if (punchStr.length > 0) punchStr += '+';
            punchStr += PUNCH_NAMES[i];
        }
    }
    if (punchStr.length > 0) {
        if (punchStr.length > 21) punchStr = punchStr.substring(0, 20) + '~';
        print(0, 12, `P:${punchStr}`, 1);

        /* Pressure bar for first active punch-in */
        let firstActive = punchActive.indexOf(true);
        if (firstActive >= 0) {
            let barWidth = Math.floor(punchPressure[firstActive] * 60);
            fill_rect(60, 13, barWidth, 5, 1);
            draw_rect(60, 13, 60, 5, 1);
        }
    }

    /* Active continuous FX */
    let contStr = '';
    let contCount = 0;
    for (let i = 0; i < 16; i++) {
        if (contActive[i]) {
            if (contStr.length > 0) contStr += '  ';
            contStr += CONT_NAMES[i];
            contCount++;
        }
    }
    if (contCount > 0) {
        print(0, 22, contStr, 1);
    } else {
        print(0, 22, 'No FX active', 1);
    }

    /* Encoder labels */
    draw_line(0, 40, SCREEN_W, 40, 1);

    if (selectedCont >= 0 && contActive[selectedCont]) {
        const labels = CONT_PARAM_LABELS[selectedCont];
        const name = CONT_NAMES[selectedCont];
        print(0, 42, `${name}:`, 1);
        for (let i = 0; i < 4; i++) {
            print(i * 32, 51, labels[i], 1);
        }
        /* Show secondary params indicator */
        print(100, 42, 'E5-8', 1);
    } else {
        for (let i = 0; i < 4; i++) {
            print(i * 32, 42, DEFAULT_KNOB_LABELS[i].substring(0, 6), 1);
        }
        for (let i = 4; i < 8; i++) {
            print((i - 4) * 32, 51, DEFAULT_KNOB_LABELS[i].substring(0, 6), 1);
        }
    }

    /* Bypass indicator */
    if (bypassed) {
        draw_rect(30, 20, 68, 14, 1);
        fill_rect(31, 21, 66, 12, 0);
        print(38, 23, 'BYPASSED', 1);
    }

    /* Mode indicators */
    if (sceneMode) {
        print(90, 0, 'SCN', 1);
    }
    if (stepRecordMode) {
        print(105, 0, 'REC', 1);
    } else if (stepSeqActive) {
        print(105, 0, 'SEQ', 1);
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

function knobToLabel(knobIndex) {
    if (selectedCont >= 0 && contActive[selectedCont]) {
        if (knobIndex < 4) {
            return CONT_PARAM_LABELS[selectedCont][knobIndex];
        }
        return `Param ${knobIndex + 1}`;
    }
    return DEFAULT_KNOB_LABELS[knobIndex];
}

function knobValueToParam(knobIndex, value) {
    const param = DEFAULT_KNOB_PARAMS[knobIndex];
    if (param === 'input_gain' || param === 'output_gain') {
        return (value / 127.0) * 2.0;
    }
    if (param === 'eq_low' || param === 'eq_mid' || param === 'eq_high') {
        return (value / 127.0) * 2.0 - 1.0;
    }
    return value / 127.0;
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
    const punchIdx = PUNCH_PADS.indexOf(note);
    if (punchIdx >= 0) {
        punchActive[punchIdx] = true;
        punchPressure[punchIdx] = velocity / 127.0;
        sendParam(`punch_${punchIdx}_on`, (velocity / 127.0).toFixed(3));
        updateSinglePadLED(PUNCH_PADS, punchIdx, PUNCH_COLORS[punchIdx]);
        return;
    }

    const contIdx = CONT_PADS.indexOf(note);
    if (contIdx >= 0) {
        if (sceneMode) {
            /* Scene mode: save, delete, morph, or recall */
            if (sceneSavePending) {
                sendParam(`scene_save_${contIdx}`, '1');
                scenePopulated[contIdx] = true;
                sceneSavePending = false;
                showOverlay('Scene', `Saved #${contIdx + 1}`, '');
                ledInitPending = true; ledInitIndex = 0;
            } else if (sceneDeletePending) {
                sendParam(`scene_clear_${contIdx}`, '1');
                scenePopulated[contIdx] = false;
                sceneDeletePending = false;
                showOverlay('Scene', `Cleared #${contIdx + 1}`, '');
                ledInitPending = true; ledInitIndex = 0;
            } else if (shiftHeld) {
                /* Shift+pad in scene mode = scene morph */
                if (sceneMorphFirst < 0) {
                    sceneMorphFirst = contIdx;
                    showOverlay('Morph', `From #${contIdx + 1}...`, 'Hold Shift + press 2nd');
                } else {
                    sendParam(`scene_morph_${sceneMorphFirst}_${contIdx}`, '1');
                    showOverlay('Morph', `#${sceneMorphFirst + 1} > #${contIdx + 1}`, '~2s');
                    sceneMorphFirst = -1;
                }
            } else {
                sceneMorphFirst = -1;
                sendParam(`scene_recall_${contIdx}`, '1');
                showOverlay('Scene', `Recall #${contIdx + 1}`, '');
            }
            return;
        }

        /* Normal continuous FX mode */
        if (contActive[contIdx]) {
            if (shiftHeld) {
                /* Shift+tap: deactivate */
                sendParam(`cont_${contIdx}_off`, '1');
                contActive[contIdx] = false;
                if (selectedCont === contIdx) selectedCont = -1;
                showOverlay(CONT_NAMES[contIdx], 'Deactivated', '');
            } else if (selectedCont === contIdx) {
                /* Already selected — deselect (return to global knobs) */
                selectedCont = -1;
                showOverlay(CONT_NAMES[contIdx], 'Deselected', '');
            } else {
                /* Active but not selected — select for encoder control */
                selectedCont = contIdx;
                showOverlay(CONT_NAMES[contIdx], 'Selected', '');
            }
        } else {
            /* Inactive — activate and select */
            /* Check max 3 simultaneous limit */
            let activeCount = contActive.filter(a => a).length;
            if (activeCount >= 3) {
                showOverlay('Max FX', '3 active limit', '');
                return;
            }
            sendParam(`cont_${contIdx}_on`, '1');
            contActive[contIdx] = true;
            selectedCont = contIdx;
            showOverlay(CONT_NAMES[contIdx], 'Activated', '');
        }
        /* Update LEDs for all cont pads to show selection */
        for (let i = 0; i < 16; i++) {
            let color = contActive[i] ? CONT_COLORS[i] : CONT_DIM_COLORS[i];
            if (selectedCont === i) color = White;
            updateSinglePadLED(CONT_PADS, i, color);
        }
        return;
    }
}

function handlePadOff(note) {
    const punchIdx = PUNCH_PADS.indexOf(note);
    if (punchIdx >= 0) {
        punchActive[punchIdx] = false;
        punchPressure[punchIdx] = 0;
        sendParam(`punch_${punchIdx}_off`, '1');
        updateSinglePadLED(PUNCH_PADS, punchIdx, PUNCH_DIM_COLORS[punchIdx]);
        return;
    }
}

function handleAftertouch(note, pressure) {
    const punchIdx = PUNCH_PADS.indexOf(note);
    if (punchIdx >= 0 && punchActive[punchIdx]) {
        punchPressure[punchIdx] = pressure / 127.0;
        sendParam(`punch_${punchIdx}_pressure`, (pressure / 127.0).toFixed(3));
        updateSinglePadLED(PUNCH_PADS, punchIdx,
            pressure > 80 ? White : PUNCH_COLORS[punchIdx]);
    }
}

function handleStep(stepIdx, pressed) {
    if (!pressed) return;

    if (shiftHeld) {
        sendParam(`step_save_${stepIdx}`, '1');
        stepPopulated[stepIdx] = true;
        currentStep = stepIdx;
        setLED(MoveSteps[stepIdx], White);
        showOverlay('Step Preset', `Saved #${stepIdx + 1}`, '');
    } else if (stepRecordMode) {
        sendParam(`step_save_${stepIdx}`, '1');
        stepPopulated[stepIdx] = true;
        setLED(MoveSteps[stepIdx], BrightRed);
        showOverlay('Step Record', `Written #${stepIdx + 1}`, '');
    } else {
        if (stepPopulated[stepIdx]) {
            sendParam(`step_recall_${stepIdx}`, '1');
            if (currentStep >= 0) setLED(MoveSteps[currentStep], LightGrey);
            currentStep = stepIdx;
            setLED(MoveSteps[stepIdx], White);
            showOverlay('Step Preset', `Recalled #${stepIdx + 1}`, '');
            syncContActiveState();
        }
    }
}

function handleKnob(knobIndex, delta) {
    if (selectedCont >= 0 && contActive[selectedCont]) {
        /* All 8 knobs control the selected continuous FX */
        const values = contKnobValues[selectedCont];
        /* Ensure we have 8 values */
        while (values.length < 8) values.push(64);
        values[knobIndex] = Math.max(0, Math.min(127, values[knobIndex] + delta));
        const param = `cont_${selectedCont}_param_${knobIndex}`;
        const label = knobToLabel(knobIndex);
        sendParam(param, (values[knobIndex] / 127.0).toFixed(3));
        showOverlay(CONT_NAMES[selectedCont], label,
            (values[knobIndex] / 127.0).toFixed(2));
    } else {
        knobValues[knobIndex] = Math.max(0, Math.min(127, knobValues[knobIndex] + delta));
        const label = DEFAULT_KNOB_LABELS[knobIndex];
        const paramVal = knobValueToParam(knobIndex, knobValues[knobIndex]);
        sendParam(DEFAULT_KNOB_PARAMS[knobIndex], paramVal.toFixed(3));
        showOverlay('Global', label, (knobValues[knobIndex] / 127.0).toFixed(2));
    }
}

function handleMasterKnob(rawValue) {
    const delta = decodeDelta(rawValue);
    if (delta === 0) return;

    if (selectedCont >= 0 && contActive[selectedCont]) {
        /* Dry/wet for selected FX */
        let mixIdx = 3;
        let v = (contKnobValues[selectedCont][mixIdx] || 64) + delta;
        v = Math.max(0, Math.min(127, v));
        contKnobValues[selectedCont][mixIdx] = v;
        sendParam(`cont_${selectedCont}_param_${mixIdx}`, (v / 127.0).toFixed(3));
        showOverlay(CONT_NAMES[selectedCont], 'Mix', (v / 127.0).toFixed(2));
    } else {
        let v = (knobValues[7] || 64) + delta;
        v = Math.max(0, Math.min(127, v));
        knobValues[7] = v;
        sendParam('output_gain', (v / 127.0 * 2.0).toFixed(3));
        showOverlay('Master', 'Output', (v / 127.0).toFixed(2));
    }
}

function handleTrackButton(trackIdx, pressed) {
    if (!pressed) return;

    if (shiftHeld) {
        /* Shift+Track: solo this track (exclusive select) */
        for (let i = 0; i < 4; i++) {
            trackRouted[i] = (i === trackIdx);
        }
    } else {
        /* Toggle this track in the mix */
        trackRouted[trackIdx] = !trackRouted[trackIdx];
    }

    /* Update track mask and send to DSP */
    let mask = 0;
    for (let i = 0; i < 4; i++) {
        if (trackRouted[i]) mask |= (1 << i);
    }
    sendParam('track_mask', String(mask));

    /* If any track is selected, switch to tracks source mode */
    if (mask > 0 && audioSource !== 2) {
        audioSource = 2; /* SOURCE_TRACKS */
        sendParam('audio_source', '2');
    } else if (mask === 0 && audioSource === 2) {
        /* No tracks selected — fall back to Move mix */
        audioSource = 1; /* SOURCE_MOVE_MIX */
        sendParam('audio_source', '1');
    }

    /* Update LEDs */
    for (let i = 0; i < 4; i++) {
        setButtonLED(TRACK_CCS[i], trackRouted[i] ? WhiteLedBright : WhiteLedDim);
    }

    /* Show feedback */
    let trackList = [];
    for (let i = 0; i < 4; i++) {
        if (trackRouted[i]) trackList.push(`T${i + 1}`);
    }
    showOverlay('Source', trackList.length > 0 ? `Tracks: ${trackList.join('+')}` : 'Move Mix', '');
}

function syncContActiveState() {
    try {
        const activeStr = getParam('cont_active');
        if (activeStr) {
            const active = JSON.parse(activeStr);
            for (let i = 0; i < 16; i++) {
                contActive[i] = active[i] === 1;
            }
        }
    } catch (e) {
        /* ignore parse errors */
    }
}

function syncScenePopulatedState() {
    try {
        const raw = getParam('scene_populated');
        if (raw) {
            const arr = JSON.parse(raw);
            for (let i = 0; i < 16; i++) {
                scenePopulated[i] = arr[i] === 1;
            }
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
    console.log('Performance FX module initializing');

    /* Detect current set for per-set persistence */
    detectSetUUID();

    /* Load global defaults (pressure curve, etc.) */
    loadDefaults();

    /* Try to load per-set state */
    stateLoaded = loadState();

    if (!stateLoaded) {
        /* Fresh start - push defaults to DSP */
        sendParam('bpm', String(bpm));
        sendParam('audio_source', String(audioSource));
    }

    /* Sync scene/step populated state from DSP */
    syncScenePopulatedState();
    syncStepPopulatedState();

    /* Start progressive LED init */
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
                syncContActiveState();
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

    /* Filter capacitive touch, noise, and clock, but NOT aftertouch */
    if (isCapacitiveTouchMessage(data)) return;
    if (isNoiseMessage(data)) return;
    if (data[0] === 0xF8 || data[0] === 0xF0 || data[0] === 0xF7) return;

    /* Polyphonic aftertouch - pad pressure */
    if (status === 0xA0) {
        handleAftertouch(d1, d2);
        return;
    }

    /* Channel aftertouch - global pressure for all active punch-ins */
    if (status === 0xD0) {
        for (let i = 0; i < 16; i++) {
            if (punchActive[i]) {
                punchPressure[i] = d1 / 127.0;
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
            handlePadOff(d1);
            return;
        }
    }

    /* Note Off */
    if (status === 0x80) {
        handlePadOff(d1);
        return;
    }

    /* CC Messages */
    if (status === 0xB0) {
        /* Shift */
        if (d1 === MoveShift) {
            shiftHeld = d2 > 0;
            if (!shiftHeld) {
                sceneMorphFirst = -1;
                sceneSavePending = false;
                sceneDeletePending = false;
            }
            return;
        }

        /* Back - exit module */
        if (d1 === MoveBack && d2 > 0) {
            /* Save state before exiting */
            saveState();
            /* Deactivate all FX before exit */
            for (let i = 0; i < 16; i++) {
                if (contActive[i]) sendParam(`cont_${i}_off`, '1');
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
                /* Always bypass on press */
                if (!bypassed) {
                    bypassed = true;
                    sendParam('bypass', '1');
                    setButtonLED(MoveUndo, WhiteLedBright);
                    showOverlay('FX', 'BYPASSED', '');
                } else {
                    /* Was already bypassed — toggle off */
                    bypassed = false;
                    sendParam('bypass', '0');
                    setButtonLED(MoveUndo, WhiteLedDim);
                    showOverlay('FX', 'ACTIVE', '');
                }
            } else {
                /* Release: if we turned bypass ON and it wasn't on before, restore (momentary) */
                if (undoHeld && bypassed && !undoWasBypassed) {
                    bypassed = false;
                    sendParam('bypass', '0');
                    setButtonLED(MoveUndo, WhiteLedDim);
                }
                undoHeld = false;
            }
            return;
        }

        /* Loop - toggle scene mode */
        if (d1 === MoveLoop && d2 > 0) {
            if (shiftHeld) {
                /* Shift+Loop: toggle step FX sequencer */
                stepSeqActive = !stepSeqActive;
                sendParam('step_seq_active', stepSeqActive ? '1' : '0');
                showOverlay('Step FX Seq', stepSeqActive ? 'ON' : 'OFF', '');
            } else {
                /* Toggle scene mode */
                sceneMode = !sceneMode;
                if (sceneMode) {
                    showOverlay('Bottom Pads', 'Scene Mode', '');
                } else {
                    showOverlay('Bottom Pads', 'FX Mode', '');
                }
                ledInitPending = true;
                ledInitIndex = 0;
            }
            setButtonLED(MoveLoop, sceneMode ? WhiteLedBright : WhiteLedDim);
            return;
        }

        /* Copy - save scene / cycle ducker rate */
        if (d1 === MoveCopy && d2 > 0) {
            if (sceneMode) {
                sceneSavePending = true;
                sceneDeletePending = false;
                showOverlay('Scene', 'Press pad to save', '');
            } else {
                /* Cycle ducker rate: 1/4 -> 1/8 -> 1/16 */
                const duckerNames = ['1/4', '1/8', '1/16'];
                duckerRate = (duckerRate + 1) % 3;
                sendParam('ducker_rate', String(duckerRate));
                showOverlay('Ducker', `Rate: ${duckerNames[duckerRate]}`, '');
            }
            return;
        }

        /* Delete */
        if (d1 === MoveDelete && d2 > 0) {
            if (sceneMode) {
                sceneDeletePending = true;
                sceneSavePending = false;
                showOverlay('Scene', 'Press pad to clear', '');
            } else if (shiftHeld) {
                if (currentStep >= 0) {
                    sendParam(`step_clear_${currentStep}`, '1');
                    stepPopulated[currentStep] = false;
                    setLED(MoveSteps[currentStep], Black);
                    showOverlay('Step Preset', `Cleared #${currentStep + 1}`, '');
                    currentStep = -1;
                }
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

        /* Capture - Resample */
        if (d1 === MoveCapture && d2 > 0) {
            if (isRecording) {
                host_sampler_stop();
                isRecording = false;
                showOverlay('Resample', 'Stopped', '');
                setButtonLED(MoveCapture, WhiteLedDim);
            } else {
                const now = new Date();
                const ts = `${now.getFullYear()}${String(now.getMonth()+1).padStart(2,'0')}${String(now.getDate()).padStart(2,'0')}_${String(now.getHours()).padStart(2,'0')}${String(now.getMinutes()).padStart(2,'0')}${String(now.getSeconds()).padStart(2,'0')}`;
                const path = `/data/UserData/UserLibrary/Recordings/pfx_${ts}.wav`;
                host_sampler_start(path);
                isRecording = true;
                showOverlay('Resample', 'Recording...', '');
                setButtonLED(MoveCapture, WhiteLedBright);
            }
            return;
        }

        /* Play - toggle transport for step sequencer */
        if (d1 === MovePlay && d2 > 0) {
            transportRunning = !transportRunning;
            sendParam('transport_running', transportRunning ? '1' : '0');
            setButtonLED(MovePlay, transportRunning ? WhiteLedBright : WhiteLedDim);
            showOverlay('Transport', transportRunning ? 'Running' : 'Stopped', '');
            return;
        }

        /* Jog wheel turn */
        if (d1 === MoveMainKnob) {
            const delta = decodeDelta(d2);
            if (shiftHeld && stepSeqActive) {
                /* Shift+Jog when step seq active: change division */
                const divNames = ['1/4', '1/8', '1/16', '1 bar'];
                stepSeqDivision = (stepSeqDivision + (delta > 0 ? 1 : -1) + 4) % 4;
                sendParam('step_seq_division', String(stepSeqDivision));
                showOverlay('Step Seq', `Div: ${divNames[stepSeqDivision]}`, '');
            } else if (shiftHeld) {
                /* Shift+Jog: adjust BPM */
                bpm = Math.max(20, Math.min(300, bpm + delta * 0.5));
                sendParam('bpm', bpm.toFixed(1));
                showOverlay('Tempo', `${bpm.toFixed(1)} BPM`, (bpm / 300).toFixed(2));
            } else if (selectedCont >= 0 && contActive[selectedCont]) {
                /* Jog turn: scroll through active continuous FX */
                let activeSlots = [];
                for (let i = 0; i < 16; i++) {
                    if (contActive[i]) activeSlots.push(i);
                }
                if (activeSlots.length > 1) {
                    let curIdx = activeSlots.indexOf(selectedCont);
                    curIdx = (curIdx + delta + activeSlots.length) % activeSlots.length;
                    selectedCont = activeSlots[curIdx];
                    showOverlay(CONT_NAMES[selectedCont], 'Selected', '');
                    /* Refresh cont pad LEDs */
                    for (let i = 0; i < 16; i++) {
                        let color = contActive[i] ? CONT_COLORS[i] : CONT_DIM_COLORS[i];
                        if (selectedCont === i) color = White;
                        if (!sceneMode) updateSinglePadLED(CONT_PADS, i, color);
                    }
                }
            }
            return;
        }

        /* Jog click */
        if (d1 === MoveMainButton && d2 > 0) {
            if (shiftHeld) {
                /* Shift+Jog click: cycle audio source mode */
                audioSource = (audioSource + 1) % 3;
                sendParam('audio_source', String(audioSource));
                const sourceNames = ['Line In', 'Move Mix', 'Tracks'];
                showOverlay('Source', sourceNames[audioSource], '');
                /* Reset track selection when switching away from tracks mode */
                if (audioSource !== 2) {
                    for (let i = 0; i < 4; i++) trackRouted[i] = false;
                    sendParam('track_mask', '0');
                    for (let i = 0; i < 4; i++) {
                        setButtonLED(TRACK_CCS[i], WhiteLedDim);
                    }
                } else {
                    /* Entering tracks mode: select all by default */
                    for (let i = 0; i < 4; i++) trackRouted[i] = true;
                    sendParam('track_mask', '15');
                    for (let i = 0; i < 4; i++) {
                        setButtonLED(TRACK_CCS[i], WhiteLedBright);
                    }
                }
            } else {
                /* Jog click: tap tempo */
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

        /* Master knob */
        if (d1 === MoveMaster) {
            handleMasterKnob(d2);
            return;
        }

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
