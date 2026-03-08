/*
 * Performance FX DSP Engine Implementation
 *
 * 32 audio effects: 16 punch-in (momentary) + 16 continuous (toggled).
 * All effects implemented in pure C, no external DSP libraries.
 */

#include "perf_fx_dsp.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================
 * Utility helpers
 * ============================================================ */

/* Use pfx_clampf from header */
#define clampf pfx_clampf

/* Overflow-safe snprintf helper */
#define SAFE_SNPRINTF(buf, n, len, ...) do { \
    n += snprintf((buf) + (n), (n) < (len) ? (len) - (n) : 0, __VA_ARGS__); \
    if ((n) >= (len)) return (len) - 1; \
} while(0)

static inline float lerpf(float a, float b, float t) __attribute__((unused));
static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float soft_clip(float x) {
    /* Continuous soft clipper using fast_tanh approximation */
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    /* Smooth polynomial in [-1.5, 1.5] */
    return x - (x * x * x) / 6.75f;
}

static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* Simple white noise */
static inline float white_noise(unsigned int *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(int)(*seed) / 2147483648.0f;
}

/* Convert 0..1 cutoff to SVF coefficient */
static inline float cutoff_to_f(float cutoff01) {
    float hz = 20.0f * powf(1000.0f, cutoff01);
    if (hz > 20000.0f) hz = 20000.0f;
    float f = 2.0f * sinf(M_PI * hz / PFX_SAMPLE_RATE);
    return clampf(f, 0.0f, 1.0f);
}

float pfx_apply_pressure_curve(float pressure, float velocity, int curve) {
    float base = velocity;
    float mod;
    switch (curve) {
        case PRESSURE_EXPONENTIAL:
            mod = pressure * pressure;
            break;
        case PRESSURE_SWITCH:
            mod = pressure > 0.3f ? 1.0f : 0.0f;
            break;
        default: /* LINEAR */
            mod = pressure;
            break;
    }
    /* Velocity sets starting point, pressure modulates from there */
    return clampf(base * 0.5f + mod * 0.5f + base * mod * 0.5f, 0.0f, 1.0f);
}

int pfx_bpm_to_samples(float bpm, float division) {
    if (bpm < 20.0f) bpm = 120.0f;
    float beat_samples = (60.0f / bpm) * PFX_SAMPLE_RATE;
    return (int)(beat_samples * division);
}

/* ============================================================
 * State Variable Filter
 * ============================================================ */

static void svf_reset(svf_t *s) {
    s->lp = s->bp = s->hp = 0.0f;
}

static inline float flush_denormal(float x) {
    union { float f; uint32_t u; } v = { .f = x };
    return (v.u & 0x7F800000) == 0 ? 0.0f : x;
}

static void svf_process(svf_t *s, float input, float f, float q,
                         float *lp, float *hp, float *bp) {
    s->hp = input - s->lp - q * s->bp;
    s->bp += f * s->hp;
    s->lp += f * s->bp;
    /* Flush denormals to prevent CPU spikes */
    s->bp = flush_denormal(s->bp);
    s->lp = flush_denormal(s->lp);
    if (lp) *lp = s->lp;
    if (hp) *hp = s->hp;
    if (bp) *bp = s->bp;
}

/* ============================================================
 * Delay line helpers
 * ============================================================ */

static void delay_init(delay_t *d, int max_len) {
    d->buf_l = (float *)calloc(max_len, sizeof(float));
    d->buf_r = (float *)calloc(max_len, sizeof(float));
    d->length = max_len;
    d->write_pos = 0;
    d->time = 0.3f;
    d->feedback = 0.4f;
    d->filter = 0.5f;
    d->mix = 0.3f;
    d->fb_lp_l = d->fb_lp_r = 0.0f;
}

static void delay_free(delay_t *d) {
    free(d->buf_l); free(d->buf_r);
    d->buf_l = d->buf_r = NULL;
}

static void delay_reset(delay_t *d) {
    if (d->buf_l) memset(d->buf_l, 0, d->length * sizeof(float));
    if (d->buf_r) memset(d->buf_r, 0, d->length * sizeof(float));
    d->write_pos = 0;
    d->fb_lp_l = d->fb_lp_r = 0.0f;
}

static void delay_write(delay_t *d, float l, float r) {
    d->buf_l[d->write_pos] = l;
    d->buf_r[d->write_pos] = r;
    d->write_pos = (d->write_pos + 1) % d->length;
}

static void delay_read(delay_t *d, int delay_samples, float *l, float *r) {
    int pos = (d->write_pos - delay_samples + d->length) % d->length;
    *l = d->buf_l[pos];
    *r = d->buf_r[pos];
}

static void delay_read_interp(delay_t *d, float delay_samples, float *l, float *r) {
    int pos0 = (d->write_pos - (int)delay_samples + d->length) % d->length;
    int pos1 = (pos0 - 1 + d->length) % d->length;
    float frac = delay_samples - (int)delay_samples;
    *l = d->buf_l[pos0] + frac * (d->buf_l[pos1] - d->buf_l[pos0]);
    *r = d->buf_r[pos0] + frac * (d->buf_r[pos1] - d->buf_r[pos0]);
}

/* ============================================================
 * Repeat buffer helpers
 * ============================================================ */

static void repeat_init(repeat_t *r, int max_len) {
    r->buf_l = (float *)calloc(max_len, sizeof(float));
    r->buf_r = (float *)calloc(max_len, sizeof(float));
    r->buf_len = max_len;
    r->write_pos = 0;
    r->read_pos = 0;
    r->repeat_len = PFX_SAMPLE_RATE / 4;
    r->repeat_pos = 0;
    r->capturing = 1;
    r->frames_captured = 0;
}

static void repeat_free(repeat_t *r) {
    free(r->buf_l); free(r->buf_r);
    r->buf_l = r->buf_r = NULL;
}

/* ============================================================
 * Reverb (Schroeder/Moorer)
 * ============================================================ */

static const int COMB_LENGTHS[4] = { 1557, 1617, 1491, 1422 };
static const int AP_LENGTHS[2] = { 225, 556 };

static void reverb_init(reverb_t *rv) {
    memset(rv, 0, sizeof(*rv));
    for (int i = 0; i < 4; i++) {
        rv->comb_len[i] = COMB_LENGTHS[i];
        rv->comb_pos[i] = 0;
        rv->comb_filt[i] = 0.0f;
        rv->comb_pos_r[i] = rv->comb_len[i] / 3; /* offset by 1/3 for stereo */
        rv->comb_filt_r[i] = 0.0f;
    }
    for (int i = 0; i < 2; i++) {
        rv->ap_len[i] = AP_LENGTHS[i];
        rv->ap_pos[i] = 0;
    }
    rv->decay = 0.7f;
    rv->damping = 0.4f;
    rv->mix = 0.3f;
}

static float reverb_process_mono(reverb_t *rv, float input) {
    float out = 0.0f;
    float decay = rv->decay;
    float damp = rv->damping;

    /* 4 parallel comb filters */
    for (int i = 0; i < 4; i++) {
        float *buf = rv->comb_buf[i];
        int pos = rv->comb_pos[i];
        float delayed = buf[pos];

        /* LP filter in feedback */
        rv->comb_filt[i] = delayed * (1.0f - damp) + rv->comb_filt[i] * damp;
        buf[pos] = input + rv->comb_filt[i] * decay;

        rv->comb_pos[i] = (pos + 1) % rv->comb_len[i];
        out += delayed;
    }
    out *= 0.25f;

    /* 2 series allpass filters */
    for (int i = 0; i < 2; i++) {
        float *buf = rv->ap_buf[i];
        int pos = rv->ap_pos[i];
        float delayed = buf[pos];
        float y = -out * 0.5f + delayed;
        buf[pos] = out + delayed * 0.5f;
        rv->ap_pos[i] = (pos + 1) % rv->ap_len[i];
        out = y;
    }

    return out;
}

static void reverb_process_stereo(reverb_t *rv, float in_l, float in_r,
                                   float *out_l, float *out_r) {
    float left = 0.0f, right = 0.0f;
    float decay = rv->decay;
    float damp = rv->damping;

    for (int i = 0; i < 4; i++) {
        float *buf = rv->comb_buf[i];

        /* Left channel */
        int pos_l = rv->comb_pos[i];
        float del_l = buf[pos_l];
        rv->comb_filt[i] = del_l * (1.0f - damp) + rv->comb_filt[i] * damp;
        buf[pos_l] = in_l + rv->comb_filt[i] * decay;
        rv->comb_pos[i] = (pos_l + 1) % rv->comb_len[i];
        left += del_l;

        /* Right channel reads from offset position in same buffer */
        int pos_r = rv->comb_pos_r[i];
        float del_r = buf[pos_r];
        rv->comb_filt_r[i] = del_r * (1.0f - damp) + rv->comb_filt_r[i] * damp;
        /* Right reads only, doesn't write — shares buffer with left */
        rv->comb_pos_r[i] = (pos_r + 1) % rv->comb_len[i];
        right += del_r;
    }
    left *= 0.25f;
    right *= 0.25f;

    /* Allpass filters — separate for L and R */
    for (int i = 0; i < 2; i++) {
        float *buf = rv->ap_buf[i];
        int pos = rv->ap_pos[i];
        float delayed = buf[pos];

        float yl = -left * 0.5f + delayed;
        buf[pos] = left + delayed * 0.5f;
        left = yl;

        float yr = -right * 0.5f + delayed;
        right = yr;

        rv->ap_pos[i] = (pos + 1) % rv->ap_len[i];
    }

    *out_l = left;
    *out_r = right;
}

/* ============================================================
 * Engine init / destroy
 * ============================================================ */

void pfx_engine_init(perf_fx_engine_t *e) {
    memset(e, 0, sizeof(*e));

    e->dry_wet = 1.0f;
    e->input_gain = 1.0f;
    e->output_gain = 1.0f;
    e->global_lp_cutoff = 1.0f;
    e->global_hp_cutoff = 0.0f;
    e->bpm = 120.0f;
    e->pressure_curve = PRESSURE_EXPONENTIAL;
    e->audio_source = SOURCE_MOVE_MIX;
    e->track_mask = 0x0F;  /* all 4 tracks */
    e->current_step_preset = -1;

    /* Allocate capture buffer for beat repeat / reverse / half-speed */
    e->capture_len = PFX_REPEAT_BUF;
    e->capture_buf_l = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
    e->capture_buf_r = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
    if (!e->capture_buf_l || !e->capture_buf_r) return;
    e->capture_write_pos = 0;

    /* Init punch-in FX state */
    for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
        punch_in_t *p = &e->punch[i];
        repeat_init(&p->repeat, PFX_REPEAT_BUF);
        p->tape.buf_l = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
        p->tape.buf_r = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
        p->tape.buf_len = PFX_REPEAT_BUF;
        p->tape.speed = 1.0f;
    }

    /* Init continuous FX state */
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        continuous_t *c = &e->cont[i];
        delay_init(&c->delay, PFX_MAX_DELAY);

        /* Modulated delay for chorus/flanger */
        c->mod_delay.buf_l = (float *)calloc(PFX_SAMPLE_RATE, sizeof(float));
        c->mod_delay.buf_r = (float *)calloc(PFX_SAMPLE_RATE, sizeof(float));
        c->mod_delay.buf_len = PFX_SAMPLE_RATE;

        reverb_init(&c->reverb);

        /* Default continuous FX params */
        for (int j = 0; j < PFX_CONT_PARAMS; j++)
            c->params[j] = 0.5f;
        c->params[3] = 0.3f; /* mix default */
        c->lofi.noise_seed = 12345 + i * 7;
        c->lofi.hold_period = 1;
    }

    /* Init scene morph */
    memset(&e->morph, 0, sizeof(e->morph));
}

void pfx_engine_destroy(perf_fx_engine_t *e) {
    free(e->capture_buf_l);
    free(e->capture_buf_r);
    for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
        repeat_free(&e->punch[i].repeat);
        free(e->punch[i].tape.buf_l);
        free(e->punch[i].tape.buf_r);
    }
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        delay_free(&e->cont[i].delay);
        free(e->cont[i].mod_delay.buf_l);
        free(e->cont[i].mod_delay.buf_r);
    }
}

void pfx_engine_reset(perf_fx_engine_t *e) {
    for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
        e->punch[i].active = 0;
        e->punch[i].pressure = 0.0f;
        e->punch[i].fading_out = 0;
        svf_reset(&e->punch[i].filter_l);
        svf_reset(&e->punch[i].filter_r);
    }
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        e->cont[i].active = 0;
        delay_reset(&e->cont[i].delay);
    }
    e->active_cont_count = 0;
    e->bypassed = 0;
    svf_reset(&e->global_lp_l);
    svf_reset(&e->global_lp_r);
    svf_reset(&e->global_hp_l);
    svf_reset(&e->global_hp_r);
    svf_reset(&e->eq_low_l);
    svf_reset(&e->eq_low_r);
    svf_reset(&e->eq_mid_l);
    svf_reset(&e->eq_mid_r);
    svf_reset(&e->eq_high_l);
    svf_reset(&e->eq_high_r);
}

/* ============================================================
 * Punch-in FX control
 * ============================================================ */

void pfx_punch_activate(perf_fx_engine_t *e, int slot, float velocity) {
    if (slot < 0 || slot >= PFX_NUM_PUNCH_IN) return;
    punch_in_t *p = &e->punch[slot];
    p->active = 1;
    p->fading_out = 0;
    p->velocity = velocity;
    p->pressure = velocity;  /* initial pressure = velocity */
    p->intensity = pfx_apply_pressure_curve(velocity, velocity, e->pressure_curve);

    /* Reset effect-specific state on activation */
    svf_reset(&p->filter_l);
    svf_reset(&p->filter_r);

    /* Tape stop: start at normal speed */
    if (slot == PUNCH_TAPE_STOP) {
        p->tape.speed = 1.0f;
        p->tape.read_pos = (float)e->capture_write_pos;
    }

    /* Beat repeat: start capturing from current position */
    if (slot >= PUNCH_BEAT_REPEAT_4 && slot <= PUNCH_STUTTER) {
        p->repeat.capturing = 1;
        p->repeat.frames_captured = 0;
        p->repeat.read_pos = 0;
        p->repeat.repeat_pos = 0;

        /* Set repeat length based on type and BPM */
        float div;
        switch (slot) {
            case PUNCH_BEAT_REPEAT_4:  div = 1.0f; break;
            case PUNCH_BEAT_REPEAT_8:  div = 0.5f; break;
            case PUNCH_BEAT_REPEAT_16: div = 0.25f; break;
            case PUNCH_BEAT_REPEAT_TRIPLET: div = 2.0f/3.0f; break;
            case PUNCH_STUTTER: div = 0.125f; break;
            default: div = 0.5f;
        }
        p->repeat.repeat_len = pfx_bpm_to_samples(e->bpm, div);
        if (p->repeat.repeat_len < 64) p->repeat.repeat_len = 64;
        if (p->repeat.repeat_len > p->repeat.buf_len)
            p->repeat.repeat_len = p->repeat.buf_len;
    }

    /* Reverse: copy capture buffer */
    if (slot == PUNCH_REVERSE) {
        int len = PFX_SAMPLE_RATE; /* 1 second */
        for (int i = 0; i < len && i < p->repeat.buf_len; i++) {
            int src = (e->capture_write_pos - len + i + e->capture_len) % e->capture_len;
            p->repeat.buf_l[i] = e->capture_buf_l[src];
            p->repeat.buf_r[i] = e->capture_buf_r[src];
        }
        p->repeat.repeat_len = len;
        p->repeat.read_pos = len - 1;
    }

    /* Half-speed: copy capture buffer */
    if (slot == PUNCH_HALF_SPEED) {
        int len = PFX_SAMPLE_RATE * 2; /* 2 seconds */
        if (len > p->tape.buf_len) len = p->tape.buf_len;
        for (int i = 0; i < len; i++) {
            int src = (e->capture_write_pos - len + i + e->capture_len) % e->capture_len;
            p->tape.buf_l[i] = e->capture_buf_l[src];
            p->tape.buf_r[i] = e->capture_buf_r[src];
        }
        p->tape.read_pos = 0.0f;
        p->tape.speed = 0.5f;
    }
}

void pfx_punch_deactivate(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PUNCH_IN) return;
    punch_in_t *p = &e->punch[slot];
    if (!p->active) return;
    /* Start fade-out instead of immediate cutoff */
    p->fading_out = 1;
    p->fade_pos = 0;
    p->fade_len = 256; /* ~5.8ms at 44100 */
    p->pressure = 0.0f;
}

void pfx_punch_set_pressure(perf_fx_engine_t *e, int slot, float pressure) {
    if (slot < 0 || slot >= PFX_NUM_PUNCH_IN) return;
    punch_in_t *p = &e->punch[slot];
    p->pressure = clampf(pressure, 0.0f, 1.0f);
    p->intensity = pfx_apply_pressure_curve(p->pressure, p->velocity, e->pressure_curve);

    /* Tape stop: pressure controls deceleration speed */
    if (slot == PUNCH_TAPE_STOP) {
        p->tape.decel_rate = 0.00001f + p->intensity * 0.0005f;
    }

    /* Beat repeat: pressure modulates repeat rate */
    if (slot >= PUNCH_BEAT_REPEAT_4 && slot <= PUNCH_BEAT_REPEAT_TRIPLET) {
        float base_div;
        switch (slot) {
            case PUNCH_BEAT_REPEAT_4:  base_div = 1.0f; break;
            case PUNCH_BEAT_REPEAT_8:  base_div = 0.5f; break;
            case PUNCH_BEAT_REPEAT_16: base_div = 0.25f; break;
            case PUNCH_BEAT_REPEAT_TRIPLET: base_div = 2.0f/3.0f; break;
            default: base_div = 0.5f;
        }
        /* Pressure halves the repeat length up to 2 divisions faster */
        float div = base_div * (1.0f - p->intensity * 0.75f);
        if (div < 0.03125f) div = 0.03125f;
        p->repeat.repeat_len = pfx_bpm_to_samples(e->bpm, div);
        if (p->repeat.repeat_len < 64) p->repeat.repeat_len = 64;
    }

    /* Ducker: pressure controls depth */
    if (slot == PUNCH_DUCKER) {
        /* depth is just the intensity */
    }
}

/* ============================================================
 * Continuous FX control
 * ============================================================ */

static void update_active_cont_list(perf_fx_engine_t *e) {
    e->active_cont_count = 0;
    for (int i = 0; i < PFX_NUM_CONTINUOUS && e->active_cont_count < PFX_MAX_CONTINUOUS; i++) {
        if (e->cont[i].active) {
            e->active_cont_slots[e->active_cont_count++] = i;
        }
    }
}

void pfx_cont_toggle(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_CONTINUOUS) return;
    if (e->cont[slot].active) {
        pfx_cont_deactivate(e, slot);
    } else {
        pfx_cont_activate(e, slot);
    }
}

void pfx_cont_activate(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_CONTINUOUS) return;

    /* If already at max, deactivate oldest */
    if (!e->cont[slot].active && e->active_cont_count >= PFX_MAX_CONTINUOUS) {
        pfx_cont_deactivate(e, e->active_cont_slots[0]);
    }

    e->cont[slot].active = 1;

    /* Reset effect state */
    continuous_t *c = &e->cont[slot];
    delay_reset(&c->delay);
    reverb_init(&c->reverb);
    memset(&c->phaser, 0, sizeof(c->phaser));
    c->mod_delay.write_pos = 0;
    c->mod_delay.lfo_phase = 0.0f;
    if (c->mod_delay.buf_l) memset(c->mod_delay.buf_l, 0, c->mod_delay.buf_len * sizeof(float));
    if (c->mod_delay.buf_r) memset(c->mod_delay.buf_r, 0, c->mod_delay.buf_len * sizeof(float));
    c->comp.env = 0.0f;
    c->freeze.captured = 0;
    c->freeze.read_pos = 0.0f;
    c->lofi.wow_phase = 0.0f;
    c->lofi.hold_count = 0;
    c->ring.phase = 0.0f;
    svf_reset(&c->filter_l);
    svf_reset(&c->filter_r);

    update_active_cont_list(e);
}

void pfx_cont_deactivate(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_CONTINUOUS) return;
    e->cont[slot].active = 0;
    update_active_cont_list(e);
}

void pfx_cont_set_param(perf_fx_engine_t *e, int slot, int param_idx, float value) {
    if (slot < 0 || slot >= PFX_NUM_CONTINUOUS) return;
    if (param_idx < 0 || param_idx >= PFX_CONT_PARAMS) return;
    e->cont[slot].params[param_idx] = clampf(value, 0.0f, 1.0f);
}

/* ============================================================
 * Scene management
 * ============================================================ */

static void capture_global_params(perf_fx_engine_t *e, float *out) {
    out[0] = e->dry_wet;
    out[1] = e->input_gain;
    out[2] = e->global_lp_cutoff;
    out[3] = e->global_hp_cutoff;
    out[4] = e->eq_low_gain;
    out[5] = e->eq_mid_gain;
    out[6] = e->eq_high_gain;
    out[7] = e->output_gain;
}

static void restore_global_params(perf_fx_engine_t *e, const float *in) {
    e->dry_wet = in[0];
    e->input_gain = in[1];
    e->global_lp_cutoff = in[2];
    e->global_hp_cutoff = in[3];
    e->eq_low_gain = in[4];
    e->eq_mid_gain = in[5];
    e->eq_high_gain = in[6];
    e->output_gain = in[7];
}

void pfx_scene_save(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    scene_t *s = &e->scenes[slot];
    s->populated = 1;
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        s->cont_active[i] = e->cont[i].active;
        memcpy(s->cont_params[i], e->cont[i].params, sizeof(float) * PFX_CONT_PARAMS);
    }
    capture_global_params(e, s->global_params);
}

void pfx_scene_recall(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    scene_t *s = &e->scenes[slot];
    if (!s->populated) return;

    /* Restore active state first, then params (activate resets params) */
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        if (s->cont_active[i] && !e->cont[i].active) {
            /* Newly activated: reset DSP state */
            pfx_cont_activate(e, i);
        } else if (!s->cont_active[i] && e->cont[i].active) {
            /* Deactivated */
            e->cont[i].active = 0;
        }
        /* Copy params after activate (activate resets params to defaults) */
        memcpy(e->cont[i].params, s->cont_params[i], sizeof(float) * PFX_CONT_PARAMS);
    }
    update_active_cont_list(e);
    restore_global_params(e, s->global_params);
}

void pfx_scene_clear(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    memset(&e->scenes[slot], 0, sizeof(scene_t));
}

void pfx_step_save(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    step_preset_t *p = &e->step_presets[slot];
    p->populated = 1;
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        p->cont_active[i] = e->cont[i].active;
        memcpy(p->cont_params[i], e->cont[i].params, sizeof(float) * PFX_CONT_PARAMS);
    }
    capture_global_params(e, p->global_params);
}

void pfx_step_recall(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    step_preset_t *p = &e->step_presets[slot];
    if (!p->populated) return;

    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        if (p->cont_active[i] && !e->cont[i].active) {
            pfx_cont_activate(e, i);
        } else if (!p->cont_active[i] && e->cont[i].active) {
            e->cont[i].active = 0;
        }
        /* Copy params after activate (activate resets params to defaults) */
        memcpy(e->cont[i].params, p->cont_params[i], sizeof(float) * PFX_CONT_PARAMS);
    }
    update_active_cont_list(e);
    restore_global_params(e, p->global_params);
    e->current_step_preset = slot;
}

void pfx_step_clear(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    memset(&e->step_presets[slot], 0, sizeof(step_preset_t));
    if (e->current_step_preset == slot) e->current_step_preset = -1;
}

/* ============================================================
 * Punch-in FX processing (per-sample)
 * ============================================================ */

static void process_punch_filter(punch_in_t *p, int type,
                                  float *l, float *r) {
    float intensity = p->intensity;
    float f, q;
    float out_l, out_r;

    switch (type) {
        case PUNCH_LP_FILTER:
            /* Pressure closes the filter: open -> closed */
            f = cutoff_to_f(1.0f - intensity * 0.95f);
            q = 0.3f + intensity * 0.5f;
            svf_process(&p->filter_l, *l, f, q, &out_l, NULL, NULL);
            svf_process(&p->filter_r, *r, f, q, &out_r, NULL, NULL);
            *l = out_l; *r = out_r;
            break;
        case PUNCH_HP_FILTER:
            f = cutoff_to_f(intensity * 0.8f);
            q = 0.3f + intensity * 0.4f;
            svf_process(&p->filter_l, *l, f, q, NULL, &out_l, NULL);
            svf_process(&p->filter_r, *r, f, q, NULL, &out_r, NULL);
            *l = out_l; *r = out_r;
            break;
        case PUNCH_BP_FILTER:
            f = cutoff_to_f(0.3f + intensity * 0.4f);
            q = 0.1f + intensity * 0.8f; /* narrower with more pressure */
            svf_process(&p->filter_l, *l, f, q, NULL, NULL, &out_l);
            svf_process(&p->filter_r, *r, f, q, NULL, NULL, &out_r);
            *l = out_l; *r = out_r;
            break;
        case PUNCH_RESONANT_PEAK:
            f = cutoff_to_f(0.4f);
            q = 0.05f + intensity * 0.02f; /* very high Q */
            svf_process(&p->filter_l, *l, f, q, NULL, NULL, &out_l);
            svf_process(&p->filter_r, *r, f, q, NULL, NULL, &out_r);
            *l = *l * (1.0f - intensity * 0.7f) + out_l * intensity;
            *r = *r * (1.0f - intensity * 0.7f) + out_r * intensity;
            break;
        default:
            break;
    }
}

static void process_punch_beat_repeat(punch_in_t *p, float *l, float *r) {
    repeat_t *rp = &p->repeat;

    if (rp->capturing) {
        /* Fill buffer with incoming audio */
        rp->buf_l[rp->write_pos] = *l;
        rp->buf_r[rp->write_pos] = *r;
        rp->write_pos = (rp->write_pos + 1) % rp->buf_len;
        rp->frames_captured++;
        if (rp->frames_captured >= rp->repeat_len) {
            rp->capturing = 0;
            rp->read_pos = (rp->write_pos - rp->repeat_len + rp->buf_len) % rp->buf_len;
            rp->repeat_pos = 0;
        }
    }

    if (!rp->capturing) {
        /* Play from repeat buffer */
        *l = rp->buf_l[rp->read_pos];
        *r = rp->buf_r[rp->read_pos];
        rp->read_pos = (rp->read_pos + 1) % rp->buf_len;
        rp->repeat_pos++;
        if (rp->repeat_pos >= rp->repeat_len) {
            rp->repeat_pos = 0;
            rp->read_pos = (rp->write_pos - rp->repeat_len + rp->buf_len) % rp->buf_len;
        }
    }
}

static void process_punch_stutter(punch_in_t *p, float *l, float *r) {
    /* Stutter = very short repeat with variable length based on pressure */
    repeat_t *rp = &p->repeat;
    int stutter_len = 64 + (int)((1.0f - p->intensity) * (rp->repeat_len - 64));

    if (rp->capturing) {
        rp->buf_l[rp->write_pos] = *l;
        rp->buf_r[rp->write_pos] = *r;
        rp->write_pos = (rp->write_pos + 1) % rp->buf_len;
        rp->frames_captured++;
        if (rp->frames_captured >= stutter_len) {
            rp->capturing = 0;
            rp->read_pos = (rp->write_pos - stutter_len + rp->buf_len) % rp->buf_len;
            rp->repeat_pos = 0;
        }
    }

    if (!rp->capturing) {
        *l = rp->buf_l[rp->read_pos];
        *r = rp->buf_r[rp->read_pos];
        rp->read_pos = (rp->read_pos + 1) % rp->buf_len;
        rp->repeat_pos++;
        if (rp->repeat_pos >= stutter_len) {
            rp->repeat_pos = 0;
            rp->read_pos = (rp->write_pos - stutter_len + rp->buf_len) % rp->buf_len;
        }
    }
}

static void process_punch_scatter(punch_in_t *p, float *l, float *r,
                                   perf_fx_engine_t *e) {
    /* Scatter: random-length slices from capture buffer, sometimes reversed */
    repeat_t *rp = &p->repeat;
    float intensity = p->intensity;

    if (rp->repeat_pos <= 0) {
        /* Pick a new random slice */
        int min_len = 128;
        int max_len = pfx_bpm_to_samples(e->bpm, 0.5f);
        if (max_len > e->capture_len) max_len = e->capture_len;
        int slice_len = min_len + (int)(white_noise(&p->scatter_seed) * 0.5f + 0.5f) * (max_len - min_len);
        slice_len = (int)(slice_len * (1.0f - intensity * 0.7f));
        if (slice_len < min_len) slice_len = min_len;

        rp->repeat_len = slice_len;
        rp->repeat_pos = slice_len;
        int offset = (int)((white_noise(&p->scatter_seed) * 0.5f + 0.5f) * e->capture_len);
        rp->read_pos = (e->capture_write_pos - offset + e->capture_len) % e->capture_len;
    }

    /* Read from capture buffer */
    *l = e->capture_buf_l[rp->read_pos];
    *r = e->capture_buf_r[rp->read_pos];

    /* Sometimes reverse direction */
    if ((rp->repeat_len & 3) == 0) {
        rp->read_pos = (rp->read_pos - 1 + e->capture_len) % e->capture_len;
    } else {
        rp->read_pos = (rp->read_pos + 1) % e->capture_len;
    }
    rp->repeat_pos--;
}

static void process_punch_reverse(punch_in_t *p, float *l, float *r) {
    repeat_t *rp = &p->repeat;
    float speed = 0.5f + p->intensity * 1.5f; /* pressure = speed */

    if (rp->read_pos >= 0 && rp->read_pos < rp->repeat_len) {
        *l = rp->buf_l[rp->read_pos];
        *r = rp->buf_r[rp->read_pos];
    }

    rp->read_pos -= (int)speed;
    if (rp->read_pos < 0)
        rp->read_pos = rp->repeat_len - 1;
}

static void process_punch_half_speed(punch_in_t *p, float *l, float *r) {
    tape_stop_t *t = &p->tape;
    float speed = 0.3f + (1.0f - p->intensity) * 0.7f; /* pressure = more slowdown */

    int pos0 = (int)t->read_pos;
    int pos1 = (pos0 + 1) % t->buf_len;
    float frac = t->read_pos - pos0;

    if (pos0 >= 0 && pos0 < t->buf_len) {
        *l = t->buf_l[pos0] + frac * (t->buf_l[pos1] - t->buf_l[pos0]);
        *r = t->buf_r[pos0] + frac * (t->buf_r[pos1] - t->buf_r[pos0]);
    }

    t->read_pos += speed;
    if (t->read_pos >= (float)t->buf_len)
        t->read_pos = 0.0f;
}

static void process_punch_bitcrush(punch_in_t *p, float *l, float *r) {
    float intensity = p->intensity;
    /* Bit depth: 16 bits down to 1 bit */
    float bits = 16.0f - intensity * 15.0f;
    if (bits < 1.0f) bits = 1.0f;
    float levels = powf(2.0f, bits);
    *l = roundf(*l * levels) / levels;
    *r = roundf(*r * levels) / levels;
}

static void process_punch_sample_rate_reduce(punch_in_t *p, float *l, float *r) {
    float intensity = p->intensity;
    /* Hold period: 1 (no reduction) to 64 (extreme aliasing) */
    int period = 1 + (int)(intensity * 63.0f);

    p->crush_count++;
    if (p->crush_count >= (unsigned int)period) {
        p->crush_count = 0;
        p->crush_hold_l = *l;
        p->crush_hold_r = *r;
    }
    *l = p->crush_hold_l;
    *r = p->crush_hold_r;
}

static void process_punch_tape_stop(punch_in_t *p, float *l, float *r,
                                     perf_fx_engine_t *e __attribute__((unused))) {
    tape_stop_t *t = &p->tape;

    /* Continuously feed the tape buffer */
    t->buf_l[t->write_pos] = *l;
    t->buf_r[t->write_pos] = *r;
    t->write_pos = (t->write_pos + 1) % t->buf_len;

    /* Slow down */
    t->speed -= t->decel_rate;
    if (t->speed < 0.0f) t->speed = 0.0f;

    if (t->speed > 0.01f) {
        /* Read at reduced speed */
        int pos0 = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos0];
        *r = t->buf_r[pos0];
        t->read_pos += t->speed;
        if (t->read_pos >= (float)t->buf_len)
            t->read_pos -= (float)t->buf_len;
    } else {
        /* Stopped - output the last sample */
        int pos = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos] * 0.98f; /* fade */
        *r = t->buf_r[pos] * 0.98f;
    }

    /* Add noise at low speeds for vinyl brake feel */
    if (t->speed < 0.3f && p->intensity > 0.5f) {
        unsigned int seed = (unsigned int)(t->read_pos * 1000.0f);
        float noise = white_noise(&seed) * (0.3f - t->speed) * p->intensity * 0.1f;
        *l += noise;
        *r += noise;
    }
}

static void process_punch_ducker(punch_in_t *p, float *l, float *r,
                                  perf_fx_engine_t *e) {
    ducker_t *dk = &p->ducker;
    float depth = p->intensity;

    /* Advance phase based on BPM and rate division */
    float div;
    switch (dk->rate_div) {
        case 0: div = 1.0f; break;   /* 1/4 */
        case 1: div = 0.5f; break;   /* 1/8 */
        case 2: div = 0.25f; break;  /* 1/16 */
        default: div = 0.5f;
    }
    float samples_per_beat = (60.0f / e->bpm) * PFX_SAMPLE_RATE * div;
    float phase_inc = 1.0f / samples_per_beat;
    dk->phase += phase_inc;
    if (dk->phase >= 1.0f) dk->phase -= 1.0f;

    /* Half-cosine pump curve */
    float gain = 1.0f - depth * (0.5f + 0.5f * cosf(dk->phase * 2.0f * M_PI));
    *l *= gain;
    *r *= gain;
}

/* Process all active punch-in FX for one sample */
static void process_punch_ins(perf_fx_engine_t *e, float *l, float *r) {
    for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
        punch_in_t *p = &e->punch[i];
        if (!p->active && !p->fading_out) continue;

        /* Save dry signal before effect processing */
        float dry_l = *l;
        float dry_r = *r;

        switch (i) {
            case PUNCH_BEAT_REPEAT_4:
            case PUNCH_BEAT_REPEAT_8:
            case PUNCH_BEAT_REPEAT_16:
            case PUNCH_BEAT_REPEAT_TRIPLET:
                process_punch_beat_repeat(p, l, r);
                break;
            case PUNCH_STUTTER:
                process_punch_stutter(p, l, r);
                break;
            case PUNCH_SCATTER:
                process_punch_scatter(p, l, r, e);
                break;
            case PUNCH_REVERSE:
                process_punch_reverse(p, l, r);
                break;
            case PUNCH_HALF_SPEED:
                process_punch_half_speed(p, l, r);
                break;
            case PUNCH_LP_FILTER:
            case PUNCH_HP_FILTER:
            case PUNCH_BP_FILTER:
            case PUNCH_RESONANT_PEAK:
                process_punch_filter(p, i, l, r);
                break;
            case PUNCH_BITCRUSH:
                process_punch_bitcrush(p, l, r);
                break;
            case PUNCH_SAMPLE_RATE_REDUCE:
                process_punch_sample_rate_reduce(p, l, r);
                break;
            case PUNCH_TAPE_STOP:
                process_punch_tape_stop(p, l, r, e);
                break;
            case PUNCH_DUCKER:
                process_punch_ducker(p, l, r, e);
                break;
        }

        /* Apply fade-out crossfade */
        if (p->fading_out) {
            float fade = 1.0f - (float)p->fade_pos / (float)p->fade_len;
            *l = dry_l * (1.0f - fade) + *l * fade;
            *r = dry_r * (1.0f - fade) + *r * fade;
            p->fade_pos++;
            if (p->fade_pos >= p->fade_len) {
                p->active = 0;
                p->fading_out = 0;
            }
        }
    }
}

/* ============================================================
 * Continuous FX processing (per-sample)
 * ============================================================ */

/* Params: [0]=time, [1]=feedback, [2]=filter, [3]=mix */
static void process_cont_delay(continuous_t *c, float *l, float *r) {
    delay_t *d = &c->delay;
    float time = c->params[0];
    float fb = c->params[1] * 0.95f;
    float filt = c->params[2];
    float mix = c->params[3];

    int delay_samples = 256 + (int)(time * (d->length - 256));
    float dl, dr;
    delay_read(d, delay_samples, &dl, &dr);

    /* LP in feedback path */
    float f_coeff = 0.1f + filt * 0.8f;
    d->fb_lp_l += f_coeff * (dl - d->fb_lp_l);
    d->fb_lp_r += f_coeff * (dr - d->fb_lp_r);

    delay_write(d, *l + d->fb_lp_l * fb, *r + d->fb_lp_r * fb);

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

/* Ping-pong: [0]=time, [1]=feedback, [2]=spread, [3]=mix */
static void process_cont_ping_pong(continuous_t *c, float *l, float *r) {
    delay_t *d = &c->delay;
    float time = c->params[0];
    float fb = c->params[1] * 0.9f;
    float spread = c->params[2];
    float mix = c->params[3];

    int delay_samples = 256 + (int)(time * (d->length - 256));
    float dl, dr;
    delay_read(d, delay_samples, &dl, &dr);

    /* Cross-feed for ping-pong effect */
    float cross = 0.3f + spread * 0.6f;
    delay_write(d,
        *l + dr * fb * cross + dl * fb * (1.0f - cross),
        *r + dl * fb * cross + dr * fb * (1.0f - cross));

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

/* Tape echo: [0]=age, [1]=wow/flutter, [2]=feedback, [3]=mix */
static void process_cont_tape_echo(continuous_t *c, float *l, float *r) {
    delay_t *d = &c->delay;
    float age = c->params[0];
    float wow = c->params[1];
    float fb = c->params[2] * 0.85f;
    float mix = c->params[3];

    /* Modulate delay time with wow/flutter */
    mod_delay_t *md = &c->mod_delay;
    md->lfo_phase += 0.5f / PFX_SAMPLE_RATE; /* slow LFO */
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;
    float mod = sinf(md->lfo_phase * 2.0f * M_PI) * wow * 200.0f;

    float delay_samples = 2000.0f + age * (float)(d->length - 4000) + mod;
    if (delay_samples < 100.0f) delay_samples = 100.0f;
    if (delay_samples >= (float)(d->length - 1)) delay_samples = (float)(d->length - 2);

    float dl, dr;
    delay_read_interp(d, delay_samples, &dl, &dr);

    /* Age = LP filter in feedback (darker with age) */
    float f_coeff = 1.0f - age * 0.7f;
    d->fb_lp_l += f_coeff * (dl - d->fb_lp_l);
    d->fb_lp_r += f_coeff * (dr - d->fb_lp_r);

    /* Add subtle saturation for tape character */
    float sat_l = fast_tanh(d->fb_lp_l * (1.0f + age));
    float sat_r = fast_tanh(d->fb_lp_r * (1.0f + age));

    delay_write(d, *l + sat_l * fb, *r + sat_r * fb);

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

/* Auto-filter: [0]=rate, [1]=depth, [2]=resonance, [3]=mix
 * [4]=shape (0=LP, 0.5=BP, 1=HP), [5]=center freq,
 * [6]=LFO shape (0=sine, 0.5=triangle, 1=square), [7]=stereo offset */
static void process_cont_auto_filter(continuous_t *c, float *l, float *r) {
    float rate = 0.05f + c->params[0] * 10.0f; /* Hz */
    float depth = c->params[1];
    float reso = 0.1f + c->params[2] * 0.08f; /* SVF Q: lower = more resonant */
    float mix = c->params[3];
    float shape = c->params[4]; /* filter type blend */
    float center = 0.1f + c->params[5] * 0.7f; /* center frequency */
    float lfo_shape = c->params[6];
    float stereo_offset = c->params[7] * 0.5f; /* L/R phase offset */

    /* LFO — reuse mod_delay's lfo_phase */
    mod_delay_t *md = &c->mod_delay;
    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    /* LFO shape: sine, triangle, square */
    float lfo_l, lfo_r;
    float phase_l = md->lfo_phase;
    float phase_r = md->lfo_phase + stereo_offset;
    if (phase_r >= 1.0f) phase_r -= 1.0f;

    if (lfo_shape < 0.33f) {
        lfo_l = sinf(phase_l * 2.0f * M_PI);
        lfo_r = sinf(phase_r * 2.0f * M_PI);
    } else if (lfo_shape < 0.66f) {
        lfo_l = 4.0f * fabsf(phase_l - 0.5f) - 1.0f;
        lfo_r = 4.0f * fabsf(phase_r - 0.5f) - 1.0f;
    } else {
        lfo_l = phase_l < 0.5f ? 1.0f : -1.0f;
        lfo_r = phase_r < 0.5f ? 1.0f : -1.0f;
    }

    /* Compute cutoff from center + LFO */
    float cut_l = center + lfo_l * depth * 0.4f;
    float cut_r = center + lfo_r * depth * 0.4f;
    cut_l = clampf(cut_l, 0.01f, 0.95f);
    cut_r = clampf(cut_r, 0.01f, 0.95f);

    float f_l = cutoff_to_f(cut_l);
    float f_r = cutoff_to_f(cut_r);

    float dry_l = *l, dry_r = *r;
    float lp_l, hp_l, bp_l, lp_r, hp_r, bp_r;
    svf_process(&c->filter_l, *l, f_l, reso, &lp_l, &hp_l, &bp_l);
    svf_process(&c->filter_r, *r, f_r, reso, &lp_r, &hp_r, &bp_r);

    /* Blend filter types based on shape param */
    if (shape < 0.33f) {
        *l = lp_l; *r = lp_r;
    } else if (shape < 0.66f) {
        *l = bp_l; *r = bp_r;
    } else {
        *l = hp_l; *r = hp_r;
    }

    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}

/* Reverb processing: [0]=decay, [1]=damping/darkness, [2]=pre-delay/mod, [3]=mix */
static void process_cont_reverb(continuous_t *c, float *l, float *r,
                                 int reverb_type) {
    reverb_t *rv = &c->reverb;
    float decay = 0.4f + c->params[0] * 0.58f;
    float damping = c->params[1];
    float mod = c->params[2];
    float mix = c->params[3];

    /* Adjust reverb character by type */
    switch (reverb_type) {
        case CONT_PLATE_REVERB:
            damping *= 0.5f;
            break;
        case CONT_DARK_REVERB:
            damping = 0.4f + damping * 0.5f;
            decay *= 1.1f;
            break;
        case CONT_SPRING_REVERB:
            damping *= 0.3f;
            break;
        case CONT_SHIMMER_REVERB:
            damping *= 0.3f;
            decay *= 1.15f;
            break;
    }
    if (decay > 0.98f) decay = 0.98f;

    rv->decay = decay;
    rv->damping = damping;

    /* Shimmer: pitch-shift reverb tail and feed back into input */
    float in_l = *l, in_r = *r;
    if (reverb_type == CONT_SHIMMER_REVERB && mod > 0.0f) {
        /* Read pitch-shifted signal from shimmer buffer */
        float pitch_rate = 1.0f + mod; /* 1.0 = octave up at mod=1 */
        int pos0 = ((int)rv->shimmer_read_pos) & 4095;
        int pos1 = (pos0 + 1) & 4095;
        float frac = rv->shimmer_read_pos - (int)rv->shimmer_read_pos;
        float pitched = rv->shimmer_buf[pos0] + frac * (rv->shimmer_buf[pos1] - rv->shimmer_buf[pos0]);
        rv->shimmer_read_pos += pitch_rate;
        if (rv->shimmer_read_pos >= 4096.0f) rv->shimmer_read_pos -= 4096.0f;

        /* Mix pitch-shifted feedback into reverb input */
        float shimmer_mix = mod * 0.4f;
        in_l += pitched * shimmer_mix;
        in_r += pitched * shimmer_mix;
    }

    float wet_l, wet_r;
    reverb_process_stereo(rv, in_l, in_r, &wet_l, &wet_r);

    /* Write reverb output to shimmer buffer for next iteration */
    if (reverb_type == CONT_SHIMMER_REVERB) {
        rv->shimmer_buf[rv->shimmer_write_pos] = (wet_l + wet_r) * 0.5f * decay * 0.3f;
        rv->shimmer_write_pos = (rv->shimmer_write_pos + 1) & 4095;
    }

    *l = *l * (1.0f - mix) + wet_l * mix;
    *r = *r * (1.0f - mix) + wet_r * mix;
}

/* Chorus: [0]=rate, [1]=depth, [2]=feedback, [3]=mix */
static void process_cont_chorus(continuous_t *c, float *l, float *r) {
    mod_delay_t *md = &c->mod_delay;
    float rate = 0.1f + c->params[0] * 5.0f;
    float depth = c->params[1] * 0.005f * PFX_SAMPLE_RATE;
    float fb = c->params[2] * 0.7f;
    float mix = c->params[3];

    /* Write to mod delay buffer with feedback from delayed output */
    int wp = md->write_pos;
    float delay_fb_l = 300.0f + depth * sinf(md->lfo_phase * 2.0f * M_PI);
    float delay_fb_r = 300.0f + depth * sinf((md->lfo_phase + 0.25f) * 2.0f * M_PI);
    if (delay_fb_l < 1.0f) delay_fb_l = 1.0f;
    if (delay_fb_r < 1.0f) delay_fb_r = 1.0f;
    if (delay_fb_l >= (float)(md->buf_len - 1)) delay_fb_l = (float)(md->buf_len - 2);
    if (delay_fb_r >= (float)(md->buf_len - 1)) delay_fb_r = (float)(md->buf_len - 2);
    int fb_pos_l = (wp - (int)delay_fb_l + md->buf_len) % md->buf_len;
    int fb_pos_r = (wp - (int)delay_fb_r + md->buf_len) % md->buf_len;
    md->buf_l[wp] = *l + md->buf_l[fb_pos_l] * fb;
    md->buf_r[wp] = *r + md->buf_r[fb_pos_r] * fb;
    md->write_pos = (wp + 1) % md->buf_len;

    /* LFO */
    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;
    float lfo = sinf(md->lfo_phase * 2.0f * M_PI);

    /* Read with modulated delay */
    float delay_l = 300.0f + depth * lfo;
    float delay_r = 300.0f + depth * sinf((md->lfo_phase + 0.25f) * 2.0f * M_PI);
    if (delay_l < 1.0f) delay_l = 1.0f;
    if (delay_r < 1.0f) delay_r = 1.0f;
    if (delay_l >= (float)(md->buf_len - 1)) delay_l = (float)(md->buf_len - 2);
    if (delay_r >= (float)(md->buf_len - 1)) delay_r = (float)(md->buf_len - 2);

    int pos_l = (md->write_pos - (int)delay_l + md->buf_len) % md->buf_len;
    int pos_r = (md->write_pos - (int)delay_r + md->buf_len) % md->buf_len;
    float wet_l = md->buf_l[pos_l];
    float wet_r = md->buf_r[pos_r];

    *l = *l * (1.0f - mix) + wet_l * mix;
    *r = *r * (1.0f - mix) + wet_r * mix;
}

/* Phaser: [0]=rate, [1]=depth, [2]=feedback, [3]=mix */
static void process_cont_phaser(continuous_t *c, float *l, float *r) {
    phaser_t *ph = &c->phaser;
    float rate = 0.05f + c->params[0] * 3.0f;
    float depth = c->params[1];
    float fb = c->params[2] * 0.9f;
    float mix = c->params[3];

    /* LFO */
    ph->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (ph->lfo_phase >= 1.0f) ph->lfo_phase -= 1.0f;
    float lfo = sinf(ph->lfo_phase * 2.0f * M_PI);

    /* Allpass chain with feedback */
    float mono = (*l + *r) * 0.5f + ph->ap[0].y1 * fb * 0.3f;
    float base_freq = 200.0f + depth * 3000.0f * (0.5f + 0.5f * lfo);
    float out = mono;

    for (int i = 0; i < PFX_NUM_ALLPASS; i++) {
        float freq = base_freq * (1.0f + (float)i * 0.3f);
        float w = 2.0f * M_PI * freq / PFX_SAMPLE_RATE;
        float cosw = cosf(w);
        float a = (cosw == 0.0f) ? 0.0f : (1.0f - sinf(w)) / cosw;
        if (a > 0.99f) a = 0.99f;
        if (a < -0.99f) a = -0.99f;

        /* First-order allpass: y[n] = -a*x[n] + x[n-1] + a*y[n-1]
         * We store y[n-1] in ph->ap[i].y1, reuse 'out' as x[n] */
        float x = out;
        float y = -a * x + ph->ap[i].y1;
        ph->ap[i].y1 = flush_denormal(a * y + x);
        out = y;
    }

    float wet = out;
    *l = *l * (1.0f - mix) + wet * mix;
    *r = *r * (1.0f - mix) + wet * mix;
}

/* Flanger: [0]=rate, [1]=depth, [2]=feedback, [3]=mix - similar to chorus but shorter delay */
static void process_cont_flanger(continuous_t *c, float *l, float *r) {
    mod_delay_t *md = &c->mod_delay;
    float rate = 0.05f + c->params[0] * 2.0f;
    float depth = 1.0f + c->params[1] * 30.0f;
    float fb = c->params[2] * 0.9f;
    float mix = c->params[3];

    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    float lfo = sinf(md->lfo_phase * 2.0f * M_PI);
    float delay_samples = 5.0f + depth * (0.5f + 0.5f * lfo);

    /* Write with feedback */
    int wp = md->write_pos;
    int rp = (wp - (int)delay_samples + md->buf_len) % md->buf_len;
    float dl = md->buf_l[rp];
    float dr = md->buf_r[rp];

    md->buf_l[wp] = *l + dl * fb;
    md->buf_r[wp] = *r + dr * fb;
    md->write_pos = (wp + 1) % md->buf_len;

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

/* Lo-Fi: [0]=noise, [1]=wow, [2]=crackle, [3]=age (SR reduction) */
static void process_cont_lofi(continuous_t *c, float *l, float *r) {
    lofi_t *lf = &c->lofi;
    float noise_amt = c->params[0];
    float wow_amt = c->params[1];
    float crackle_amt = c->params[2];
    float age = c->params[3];

    /* Sample rate reduction (age) */
    int hold_period = 1 + (int)(age * 15.0f);
    lf->hold_count++;
    if (lf->hold_count >= hold_period) {
        lf->hold_count = 0;
        lf->hold_l = *l;
        lf->hold_r = *r;
    }
    *l = lf->hold_l;
    *r = lf->hold_r;

    /* Wow (pitch modulation via slow phase shift) */
    lf->wow_phase += (0.3f + wow_amt * 2.0f) / PFX_SAMPLE_RATE;
    if (lf->wow_phase >= 1.0f) lf->wow_phase -= 1.0f;
    float wow = sinf(lf->wow_phase * 2.0f * M_PI) * wow_amt * 0.003f;
    /* Apply as subtle gain modulation (approximation of pitch wow) */
    float wow_gain = 1.0f + wow;
    *l *= wow_gain;
    *r *= wow_gain;

    /* Noise floor */
    float noise = white_noise(&lf->noise_seed) * noise_amt * 0.05f;
    *l += noise;
    *r += noise;

    /* Crackle */
    float rnd = white_noise(&lf->noise_seed);
    if (rnd > (1.0f - crackle_amt * 0.02f)) {
        float pop = white_noise(&lf->noise_seed) * crackle_amt * 0.3f;
        *l += pop;
        *r += pop;
    }

    /* Soft clip for warmth */
    *l = fast_tanh(*l);
    *r = fast_tanh(*r);
}

/* Compressor: [0]=threshold, [1]=ratio, [2]=attack, [3]=mix */
static void process_cont_compressor(continuous_t *c, float *l, float *r) {
    compressor_t *comp = &c->comp;
    float threshold = 0.01f + c->params[0] * 0.99f;
    float ratio = 1.0f + c->params[1] * 19.0f; /* 1:1 to 20:1 */
    float attack_ms = 0.1f + c->params[2] * 50.0f;
    float mix = c->params[3];

    float attack_coeff = expf(-1.0f / (attack_ms * 0.001f * PFX_SAMPLE_RATE));
    float release_coeff = expf(-1.0f / (0.1f * PFX_SAMPLE_RATE)); /* 100ms release */

    /* Envelope follower */
    float input_level = fabsf(*l) > fabsf(*r) ? fabsf(*l) : fabsf(*r);
    if (input_level > comp->env)
        comp->env = attack_coeff * comp->env + (1.0f - attack_coeff) * input_level;
    else
        comp->env = release_coeff * comp->env + (1.0f - release_coeff) * input_level;

    /* Gain reduction */
    float gain = 1.0f;
    if (comp->env > threshold) {
        float over_db = 20.0f * log10f(comp->env / threshold);
        float reduction_db = over_db * (1.0f - 1.0f / ratio);
        gain = powf(10.0f, -reduction_db / 20.0f);
    }

    /* Makeup gain */
    float makeup = 1.0f + c->params[1] * 0.5f;

    float dry_l = *l, dry_r = *r;
    *l = *l * gain * makeup;
    *r = *r * gain * makeup;

    /* Wet/dry mix */
    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}

/* Saturator: [0]=drive, [1]=tone, [2]=curve, [3]=mix */
static void process_cont_saturator(continuous_t *c, float *l, float *r) {
    float drive = 1.0f + c->params[0] * 20.0f;
    float tone = c->params[1];
    float curve = c->params[2]; /* 0=soft tanh, 0.5=hard clip, 1=foldback */
    float mix = c->params[3];

    float dry_l = *l, dry_r = *r;

    /* Apply drive */
    float dl = *l * drive;
    float dr = *r * drive;

    /* Shape based on curve parameter */
    if (curve < 0.33f) {
        /* Soft saturation (tanh) */
        *l = fast_tanh(dl);
        *r = fast_tanh(dr);
    } else if (curve < 0.66f) {
        /* Hard clip */
        *l = soft_clip(dl);
        *r = soft_clip(dr);
    } else {
        /* Foldback distortion */
        *l = sinf(dl * M_PI * 0.5f);
        *r = sinf(dr * M_PI * 0.5f);
    }

    /* Tone (LP filter) - stereo */
    float f = cutoff_to_f(0.3f + tone * 0.7f);
    float fl, fr;
    svf_process(&c->filter_l, *l, f, 0.5f, &fl, NULL, NULL);
    svf_process(&c->filter_r, *r, f, 0.5f, &fr, NULL, NULL);
    *l = fl; *r = fr;

    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}

/* Ring modulator: [0]=freq, [1]=depth, [2]=tone, [3]=mix */
static void process_cont_ring_mod(continuous_t *c, float *l, float *r) {
    ring_mod_t *rm = &c->ring;
    float freq = 20.0f + c->params[0] * 2000.0f;
    float depth = c->params[1];
    float mix = c->params[3];

    rm->phase += freq / PFX_SAMPLE_RATE;
    if (rm->phase >= 1.0f) rm->phase -= 1.0f;

    float mod = sinf(rm->phase * 2.0f * M_PI);
    float ring = 1.0f - depth + depth * mod;

    float dry_l = *l, dry_r = *r;
    *l *= ring;
    *r *= ring;

    /* Tone filter - stereo */
    float f = cutoff_to_f(0.2f + c->params[2] * 0.6f);
    float fl, fr;
    svf_process(&c->filter_l, *l, f, 0.4f, &fl, NULL, NULL);
    svf_process(&c->filter_r, *r, f, 0.4f, &fr, NULL, NULL);
    *l = fl; *r = fr;

    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}

/* Freeze: [0]=freeze_pos, [1]=decay, [2]=pitch_shift, [3]=mix */
static void process_cont_freeze(continuous_t *c, float *l, float *r) {
    freeze_t *fz = &c->freeze;
    float mix = c->params[3];
    float pitch = 0.5f + c->params[2] * 1.5f;

    if (!fz->captured) {
        /* Capture a grain from current audio */
        fz->buf_l[(int)fz->read_pos % PFX_FREEZE_GRAIN] = *l;
        fz->buf_r[(int)fz->read_pos % PFX_FREEZE_GRAIN] = *r;
        fz->read_pos += 1.0f;
        if (fz->read_pos >= PFX_FREEZE_GRAIN) {
            fz->captured = 1;
            fz->grain_len = PFX_FREEZE_GRAIN;
            fz->read_pos = 0.0f;
        }
        return; /* Pass through while capturing */
    }

    /* Read from frozen grain with crossfade */
    int pos0 = ((int)fz->read_pos) % fz->grain_len;
    int pos1 = (pos0 + 1) % fz->grain_len;
    float frac = fz->read_pos - (int)fz->read_pos;

    float wet_l = fz->buf_l[pos0] + frac * (fz->buf_l[pos1] - fz->buf_l[pos0]);
    float wet_r = fz->buf_r[pos0] + frac * (fz->buf_r[pos1] - fz->buf_r[pos0]);

    /* Crossfade envelope at grain boundaries */
    float norm_pos = fz->read_pos / (float)fz->grain_len;
    float env = sinf(norm_pos * M_PI); /* hanning-ish window */
    wet_l *= env;
    wet_r *= env;

    fz->read_pos += pitch;
    if (fz->read_pos >= (float)fz->grain_len) {
        fz->read_pos -= (float)fz->grain_len;

        /* Apply decay once per grain cycle, not per sample */
        float decay = 0.95f + c->params[1] * 0.05f;
        for (int i = 0; i < fz->grain_len; i++) {
            fz->buf_l[i] *= decay;
            fz->buf_r[i] *= decay;
        }
    }

    *l = *l * (1.0f - mix) + wet_l * mix;
    *r = *r * (1.0f - mix) + wet_r * mix;
}

/* Process one continuous FX slot for one sample */
static void process_continuous_fx(continuous_t *c, int type,
                                   float *l, float *r) {
    switch (type) {
        case CONT_DELAY:         process_cont_delay(c, l, r); break;
        case CONT_PING_PONG:     process_cont_ping_pong(c, l, r); break;
        case CONT_TAPE_ECHO:     process_cont_tape_echo(c, l, r); break;
        case CONT_AUTO_FILTER:   process_cont_auto_filter(c, l, r); break;
        case CONT_PLATE_REVERB:
        case CONT_DARK_REVERB:
        case CONT_SPRING_REVERB:
        case CONT_SHIMMER_REVERB:
            process_cont_reverb(c, l, r, type);
            break;
        case CONT_CHORUS:        process_cont_chorus(c, l, r); break;
        case CONT_PHASER:        process_cont_phaser(c, l, r); break;
        case CONT_FLANGER:       process_cont_flanger(c, l, r); break;
        case CONT_LOFI:          process_cont_lofi(c, l, r); break;
        case CONT_COMPRESSOR:    process_cont_compressor(c, l, r); break;
        case CONT_SATURATOR:     process_cont_saturator(c, l, r); break;
        case CONT_RING_MOD:      process_cont_ring_mod(c, l, r); break;
        case CONT_FREEZE:        process_cont_freeze(c, l, r); break;
    }
}

/* ============================================================
 * Main render
 * ============================================================ */

void pfx_engine_render(perf_fx_engine_t *e, int16_t *out_lr, int frames) {
    /* Read input audio */
    int16_t *audio_src = NULL;
    int use_track_mix = 0;

    if (e->audio_source == SOURCE_TRACKS && e->track_audio_valid && e->track_mask) {
        use_track_mix = 1;
    } else if (e->mapped_memory) {
        switch (e->audio_source) {
            case SOURCE_LINE_IN:
                audio_src = (int16_t *)(e->mapped_memory + e->audio_in_offset);
                break;
            case SOURCE_MOVE_MIX:
            default:
                audio_src = (int16_t *)(e->mapped_memory + e->audio_out_offset);
                break;
        }
    }

    /* Convert input to float */
    for (int i = 0; i < frames; i++) {
        if (use_track_mix) {
            /* Mix selected Link Audio tracks */
            float mix_l = 0.0f, mix_r = 0.0f;
            for (int t = 0; t < PFX_TRACK_COUNT; t++) {
                if ((e->track_mask & (1 << t)) && e->track_audio[t]) {
                    mix_l += (float)e->track_audio[t][i * 2] / 32768.0f;
                    mix_r += (float)e->track_audio[t][i * 2 + 1] / 32768.0f;
                }
            }
            e->work_l[i] = mix_l;
            e->work_r[i] = mix_r;
        } else if (audio_src) {
            e->work_l[i] = (float)audio_src[i * 2] / 32768.0f;
            e->work_r[i] = (float)audio_src[i * 2 + 1] / 32768.0f;
        } else {
            e->work_l[i] = 0.0f;
            e->work_r[i] = 0.0f;
        }

        /* Apply input gain */
        e->work_l[i] *= e->input_gain;
        e->work_r[i] *= e->input_gain;

        /* Save dry signal */
        e->dry_l[i] = e->work_l[i];
        e->dry_r[i] = e->work_r[i];
    }

    /* Step FX sequencer */
    if (e->step_seq_active && e->transport_running) {
        float samples_per_div;
        switch (e->step_seq_division) {
            case 0: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE; break; /* 1/4 */
            case 1: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 0.5f; break;
            case 2: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 0.25f; break;
            case 3: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 4.0f; break; /* 1 bar */
            default: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE; break;
        }
        e->step_seq_phase += (float)frames / samples_per_div;
        if (e->step_seq_phase >= 1.0f) {
            e->step_seq_phase -= 1.0f;
            /* Advance to next populated step (skip empty) */
            int start = e->step_seq_pos;
            for (int s = 0; s < PFX_NUM_PRESETS; s++) {
                int next = (start + 1 + s) % PFX_NUM_PRESETS;
                if (e->step_presets[next].populated) {
                    e->step_seq_pos = next;
                    pfx_step_recall(e, next);
                    break;
                }
            }
        }
    }

    /* Process per-sample */
    for (int i = 0; i < frames; i++) {
        float l = e->work_l[i];
        float r = e->work_r[i];

        /* Update capture buffer (for beat repeat/reverse/half-speed/scatter) */
        e->capture_buf_l[e->capture_write_pos] = l;
        e->capture_buf_r[e->capture_write_pos] = r;
        e->capture_write_pos = (e->capture_write_pos + 1) % e->capture_len;

        if (!e->bypassed) {
            /* Punch-in FX (serial chain) */
            process_punch_ins(e, &l, &r);

            e->work_l[i] = l;
            e->work_r[i] = r;
        }
    }

    /* Scene morph tick */
    pfx_scene_morph_tick(e, frames);

    /* Per-sample: continuous FX, global filters, output */
    for (int i = 0; i < frames; i++) {
        float l = e->work_l[i];
        float r = e->work_r[i];

        if (!e->bypassed) {
            /* Continuous FX (serial chain of active slots) */
            for (int c = 0; c < e->active_cont_count; c++) {
                int slot = e->active_cont_slots[c];
                process_continuous_fx(&e->cont[slot], slot, &l, &r);
            }

            /* Global LP filter - stereo */
            if (e->global_lp_cutoff < 0.99f) {
                float f = cutoff_to_f(e->global_lp_cutoff);
                float lp_l, lp_r;
                svf_process(&e->global_lp_l, l, f, 0.4f, &lp_l, NULL, NULL);
                svf_process(&e->global_lp_r, r, f, 0.4f, &lp_r, NULL, NULL);
                l = lp_l;
                r = lp_r;
            }

            /* Global HP filter - stereo */
            if (e->global_hp_cutoff > 0.01f) {
                float f = cutoff_to_f(e->global_hp_cutoff);
                float hp_l, hp_r;
                svf_process(&e->global_hp_l, l, f, 0.4f, NULL, &hp_l, NULL);
                svf_process(&e->global_hp_r, r, f, 0.4f, NULL, &hp_r, NULL);
                l = hp_l;
                r = hp_r;
            }

            /* EQ (3-band, stereo) */
            if (e->eq_low_gain != 0.0f) {
                float f = cutoff_to_f(0.15f); /* ~150Hz */
                float lp_l, lp_r;
                svf_process(&e->eq_low_l, l, f, 0.5f, &lp_l, NULL, NULL);
                svf_process(&e->eq_low_r, r, f, 0.5f, &lp_r, NULL, NULL);
                float eq_gain = 1.0f + e->eq_low_gain * 2.0f;
                l += (lp_l - l) * (eq_gain - 1.0f) * 0.5f;
                r += (lp_r - r) * (eq_gain - 1.0f) * 0.5f;
            }
            if (e->eq_mid_gain != 0.0f) {
                float f = cutoff_to_f(0.45f); /* ~1kHz */
                float bp_l, bp_r;
                svf_process(&e->eq_mid_l, l, f, 0.3f, NULL, NULL, &bp_l);
                svf_process(&e->eq_mid_r, r, f, 0.3f, NULL, NULL, &bp_r);
                float eq_gain = e->eq_mid_gain * 2.0f;
                l += bp_l * eq_gain * 0.5f;
                r += bp_r * eq_gain * 0.5f;
            }
            if (e->eq_high_gain != 0.0f) {
                float f = cutoff_to_f(0.75f); /* ~8kHz */
                float hp_l, hp_r;
                svf_process(&e->eq_high_l, l, f, 0.5f, NULL, &hp_l, NULL);
                svf_process(&e->eq_high_r, r, f, 0.5f, NULL, &hp_r, NULL);
                float eq_gain = e->eq_high_gain * 2.0f;
                l += hp_l * eq_gain * 0.5f;
                r += hp_r * eq_gain * 0.5f;
            }

            /* Dry/wet mix */
            l = e->dry_l[i] * (1.0f - e->dry_wet) + l * e->dry_wet;
            r = e->dry_r[i] * (1.0f - e->dry_wet) + r * e->dry_wet;
        }

        /* Output gain */
        l *= e->output_gain;
        r *= e->output_gain;

        /* Soft clip output */
        l = soft_clip(l);
        r = soft_clip(r);

        /* Convert to int16 */
        int32_t sl = (int32_t)(l * 32767.0f);
        int32_t sr = (int32_t)(r * 32767.0f);
        if (sl > 32767) sl = 32767;
        if (sl < -32768) sl = -32768;
        if (sr > 32767) sr = 32767;
        if (sr < -32768) sr = -32768;

        out_lr[i * 2] = (int16_t)sl;
        out_lr[i * 2 + 1] = (int16_t)sr;
    }
}

/* ============================================================
 * Scene Morphing
 * ============================================================ */

void pfx_scene_morph_start(perf_fx_engine_t *e, int scene_a, int scene_b) {
    if (scene_a < 0 || scene_a >= PFX_NUM_SCENES) return;
    if (scene_b < 0 || scene_b >= PFX_NUM_SCENES) return;
    if (!e->scenes[scene_a].populated || !e->scenes[scene_b].populated) return;

    e->morph.active = 1;
    e->morph.scene_a = scene_a;
    e->morph.scene_b = scene_b;
    e->morph.progress = 0.0f;
    /* ~2 seconds at 44100Hz / 128 frames per block */
    e->morph.rate = 1.0f / (2.0f * PFX_SAMPLE_RATE);
}

void pfx_scene_morph_tick(perf_fx_engine_t *e, int frames) {
    scene_morph_t *m = &e->morph;
    if (!m->active) return;

    m->progress += m->rate * frames;
    if (m->progress >= 1.0f) {
        m->progress = 1.0f;
        m->active = 0;
        /* Snap to scene B */
        pfx_scene_recall(e, m->scene_b);
        return;
    }

    float a = 1.0f - m->progress;
    float b = m->progress;
    scene_t *sa = &e->scenes[m->scene_a];
    scene_t *sb = &e->scenes[m->scene_b];

    /* Interpolate global params */
    e->dry_wet = sa->global_params[0] * a + sb->global_params[0] * b;
    e->input_gain = sa->global_params[1] * a + sb->global_params[1] * b;
    e->global_lp_cutoff = sa->global_params[2] * a + sb->global_params[2] * b;
    e->global_hp_cutoff = sa->global_params[3] * a + sb->global_params[3] * b;
    e->eq_low_gain = sa->global_params[4] * a + sb->global_params[4] * b;
    e->eq_mid_gain = sa->global_params[5] * a + sb->global_params[5] * b;
    e->eq_high_gain = sa->global_params[6] * a + sb->global_params[6] * b;
    e->output_gain = sa->global_params[7] * a + sb->global_params[7] * b;

    /* Interpolate continuous FX params (for FX active in both scenes) */
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        if (sa->cont_active[i] && sb->cont_active[i]) {
            /* Both active: interpolate params */
            for (int j = 0; j < PFX_CONT_PARAMS; j++) {
                e->cont[i].params[j] = sa->cont_params[i][j] * a +
                                         sb->cont_params[i][j] * b;
            }
        } else if (sb->cont_active[i] && !sa->cont_active[i]) {
            /* Fading in: activate when past 50% */
            if (m->progress > 0.5f && !e->cont[i].active) {
                pfx_cont_activate(e, i);
            }
            if (e->cont[i].active) {
                for (int j = 0; j < PFX_CONT_PARAMS; j++)
                    e->cont[i].params[j] = sb->cont_params[i][j];
                /* Fade mix in */
                e->cont[i].params[3] *= (m->progress - 0.5f) * 2.0f;
            }
        } else if (sa->cont_active[i] && !sb->cont_active[i]) {
            /* Fading out: deactivate when past 50% */
            if (m->progress > 0.5f && e->cont[i].active) {
                pfx_cont_deactivate(e, i);
            }
            if (e->cont[i].active) {
                /* Fade mix out */
                e->cont[i].params[3] *= (1.0f - m->progress) * 2.0f;
            }
        }
    }
}

/* ============================================================
 * State serialization (JSON)
 * ============================================================ */

int pfx_serialize_state(perf_fx_engine_t *e, char *buf, int buf_len) {
    int n = 0;
    SAFE_SNPRINTF(buf, n, buf_len, "{\"bpm\":%.1f", e->bpm);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"dry_wet\":%.3f", e->dry_wet);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"input_gain\":%.3f", e->input_gain);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"output_gain\":%.3f", e->output_gain);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"global_lp\":%.3f", e->global_lp_cutoff);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"global_hp\":%.3f", e->global_hp_cutoff);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"eq_low\":%.3f", e->eq_low_gain);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"eq_mid\":%.3f", e->eq_mid_gain);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"eq_high\":%.3f", e->eq_high_gain);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"pressure_curve\":%d", e->pressure_curve);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"audio_source\":%d", e->audio_source);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"track_mask\":%d", e->track_mask);

    /* Continuous FX state */
    SAFE_SNPRINTF(buf, n, buf_len, ",\"cont\":[");
    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        if (i > 0) SAFE_SNPRINTF(buf, n, buf_len, ",");
        SAFE_SNPRINTF(buf, n, buf_len, "{\"a\":%d,\"p\":[", e->cont[i].active);
        for (int j = 0; j < PFX_CONT_PARAMS; j++) {
            if (j > 0) SAFE_SNPRINTF(buf, n, buf_len, ",");
            SAFE_SNPRINTF(buf, n, buf_len, "%.3f", e->cont[i].params[j]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]}");
    }
    SAFE_SNPRINTF(buf, n, buf_len, "]}");
    return n;
}
