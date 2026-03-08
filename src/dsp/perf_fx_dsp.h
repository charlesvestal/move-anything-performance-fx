/*
 * Performance FX DSP Engine v2
 *
 * 32 unified punch-in FX, all momentary by default with shift+pad latch.
 * Row 4 (0-7):   Time/Repeat
 * Row 3 (8-15):  Filter Sweeps (animated, phase-driven)
 * Row 2 (16-23): Space Throws (tail decays on release)
 * Row 1 (24-31): Distortion & Rhythm
 */

#ifndef PERF_FX_DSP_H
#define PERF_FX_DSP_H

#include <stdint.h>

static inline float pfx_clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

#define PFX_SAMPLE_RATE     44100
#define PFX_BLOCK_SIZE      128
#define PFX_MAX_DELAY       (PFX_SAMPLE_RATE * 4)   /* 4 seconds */
#define PFX_REPEAT_BUF      (PFX_SAMPLE_RATE * 2)   /* 2 seconds per repeat slot */
#define PFX_CAPTURE_BUF     (PFX_SAMPLE_RATE * 4)   /* 4 seconds shared capture */
#define PFX_CHORUS_BUF      (PFX_SAMPLE_RATE * 1)   /* 1 second for chorus/flanger */
#define PFX_REVERB_COMB_MAX 4096
#define PFX_REVERB_AP_MAX   1024
#define PFX_NUM_FX          32
#define PFX_NUM_SCENES      16
#define PFX_NUM_PRESETS     16
#define PFX_SLOT_PARAMS     4   /* params per FX slot (E1-E4) */
#define PFX_NUM_GLOBALS     4   /* global params (E5-E8) */
#define PFX_NUM_ALLPASS     6   /* phaser stages */

/* ---- Pressure curve modes ---- */
enum {
    PRESSURE_LINEAR = 0,
    PRESSURE_EXPONENTIAL,
    PRESSURE_SWITCH
};

/* ---- Unified FX types (32 total) ---- */
enum {
    /* Row 4: Time/Repeat (pads 92-99 -> slots 0-7) */
    FX_RPT_1_4 = 0,
    FX_RPT_1_8,
    FX_RPT_1_16,
    FX_RPT_TRIP,
    FX_STUTTER,
    FX_SCATTER,
    FX_REVERSE,
    FX_HALF_SPEED,

    /* Row 3: Filter Sweeps (pads 84-91 -> slots 8-15) */
    FX_LP_SWEEP_DOWN,
    FX_HP_SWEEP_UP,
    FX_BP_RISE,
    FX_BP_FALL,
    FX_RESO_SWEEP,
    FX_PHASER,
    FX_FLANGER,
    FX_AUTO_FILTER,

    /* Row 2: Space Throws (pads 76-83 -> slots 16-23) */
    FX_DELAY,
    FX_PING_PONG,
    FX_TAPE_ECHO,
    FX_ECHO_FREEZE,
    FX_REVERB,
    FX_SHIMMER,
    FX_DARK_VERB,
    FX_SPRING,

    /* Row 1: Distortion & Rhythm (pads 68-75 -> slots 24-31) */
    FX_BITCRUSH,
    FX_DOWNSAMPLE,
    FX_TAPE_STOP,
    FX_VINYL_BRAKE,
    FX_SATURATE,
    FX_GATE_DUCK,
    FX_TREMOLO,
    FX_CHORUS
};

/* ---- FX category helpers ---- */
#define FX_IS_REPEAT(s)  ((s) >= FX_RPT_1_4 && (s) <= FX_HALF_SPEED)
#define FX_IS_FILTER(s)  ((s) >= FX_LP_SWEEP_DOWN && (s) <= FX_AUTO_FILTER)
#define FX_IS_SPACE(s)   ((s) >= FX_DELAY && (s) <= FX_SPRING)
#define FX_IS_DISTORT(s) ((s) >= FX_BITCRUSH && (s) <= FX_CHORUS)

/* ---- State Variable Filter ---- */
typedef struct {
    float lp, bp, hp;
} svf_t;

/* ---- Delay line ---- */
typedef struct {
    float *buf_l;
    float *buf_r;
    int length;     /* allocated length */
    int write_pos;
    float time;     /* 0..1 mapped to delay range */
    float feedback;
    float filter;   /* LP in feedback (0..1) */
    float mix;
    float fb_lp_l, fb_lp_r;  /* feedback filter state */
} delay_t;

/* ---- Beat repeat / stutter ---- */
typedef struct {
    float *buf_l;
    float *buf_r;
    int buf_len;
    int write_pos;
    int read_pos;
    int repeat_len;    /* samples per repeat */
    int repeat_pos;    /* position within current repeat */
    int capturing;     /* 1 = filling buffer */
    int frames_captured;
} repeat_t;

/* ---- Tape stop / vinyl brake ---- */
typedef struct {
    float *buf_l;
    float *buf_r;
    int buf_len;
    int write_pos;
    float read_pos;   /* fractional for pitch shift */
    float speed;      /* 1.0 = normal, 0.0 = stopped */
    float decel_rate; /* speed decrease per sample */
} tape_stop_t;

/* ---- Compressor ---- */
typedef struct {
    float env;        /* envelope follower */
    float threshold;
    float ratio;
    float attack;     /* coefficient */
    float release;    /* coefficient */
    float makeup;
} compressor_t;

/* ---- Allpass for phaser ---- */
typedef struct {
    float y1;
} allpass1_t;

/* ---- Phaser ---- */
typedef struct {
    allpass1_t ap[PFX_NUM_ALLPASS];
    float lfo_phase;
} phaser_t;

/* ---- Chorus / Flanger modulated delay ---- */
typedef struct {
    float *buf_l;
    float *buf_r;
    int buf_len;
    int write_pos;
    float lfo_phase;
} mod_delay_t;

/* ---- Reverb (Schroeder) ---- */
typedef struct {
    float comb_buf[4][PFX_REVERB_COMB_MAX];
    int   comb_pos[4];
    int   comb_len[4];
    float comb_filt[4];
    int   comb_pos_r[4];     /* separate read positions for right channel */
    float comb_filt_r[4];
    float ap_buf[2][PFX_REVERB_AP_MAX];
    float ap_buf_r[2][PFX_REVERB_AP_MAX];  /* separate allpass buffers for R */
    int   ap_pos[2];
    int   ap_pos_r[2];
    int   ap_len[2];
    /* Shimmer pitch shift state */
    float shimmer_buf[4096];
    int shimmer_write_pos;
    float shimmer_read_pos;
    float damping;
    float decay;
    float mix;
} reverb_t;

/* ---- Ducker ---- */
typedef struct {
    float phase;     /* 0..1 within beat division */
    float env;       /* smoothed gain */
} ducker_t;

/* ---- Unified FX slot ---- */
typedef struct {
    int active;           /* 1 = currently processing */
    int latched;          /* 1 = stays active after release */
    int tail_active;      /* 1 = space FX tail still decaying */
    float pressure;       /* 0..1 current pressure */
    float velocity;       /* 0..1 note-on velocity */
    float phase;          /* 0..1 animation phase (filter sweeps) */
    float params[PFX_SLOT_PARAMS];  /* per-FX params (E1-E4) */

    /* Fade state for smooth transitions */
    int fading_out;
    int fade_pos;
    int fade_len;

    /* Tail silence counter for space FX */
    int tail_silence_count;

    /* Per-type DSP state — allocated/used based on slot type */
    /* Repeat FX (slots 0-7) */
    repeat_t repeat;
    tape_stop_t tape;     /* also used for half-speed, tape stop, vinyl brake */

    /* Filter FX (slots 8-15) */
    svf_t filter_l;
    svf_t filter_r;
    phaser_t phaser;
    mod_delay_t mod_delay; /* for flanger */

    /* Space FX (slots 16-23) */
    delay_t delay;
    reverb_t *reverb;     /* heap allocated — only for reverb slots */
    int echo_frozen;      /* echo freeze flag */

    /* Distortion FX (slots 24-31) */
    float crush_hold_l, crush_hold_r;
    unsigned int crush_count;
    unsigned int scatter_seed;
    ducker_t ducker;
    float trem_lfo_phase;    /* tremolo LFO */
    /* Chorus uses mod_delay above */

    /* Saturate tone filter */
    svf_t sat_filter_l;
    svf_t sat_filter_r;
} pfx_slot_t;

/* ---- Scene snapshot (simplified) ---- */
typedef struct {
    int populated;
    int latched[PFX_NUM_FX];
    float params[PFX_NUM_FX][PFX_SLOT_PARAMS];
    float globals[PFX_NUM_GLOBALS];
} pfx_scene_t;

/* ---- Step chain preset ---- */
typedef struct {
    int populated;
    int latched[PFX_NUM_FX];
    float params[PFX_NUM_FX][PFX_SLOT_PARAMS];
    float globals[PFX_NUM_GLOBALS];
} pfx_step_preset_t;

/* ---- Audio source modes ---- */
enum {
    SOURCE_LINE_IN = 0,      /* Line-in / mic */
    SOURCE_MOVE_MIX,         /* Move's mixed audio output */
    SOURCE_TRACKS            /* Per-track from Link Audio */
};
#define PFX_TRACK_COUNT 4

/* ---- Tail silence threshold ---- */
#define PFX_TAIL_THRESHOLD  0.001f
#define PFX_TAIL_SILENCE_FRAMES 1000

/* ---- Main engine ---- */
typedef struct {
    /* Global parameters (E5-E8) */
    float global_hpf;       /* 0..1, default 0 = off */
    float global_lpf;       /* 0..1, default 1 = open */
    float dry_wet;          /* 0..1, default 1 = full wet */
    float eq_bump_freq;     /* 0..1, default 0.5 = centered */

    /* Global filters (stereo pairs) */
    svf_t global_lp_l, global_lp_r;
    svf_t global_hp_l, global_hp_r;
    svf_t eq_bump_l, eq_bump_r;

    /* Tempo */
    float bpm;
    int transport_running;

    /* Pressure curve */
    int pressure_curve;

    /* Audio source */
    int audio_source;
    int track_mask;

    /* Per-track audio from Link Audio */
    int16_t *track_audio[4];
    int track_audio_valid;

    /* Bypass */
    int bypassed;

    /* 32 unified FX slots */
    pfx_slot_t slots[PFX_NUM_FX];

    /* Last touched slot for E1-E4 mapping */
    int last_touched_slot;

    /* Scenes and step presets */
    pfx_scene_t scenes[PFX_NUM_SCENES];
    pfx_step_preset_t step_presets[PFX_NUM_PRESETS];
    int current_step_preset;

    /* Step FX sequencer */
    int step_seq_active;
    int step_seq_pos;
    float step_seq_phase;
    int step_seq_division;

    /* Shared capture buffer for repeat/reverse/half-speed/scatter */
    float *capture_buf_l;
    float *capture_buf_r;
    int capture_len;
    int capture_write_pos;

    /* Working buffers */
    float work_l[PFX_BLOCK_SIZE];
    float work_r[PFX_BLOCK_SIZE];
    float dry_l[PFX_BLOCK_SIZE];
    float dry_r[PFX_BLOCK_SIZE];

    /* Host audio pointers (set from mapped_memory) */
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;

    /* Direct input: if non-NULL, render reads from this instead of mapped_memory */
    int16_t *direct_input;
} perf_fx_engine_t;

/* ---- API ---- */
void pfx_engine_init(perf_fx_engine_t *e);
void pfx_engine_destroy(perf_fx_engine_t *e);
void pfx_engine_reset(perf_fx_engine_t *e);

/* Process one block (128 frames). Reads from host audio, writes to out_lr */
void pfx_engine_render(perf_fx_engine_t *e, int16_t *out_lr, int frames);

/* Unified FX control */
void pfx_activate(perf_fx_engine_t *e, int slot, float velocity);
void pfx_deactivate(perf_fx_engine_t *e, int slot);
void pfx_set_pressure(perf_fx_engine_t *e, int slot, float pressure);
void pfx_set_param(perf_fx_engine_t *e, int slot, int idx, float val);
void pfx_set_latched(perf_fx_engine_t *e, int slot, int latched);

/* Scene management */
void pfx_scene_save(perf_fx_engine_t *e, int slot);
void pfx_scene_recall(perf_fx_engine_t *e, int slot);
void pfx_scene_clear(perf_fx_engine_t *e, int slot);

/* Step preset management */
void pfx_step_save(perf_fx_engine_t *e, int slot);
void pfx_step_recall(perf_fx_engine_t *e, int slot);
void pfx_step_clear(perf_fx_engine_t *e, int slot);

/* State serialization */
int pfx_serialize_state(perf_fx_engine_t *e, char *buf, int buf_len);

/* DSP helpers */
float pfx_apply_pressure_curve(float pressure, float velocity, int curve);
int pfx_bpm_to_samples(float bpm, float division);

#endif /* PERF_FX_DSP_H */
