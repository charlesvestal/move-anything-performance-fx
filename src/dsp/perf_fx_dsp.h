/*
 * Performance FX DSP Engine
 *
 * All audio effects for the Performance FX module.
 * 16 punch-in FX (momentary, pressure-sensitive)
 * 16 continuous FX (toggled, parameter-controlled)
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
#define PFX_REPEAT_BUF      (PFX_SAMPLE_RATE * 4)   /* 4 seconds for beat repeat */
#define PFX_REVERB_COMB_MAX 4096
#define PFX_REVERB_AP_MAX   1024
#define PFX_FREEZE_GRAIN    4096
#define PFX_MAX_CONTINUOUS   3  /* max simultaneous continuous FX */
#define PFX_NUM_PUNCH_IN    16
#define PFX_NUM_CONTINUOUS  16
#define PFX_NUM_PRESETS     16
#define PFX_NUM_SCENES      16
#define PFX_CONT_PARAMS     8  /* params per continuous FX */
#define PFX_NUM_ALLPASS      6  /* phaser stages */

/* ---- Pressure curve modes ---- */
enum {
    PRESSURE_LINEAR = 0,
    PRESSURE_EXPONENTIAL,
    PRESSURE_SWITCH
};

/* ---- Punch-in FX types (top 2 rows, pads 1-16) ---- */
enum {
    PUNCH_BEAT_REPEAT_4 = 0,
    PUNCH_BEAT_REPEAT_8,
    PUNCH_BEAT_REPEAT_16,
    PUNCH_BEAT_REPEAT_TRIPLET,
    PUNCH_STUTTER,
    PUNCH_SCATTER,
    PUNCH_REVERSE,
    PUNCH_HALF_SPEED,
    PUNCH_LP_FILTER,
    PUNCH_HP_FILTER,
    PUNCH_BP_FILTER,
    PUNCH_RESONANT_PEAK,
    PUNCH_BITCRUSH,
    PUNCH_SAMPLE_RATE_REDUCE,
    PUNCH_TAPE_STOP,
    PUNCH_DUCKER
};

/* ---- Continuous FX types (bottom 2 rows, pads 17-32) ---- */
enum {
    CONT_DELAY = 0,
    CONT_PING_PONG,
    CONT_TAPE_ECHO,
    CONT_AUTO_FILTER,
    CONT_PLATE_REVERB,
    CONT_DARK_REVERB,
    CONT_SPRING_REVERB,
    CONT_SHIMMER_REVERB,
    CONT_CHORUS,
    CONT_PHASER,
    CONT_FLANGER,
    CONT_TREMOLO_PAN,
    CONT_COMPRESSOR,
    CONT_SATURATOR,
    CONT_PITCH_SHIFT,
    CONT_DUCKER
};

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

/* ---- Tape stop ---- */
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

/* ---- Chorus / Flanger ---- */
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
    int   ap_pos[2];
    int   ap_len[2];
    /* Shimmer pitch shift state */
    float shimmer_buf[4096];
    int shimmer_write_pos;
    float shimmer_read_pos;
    float damping;
    float decay;
    float mix;
} reverb_t;

/* ---- Freeze (granular hold) ---- */
typedef struct {
    float buf_l[PFX_FREEZE_GRAIN];
    float buf_r[PFX_FREEZE_GRAIN];
    int grain_len;
    float read_pos;
    float pitch_shift; /* 1.0 = normal */
    int captured;
    float env;         /* crossfade envelope */
} freeze_t;

/* ---- Ring Modulator ---- */
typedef struct {
    float phase;
    float freq;
} ring_mod_t;

/* ---- Ducker ---- */
typedef struct {
    float phase;     /* 0..1 within beat division */
    int rate_div;    /* 0=1/4, 1=1/8, 2=1/16 */
} ducker_t;

/* ---- Punch-in FX slot ---- */
typedef struct {
    int active;
    float pressure;
    float velocity;
    float intensity;   /* computed from velocity + pressure + curve */

    /* Shared state for various punch-in types */
    svf_t filter_l;
    svf_t filter_r;
    repeat_t repeat;
    tape_stop_t tape;
    ducker_t ducker;
    float crush_hold_l, crush_hold_r;
    unsigned int crush_count;    /* sample rate reduce counter */
    unsigned int scatter_seed;   /* separate RNG seed for scatter */

    int fading_out;          /* 1 = crossfading to dry */
    int fade_pos;            /* current position in fade */
    int fade_len;            /* total fade length in samples */
} punch_in_t;

/* ---- Continuous FX slot ---- */
typedef struct {
    int active;
    float params[PFX_CONT_PARAMS];

    /* Effect-specific state */
    delay_t delay;
    reverb_t reverb;
    phaser_t phaser;
    mod_delay_t mod_delay;
    compressor_t comp;
    freeze_t freeze;
    ring_mod_t ring;
    svf_t filter_l;    /* for tone controls */
    svf_t filter_r;
} continuous_t;

/* ---- Scene snapshot ---- */
typedef struct {
    int populated;
    int cont_active[PFX_NUM_CONTINUOUS];
    float cont_params[PFX_NUM_CONTINUOUS][PFX_CONT_PARAMS];
    float global_params[8];  /* dry_wet, in_gain, lp, hp, eq_l, eq_m, eq_h, out_gain */
} scene_t;

/* ---- Step chain preset ---- */
typedef struct {
    int populated;
    int cont_active[PFX_NUM_CONTINUOUS];
    float cont_params[PFX_NUM_CONTINUOUS][PFX_CONT_PARAMS];
    float global_params[8];
} step_preset_t;

/* ---- Audio source modes ---- */
enum {
    SOURCE_LINE_IN = 0,      /* Line-in / mic */
    SOURCE_MOVE_MIX,         /* Move's mixed audio output */
    SOURCE_TRACKS            /* Per-track from Link Audio */
};
#define PFX_TRACK_COUNT 4

/* ---- Scene morph ---- */
typedef struct {
    int active;
    int scene_a;
    int scene_b;
    float progress;          /* 0..1 (0 = scene_a, 1 = scene_b) */
    float rate;              /* progress per sample (~2 sec) */
} scene_morph_t;

/* ---- Main engine ---- */
typedef struct {
    /* Global parameters */
    float dry_wet;       /* 0..1 */
    float input_gain;    /* 0..2 */
    float output_gain;   /* 0..2 */
    float global_lp_cutoff;   /* 0..1 */
    float global_hp_cutoff;   /* 0..1 */
    float eq_low_gain;   /* -1..1 */
    float eq_mid_gain;
    float eq_high_gain;

    /* Global filters (stereo pairs) */
    svf_t global_lp_l, global_lp_r;
    svf_t global_hp_l, global_hp_r;
    svf_t eq_low_l, eq_low_r;
    svf_t eq_mid_l, eq_mid_r;
    svf_t eq_high_l, eq_high_r;

    /* Tempo */
    float bpm;
    int transport_running;

    /* Pressure curve */
    int pressure_curve;   /* PRESSURE_LINEAR etc */

    /* Audio source */
    int audio_source;
    int track_mask;          /* bitmask: which tracks to mix (bits 0-3) */

    /* Per-track audio from Link Audio (set by plugin from shm) */
    int16_t *track_audio[4]; /* pointers to per-track stereo interleaved data */
    int track_audio_valid;   /* set each block if track data available */

    /* Bypass */
    int bypassed;

    /* Punch-in FX */
    punch_in_t punch[PFX_NUM_PUNCH_IN];

    /* Continuous FX */
    continuous_t cont[PFX_NUM_CONTINUOUS];
    int active_cont_slots[PFX_MAX_CONTINUOUS]; /* indices of active continuous FX */
    int active_cont_count;

    /* Scenes and presets */
    scene_t scenes[PFX_NUM_SCENES];
    step_preset_t step_presets[PFX_NUM_PRESETS];
    int current_step_preset;  /* -1 = none */

    /* Step FX sequencer */
    int step_seq_active;
    int step_seq_pos;
    float step_seq_phase;
    int step_seq_division;  /* 0=1/4, 1=1/8, 2=1/16, 3=1bar */

    /* Capture buffer for beat repeat / reverse / half-speed */
    float *capture_buf_l;
    float *capture_buf_r;
    int capture_len;
    int capture_write_pos;

    /* Working buffers */
    float work_l[PFX_BLOCK_SIZE];
    float work_r[PFX_BLOCK_SIZE];
    float dry_l[PFX_BLOCK_SIZE];
    float dry_r[PFX_BLOCK_SIZE];

    /* Scene morph */
    scene_morph_t morph;

    /* Host audio pointers (set from mapped_memory) */
    uint8_t *mapped_memory;
    int audio_out_offset;
    int audio_in_offset;
} perf_fx_engine_t;

/* ---- API ---- */
void pfx_engine_init(perf_fx_engine_t *e);
void pfx_engine_destroy(perf_fx_engine_t *e);
void pfx_engine_reset(perf_fx_engine_t *e);

/* Process one block (128 frames). Reads from host audio, writes to out_lr */
void pfx_engine_render(perf_fx_engine_t *e, int16_t *out_lr, int frames);

/* Punch-in FX control */
void pfx_punch_activate(perf_fx_engine_t *e, int slot, float velocity);
void pfx_punch_deactivate(perf_fx_engine_t *e, int slot);
void pfx_punch_set_pressure(perf_fx_engine_t *e, int slot, float pressure);

/* Continuous FX control */
void pfx_cont_toggle(perf_fx_engine_t *e, int slot);
void pfx_cont_activate(perf_fx_engine_t *e, int slot);
void pfx_cont_deactivate(perf_fx_engine_t *e, int slot);
void pfx_cont_set_param(perf_fx_engine_t *e, int slot, int param_idx, float value);

/* Scene management */
void pfx_scene_save(perf_fx_engine_t *e, int slot);
void pfx_scene_recall(perf_fx_engine_t *e, int slot);
void pfx_scene_clear(perf_fx_engine_t *e, int slot);

/* Step preset management */
void pfx_step_save(perf_fx_engine_t *e, int slot);
void pfx_step_recall(perf_fx_engine_t *e, int slot);
void pfx_step_clear(perf_fx_engine_t *e, int slot);

/* Scene morphing */
void pfx_scene_morph_start(perf_fx_engine_t *e, int scene_a, int scene_b);
void pfx_scene_morph_tick(perf_fx_engine_t *e, int frames);

/* State serialization */
int pfx_serialize_state(perf_fx_engine_t *e, char *buf, int buf_len);

/* DSP helpers */
float pfx_apply_pressure_curve(float pressure, float velocity, int curve);
int pfx_bpm_to_samples(float bpm, float division);

#endif /* PERF_FX_DSP_H */
