# Performance FX DSP Quality & FX Additions Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix all DSP quality issues identified in the performer workflow review and add missing effects (auto-filter, tremolo/autopan, continuous ducker, pitch shift).

**Architecture:** All changes are in the DSP engine (`perf_fx_dsp.c`/`.h`), plugin param routing (`perf_fx_plugin.c`), UI (`ui.js`), and tests (`test_perf_fx.c`). Effects are pure C with no external dependencies. The continuous FX grid has 16 slots (2 rows of 8), all of which are currently filled. We'll replace 4 underperforming effects to make room for the new ones: replace CLOUD_DELAY with AUTO_FILTER, replace LOFI with TREMOLO_PAN, replace RING_MOD with PITCH_SHIFT, and replace FREEZE with CONT_DUCKER. (Lo-fi, ring mod, and freeze are niche; auto-filter, tremolo, pitch shift, and ducker are essential performance tools. Cloud delay is the weakest of the 4 delays.)

**Tech Stack:** C (DSP), JavaScript (UI), ARM64 cross-compilation via Docker

**Key files:**
- `src/dsp/perf_fx_dsp.h` — types, enums, struct definitions
- `src/dsp/perf_fx_dsp.c` — all DSP processing
- `src/dsp/perf_fx_plugin.c` — parameter routing, names
- `src/ui.js` — pad labels, colors, knob labels
- `src/dsp/test_perf_fx.c` — unit tests

**Test command:** `cc -o test_pfx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -Isrc/dsp -lm && ./test_pfx`

**Build command:** `./scripts/build.sh`

---

## Task 1: Punch-in Release Crossfade

The biggest usability bug. When a performer releases a punch-in pad (beat repeat, filter, etc.), audio cuts instantly back to dry, causing clicks. Add a short crossfade (256 samples ≈ 5.8ms) on deactivation.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h` — add fade fields to `punch_in_t`
- Modify: `src/dsp/perf_fx_dsp.c:342-413` — activate/deactivate, `process_punch_ins`
- Modify: `src/dsp/test_perf_fx.c` — add crossfade test

**Step 1: Add fade state to punch_in_t in perf_fx_dsp.h**

In `punch_in_t` struct, after `unsigned int scatter_seed;`, add:

```c
    int fading_out;          /* 1 = crossfading to dry */
    int fade_pos;            /* current position in fade */
    int fade_len;            /* total fade length in samples */
```

**Step 2: Implement crossfade in perf_fx_dsp.c**

In `pfx_punch_deactivate()`, instead of immediately setting `active = 0`:

```c
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
```

In `process_punch_ins()`, modify the loop to handle fading:

```c
static void process_punch_ins(perf_fx_engine_t *e, float *l, float *r) {
    for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
        punch_in_t *p = &e->punch[i];
        if (!p->active && !p->fading_out) continue;

        float dry_l = *l, dry_r = *r;

        switch (i) {
            /* ... existing switch cases unchanged ... */
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
```

Also update `pfx_punch_activate()` to cancel any pending fade:

```c
    p->fading_out = 0;  /* cancel any pending fade-out */
```

**Step 3: Add test**

```c
static int test_punch_crossfade(void) {
    perf_fx_engine_t e;
    pfx_engine_init(&e);
    pfx_punch_activate(&e, 0, 1.0f);
    ASSERT(e.punch[0].active == 1);
    pfx_punch_deactivate(&e, 0);
    /* Should be fading, not immediately off */
    ASSERT(e.punch[0].active == 1 || e.punch[0].fading_out == 1);
    ASSERT(e.punch[0].fading_out == 1);
    ASSERT(e.punch[0].fade_len == 256);
    pfx_engine_destroy(&e);
    return 1;
}
```

**Step 4:** Run `cc -o test_pfx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -Isrc/dsp -lm && ./test_pfx` — all tests pass.

**Step 5:** Commit: `git commit -am "feat: add punch-in release crossfade (256 samples)"`

---

## Task 2: Fix Scene Recall Resetting Already-Active FX

When recalling a scene that has the same FX active as the current state, `pfx_cont_activate()` resets the delay buffers, causing an audible gap. Fix: only call activate for FX that weren't already active.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.c:551-569` — `pfx_scene_recall`
- Modify: `src/dsp/perf_fx_dsp.c:588-604` — `pfx_step_recall`
- Modify: `src/dsp/test_perf_fx.c` — add test

**Step 1: Fix pfx_scene_recall**

The current code already checks `if (s->cont_active[i] && !e->cont[i].active)` before calling `pfx_cont_activate`. This looks correct. BUT — after activate, params are set via memcpy. The issue is that `pfx_cont_activate` resets params to defaults (line 287-289 in init, and activate calls `delay_reset`, `reverb_init`, etc.). The memcpy then overwrites params but the **DSP state** (delay buffer, reverb state) is already zeroed.

The fix: for FX that are *already active in both states*, just update params without resetting DSP state:

```c
void pfx_scene_recall(perf_fx_engine_t *e, int slot) {
    if (slot < 0 || slot >= PFX_NUM_SCENES) return;
    scene_t *s = &e->scenes[slot];
    if (!s->populated) return;

    for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
        if (s->cont_active[i] && !e->cont[i].active) {
            pfx_cont_activate(e, i);
        } else if (!s->cont_active[i] && e->cont[i].active) {
            e->cont[i].active = 0;
        }
        /* Params always updated (works for both newly-activated and already-active) */
        memcpy(e->cont[i].params, s->cont_params[i], sizeof(float) * PFX_CONT_PARAMS);
    }
    update_active_cont_list(e);
    restore_global_params(e, s->global_params);
}
```

This is actually the same code — the fix is already correct! The issue was already handled. Let me re-examine... Actually the problem is that `pfx_cont_activate` calls `delay_reset(&c->delay)` and `reverb_init(&c->reverb)` etc., which zeroes the delay/reverb buffers. For *already-active* FX (both old and new scene have it active), activate is NOT called, so buffers are preserved. The code is already correct.

Wait — the *step_recall* has the same pattern and IS correct too. Let me verify there's no actual bug by checking if there's a case where it breaks... The only bug would be if `pfx_cont_activate` is called when the FX is already active, which the `!e->cont[i].active` guard prevents. **This task is already fixed.** Skip it.

---

## Task 2 (revised): Wire Up Saturator Curve Parameter

The saturator's `params[2]` ("Curve") is read but never used. It should control the saturation shape.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.c:1287-1308` — `process_cont_saturator`

**Step 1: Implement curve parameter**

Replace the current `process_cont_saturator`:

```c
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
```

**Step 2:** Run tests, build. Commit: `git commit -am "feat: wire up saturator curve param (tanh/clip/foldback)"`

---

## Task 3: Stereo Reverb

All 4 reverbs are mono (collapse L+R, process, output same signal to both channels). Fix: use different comb lengths for L and R channels to create stereo width.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h` — add second reverb for stereo
- Modify: `src/dsp/perf_fx_dsp.c:143-237` — `reverb_init`, `reverb_process_mono`
- Modify: `src/dsp/perf_fx_dsp.c:1042-1090` — `process_cont_reverb`

**Step 1: Add stereo reverb processing**

Rather than doubling the reverb struct (too much memory on ARM), use offset comb reads for the right channel. Add to `reverb_t`:

```c
    int comb_pos_r[4];     /* separate read positions for right channel */
    float comb_filt_r[4];
```

In `reverb_init`, set right-channel comb positions with offset from left:

```c
    /* Offset right channel comb positions for stereo decorrelation */
    for (int i = 0; i < 4; i++) {
        rv->comb_pos_r[i] = rv->comb_len[i] / 3; /* offset by 1/3 */
        rv->comb_filt_r[i] = 0.0f;
    }
```

Add `reverb_process_stereo`:

```c
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
        /* Don't write to buffer again — share with left channel */
        rv->comb_pos_r[i] = (pos_r + 1) % rv->comb_len[i];
        right += del_r;
    }
    left *= 0.25f;
    right *= 0.25f;

    /* Allpass filters (shared, applied to both) */
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
```

Update `process_cont_reverb` to use stereo version:

```c
    float wet_l, wet_r;
    reverb_process_stereo(rv, *l, *r, &wet_l, &wet_r);

    /* Shimmer modulation (apply to both channels) */
    if (reverb_type == CONT_SHIMMER_REVERB && mod > 0.0f) {
        c->phaser.lfo_phase += (1.0f + mod * 3.0f) / PFX_SAMPLE_RATE;
        if (c->phaser.lfo_phase >= 1.0f) c->phaser.lfo_phase -= 1.0f;
        float shimmer = sinf(c->phaser.lfo_phase * 2.0f * M_PI) * mod * 0.3f;
        wet_l += wet_l * shimmer;
        wet_r -= wet_r * shimmer; /* opposite phase for width */
    }

    *l = *l * (1.0f - mix) + wet_l * mix;
    *r = *r * (1.0f - mix) + wet_r * mix;
```

**Step 2:** Run tests, build. Commit: `git commit -am "feat: stereo reverb with L/R comb offset decorrelation"`

---

## Task 4: Improved Shimmer Reverb

Real shimmer uses pitch-shifted feedback. Approximate with a small pitch-shift buffer in the reverb feedback path.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h` — add pitch shift state to `reverb_t`
- Modify: `src/dsp/perf_fx_dsp.c` — shimmer feedback processing

**Step 1: Add shimmer buffer to reverb_t**

```c
    /* Shimmer pitch shift state */
    float shimmer_buf[4096];
    int shimmer_write_pos;
    float shimmer_read_pos;
```

**Step 2: Implement shimmer pitch shift in reverb feedback**

In `process_cont_reverb`, for CONT_SHIMMER_REVERB, before the main reverb processing, pitch-shift the input into the reverb:

```c
    if (reverb_type == CONT_SHIMMER_REVERB) {
        /* Pitch shift by octave up in feedback path */
        float pitch_rate = 1.0f + mod; /* 1.0 = octave up at mod=1 */
        rv->shimmer_buf[rv->shimmer_write_pos] = (wet_l + wet_r) * 0.5f * decay * 0.3f;
        rv->shimmer_write_pos = (rv->shimmer_write_pos + 1) % 4096;

        /* Read at pitch-shifted rate */
        int pos0 = ((int)rv->shimmer_read_pos) % 4096;
        int pos1 = (pos0 + 1) % 4096;
        float frac = rv->shimmer_read_pos - (int)rv->shimmer_read_pos;
        float pitched = rv->shimmer_buf[pos0] + frac * (rv->shimmer_buf[pos1] - rv->shimmer_buf[pos0]);
        rv->shimmer_read_pos += pitch_rate;
        if (rv->shimmer_read_pos >= 4096.0f) rv->shimmer_read_pos -= 4096.0f;

        /* Mix pitch-shifted signal back into reverb input */
        float shimmer_mix = mod * 0.4f;
        *l += pitched * shimmer_mix;
        *r += pitched * shimmer_mix;
    }
```

Move this BEFORE the `reverb_process_stereo` call so the pitched signal feeds back into the reverb.

**Step 3:** Run tests, build. Commit: `git commit -am "feat: real shimmer reverb with octave-up pitch shift feedback"`

---

## Task 5: Replace Cloud Delay → Auto-Filter

Cloud delay is the weakest delay variant. Replace slot `CONT_CLOUD_DELAY` (index 3) with Auto-Filter — an LFO-swept filter that's essential for live performance.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h:63` — rename enum `CONT_CLOUD_DELAY` to `CONT_AUTO_FILTER`
- Modify: `src/dsp/perf_fx_dsp.c` — remove `process_cont_cloud_delay`, add `process_cont_auto_filter`
- Modify: `src/dsp/perf_fx_plugin.c:43,51` — update name and param names
- Modify: `src/ui.js` — update pad label

**Step 1: Update enum in perf_fx_dsp.h**

```c
    CONT_AUTO_FILTER,    /* was CONT_CLOUD_DELAY */
```

**Step 2: Implement auto-filter**

```c
/* Auto-filter: [0]=rate, [1]=depth, [2]=resonance, [3]=mix
 * Params [4]=shape (0=LP, 0.5=BP, 1=HP), [5]=center freq,
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

    /* LFO */
    mod_delay_t *md = &c->mod_delay; /* reuse LFO phase from mod_delay */
    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    /* LFO shape: sine, triangle, square */
    float lfo_l, lfo_r;
    float phase_l = md->lfo_phase;
    float phase_r = md->lfo_phase + stereo_offset;
    if (phase_r >= 1.0f) phase_r -= 1.0f;

    if (lfo_shape < 0.33f) {
        /* Sine */
        lfo_l = sinf(phase_l * 2.0f * M_PI);
        lfo_r = sinf(phase_r * 2.0f * M_PI);
    } else if (lfo_shape < 0.66f) {
        /* Triangle */
        lfo_l = 4.0f * fabsf(phase_l - 0.5f) - 1.0f;
        lfo_r = 4.0f * fabsf(phase_r - 0.5f) - 1.0f;
    } else {
        /* Square */
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
```

**Step 3: Update switch in process_continuous_fx**

```c
    case CONT_AUTO_FILTER:   process_cont_auto_filter(c, l, r); break;
```

**Step 4: Update plugin param names**

In `perf_fx_plugin.c`, update `CONT_NAMES[3]` to `"Auto-Filter"` and `CONT_PARAM_NAMES[3]` to `{"Rate", "Depth", "Reso", "Mix"}`.

**Step 5: Update ui.js**

Change `CONT_NAMES[3]` from `'CLOUD'` to `'AUTFLT'` (6 chars for pad display). Update `CONT_PARAM_LABELS[3]` to `['Rate', 'Depth', 'Reso', 'Mix', 'Shape', 'Center', 'LFO Shp', 'Stereo']`.

**Step 6:** Run tests, build. Commit: `git commit -am "feat: replace cloud delay with auto-filter (LFO-swept SVF)"`

---

## Task 6: Replace Lo-Fi → Tremolo/Autopan

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h:72` — rename `CONT_LOFI` to `CONT_TREMOLO_PAN`
- Modify: `src/dsp/perf_fx_dsp.c` — remove `process_cont_lofi`, add `process_cont_tremolo_pan`
- Modify: `src/dsp/perf_fx_plugin.c` — update names
- Modify: `src/ui.js` — update labels

**Step 1: Update enum**

```c
    CONT_TREMOLO_PAN,    /* was CONT_LOFI */
```

**Step 2: Implement tremolo/autopan**

```c
/* Tremolo/Pan: [0]=rate, [1]=depth, [2]=shape, [3]=mix
 * [4]=stereo mode (0=mono trem, 0.5=stereo trem, 1=autopan),
 * [5]=phase (for tempo sync feel), [6]=smooth, [7]=unused */
static void process_cont_tremolo_pan(continuous_t *c, float *l, float *r) {
    float rate = 0.1f + c->params[0] * 20.0f;
    float depth = c->params[1];
    float shape = c->params[2];
    float mix = c->params[3];
    float stereo_mode = c->params[4];

    mod_delay_t *md = &c->mod_delay;
    md->lfo_phase += rate / PFX_SAMPLE_RATE;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    /* LFO shape */
    float lfo;
    if (shape < 0.33f) {
        lfo = sinf(md->lfo_phase * 2.0f * M_PI);
    } else if (shape < 0.66f) {
        lfo = 4.0f * fabsf(md->lfo_phase - 0.5f) - 1.0f;
    } else {
        lfo = md->lfo_phase < 0.5f ? 1.0f : -1.0f;
    }

    float dry_l = *l, dry_r = *r;
    float gain_l, gain_r;

    if (stereo_mode < 0.33f) {
        /* Mono tremolo — both channels same */
        float g = 1.0f - depth * (0.5f + 0.5f * lfo);
        gain_l = gain_r = g;
    } else if (stereo_mode < 0.66f) {
        /* Stereo tremolo — slight offset */
        float lfo_r = sinf((md->lfo_phase + 0.1f) * 2.0f * M_PI);
        gain_l = 1.0f - depth * (0.5f + 0.5f * lfo);
        gain_r = 1.0f - depth * (0.5f + 0.5f * lfo_r);
    } else {
        /* Autopan — L and R are opposite phase */
        gain_l = 1.0f - depth * (0.5f + 0.5f * lfo);
        gain_r = 1.0f - depth * (0.5f - 0.5f * lfo);
    }

    *l = *l * gain_l;
    *r = *r * gain_r;

    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}
```

**Step 3: Update switch, names, UI** (same pattern as Task 5)

`CONT_NAMES[11]` = `"Trem/Pan"`, params = `{"Rate", "Depth", "Shape", "Mix"}`.
UI label: `'TRMPAN'`.

**Step 4:** Run tests, build. Commit: `git commit -am "feat: replace lo-fi with tremolo/autopan"`

---

## Task 7: Replace Ring Mod → Pitch Shift

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h:75` — rename `CONT_RING_MOD` to `CONT_PITCH_SHIFT`
- Modify: `src/dsp/perf_fx_dsp.h` — remove `ring_mod_t`, no new struct needed (reuse `mod_delay`)
- Modify: `src/dsp/perf_fx_dsp.c` — remove `process_cont_ring_mod`, add `process_cont_pitch_shift`
- Modify: `src/dsp/perf_fx_plugin.c` — update names
- Modify: `src/ui.js` — update labels

**Step 1: Update enum**

```c
    CONT_PITCH_SHIFT,    /* was CONT_RING_MOD */
```

**Step 2: Implement pitch shift using granular overlap-add**

```c
/* Pitch shift: [0]=pitch (0=octave down, 0.5=unity, 1=octave up),
 * [1]=grain size, [2]=quality/overlap, [3]=mix,
 * [4]=fine tune, [5]=feedback, [6]=tone, [7]=unused */
static void process_cont_pitch_shift(continuous_t *c, float *l, float *r) {
    mod_delay_t *md = &c->mod_delay;
    float pitch_param = c->params[0];
    float grain_sz = 0.01f + c->params[1] * 0.04f; /* 10-50ms in seconds */
    float mix = c->params[3];
    float fine = (c->params[4] - 0.5f) * 0.1f; /* +/- 5% fine tune */
    float fb = c->params[5] * 0.6f;
    float tone = c->params[6];

    /* Pitch ratio: 0.5 (octave down) to 2.0 (octave up) */
    float ratio = powf(2.0f, (pitch_param - 0.5f) * 2.0f + fine);

    int grain_len = (int)(grain_sz * PFX_SAMPLE_RATE);
    if (grain_len < 256) grain_len = 256;
    if (grain_len > md->buf_len / 2) grain_len = md->buf_len / 2;

    /* Write input + feedback to buffer */
    int wp = md->write_pos;
    int fb_pos = (wp - grain_len + md->buf_len) % md->buf_len;
    md->buf_l[wp] = *l + md->buf_l[fb_pos] * fb;
    md->buf_r[wp] = *r + md->buf_r[fb_pos] * fb;
    md->write_pos = (wp + 1) % md->buf_len;

    /* Two overlapping grains for smooth output */
    float out_l = 0.0f, out_r = 0.0f;
    for (int g = 0; g < 2; g++) {
        float phase = md->lfo_phase + (float)g * 0.5f;
        if (phase >= 1.0f) phase -= 1.0f;

        /* Read position based on pitch ratio */
        float delay = (float)grain_len * phase * ratio;
        if (delay >= (float)(md->buf_len - 1)) delay = (float)(md->buf_len - 2);
        int rp = (wp - (int)delay + md->buf_len) % md->buf_len;

        float gl = md->buf_l[rp];
        float gr = md->buf_r[rp];

        /* Hanning window */
        float env = sinf(phase * M_PI);
        out_l += gl * env;
        out_r += gr * env;
    }

    /* Advance grain phase */
    md->lfo_phase += 1.0f / (float)grain_len;
    if (md->lfo_phase >= 1.0f) md->lfo_phase -= 1.0f;

    /* Tone filter */
    if (tone < 0.95f) {
        float f = cutoff_to_f(0.3f + tone * 0.65f);
        float fl, fr;
        svf_process(&c->filter_l, out_l, f, 0.5f, &fl, NULL, NULL);
        svf_process(&c->filter_r, out_r, f, 0.5f, &fr, NULL, NULL);
        out_l = fl; out_r = fr;
    }

    float dry_l = *l, dry_r = *r;
    *l = dry_l * (1.0f - mix) + out_l * mix;
    *r = dry_r * (1.0f - mix) + out_r * mix;
}
```

**Step 3: Update switch, names, UI**

`CONT_NAMES[14]` = `"Pitch"`, params = `{"Pitch", "Grain", "Quality", "Mix"}`.
UI label: `'PITCH'`.

**Step 4:** Run tests, build. Commit: `git commit -am "feat: replace ring mod with granular pitch shift"`

---

## Task 8: Replace Freeze → Continuous Ducker

Freeze is niche. A continuous auto-sidechain ducker is essential for DJ/performance use.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h:77` — rename `CONT_FREEZE` to `CONT_DUCKER`
- Modify: `src/dsp/perf_fx_dsp.c` — remove `process_cont_freeze`, add `process_cont_ducker`
- Modify: `src/dsp/perf_fx_plugin.c` — update names
- Modify: `src/ui.js` — update labels

**Step 1: Update enum**

```c
    CONT_DUCKER,         /* was CONT_FREEZE */
```

**Step 2: Implement continuous ducker**

```c
/* Continuous Ducker: [0]=rate (beat division), [1]=depth, [2]=shape, [3]=mix
 * [4]=attack, [5]=release, [6]=swing, [7]=unused
 * Uses the engine's BPM for tempo sync. */
static void process_cont_ducker(continuous_t *c, float *l, float *r,
                                 perf_fx_engine_t *e) {
    float rate_param = c->params[0];
    float depth = c->params[1];
    float shape = c->params[2];
    float mix = c->params[3];
    float attack = 0.001f + c->params[4] * 0.05f; /* 1-50ms */
    float release_time = 0.01f + c->params[5] * 0.5f; /* 10-500ms */
    float swing = c->params[6] * 0.3f; /* 0-30% swing */

    /* Rate: 0=1bar, 0.25=1/2, 0.5=1/4, 0.75=1/8, 1.0=1/16 */
    float div;
    if (rate_param < 0.15f) div = 4.0f;       /* 1 bar */
    else if (rate_param < 0.35f) div = 2.0f;   /* 1/2 */
    else if (rate_param < 0.55f) div = 1.0f;   /* 1/4 */
    else if (rate_param < 0.75f) div = 0.5f;   /* 1/8 */
    else div = 0.25f;                           /* 1/16 */

    float samples_per_beat = (60.0f / e->bpm) * PFX_SAMPLE_RATE * div;
    float phase_inc = 1.0f / samples_per_beat;

    /* Advance phase with swing */
    c->ring.phase += phase_inc;
    if (c->ring.phase >= 1.0f) {
        c->ring.phase -= 1.0f;
    }

    /* Apply swing to even beats */
    float phase = c->ring.phase;
    if (phase > 0.5f) {
        phase = 0.5f + (phase - 0.5f) * (1.0f - swing) / (1.0f - swing * 0.5f);
    }

    /* Duck curve shape */
    float duck;
    if (shape < 0.33f) {
        /* Half-cosine (smooth) */
        duck = 0.5f + 0.5f * cosf(phase * 2.0f * M_PI);
    } else if (shape < 0.66f) {
        /* Exponential (sharp attack, slow release) */
        float norm = phase;
        duck = (norm < 0.1f) ? 1.0f - norm * 10.0f : expf(-norm * 5.0f);
    } else {
        /* Hard gate */
        duck = phase < 0.3f ? 0.0f : 1.0f;
    }

    float target_gain = 1.0f - depth * (1.0f - duck);

    /* Smooth gain changes with attack/release */
    float att_coeff = expf(-1.0f / (attack * PFX_SAMPLE_RATE));
    float rel_coeff = expf(-1.0f / (release_time * PFX_SAMPLE_RATE));
    float coeff = (target_gain < c->comp.env) ? att_coeff : rel_coeff;
    c->comp.env = coeff * c->comp.env + (1.0f - coeff) * target_gain;

    float dry_l = *l, dry_r = *r;
    *l *= c->comp.env;
    *r *= c->comp.env;

    *l = dry_l * (1.0f - mix) + *l * mix;
    *r = dry_r * (1.0f - mix) + *r * mix;
}
```

Note: This reuses `c->ring.phase` for the ducker phase and `c->comp.env` for the smoothed gain — no new struct fields needed since we removed ring mod and freeze.

**Step 3: Update switch** — Note this needs the engine pointer:

```c
    case CONT_DUCKER:        process_cont_ducker(c, l, r, e); break;
```

The `process_continuous_fx` function signature needs to add the engine pointer. Currently it's:

```c
static void process_continuous_fx(continuous_t *c, int type, float *l, float *r);
```

Change to:

```c
static void process_continuous_fx(continuous_t *c, int type, float *l, float *r,
                                   perf_fx_engine_t *e);
```

And update the call site in the render loop (line 1522):

```c
    process_continuous_fx(&e->cont[slot], slot, &l, &r, e);
```

**Step 4: Update names, UI**

`CONT_NAMES[15]` = `"Ducker"`, params = `{"Rate", "Depth", "Shape", "Mix"}`.
UI label: `'DUCKR'`.

**Step 5:** Run tests, build. Commit: `git commit -am "feat: replace freeze with continuous auto-ducker"`

---

## Task 9: Update All UI Labels and Param Names

After the 4 FX replacements, comprehensively update all UI references.

**Files:**
- Modify: `src/dsp/perf_fx_plugin.c` — `CONT_NAMES[]` and `CONT_PARAM_NAMES[]`
- Modify: `src/ui.js` — `CONT_NAMES[]`, `CONT_PARAM_LABELS[]`, pad colors if desired
- Modify: `src/help.json` — update Continuous FX section
- Modify: `manual.md` — update effect tables

**Step 1: Update perf_fx_plugin.c**

```c
static const char *CONT_NAMES[PFX_NUM_CONTINUOUS] = {
    "Delay", "Ping-Pong", "Tape Echo", "Auto-Filter",
    "Plate Verb", "Dark Verb", "Spring Verb", "Shimmer",
    "Chorus", "Phaser", "Flanger", "Trem/Pan",
    "Compressor", "Saturator", "Pitch Shift", "Ducker"
};

static const char *CONT_PARAM_NAMES[PFX_NUM_CONTINUOUS][4] = {
    {"Time", "Feedback", "Filter", "Mix"},         /* Delay */
    {"Time", "Feedback", "Spread", "Mix"},         /* Ping-Pong */
    {"Age", "Wow/Flut", "Feedback", "Mix"},        /* Tape Echo */
    {"Rate", "Depth", "Reso", "Mix"},              /* Auto-Filter */
    {"Decay", "Damping", "Pre-Dly", "Mix"},        /* Plate Reverb */
    {"Decay", "Darkness", "Mod", "Mix"},           /* Dark Reverb */
    {"Decay", "Tone", "Drip", "Mix"},              /* Spring Reverb */
    {"Decay", "Shimmer", "Mod", "Mix"},            /* Shimmer */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Chorus */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Phaser */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Flanger */
    {"Rate", "Depth", "Shape", "Mix"},             /* Trem/Pan */
    {"Thresh", "Ratio", "Attack", "Mix"},          /* Compressor */
    {"Drive", "Tone", "Curve", "Mix"},             /* Saturator */
    {"Pitch", "Grain", "Quality", "Mix"},          /* Pitch Shift */
    {"Rate", "Depth", "Shape", "Mix"}              /* Ducker */
};
```

**Step 2: Update ui.js CONT_NAMES and CONT_PARAM_LABELS arrays** to match.

**Step 3: Update help.json** — Row 2 and Row 1 effect lists.

**Step 4: Update manual.md** — effect tables.

**Step 5:** Commit: `git commit -am "docs: update all UI labels, help, and manual for new FX"`

---

## Task 10: Remove lofi_t, ring_mod_t, freeze_t Structs

Clean up dead code from the removed effects.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.h` — remove `lofi_t`, `ring_mod_t`, `freeze_t` typedefs
- Modify: `src/dsp/perf_fx_dsp.h` — remove corresponding fields from `continuous_t`
- Modify: `src/dsp/perf_fx_dsp.c` — remove `process_cont_lofi`, `process_cont_ring_mod`, `process_cont_freeze`, `process_cont_cloud_delay`
- Modify: `src/dsp/perf_fx_dsp.c` — remove init code for lofi/ring/freeze in `pfx_engine_init` and `pfx_cont_activate`

**Important:** The continuous ducker reuses `c->ring.phase` and `c->comp.env`. Since we're keeping `compressor_t` and need a phase field, either:
- Keep `ring_mod_t` in the struct (renamed to something generic), OR
- Add a `float phase` field to `continuous_t` directly

Cleanest: add `float lfo_phase2;` to `continuous_t` for the ducker, and remove `ring_mod_t` entirely. Update the ducker to use `c->lfo_phase2` instead of `c->ring.phase`.

**Step 1:** Make all struct changes, update ducker code.
**Step 2:** Run tests, build.
**Step 3:** Commit: `git commit -am "refactor: remove dead lofi/ring_mod/freeze/cloud structs and code"`

---

## Task 11: Activation-Order FX Chain

Currently continuous FX process in index order (0→15), not activation order. Fix: track activation order.

**Files:**
- Modify: `src/dsp/perf_fx_dsp.c:453-460` — `update_active_cont_list`
- Modify: `src/dsp/perf_fx_dsp.h` — add activation tracking to engine

**Step 1: Add activation counter**

In `perf_fx_engine_t`, add:

```c
    int cont_activation_order[PFX_NUM_CONTINUOUS]; /* activation sequence number per slot */
    int cont_activation_counter;                    /* monotonic counter */
```

**Step 2: Update activate/deactivate**

In `pfx_cont_activate`:
```c
    e->cont_activation_order[slot] = ++e->cont_activation_counter;
```

In `pfx_cont_deactivate`:
```c
    e->cont_activation_order[slot] = 0;
```

**Step 3: Sort active list by activation order**

```c
static void update_active_cont_list(perf_fx_engine_t *e) {
    e->active_cont_count = 0;
    for (int i = 0; i < PFX_NUM_CONTINUOUS && e->active_cont_count < PFX_MAX_CONTINUOUS; i++) {
        if (e->cont[i].active) {
            e->active_cont_slots[e->active_cont_count++] = i;
        }
    }
    /* Sort by activation order */
    for (int i = 0; i < e->active_cont_count - 1; i++) {
        for (int j = i + 1; j < e->active_cont_count; j++) {
            if (e->cont_activation_order[e->active_cont_slots[i]] >
                e->cont_activation_order[e->active_cont_slots[j]]) {
                int tmp = e->active_cont_slots[i];
                e->active_cont_slots[i] = e->active_cont_slots[j];
                e->active_cont_slots[j] = tmp;
            }
        }
    }
}
```

**Step 4:** Add test for activation order.
**Step 5:** Run tests, build. Commit: `git commit -am "feat: process continuous FX in activation order, not index order"`

---

## Task 12: Run Full Build and Final Test

**Step 1:** Run test suite: `cc -o test_pfx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -Isrc/dsp -lm && ./test_pfx`

**Step 2:** Cross-compile: `./scripts/build.sh`

**Step 3:** Verify no warnings in build output.

**Step 4:** Final commit if any cleanup needed.

---

## Summary of Changes

| Task | What | Priority |
|------|------|----------|
| 1 | Punch-in release crossfade | High |
| 2 | Saturator curve param | High (trivial) |
| 3 | Stereo reverb | Medium |
| 4 | Real shimmer pitch shift | Low |
| 5 | Cloud delay → Auto-filter | Medium |
| 6 | Lo-fi → Tremolo/autopan | Medium |
| 7 | Ring mod → Pitch shift | Low |
| 8 | Freeze → Continuous ducker | Medium |
| 9 | Update all labels/docs | Required |
| 10 | Remove dead code | Cleanup |
| 11 | Activation-order FX chain | Small |
| 12 | Final build verification | Required |

**Not addressed in this plan (future work):**
- Noise gate on input (#7 from review) — would add latency, better as a separate utility
- Pressure visual feedback (#3) — UI-only change, low priority
- Scatter musicality (#2) — needs design work
- Parallel vs serial FX routing (#6) — large architectural change, separate plan
