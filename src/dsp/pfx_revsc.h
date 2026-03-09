/*
 * pfx_revsc.h - Costello/Soundpipe-style stereo reverb (revsc)
 *
 * 8-delay FDN reverb based on Sean Costello's design.
 * Originally from Csound's reverbsc opcode / Soundpipe library.
 * MIT licensed algorithm, clean reimplementation for PFX.
 *
 * Parameters:
 *   feedback: 0.0 - 1.0 (reverb time / decay)
 *   lpfreq:   lowpass cutoff in Hz (damping)
 */

#ifndef PFX_REVSC_H
#define PFX_REVSC_H

#define REVSC_NUM_DELAYS 8
#define REVSC_MAX_DELAY  8192  /* max samples per delay line */

typedef struct {
    /* Delay lines */
    float buf[REVSC_NUM_DELAYS][REVSC_MAX_DELAY];
    int   pos[REVSC_NUM_DELAYS];
    int   len[REVSC_NUM_DELAYS];

    /* Lowpass filter state (one per delay) */
    float filt[REVSC_NUM_DELAYS];

    /* Parameters */
    float feedback;
    float lpfreq;
    float sample_rate;

    /* Jitter LFO */
    float jitter_phase;
} pfx_revsc_t;

static inline void pfx_revsc_init(pfx_revsc_t *rv, float sample_rate) {
    /* Prime-number delay lengths for maximum density (scaled for 44100 Hz) */
    static const int base_lens[REVSC_NUM_DELAYS] = {
        2473, 2767, 3217, 3557, 3907, 4127, 4507, 4877
    };

    for (int i = 0; i < REVSC_NUM_DELAYS; i++) {
        rv->len[i] = base_lens[i];
        rv->pos[i] = 0;
        rv->filt[i] = 0.0f;
        for (int j = 0; j < REVSC_MAX_DELAY; j++)
            rv->buf[i][j] = 0.0f;
    }

    rv->feedback = 0.8f;
    rv->lpfreq = 10000.0f;
    rv->sample_rate = sample_rate;
    rv->jitter_phase = 0.0f;
}

static inline void pfx_revsc_process(pfx_revsc_t *rv,
                                      float in_l, float in_r,
                                      float *out_l, float *out_r) {
    float fb = rv->feedback;
    if (fb > 0.98f) fb = 0.98f;

    /* One-pole LP coefficient from cutoff frequency */
    float w = 2.0f * 3.14159f * rv->lpfreq / rv->sample_rate;
    if (w > 1.0f) w = 1.0f;
    float lp_coeff = w / (1.0f + w);  /* bilinear approx */

    /* Read from all 8 delay lines and compute Householder-style mixing */
    float tap[REVSC_NUM_DELAYS];
    for (int i = 0; i < REVSC_NUM_DELAYS; i++) {
        tap[i] = rv->buf[i][rv->pos[i]];
    }

    /* FDN mixing matrix: sum all taps, subtract 2x each tap (Householder) */
    float sum = 0.0f;
    for (int i = 0; i < REVSC_NUM_DELAYS; i++)
        sum += tap[i];
    float scale = 2.0f / REVSC_NUM_DELAYS;

    /* Input injection: L into even delays, R into odd */
    float input[REVSC_NUM_DELAYS];
    for (int i = 0; i < REVSC_NUM_DELAYS; i++) {
        float mixed = sum * scale - tap[i];
        float in_sig = (i & 1) ? in_r : in_l;
        input[i] = in_sig + mixed * fb;
    }

    /* Write back through lowpass filter */
    for (int i = 0; i < REVSC_NUM_DELAYS; i++) {
        /* One-pole lowpass */
        rv->filt[i] += lp_coeff * (input[i] - rv->filt[i]);
        rv->buf[i][rv->pos[i]] = rv->filt[i];
        rv->pos[i] = (rv->pos[i] + 1) % rv->len[i];
    }

    /* Output: sum even taps for L, odd for R */
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < REVSC_NUM_DELAYS; i++) {
        if (i & 1) r += tap[i];
        else       l += tap[i];
    }
    *out_l = l * 0.25f;  /* 4 taps per channel, scale down */
    *out_r = r * 0.25f;
}

#endif /* PFX_REVSC_H */
