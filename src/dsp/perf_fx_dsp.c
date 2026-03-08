/*
 * Performance FX DSP Engine v2
 *
 * 32 unified punch-in FX. All momentary by default, shift+pad to latch.
 * Animated filter sweeps, space throw tails, per-FX pressure mappings.
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

#define clampf pfx_clampf

#define SAFE_SNPRINTF(buf, n, len, ...) do { \
    n += snprintf((buf) + (n), (n) < (len) ? (len) - (n) : 0, __VA_ARGS__); \
    if ((n) >= (len)) return (len) - 1; \
} while(0)

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float soft_clip(float x) {
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) / 6.75f;
}

static inline float fast_tanh(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

static inline float white_noise(unsigned int *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return (float)(int)(*seed) / 2147483648.0f;
}

static inline float flush_denormal(float x) {
    union { float f; uint32_t u; } v = { .f = x };
    return (v.u & 0x7F800000) == 0 ? 0.0f : x;
}

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
        default:
            mod = pressure;
            break;
    }
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

static void svf_process(svf_t *s, float input, float f, float q,
                         float *lp, float *hp, float *bp) {
    s->hp = input - s->lp - q * s->bp;
    s->bp += f * s->hp;
    s->lp += f * s->bp;
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
        rv->comb_pos_r[i] = rv->comb_len[i] / 3;
        rv->comb_filt_r[i] = 0.0f;
    }
    for (int i = 0; i < 2; i++) {
        rv->ap_len[i] = AP_LENGTHS[i];
        rv->ap_pos[i] = 0;
        rv->ap_pos_r[i] = 0;
    }
    rv->decay = 0.7f;
    rv->damping = 0.4f;
    rv->mix = 0.3f;
}

static void reverb_process_stereo(reverb_t *rv, float in_l, float in_r,
                                   float *out_l, float *out_r) {
    float left = 0.0f, right = 0.0f;
    float decay = rv->decay;
    float damp = rv->damping;
    float mono_in = (in_l + in_r) * 0.5f;

    for (int i = 0; i < 4; i++) {
        float *buf = rv->comb_buf[i];

        int pos_l = rv->comb_pos[i];
        float del_l = buf[pos_l];
        rv->comb_filt[i] = del_l * (1.0f - damp) + rv->comb_filt[i] * damp;
        buf[pos_l] = mono_in + rv->comb_filt[i] * decay;
        rv->comb_pos[i] = (pos_l + 1) % rv->comb_len[i];
        left += del_l;

        int pos_r = rv->comb_pos_r[i];
        float del_r = buf[pos_r];
        rv->comb_filt_r[i] = del_r * (1.0f - damp) + rv->comb_filt_r[i] * damp;
        rv->comb_pos_r[i] = (pos_r + 1) % rv->comb_len[i];
        right += del_r;
    }
    left *= 0.25f;
    right *= 0.25f;

    /* Allpass filters — separate buffers for L and R */
    for (int i = 0; i < 2; i++) {
        float *buf_l = rv->ap_buf[i];
        int pos_l = rv->ap_pos[i];
        float delayed_l = buf_l[pos_l];
        float yl = -left * 0.5f + delayed_l;
        buf_l[pos_l] = left + delayed_l * 0.5f;
        left = yl;
        rv->ap_pos[i] = (pos_l + 1) % rv->ap_len[i];

        float *buf_r = rv->ap_buf_r[i];
        int pos_r = rv->ap_pos_r[i];
        float delayed_r = buf_r[pos_r];
        float yr = -right * 0.5f + delayed_r;
        buf_r[pos_r] = right + delayed_r * 0.5f;
        right = yr;
        rv->ap_pos_r[i] = (pos_r + 1) % rv->ap_len[i];
    }

    *out_l = left;
    *out_r = right;
}

/* ============================================================
 * Engine init / destroy / reset
 * ============================================================ */

void pfx_engine_init(perf_fx_engine_t *e) {
    memset(e, 0, sizeof(*e));

    /* Global defaults */
    e->global_hpf = 0.0f;
    e->global_lpf = 1.0f;
    e->dry_wet = 1.0f;
    e->eq_bump_freq = 0.5f;
    e->bpm = 120.0f;
    e->pressure_curve = PRESSURE_EXPONENTIAL;
    e->audio_source = SOURCE_MOVE_MIX;
    e->track_mask = 0x0F;
    e->current_step_preset = -1;
    e->last_touched_slot = -1;

    /* Allocate shared capture buffer */
    e->capture_len = PFX_CAPTURE_BUF;
    e->capture_buf_l = (float *)calloc(PFX_CAPTURE_BUF, sizeof(float));
    e->capture_buf_r = (float *)calloc(PFX_CAPTURE_BUF, sizeof(float));
    if (!e->capture_buf_l || !e->capture_buf_r) return;

    /* Init all 32 slots based on type */
    for (int i = 0; i < PFX_NUM_FX; i++) {
        pfx_slot_t *s = &e->slots[i];

        /* Default params to 0.5 */
        for (int j = 0; j < PFX_SLOT_PARAMS; j++)
            s->params[j] = 0.5f;

        if (FX_IS_REPEAT(i)) {
            /* Repeat slots: repeat buffer */
            repeat_init(&s->repeat, PFX_REPEAT_BUF);
            /* Tape stop / half speed buffer */
            s->tape.buf_l = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
            s->tape.buf_r = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
            s->tape.buf_len = PFX_REPEAT_BUF;
            s->tape.speed = 1.0f;
        } else if (FX_IS_FILTER(i)) {
            /* Filter slots: SVF state (inline, no alloc needed) */
            /* Phaser/Flanger need mod_delay */
            if (i == FX_FLANGER) {
                s->mod_delay.buf_l = (float *)calloc(PFX_CHORUS_BUF, sizeof(float));
                s->mod_delay.buf_r = (float *)calloc(PFX_CHORUS_BUF, sizeof(float));
                s->mod_delay.buf_len = PFX_CHORUS_BUF;
            }
        } else if (FX_IS_SPACE(i)) {
            /* Space slots: delay or reverb */
            if (i >= FX_DELAY && i <= FX_ECHO_FREEZE) {
                delay_init(&s->delay, PFX_MAX_DELAY);
            }
            if (i >= FX_REVERB && i <= FX_SPRING) {
                s->reverb = (reverb_t *)calloc(1, sizeof(reverb_t));
                if (s->reverb) reverb_init(s->reverb);
            }
        } else if (FX_IS_DISTORT(i)) {
            /* Distortion slots */
            if (i == FX_TAPE_STOP || i == FX_VINYL_BRAKE) {
                s->tape.buf_l = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
                s->tape.buf_r = (float *)calloc(PFX_REPEAT_BUF, sizeof(float));
                s->tape.buf_len = PFX_REPEAT_BUF;
                s->tape.speed = 1.0f;
            }
            if (i == FX_CHORUS) {
                s->mod_delay.buf_l = (float *)calloc(PFX_CHORUS_BUF, sizeof(float));
                s->mod_delay.buf_r = (float *)calloc(PFX_CHORUS_BUF, sizeof(float));
                s->mod_delay.buf_len = PFX_CHORUS_BUF;
            }
        }
    }
}

void pfx_engine_destroy(perf_fx_engine_t *e) {
    free(e->capture_buf_l);
    free(e->capture_buf_r);

    for (int i = 0; i < PFX_NUM_FX; i++) {
        pfx_slot_t *s = &e->slots[i];
        repeat_free(&s->repeat);
        free(s->tape.buf_l);
        free(s->tape.buf_r);
        s->tape.buf_l = s->tape.buf_r = NULL;
        delay_free(&s->delay);
        free(s->mod_delay.buf_l);
        free(s->mod_delay.buf_r);
        s->mod_delay.buf_l = s->mod_delay.buf_r = NULL;
        free(s->reverb);
        s->reverb = NULL;
    }
}

void pfx_engine_reset(perf_fx_engine_t *e) {
    for (int i = 0; i < PFX_NUM_FX; i++) {
        pfx_slot_t *s = &e->slots[i];
        s->active = 0;
        s->latched = 0;
        s->tail_active = 0;
        s->pressure = 0.0f;
        s->fading_out = 0;
        s->phase = 0.0f;
        s->tail_silence_count = 0;
        svf_reset(&s->filter_l);
        svf_reset(&s->filter_r);
        svf_reset(&s->sat_filter_l);
        svf_reset(&s->sat_filter_r);
        if (s->delay.buf_l) delay_reset(&s->delay);
        if (s->reverb) reverb_init(s->reverb);
    }
    e->bypassed = 0;
    e->last_touched_slot = -1;
    svf_reset(&e->global_lp_l);
    svf_reset(&e->global_lp_r);
    svf_reset(&e->global_hp_l);
    svf_reset(&e->global_hp_r);
    svf_reset(&e->eq_bump_l);
    svf_reset(&e->eq_bump_r);
}

/* ============================================================
 * Unified FX control
 * ============================================================ */

void pfx_activate(perf_fx_engine_t *e, int slot, float velocity) {
    if (slot < 0 || slot >= PFX_NUM_FX) return;
    pfx_slot_t *s = &e->slots[slot];
    s->active = 1;
    s->fading_out = 0;
    s->tail_active = 0;
    s->tail_silence_count = 0;
    s->velocity = velocity;
    s->pressure = velocity;
    s->phase = 0.0f;
    e->last_touched_slot = slot;

    /* Reset filter state */
    svf_reset(&s->filter_l);
    svf_reset(&s->filter_r);

    /* Type-specific activation */
    if (FX_IS_REPEAT(slot)) {
        /* Beat repeat: start capturing */
        if (slot >= FX_RPT_1_4 && slot <= FX_STUTTER) {
            s->repeat.capturing = 1;
            s->repeat.frames_captured = 0;
            s->repeat.read_pos = 0;
            s->repeat.repeat_pos = 0;

            float div;
            switch (slot) {
                case FX_RPT_1_4:  div = 1.0f; break;
                case FX_RPT_1_8:  div = 0.5f; break;
                case FX_RPT_1_16: div = 0.25f; break;
                case FX_RPT_TRIP: div = 2.0f / 3.0f; break;
                case FX_STUTTER:  div = 0.125f; break;
                default: div = 0.5f;
            }
            s->repeat.repeat_len = pfx_bpm_to_samples(e->bpm, div);
            if (s->repeat.repeat_len < 64) s->repeat.repeat_len = 64;
            if (s->repeat.repeat_len > s->repeat.buf_len)
                s->repeat.repeat_len = s->repeat.buf_len;
        }

        /* Scatter */
        if (slot == FX_SCATTER) {
            s->scatter_seed = (unsigned int)(velocity * 12345.0f + 67890u);
        }

        /* Reverse: copy from capture buffer */
        if (slot == FX_REVERSE) {
            int len = PFX_SAMPLE_RATE;
            if (len > s->repeat.buf_len) len = s->repeat.buf_len;
            for (int i = 0; i < len; i++) {
                int src = (e->capture_write_pos - len + i + e->capture_len) % e->capture_len;
                s->repeat.buf_l[i] = e->capture_buf_l[src];
                s->repeat.buf_r[i] = e->capture_buf_r[src];
            }
            s->repeat.repeat_len = len;
            s->repeat.read_pos = len - 1;
        }

        /* Half-speed: copy from capture buffer */
        if (slot == FX_HALF_SPEED) {
            int len = PFX_SAMPLE_RATE * 2;
            if (len > s->tape.buf_len) len = s->tape.buf_len;
            for (int i = 0; i < len; i++) {
                int src = (e->capture_write_pos - len + i + e->capture_len) % e->capture_len;
                s->tape.buf_l[i] = e->capture_buf_l[src];
                s->tape.buf_r[i] = e->capture_buf_r[src];
            }
            s->tape.read_pos = 0.0f;
            s->tape.speed = 0.5f;
        }
    }

    if (FX_IS_FILTER(slot)) {
        /* Reset phase for animated sweeps */
        s->phase = 0.0f;
        if (slot == FX_PHASER) {
            memset(&s->phaser, 0, sizeof(s->phaser));
        }
        if (slot == FX_FLANGER && s->mod_delay.buf_l) {
            memset(s->mod_delay.buf_l, 0, s->mod_delay.buf_len * sizeof(float));
            memset(s->mod_delay.buf_r, 0, s->mod_delay.buf_len * sizeof(float));
            s->mod_delay.write_pos = 0;
            s->mod_delay.lfo_phase = 0.0f;
        }
    }

    if (FX_IS_SPACE(slot)) {
        /* Reset space FX state */
        if (slot >= FX_DELAY && slot <= FX_ECHO_FREEZE) {
            delay_reset(&s->delay);
            s->echo_frozen = 0;
        }
        if (slot >= FX_REVERB && slot <= FX_SPRING && s->reverb) {
            reverb_init(s->reverb);
        }
    }

    if (FX_IS_DISTORT(slot)) {
        if (slot == FX_TAPE_STOP || slot == FX_VINYL_BRAKE) {
            s->tape.speed = 1.0f;
            s->tape.read_pos = 0.0f;
            s->tape.write_pos = 0;
            /* Vinyl brake decelerates slower than tape stop */
            s->tape.decel_rate = (slot == FX_VINYL_BRAKE) ? 0.00002f : 0.0001f;
        }
        if (slot == FX_BITCRUSH || slot == FX_DOWNSAMPLE) {
            s->crush_count = 0;
            s->crush_hold_l = s->crush_hold_r = 0.0f;
        }
        if (slot == FX_GATE_DUCK) {
            s->ducker.phase = 0.0f;
            s->ducker.env = 1.0f;
        }
        if (slot == FX_TREMOLO) {
            s->trem_lfo_phase = 0.0f;
        }
        if (slot == FX_CHORUS && s->mod_delay.buf_l) {
            memset(s->mod_delay.buf_l, 0, s->mod_delay.buf_len * sizeof(float));
            memset(s->mod_delay.buf_r, 0, s->mod_delay.buf_len * sizeof(float));
            s->mod_delay.write_pos = 0;
            s->mod_delay.lfo_phase = 0.0f;
        }
        if (slot == FX_SATURATE) {
            svf_reset(&s->sat_filter_l);
            svf_reset(&s->sat_filter_r);
        }
    }
}

void pfx_deactivate(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_FX) return;
    pfx_slot_t *s = &e->slots[slot];

    /* If latched, don't deactivate on release */
    if (s->latched) return;

    if (!s->active) return;

    /* Space FX: switch to tail mode instead of immediate cutoff */
    if (FX_IS_SPACE(slot)) {
        s->active = 0;
        s->tail_active = 1;
        s->tail_silence_count = 0;
        s->pressure = 0.0f;
        return;
    }

    /* Other FX: fade out */
    s->fading_out = 1;
    s->fade_pos = 0;
    s->fade_len = 256; /* ~5.8ms */
    s->pressure = 0.0f;
}

void pfx_set_pressure(perf_fx_engine_t *e, int slot, float pressure) {
    if (slot < 0 || slot >= PFX_NUM_FX) return;
    pfx_slot_t *s = &e->slots[slot];
    s->pressure = clampf(pressure, 0.0f, 1.0f);

    /* Per-FX pressure mappings are applied during processing */
}

void pfx_set_param(perf_fx_engine_t *e, int slot, int idx, float val) {
    if (slot < 0 || slot >= PFX_NUM_FX) return;
    if (idx < 0 || idx >= PFX_SLOT_PARAMS) return;
    e->slots[slot].params[idx] = clampf(val, 0.0f, 1.0f);
}

void pfx_set_latched(perf_fx_engine_t *e, int slot, int latched) {
    if (slot < 0 || slot >= PFX_NUM_FX) return;
    pfx_slot_t *s = &e->slots[slot];
    s->latched = latched;

    if (latched && !s->active) {
        /* Latching an inactive slot: activate it */
        pfx_activate(e, slot, 0.7f);
    }
    if (!latched && s->active) {
        /* Unlatching: if pad is not physically held, deactivate.
         * For space FX in latched mode = continuous processing,
         * unlatching switches to tail decay. */
        if (s->pressure <= 0.0f) {
            /* Force deactivate by temporarily clearing latched */
            s->latched = 0;
            pfx_deactivate(e, slot);
        }
    }
}

/* ============================================================
 * Scene management (simplified, no morph)
 * ============================================================ */

void pfx_scene_save(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    pfx_scene_t *sc = &e->scenes[slot];
    sc->populated = 1;
    for (int i = 0; i < PFX_NUM_FX; i++) {
        sc->latched[i] = e->slots[i].latched;
        memcpy(sc->params[i], e->slots[i].params, sizeof(float) * PFX_SLOT_PARAMS);
    }
    sc->globals[0] = e->global_hpf;
    sc->globals[1] = e->global_lpf;
    sc->globals[2] = e->dry_wet;
    sc->globals[3] = e->eq_bump_freq;
}

void pfx_scene_recall(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    pfx_scene_t *sc = &e->scenes[slot];
    if (!sc->populated) return;

    /* Deactivate slots that are latched but won't be in new scene */
    for (int i = 0; i < PFX_NUM_FX; i++) {
        if (e->slots[i].latched && !sc->latched[i]) {
            e->slots[i].latched = 0;
            if (e->slots[i].active) {
                pfx_deactivate(e, i);
            }
        }
    }

    /* Activate/update slots from scene */
    for (int i = 0; i < PFX_NUM_FX; i++) {
        memcpy(e->slots[i].params, sc->params[i], sizeof(float) * PFX_SLOT_PARAMS);
        if (sc->latched[i] && !e->slots[i].latched) {
            pfx_set_latched(e, i, 1);
        }
    }

    e->global_hpf = sc->globals[0];
    e->global_lpf = sc->globals[1];
    e->dry_wet = sc->globals[2];
    e->eq_bump_freq = sc->globals[3];
}

void pfx_scene_clear(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    memset(&e->scenes[slot], 0, sizeof(pfx_scene_t));
}

void pfx_step_save(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    pfx_step_preset_t *p = &e->step_presets[slot];
    p->populated = 1;
    for (int i = 0; i < PFX_NUM_FX; i++) {
        p->latched[i] = e->slots[i].latched;
        memcpy(p->params[i], e->slots[i].params, sizeof(float) * PFX_SLOT_PARAMS);
    }
    p->globals[0] = e->global_hpf;
    p->globals[1] = e->global_lpf;
    p->globals[2] = e->dry_wet;
    p->globals[3] = e->eq_bump_freq;
}

void pfx_step_recall(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    pfx_step_preset_t *p = &e->step_presets[slot];
    if (!p->populated) return;

    for (int i = 0; i < PFX_NUM_FX; i++) {
        if (e->slots[i].latched && !p->latched[i]) {
            e->slots[i].latched = 0;
            if (e->slots[i].active) pfx_deactivate(e, i);
        }
    }
    for (int i = 0; i < PFX_NUM_FX; i++) {
        memcpy(e->slots[i].params, p->params[i], sizeof(float) * PFX_SLOT_PARAMS);
        if (p->latched[i] && !e->slots[i].latched) {
            pfx_set_latched(e, i, 1);
        }
    }

    e->global_hpf = p->globals[0];
    e->global_lpf = p->globals[1];
    e->dry_wet = p->globals[2];
    e->eq_bump_freq = p->globals[3];
    e->current_step_preset = slot;
}

void pfx_step_clear(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_PRESETS) return;
    memset(&e->step_presets[slot], 0, sizeof(pfx_step_preset_t));
    if (e->current_step_preset == slot) e->current_step_preset = -1;
}

/* ============================================================
 * Row 4: Time/Repeat FX processing (slots 0-7)
 * ============================================================ */

/* Pressure mapping for beat repeat: pitch drift on repeats */
static void process_beat_repeat(pfx_slot_t *s, int slot, float *l, float *r,
                                 perf_fx_engine_t *e) {
    repeat_t *rp = &s->repeat;
    float pressure = s->pressure;

    /* Pressure -> pitch drift on repeats */
    float pitch_drift = 1.0f + pressure * 0.3f; /* up to 30% speed-up */

    if (rp->capturing) {
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
        *l = rp->buf_l[rp->read_pos];
        *r = rp->buf_r[rp->read_pos];
        /* Apply pitch drift via variable read speed */
        int advance = (int)pitch_drift;
        if (advance < 1) advance = 1;
        rp->read_pos = (rp->read_pos + advance) % rp->buf_len;
        rp->repeat_pos += advance;
        if (rp->repeat_pos >= rp->repeat_len) {
            rp->repeat_pos = 0;
            rp->read_pos = (rp->write_pos - rp->repeat_len + rp->buf_len) % rp->buf_len;
        }
    }
    (void)e;
}

static void process_stutter(pfx_slot_t *s, float *l, float *r) {
    repeat_t *rp = &s->repeat;
    float pressure = s->pressure;
    int stutter_len = 64 + (int)((1.0f - pressure) * (rp->repeat_len - 64));
    if (stutter_len < 64) stutter_len = 64;

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

static void process_scatter(pfx_slot_t *s, float *l, float *r,
                             perf_fx_engine_t *e) {
    repeat_t *rp = &s->repeat;
    float pressure = s->pressure;

    if (rp->repeat_pos <= 0) {
        int min_len = 128;
        int max_len = pfx_bpm_to_samples(e->bpm, 0.5f);
        if (max_len > e->capture_len) max_len = e->capture_len;
        int slice_len = min_len + (int)(white_noise(&s->scatter_seed) * 0.5f + 0.5f) * (max_len - min_len);
        slice_len = (int)(slice_len * (1.0f - pressure * 0.7f));
        if (slice_len < min_len) slice_len = min_len;

        rp->repeat_len = slice_len;
        rp->repeat_pos = slice_len;
        int offset = (int)((white_noise(&s->scatter_seed) * 0.5f + 0.5f) * e->capture_len);
        rp->read_pos = (e->capture_write_pos - offset + e->capture_len) % e->capture_len;
    }

    *l = e->capture_buf_l[rp->read_pos];
    *r = e->capture_buf_r[rp->read_pos];

    if ((rp->repeat_len & 3) == 0)
        rp->read_pos = (rp->read_pos - 1 + e->capture_len) % e->capture_len;
    else
        rp->read_pos = (rp->read_pos + 1) % e->capture_len;
    rp->repeat_pos--;
}

static void process_reverse(pfx_slot_t *s, float *l, float *r) {
    repeat_t *rp = &s->repeat;
    float speed = 0.5f + s->pressure * 1.5f;

    if (rp->read_pos >= 0 && rp->read_pos < rp->repeat_len) {
        *l = rp->buf_l[rp->read_pos];
        *r = rp->buf_r[rp->read_pos];
    }

    rp->read_pos -= (int)speed;
    if (rp->read_pos < 0) rp->read_pos = rp->repeat_len - 1;
}

static void process_half_speed(pfx_slot_t *s, float *l, float *r) {
    tape_stop_t *t = &s->tape;
    float speed = 0.3f + (1.0f - s->pressure) * 0.7f;

    int pos0 = (int)t->read_pos;
    int pos1 = (pos0 + 1) % t->buf_len;
    float frac = t->read_pos - pos0;

    if (pos0 >= 0 && pos0 < t->buf_len) {
        *l = t->buf_l[pos0] + frac * (t->buf_l[pos1] - t->buf_l[pos0]);
        *r = t->buf_r[pos0] + frac * (t->buf_r[pos1] - t->buf_r[pos0]);
    }

    t->read_pos += speed;
    if (t->read_pos >= (float)t->buf_len) t->read_pos = 0.0f;
}

/* ============================================================
 * Row 3: Filter Sweep FX (slots 8-15)
 * Phase counter 0->1 drives the sweep while active
 * ============================================================ */

/* Advance phase per-sample. Pressure controls sweep speed for some FX. */
static void advance_filter_phase(pfx_slot_t *s, int slot) {
    /* Base sweep speed: complete sweep in ~2 seconds */
    float base_rate = 1.0f / (2.0f * PFX_SAMPLE_RATE);

    switch (slot) {
        case FX_LP_SWEEP_DOWN:
        case FX_HP_SWEEP_UP:
            /* Pressure -> sweep speed (harder = faster) */
            base_rate *= (0.5f + s->pressure * 3.0f);
            break;
        case FX_BP_RISE:
        case FX_BP_FALL:
        case FX_RESO_SWEEP:
        case FX_AUTO_FILTER:
            base_rate *= (0.5f + s->pressure * 2.0f);
            break;
        case FX_PHASER:
        case FX_FLANGER:
            /* Pressure -> LFO speed */
            base_rate *= (0.3f + s->pressure * 4.0f);
            break;
        default:
            break;
    }

    s->phase += base_rate;
    /* For looping FX (phaser, flanger, auto filter), wrap phase */
    if (slot == FX_PHASER || slot == FX_FLANGER || slot == FX_AUTO_FILTER) {
        if (s->phase >= 1.0f) s->phase -= 1.0f;
    } else {
        /* For sweep FX, clamp at 1.0 */
        if (s->phase > 1.0f) s->phase = 1.0f;
    }
}

static void process_lp_sweep_down(pfx_slot_t *s, float *l, float *r) {
    /* Phase 0 = open (cutoff=1), phase 1 = closed (cutoff=0.05) */
    float cutoff = 1.0f - s->phase * 0.95f;
    float f = cutoff_to_f(cutoff);
    float q = 0.2f + s->phase * 0.5f; /* resonance increases as it sweeps down */
    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, q, &out_l, NULL, NULL);
    svf_process(&s->filter_r, *r, f, q, &out_r, NULL, NULL);
    *l = out_l; *r = out_r;
}

static void process_hp_sweep_up(pfx_slot_t *s, float *l, float *r) {
    /* Phase 0 = no HP (cutoff=0), phase 1 = full HP (cutoff=0.9) */
    float cutoff = s->phase * 0.9f;
    float f = cutoff_to_f(cutoff);
    float q = 0.2f + s->phase * 0.4f;
    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, q, NULL, &out_l, NULL);
    svf_process(&s->filter_r, *r, f, q, NULL, &out_r, NULL);
    *l = out_l; *r = out_r;
}

static void process_bp_rise(pfx_slot_t *s, float *l, float *r) {
    /* Phase 0 = low freq BP, phase 1 = high freq BP */
    float cutoff = 0.1f + s->phase * 0.7f;
    float f = cutoff_to_f(cutoff);
    float q = 0.1f + (1.0f - s->phase) * 0.3f;
    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, q, NULL, NULL, &out_l);
    svf_process(&s->filter_r, *r, f, q, NULL, NULL, &out_r);
    *l = out_l; *r = out_r;
}

static void process_bp_fall(pfx_slot_t *s, float *l, float *r) {
    /* Phase 0 = high freq BP, phase 1 = low freq BP */
    float cutoff = 0.8f - s->phase * 0.7f;
    float f = cutoff_to_f(cutoff);
    float q = 0.1f + s->phase * 0.3f;
    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, q, NULL, NULL, &out_l);
    svf_process(&s->filter_r, *r, f, q, NULL, NULL, &out_r);
    *l = out_l; *r = out_r;
}

static void process_reso_sweep(pfx_slot_t *s, float *l, float *r) {
    /* High resonance sweep: cutoff sweeps up with phase */
    float cutoff = 0.15f + s->phase * 0.6f;
    float f = cutoff_to_f(cutoff);
    float q = 0.05f + (1.0f - s->pressure) * 0.03f; /* very high Q, pressure tightens */
    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, q, NULL, NULL, &out_l);
    svf_process(&s->filter_r, *r, f, q, NULL, NULL, &out_r);
    /* Blend: mostly resonant peak + some dry */
    *l = *l * 0.3f + out_l * 0.7f;
    *r = *r * 0.3f + out_r * 0.7f;
}

static void process_phaser_fx(pfx_slot_t *s, float *l, float *r) {
    phaser_t *ph = &s->phaser;
    float depth = 0.5f + s->params[0] * 0.5f;
    float fb = 0.3f + s->params[1] * 0.5f;

    float lfo = sinf(s->phase * 2.0f * M_PI);
    float mono = (*l + *r) * 0.5f + ph->ap[0].y1 * fb * 0.3f;
    float base_freq = 200.0f + depth * 3000.0f * (0.5f + 0.5f * lfo);
    float out = mono;

    for (int i = 0; i < PFX_NUM_ALLPASS; i++) {
        float freq = base_freq * (1.0f + (float)i * 0.3f);
        float w = 2.0f * M_PI * freq / PFX_SAMPLE_RATE;
        float cosw = cosf(w);
        float a = (cosw == 0.0f) ? 0.0f : (1.0f - sinf(w)) / cosw;
        a = clampf(a, -0.99f, 0.99f);
        float x = out;
        float y = -a * x + ph->ap[i].y1;
        ph->ap[i].y1 = flush_denormal(a * y + x);
        out = y;
    }

    /* Dramatic on tap: full mix by default */
    *l = *l * 0.3f + out * 0.7f;
    *r = *r * 0.3f + out * 0.7f;
}

static void process_flanger_fx(pfx_slot_t *s, float *l, float *r) {
    mod_delay_t *md = &s->mod_delay;
    if (!md->buf_l) return;

    float depth = 1.0f + s->params[0] * 30.0f;
    float fb = 0.5f + s->params[1] * 0.4f;

    float lfo = sinf(s->phase * 2.0f * M_PI);
    float delay_samples = 5.0f + depth * (0.5f + 0.5f * lfo);

    int wp = md->write_pos;
    int rp = (wp - (int)delay_samples + md->buf_len) % md->buf_len;
    float dl = md->buf_l[rp];
    float dr = md->buf_r[rp];

    md->buf_l[wp] = *l + dl * fb;
    md->buf_r[wp] = *r + dr * fb;
    md->write_pos = (wp + 1) % md->buf_len;

    /* Dramatic mix */
    *l = *l * 0.4f + dl * 0.6f;
    *r = *r * 0.4f + dr * 0.6f;
}

static void process_auto_filter(pfx_slot_t *s, float *l, float *r) {
    float lfo = sinf(s->phase * 2.0f * M_PI);
    float depth = 0.3f + s->params[0] * 0.3f;
    float center = 0.2f + s->params[1] * 0.5f;
    float reso = 0.1f + s->params[2] * 0.08f;

    float cutoff = center + lfo * depth;
    cutoff = clampf(cutoff, 0.01f, 0.95f);
    float f = cutoff_to_f(cutoff);

    float out_l, out_r;
    svf_process(&s->filter_l, *l, f, reso, &out_l, NULL, NULL);
    svf_process(&s->filter_r, *r, f, reso, &out_r, NULL, NULL);
    *l = out_l; *r = out_r;
}

/* ============================================================
 * Row 2: Space Throw FX (slots 16-23)
 * Audio feeds in while held, tail decays on release
 * ============================================================ */

/* Params: [0]=time, [1]=feedback, [2]=filter, [3]=mix
 * Pressure -> feedback amount */
static void process_delay_throw(pfx_slot_t *s, float *l, float *r, int feeding) {
    delay_t *d = &s->delay;
    float time = s->params[0];
    float base_fb = s->params[1] * 0.8f;
    float filt = s->params[2];
    float mix = 0.7f; /* dramatic on tap */

    /* Pressure adds feedback */
    float fb = base_fb + s->pressure * (0.95f - base_fb);

    int delay_samples = 256 + (int)(time * (d->length - 256));
    float dl, dr;
    delay_read(d, delay_samples, &dl, &dr);

    float f_coeff = 0.1f + filt * 0.8f;
    d->fb_lp_l += f_coeff * (dl - d->fb_lp_l);
    d->fb_lp_r += f_coeff * (dr - d->fb_lp_r);

    if (feeding)
        delay_write(d, *l + d->fb_lp_l * fb, *r + d->fb_lp_r * fb);
    else
        delay_write(d, d->fb_lp_l * fb, d->fb_lp_r * fb);

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

static void process_ping_pong_throw(pfx_slot_t *s, float *l, float *r, int feeding) {
    delay_t *d = &s->delay;
    float time = s->params[0];
    float base_fb = s->params[1] * 0.8f;
    float spread = s->params[2];
    float mix = 0.7f;

    float fb = base_fb + s->pressure * (0.95f - base_fb);

    int delay_samples = 256 + (int)(time * (d->length - 256));
    float dl, dr;
    delay_read(d, delay_samples, &dl, &dr);

    float cross = 0.3f + spread * 0.6f;
    float in_l = feeding ? *l : 0.0f;
    float in_r = feeding ? *r : 0.0f;
    delay_write(d,
        in_l + dr * fb * cross + dl * fb * (1.0f - cross),
        in_r + dl * fb * cross + dr * fb * (1.0f - cross));

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

static void process_tape_echo_throw(pfx_slot_t *s, float *l, float *r, int feeding) {
    delay_t *d = &s->delay;
    float age = s->params[0];
    float wow = s->params[1];
    float base_fb = s->params[2] * 0.75f;
    float mix = 0.7f;

    float fb = base_fb + s->pressure * (0.9f - base_fb);

    /* Wow/flutter modulation */
    s->phase += 0.5f / PFX_SAMPLE_RATE;
    if (s->phase >= 1.0f) s->phase -= 1.0f;
    float mod = sinf(s->phase * 2.0f * M_PI) * wow * 200.0f;

    float delay_samples = 2000.0f + age * (float)(d->length - 4000) + mod;
    delay_samples = clampf(delay_samples, 100.0f, (float)(d->length - 2));

    float dl, dr;
    delay_read_interp(d, delay_samples, &dl, &dr);

    float f_coeff = 1.0f - age * 0.7f;
    d->fb_lp_l += f_coeff * (dl - d->fb_lp_l);
    d->fb_lp_r += f_coeff * (dr - d->fb_lp_r);

    float sat_l = fast_tanh(d->fb_lp_l * (1.0f + age));
    float sat_r = fast_tanh(d->fb_lp_r * (1.0f + age));

    if (feeding)
        delay_write(d, *l + sat_l * fb, *r + sat_r * fb);
    else
        delay_write(d, sat_l * fb, sat_r * fb);

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

static void process_echo_freeze(pfx_slot_t *s, float *l, float *r, int feeding) {
    delay_t *d = &s->delay;
    float time = s->params[0];
    float mix = 0.8f;

    int delay_samples = 256 + (int)(time * (d->length - 256));
    float dl, dr;
    delay_read(d, delay_samples, &dl, &dr);

    if (feeding && !s->echo_frozen) {
        /* Feed audio in while held */
        delay_write(d, *l + dl * 0.95f, *r + dr * 0.95f);
    } else {
        /* Frozen: high feedback loop, no new input */
        delay_write(d, dl * 0.99f, dr * 0.99f);
    }

    /* Pressure freezes the echo */
    s->echo_frozen = (s->pressure > 0.5f) ? 1 : 0;

    *l = *l * (1.0f - mix) + dl * mix;
    *r = *r * (1.0f - mix) + dr * mix;
}

/* Reverb processing. Pressure -> decay time */
static void process_reverb_throw(pfx_slot_t *s, float *l, float *r,
                                  int slot, int feeding) {
    reverb_t *rv = s->reverb;
    if (!rv) return;

    float base_decay = 0.5f + s->params[0] * 0.4f;
    float damping = s->params[1];
    float mod = s->params[2];

    /* Pressure -> decay time */
    float decay = base_decay + s->pressure * (0.98f - base_decay);

    switch (slot) {
        case FX_REVERB:
            damping *= 0.5f;
            break;
        case FX_SHIMMER:
            damping *= 0.3f;
            decay *= 1.1f;
            break;
        case FX_DARK_VERB:
            damping = 0.4f + damping * 0.5f;
            decay *= 1.1f;
            break;
        case FX_SPRING:
            damping *= 0.3f;
            break;
    }
    if (decay > 0.98f) decay = 0.98f;

    rv->decay = decay;
    rv->damping = damping;

    float in_l = feeding ? *l : 0.0f;
    float in_r = feeding ? *r : 0.0f;

    /* Shimmer: pitch-shift feedback */
    if (slot == FX_SHIMMER && mod > 0.0f) {
        float pitch_rate = 1.0f + mod;
        int pos0 = ((int)rv->shimmer_read_pos) & 4095;
        int pos1 = (pos0 + 1) & 4095;
        float frac = rv->shimmer_read_pos - (int)rv->shimmer_read_pos;
        float pitched = rv->shimmer_buf[pos0] + frac * (rv->shimmer_buf[pos1] - rv->shimmer_buf[pos0]);
        rv->shimmer_read_pos += pitch_rate;
        if (rv->shimmer_read_pos >= 4096.0f) rv->shimmer_read_pos -= 4096.0f;

        float shimmer_mix = mod * 0.4f;
        in_l += pitched * shimmer_mix;
        in_r += pitched * shimmer_mix;
    }

    float wet_l, wet_r;
    reverb_process_stereo(rv, in_l, in_r, &wet_l, &wet_r);

    if (slot == FX_SHIMMER) {
        rv->shimmer_buf[rv->shimmer_write_pos] = (wet_l + wet_r) * 0.5f * decay * 0.3f;
        rv->shimmer_write_pos = (rv->shimmer_write_pos + 1) & 4095;
    }

    float mix = 0.7f;
    *l = *l * (1.0f - mix) + wet_l * mix;
    *r = *r * (1.0f - mix) + wet_r * mix;
}

/* ============================================================
 * Row 1: Distortion & Rhythm FX (slots 24-31)
 * ============================================================ */

/* Pressure -> bit depth (harder = fewer bits) */
static void process_bitcrush(pfx_slot_t *s, float *l, float *r) {
    /* Zero pressure = 8 bits (dramatic), full pressure = 1 bit */
    float bits = 8.0f - s->pressure * 7.0f;
    if (bits < 1.0f) bits = 1.0f;
    float levels = powf(2.0f, bits);
    *l = roundf(*l * levels) / levels;
    *r = roundf(*r * levels) / levels;
}

/* Pressure -> sample rate reduction */
static void process_downsample(pfx_slot_t *s, float *l, float *r) {
    /* Zero pressure = period 8 (noticeable), full pressure = period 64 (extreme) */
    int period = 8 + (int)(s->pressure * 56.0f);

    s->crush_count++;
    if (s->crush_count >= (unsigned int)period) {
        s->crush_count = 0;
        s->crush_hold_l = *l;
        s->crush_hold_r = *r;
    }
    *l = s->crush_hold_l;
    *r = s->crush_hold_r;
}

/* Pressure -> deceleration speed */
static void process_tape_stop(pfx_slot_t *s, float *l, float *r) {
    tape_stop_t *t = &s->tape;

    t->buf_l[t->write_pos] = *l;
    t->buf_r[t->write_pos] = *r;
    t->write_pos = (t->write_pos + 1) % t->buf_len;

    /* Pressure controls deceleration: more pressure = faster stop */
    float decel = 0.00005f + s->pressure * 0.0005f;
    t->speed -= decel;
    if (t->speed < 0.0f) t->speed = 0.0f;

    if (t->speed > 0.01f) {
        int pos0 = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos0];
        *r = t->buf_r[pos0];
        t->read_pos += t->speed;
        if (t->read_pos >= (float)t->buf_len)
            t->read_pos -= (float)t->buf_len;
    } else {
        int pos = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos] * 0.98f;
        *r = t->buf_r[pos] * 0.98f;
    }
}

/* Vinyl brake: same as tape stop but slower with spindown character */
static void process_vinyl_brake(pfx_slot_t *s, float *l, float *r) {
    tape_stop_t *t = &s->tape;

    t->buf_l[t->write_pos] = *l;
    t->buf_r[t->write_pos] = *r;
    t->write_pos = (t->write_pos + 1) % t->buf_len;

    /* Slower, more gradual stop. Pressure controls speed. */
    float decel = 0.00002f + s->pressure * 0.0002f;
    t->speed -= decel;
    if (t->speed < 0.0f) t->speed = 0.0f;

    if (t->speed > 0.01f) {
        int pos0 = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos0];
        *r = t->buf_r[pos0];
        t->read_pos += t->speed;
        if (t->read_pos >= (float)t->buf_len)
            t->read_pos -= (float)t->buf_len;
    } else {
        int pos = ((int)t->read_pos) % t->buf_len;
        *l = t->buf_l[pos] * 0.97f;
        *r = t->buf_r[pos] * 0.97f;
    }

    /* Add noise at low speeds for vinyl character */
    if (t->speed < 0.3f) {
        unsigned int seed = (unsigned int)(t->read_pos * 1000.0f);
        float noise = white_noise(&seed) * (0.3f - t->speed) * 0.08f;
        *l += noise;
        *r += noise;
    }
}

/* Pressure -> drive amount */
static void process_saturate(pfx_slot_t *s, float *l, float *r) {
    /* Zero pressure = moderate drive (3x), full pressure = heavy drive (20x) */
    float drive = 3.0f + s->pressure * 17.0f;
    float tone = s->params[0];

    float dry_l = *l, dry_r = *r;

    *l = fast_tanh(*l * drive);
    *r = fast_tanh(*r * drive);

    /* Tone filter */
    if (tone < 0.95f) {
        float f = cutoff_to_f(0.3f + tone * 0.65f);
        float fl, fr;
        svf_process(&s->sat_filter_l, *l, f, 0.5f, &fl, NULL, NULL);
        svf_process(&s->sat_filter_r, *r, f, 0.5f, &fr, NULL, NULL);
        *l = fl; *r = fr;
    }

    /* 70% wet for dramatic effect on tap */
    *l = dry_l * 0.3f + *l * 0.7f;
    *r = dry_r * 0.3f + *r * 0.7f;
}

/* Pressure -> gate depth */
static void process_gate_duck(pfx_slot_t *s, float *l, float *r,
                               perf_fx_engine_t *e) {
    ducker_t *dk = &s->ducker;
    /* Zero pressure = moderate depth (0.5), full pressure = full depth */
    float depth = 0.5f + s->pressure * 0.5f;

    /* Rate from params[0] */
    float rate_param = s->params[0];
    float div;
    if (rate_param < 0.25f) div = 1.0f;       /* 1/4 */
    else if (rate_param < 0.5f) div = 0.5f;    /* 1/8 */
    else if (rate_param < 0.75f) div = 0.25f;  /* 1/16 */
    else div = 0.125f;                          /* 1/32 */

    float samples_per_beat = (60.0f / e->bpm) * PFX_SAMPLE_RATE * div;
    float phase_inc = 1.0f / samples_per_beat;
    dk->phase += phase_inc;
    if (dk->phase >= 1.0f) dk->phase -= 1.0f;

    /* Half-cosine pump curve */
    float target_gain = 1.0f - depth * (0.5f + 0.5f * cosf(dk->phase * 2.0f * M_PI));

    /* Smooth with attack/release */
    float coeff = (target_gain < dk->env) ? 0.99f : 0.995f;
    dk->env = coeff * dk->env + (1.0f - coeff) * target_gain;

    *l *= dk->env;
    *r *= dk->env;
}

/* Pressure -> LFO speed for tremolo */
static void process_tremolo(pfx_slot_t *s, float *l, float *r) {
    /* Base rate 2 Hz, pressure goes up to 20 Hz */
    float rate = 2.0f + s->pressure * 18.0f;
    float depth = 0.5f + s->params[0] * 0.5f;

    s->trem_lfo_phase += rate / PFX_SAMPLE_RATE;
    if (s->trem_lfo_phase >= 1.0f) s->trem_lfo_phase -= 1.0f;

    float lfo = sinf(s->trem_lfo_phase * 2.0f * M_PI);
    float gain = 1.0f - depth * (0.5f + 0.5f * lfo);

    *l *= gain;
    *r *= gain;
}

/* Pressure -> chorus depth */
static void process_chorus_fx(pfx_slot_t *s, float *l, float *r) {
    mod_delay_t *md = &s->mod_delay;
    if (!md->buf_l) return;

    float rate = 0.5f + s->params[0] * 3.0f;
    /* Zero pressure = moderate depth, full pressure = deep chorus */
    float depth = (0.002f + s->pressure * 0.008f) * PFX_SAMPLE_RATE;
    float fb = s->params[1] * 0.5f;

    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    float lfo = sinf(md->lfo_phase * 2.0f * M_PI);

    /* Write with feedback */
    int wp = md->write_pos;
    float delay_l = 300.0f + depth * lfo;
    float delay_r = 300.0f + depth * sinf((md->lfo_phase + 0.25f) * 2.0f * M_PI);
    delay_l = clampf(delay_l, 1.0f, (float)(md->buf_len - 2));
    delay_r = clampf(delay_r, 1.0f, (float)(md->buf_len - 2));

    int fb_pos_l = (wp - (int)delay_l + md->buf_len) % md->buf_len;
    int fb_pos_r = (wp - (int)delay_r + md->buf_len) % md->buf_len;
    md->buf_l[wp] = *l + md->buf_l[fb_pos_l] * fb;
    md->buf_r[wp] = *r + md->buf_r[fb_pos_r] * fb;
    md->write_pos = (wp + 1) % md->buf_len;

    int pos_l = (md->write_pos - (int)delay_l + md->buf_len) % md->buf_len;
    int pos_r = (md->write_pos - (int)delay_r + md->buf_len) % md->buf_len;
    float wet_l = md->buf_l[pos_l];
    float wet_r = md->buf_r[pos_r];

    *l = *l * 0.5f + wet_l * 0.5f;
    *r = *r * 0.5f + wet_r * 0.5f;
}

/* ============================================================
 * Process all active FX for one sample
 * ============================================================ */

static void process_slot(perf_fx_engine_t *e, int slot, float *l, float *r,
                          int feeding) {
    pfx_slot_t *s = &e->slots[slot];

    switch (slot) {
        /* Row 4: Time/Repeat */
        case FX_RPT_1_4:
        case FX_RPT_1_8:
        case FX_RPT_1_16:
        case FX_RPT_TRIP:
            process_beat_repeat(s, slot, l, r, e);
            break;
        case FX_STUTTER:
            process_stutter(s, l, r);
            break;
        case FX_SCATTER:
            process_scatter(s, l, r, e);
            break;
        case FX_REVERSE:
            process_reverse(s, l, r);
            break;
        case FX_HALF_SPEED:
            process_half_speed(s, l, r);
            break;

        /* Row 3: Filter Sweeps */
        case FX_LP_SWEEP_DOWN:
            advance_filter_phase(s, slot);
            process_lp_sweep_down(s, l, r);
            break;
        case FX_HP_SWEEP_UP:
            advance_filter_phase(s, slot);
            process_hp_sweep_up(s, l, r);
            break;
        case FX_BP_RISE:
            advance_filter_phase(s, slot);
            process_bp_rise(s, l, r);
            break;
        case FX_BP_FALL:
            advance_filter_phase(s, slot);
            process_bp_fall(s, l, r);
            break;
        case FX_RESO_SWEEP:
            advance_filter_phase(s, slot);
            process_reso_sweep(s, l, r);
            break;
        case FX_PHASER:
            advance_filter_phase(s, slot);
            process_phaser_fx(s, l, r);
            break;
        case FX_FLANGER:
            advance_filter_phase(s, slot);
            process_flanger_fx(s, l, r);
            break;
        case FX_AUTO_FILTER:
            advance_filter_phase(s, slot);
            process_auto_filter(s, l, r);
            break;

        /* Row 2: Space Throws */
        case FX_DELAY:
            process_delay_throw(s, l, r, feeding);
            break;
        case FX_PING_PONG:
            process_ping_pong_throw(s, l, r, feeding);
            break;
        case FX_TAPE_ECHO:
            process_tape_echo_throw(s, l, r, feeding);
            break;
        case FX_ECHO_FREEZE:
            process_echo_freeze(s, l, r, feeding);
            break;
        case FX_REVERB:
        case FX_SHIMMER:
        case FX_DARK_VERB:
        case FX_SPRING:
            process_reverb_throw(s, l, r, slot, feeding);
            break;

        /* Row 1: Distortion & Rhythm */
        case FX_BITCRUSH:
            process_bitcrush(s, l, r);
            break;
        case FX_DOWNSAMPLE:
            process_downsample(s, l, r);
            break;
        case FX_TAPE_STOP:
            process_tape_stop(s, l, r);
            break;
        case FX_VINYL_BRAKE:
            process_vinyl_brake(s, l, r);
            break;
        case FX_SATURATE:
            process_saturate(s, l, r);
            break;
        case FX_GATE_DUCK:
            process_gate_duck(s, l, r, e);
            break;
        case FX_TREMOLO:
            process_tremolo(s, l, r);
            break;
        case FX_CHORUS:
            process_chorus_fx(s, l, r);
            break;
    }
}

static void process_all_slots(perf_fx_engine_t *e, float *l, float *r) {
    for (int i = 0; i < PFX_NUM_FX; i++) {
        pfx_slot_t *s = &e->slots[i];

        int is_active = s->active || s->fading_out;
        int is_tail = s->tail_active;

        if (!is_active && !is_tail) continue;

        float dry_l = *l;
        float dry_r = *r;

        /* Space FX: feeding = active (held or latched), tail = decaying */
        int feeding = s->active;
        process_slot(e, i, l, r, feeding);

        /* Apply fade-out crossfade for non-space FX */
        if (s->fading_out) {
            float fade = 1.0f - (float)s->fade_pos / (float)s->fade_len;
            *l = dry_l * (1.0f - fade) + *l * fade;
            *r = dry_r * (1.0f - fade) + *r * fade;
            s->fade_pos++;
            if (s->fade_pos >= s->fade_len) {
                s->active = 0;
                s->fading_out = 0;
            }
        }

        /* Check tail silence for space FX */
        if (is_tail && !is_active) {
            float max_out = fabsf(*l - dry_l);
            float max_out_r = fabsf(*r - dry_r);
            if (max_out_r > max_out) max_out = max_out_r;
            if (max_out < PFX_TAIL_THRESHOLD) {
                s->tail_silence_count++;
                if (s->tail_silence_count >= PFX_TAIL_SILENCE_FRAMES) {
                    s->tail_active = 0;
                }
            } else {
                s->tail_silence_count = 0;
            }
        }
    }
}

/* ============================================================
 * Main render
 * ============================================================ */

void pfx_engine_render(perf_fx_engine_t *e, int16_t *out_lr, int frames) {
    /* Read input audio */
    int16_t *audio_src = NULL;
    int use_track_mix = 0;

    if (e->direct_input) {
        audio_src = e->direct_input;
    } else if (e->audio_source == SOURCE_TRACKS && e->track_audio_valid && e->track_mask) {
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

        /* Save dry signal */
        e->dry_l[i] = e->work_l[i];
        e->dry_r[i] = e->work_r[i];
    }

    /* Step FX sequencer */
    if (e->step_seq_active && e->transport_running) {
        float samples_per_div;
        switch (e->step_seq_division) {
            case 0: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE; break;
            case 1: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 0.5f; break;
            case 2: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 0.25f; break;
            case 3: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE * 4.0f; break;
            default: samples_per_div = (60.0f / e->bpm) * PFX_SAMPLE_RATE; break;
        }
        e->step_seq_phase += (float)frames / samples_per_div;
        if (e->step_seq_phase >= 1.0f) {
            e->step_seq_phase -= 1.0f;
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

    /* Per-sample processing */
    for (int i = 0; i < frames; i++) {
        float l = e->work_l[i];
        float r = e->work_r[i];

        /* Update capture buffer */
        e->capture_buf_l[e->capture_write_pos] = l;
        e->capture_buf_r[e->capture_write_pos] = r;
        e->capture_write_pos = (e->capture_write_pos + 1) % e->capture_len;

        if (!e->bypassed) {
            /* Process all active FX slots */
            process_all_slots(e, &l, &r);

            /* Global HP filter (E5) */
            if (e->global_hpf > 0.01f) {
                float f = cutoff_to_f(e->global_hpf);
                float hp_l, hp_r;
                svf_process(&e->global_hp_l, l, f, 0.4f, NULL, &hp_l, NULL);
                svf_process(&e->global_hp_r, r, f, 0.4f, NULL, &hp_r, NULL);
                l = hp_l;
                r = hp_r;
            }

            /* Global LP filter (E6) */
            if (e->global_lpf < 0.99f) {
                float f = cutoff_to_f(e->global_lpf);
                float lp_l, lp_r;
                svf_process(&e->global_lp_l, l, f, 0.4f, &lp_l, NULL, NULL);
                svf_process(&e->global_lp_r, r, f, 0.4f, &lp_r, NULL, NULL);
                l = lp_l;
                r = lp_r;
            }

            /* EQ bump (E8): bandpass peak sweep */
            if (e->eq_bump_freq != 0.5f) {
                float bump_offset = (e->eq_bump_freq - 0.5f) * 2.0f; /* -1..1 */
                float bump_cutoff = 0.3f + e->eq_bump_freq * 0.5f;
                float f = cutoff_to_f(bump_cutoff);
                float bp_l, bp_r;
                svf_process(&e->eq_bump_l, l, f, 0.15f, NULL, NULL, &bp_l);
                svf_process(&e->eq_bump_r, r, f, 0.15f, NULL, NULL, &bp_r);
                l += bp_l * bump_offset * 0.5f;
                r += bp_r * bump_offset * 0.5f;
            }

            /* Dry/wet mix (E7) */
            l = e->dry_l[i] * (1.0f - e->dry_wet) + l * e->dry_wet;
            r = e->dry_r[i] * (1.0f - e->dry_wet) + r * e->dry_wet;
        }

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
 * State serialization (JSON)
 * ============================================================ */

int pfx_serialize_state(perf_fx_engine_t *e, char *buf, int buf_len) {
    int n = 0;
    SAFE_SNPRINTF(buf, n, buf_len, "{\"bpm\":%.1f", e->bpm);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"global_hpf\":%.3f", e->global_hpf);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"global_lpf\":%.3f", e->global_lpf);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"dry_wet\":%.3f", e->dry_wet);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"eq_bump_freq\":%.3f", e->eq_bump_freq);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"pressure_curve\":%d", e->pressure_curve);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"audio_source\":%d", e->audio_source);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"track_mask\":%d", e->track_mask);
    SAFE_SNPRINTF(buf, n, buf_len, ",\"last_touched\":%d", e->last_touched_slot);

    /* FX slots state */
    SAFE_SNPRINTF(buf, n, buf_len, ",\"slots\":[");
    for (int i = 0; i < PFX_NUM_FX; i++) {
        pfx_slot_t *s = &e->slots[i];
        if (i > 0) SAFE_SNPRINTF(buf, n, buf_len, ",");
        SAFE_SNPRINTF(buf, n, buf_len, "{\"a\":%d,\"l\":%d,\"t\":%d,\"p\":[",
                      s->active || s->tail_active, s->latched,
                      s->tail_active);
        for (int j = 0; j < PFX_SLOT_PARAMS; j++) {
            if (j > 0) SAFE_SNPRINTF(buf, n, buf_len, ",");
            SAFE_SNPRINTF(buf, n, buf_len, "%.3f", s->params[j]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]}");
    }
    SAFE_SNPRINTF(buf, n, buf_len, "]}");
    return n;
}
