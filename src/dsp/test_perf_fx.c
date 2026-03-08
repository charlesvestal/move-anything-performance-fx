/*
 * Performance FX DSP Unit Tests (v2)
 *
 * Compile and run on host (macOS/Linux):
 *   gcc -o test_perf_fx src/dsp/test_perf_fx.c src/dsp/perf_fx_dsp.c -lm -I src/dsp
 *   ./test_perf_fx
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "perf_fx_dsp.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  TEST: %-55s ", name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

#define ASSERT_EQ_INT(a, b, msg) do { \
    if ((a) != (b)) { \
        printf("FAIL: %s (got %d, expected %d)\n", msg, (int)(a), (int)(b)); \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) do { \
    if (fabsf((a) - (b)) > (tol)) { \
        printf("FAIL: %s (got %.6f, expected %.6f)\n", msg, (float)(a), (float)(b)); \
        return; \
    } \
} while(0)

/* Helper: create and init an engine, returns by value */
static perf_fx_engine_t make_engine(void) {
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);
    return e;
}

/* ============================================================
 * 1. Core: pfx_clampf, init/destroy, reset, pressure curves, bpm_to_samples
 * ============================================================ */

static void test_clampf(void) {
    TEST("pfx_clampf");
    ASSERT_NEAR(pfx_clampf(0.5f, 0.0f, 1.0f), 0.5f, 0.001f, "in range");
    ASSERT_NEAR(pfx_clampf(-1.0f, 0.0f, 1.0f), 0.0f, 0.001f, "below min");
    ASSERT_NEAR(pfx_clampf(2.0f, 0.0f, 1.0f), 1.0f, 0.001f, "above max");
    ASSERT_NEAR(pfx_clampf(0.0f, 0.0f, 1.0f), 0.0f, 0.001f, "at min");
    ASSERT_NEAR(pfx_clampf(1.0f, 0.0f, 1.0f), 1.0f, 0.001f, "at max");
    PASS();
}

static void test_engine_init_destroy(void) {
    TEST("Engine init and destroy");
    perf_fx_engine_t e = make_engine();

    ASSERT_NEAR(e.dry_wet, 1.0f, 0.01f, "dry_wet default");
    ASSERT_NEAR(e.global_lpf, 1.0f, 0.01f, "global_lpf default");
    ASSERT_NEAR(e.global_hpf, 0.0f, 0.01f, "global_hpf default");
    ASSERT_NEAR(e.eq_bump_freq, 0.5f, 0.01f, "eq_bump_freq default");
    ASSERT_NEAR(e.bpm, 120.0f, 0.01f, "bpm default");
    ASSERT_EQ_INT(e.last_touched_slot, -1, "last_touched default");
    ASSERT_TRUE(e.capture_buf_l != NULL, "capture buf L allocated");
    ASSERT_TRUE(e.capture_buf_r != NULL, "capture buf R allocated");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_engine_reset(void) {
    TEST("Engine reset");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, 3, 0.5f);
    pfx_set_latched(&e, 3, 1);
    pfx_activate(&e, FX_DELAY, 0.8f);
    e.bypassed = 1;

    pfx_engine_reset(&e);
    ASSERT_EQ_INT(e.slots[3].active, 0, "slot 3 reset");
    ASSERT_EQ_INT(e.slots[3].latched, 0, "slot 3 latch reset");
    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 0, "delay reset");
    ASSERT_EQ_INT(e.slots[FX_DELAY].tail_active, 0, "delay tail reset");
    ASSERT_EQ_INT(e.bypassed, 0, "bypass reset");
    ASSERT_EQ_INT(e.last_touched_slot, -1, "last_touched reset");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_pressure_curve_linear(void) {
    TEST("Pressure curve: linear");
    float result = pfx_apply_pressure_curve(0.5f, 0.5f, PRESSURE_LINEAR);
    ASSERT_NEAR(result, 0.625f, 0.01f, "linear midpoint");
    float zero = pfx_apply_pressure_curve(0.0f, 1.0f, PRESSURE_LINEAR);
    ASSERT_NEAR(zero, 0.5f, 0.01f, "zero pressure, full velocity");
    PASS();
}

static void test_pressure_curve_exponential(void) {
    TEST("Pressure curve: exponential");
    float result = pfx_apply_pressure_curve(0.5f, 1.0f, PRESSURE_EXPONENTIAL);
    ASSERT_TRUE(result > 0.0f && result < 1.0f, "exponential in range");
    /* exponential squares the pressure, so at p=0.5 mod=0.25 */
    /* base*0.5 + mod*0.5 + base*mod*0.5 = 0.5 + 0.125 + 0.125 = 0.75 */
    ASSERT_NEAR(result, 0.75f, 0.01f, "exponential value at p=0.5 v=1.0");
    PASS();
}

static void test_pressure_curve_switch(void) {
    TEST("Pressure curve: switch");
    float low = pfx_apply_pressure_curve(0.2f, 1.0f, PRESSURE_SWITCH);
    ASSERT_NEAR(low, 0.5f, 0.01f, "switch below threshold");
    float high = pfx_apply_pressure_curve(0.7f, 1.0f, PRESSURE_SWITCH);
    ASSERT_NEAR(high, 1.0f, 0.01f, "switch above threshold");
    PASS();
}

static void test_bpm_to_samples(void) {
    TEST("BPM to samples conversion");
    int samples = pfx_bpm_to_samples(120.0f, 1.0f);
    ASSERT_EQ_INT(samples, 22050, "120 BPM one beat");
    PASS();
}

static void test_bpm_to_samples_8th(void) {
    TEST("BPM to samples (half beat)");
    int samples = pfx_bpm_to_samples(120.0f, 0.5f);
    ASSERT_EQ_INT(samples, 11025, "120 BPM half beat");
    PASS();
}

/* ============================================================
 * 2. Activation: activate/deactivate for each row
 * ============================================================ */

static void test_activate_repeat(void) {
    TEST("Activate/deactivate repeat FX (row 4)");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_RPT_1_4, 0.8f);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "repeat active");
    ASSERT_NEAR(e.slots[FX_RPT_1_4].velocity, 0.8f, 0.01f, "velocity");
    ASSERT_NEAR(e.slots[FX_RPT_1_4].phase, 0.0f, 0.01f, "phase starts 0");

    pfx_deactivate(&e, FX_RPT_1_4);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].fading_out, 1, "fading out");
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "still active during fade");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_activate_filter(void) {
    TEST("Activate/deactivate filter FX (row 3)");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_LP_SWEEP_DOWN, 0.7f);
    ASSERT_EQ_INT(e.slots[FX_LP_SWEEP_DOWN].active, 1, "filter active");
    ASSERT_NEAR(e.slots[FX_LP_SWEEP_DOWN].velocity, 0.7f, 0.01f, "velocity");

    pfx_deactivate(&e, FX_LP_SWEEP_DOWN);
    ASSERT_EQ_INT(e.slots[FX_LP_SWEEP_DOWN].fading_out, 1, "filter fading");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_activate_space(void) {
    TEST("Activate/deactivate space FX (row 2)");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_DELAY, 0.9f);
    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 1, "delay active");
    ASSERT_NEAR(e.slots[FX_DELAY].velocity, 0.9f, 0.01f, "velocity");

    pfx_deactivate(&e, FX_DELAY);
    /* Space FX go to tail mode instead of fade */
    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 0, "delay not active after deactivate");
    ASSERT_EQ_INT(e.slots[FX_DELAY].tail_active, 1, "delay tail active");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_activate_distort(void) {
    TEST("Activate/deactivate distort FX (row 1)");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_BITCRUSH, 0.6f);
    ASSERT_EQ_INT(e.slots[FX_BITCRUSH].active, 1, "bitcrush active");
    ASSERT_NEAR(e.slots[FX_BITCRUSH].velocity, 0.6f, 0.01f, "velocity");

    pfx_deactivate(&e, FX_BITCRUSH);
    ASSERT_EQ_INT(e.slots[FX_BITCRUSH].fading_out, 1, "fading out");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_activate_bounds(void) {
    TEST("Activate/deactivate bounds checking");
    perf_fx_engine_t e = make_engine();

    /* Should not crash on out-of-range slots */
    pfx_activate(&e, -1, 0.5f);
    pfx_activate(&e, 32, 0.5f);
    pfx_deactivate(&e, -1);
    pfx_deactivate(&e, 99);
    pfx_set_pressure(&e, -1, 0.5f);
    pfx_set_pressure(&e, 99, 0.5f);

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 3. Pressure: set_pressure for representative FX
 * ============================================================ */

static void test_pressure_beat_repeat(void) {
    TEST("Pressure: beat repeat");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_RPT_1_8, 0.5f);
    pfx_set_pressure(&e, FX_RPT_1_8, 0.9f);
    ASSERT_NEAR(e.slots[FX_RPT_1_8].pressure, 0.9f, 0.01f, "pressure stored");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_pressure_lp_sweep(void) {
    TEST("Pressure: LP sweep");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_LP_SWEEP_DOWN, 0.6f);
    pfx_set_pressure(&e, FX_LP_SWEEP_DOWN, 0.3f);
    ASSERT_NEAR(e.slots[FX_LP_SWEEP_DOWN].pressure, 0.3f, 0.01f, "pressure stored");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_pressure_delay(void) {
    TEST("Pressure: delay throw");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_DELAY, 0.8f);
    pfx_set_pressure(&e, FX_DELAY, 0.75f);
    ASSERT_NEAR(e.slots[FX_DELAY].pressure, 0.75f, 0.01f, "pressure stored");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_pressure_bitcrush(void) {
    TEST("Pressure: bitcrush");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_BITCRUSH, 0.5f);
    pfx_set_pressure(&e, FX_BITCRUSH, 1.0f);
    ASSERT_NEAR(e.slots[FX_BITCRUSH].pressure, 1.0f, 0.01f, "pressure stored");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 4. Latch: set_latched, verify latched flag, deactivate latched
 * ============================================================ */

static void test_latch_basic(void) {
    TEST("Latch: set and verify");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_RPT_1_4, 0.8f);
    pfx_set_latched(&e, FX_RPT_1_4, 1);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].latched, 1, "latched flag set");
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "still active");

    /* Deactivate should be ignored when latched */
    pfx_deactivate(&e, FX_RPT_1_4);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "latched ignores deactivate");
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].fading_out, 0, "no fade when latched");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_latch_unlatch_fade(void) {
    TEST("Latch: unlatch triggers deactivate");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_STUTTER, 0.7f);
    pfx_set_latched(&e, FX_STUTTER, 1);
    /* Simulate finger release: pressure goes to 0 */
    e.slots[FX_STUTTER].pressure = 0.0f;

    /* Unlatch should trigger deactivate since pressure is 0 */
    pfx_set_latched(&e, FX_STUTTER, 0);
    ASSERT_EQ_INT(e.slots[FX_STUTTER].latched, 0, "unlatched");
    ASSERT_EQ_INT(e.slots[FX_STUTTER].fading_out, 1, "fading after unlatch");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_latch_inactive_activates(void) {
    TEST("Latch: latching inactive slot activates it");
    perf_fx_engine_t e = make_engine();

    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 0, "initially inactive");
    pfx_set_latched(&e, FX_DELAY, 1);
    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 1, "activated by latch");
    ASSERT_EQ_INT(e.slots[FX_DELAY].latched, 1, "latched");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 5. Last touched: activate sets last_touched_slot
 * ============================================================ */

static void test_last_touched(void) {
    TEST("Last touched slot tracking");
    perf_fx_engine_t e = make_engine();

    ASSERT_EQ_INT(e.last_touched_slot, -1, "initial -1");

    pfx_activate(&e, FX_REVERB, 0.5f);
    ASSERT_EQ_INT(e.last_touched_slot, FX_REVERB, "set to reverb");

    pfx_activate(&e, FX_BITCRUSH, 0.5f);
    ASSERT_EQ_INT(e.last_touched_slot, FX_BITCRUSH, "updated to bitcrush");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 6. Params: pfx_set_param, verify params[0-3] stored
 * ============================================================ */

static void test_set_param(void) {
    TEST("pfx_set_param stores values");
    perf_fx_engine_t e = make_engine();

    pfx_set_param(&e, FX_DELAY, 0, 0.1f);
    pfx_set_param(&e, FX_DELAY, 1, 0.2f);
    pfx_set_param(&e, FX_DELAY, 2, 0.3f);
    pfx_set_param(&e, FX_DELAY, 3, 0.4f);

    ASSERT_NEAR(e.slots[FX_DELAY].params[0], 0.1f, 0.001f, "param 0");
    ASSERT_NEAR(e.slots[FX_DELAY].params[1], 0.2f, 0.001f, "param 1");
    ASSERT_NEAR(e.slots[FX_DELAY].params[2], 0.3f, 0.001f, "param 2");
    ASSERT_NEAR(e.slots[FX_DELAY].params[3], 0.4f, 0.001f, "param 3");

    /* Out of range idx should be ignored */
    pfx_set_param(&e, FX_DELAY, -1, 0.9f);
    pfx_set_param(&e, FX_DELAY, 4, 0.9f);

    /* Values should be clamped */
    pfx_set_param(&e, FX_RPT_1_4, 0, 2.0f);
    ASSERT_NEAR(e.slots[FX_RPT_1_4].params[0], 1.0f, 0.001f, "param clamped high");
    pfx_set_param(&e, FX_RPT_1_4, 0, -0.5f);
    ASSERT_NEAR(e.slots[FX_RPT_1_4].params[0], 0.0f, 0.001f, "param clamped low");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 7. Animated phase: activate filter sweep, render, verify phase > 0
 * ============================================================ */

static void test_animated_phase(void) {
    TEST("Animated phase advances on render");
    perf_fx_engine_t e = make_engine();

    /* Set up fake mapped memory for render */
    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    /* Put a simple tone in audio_out area */
    int16_t *audio_out = (int16_t *)(fake_mem + 256);
    for (int i = 0; i < 128; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = s;
        audio_out[i * 2 + 1] = s;
    }
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.audio_source = SOURCE_MOVE_MIX;

    pfx_activate(&e, FX_LP_SWEEP_DOWN, 0.8f);
    ASSERT_NEAR(e.slots[FX_LP_SWEEP_DOWN].phase, 0.0f, 0.001f, "phase starts at 0");

    /* Render a few blocks to advance phase */
    int16_t out[256];
    for (int b = 0; b < 10; b++) {
        pfx_engine_render(&e, out, 128);
    }

    ASSERT_TRUE(e.slots[FX_LP_SWEEP_DOWN].phase > 0.0f, "phase advanced after render");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 8. Space FX tail: activate delay, deactivate, check tail_active
 * ============================================================ */

static void test_space_tail(void) {
    TEST("Space FX tail on deactivate");
    perf_fx_engine_t e = make_engine();

    /* Test all space FX types */
    int space_slots[] = { FX_DELAY, FX_PING_PONG, FX_TAPE_ECHO, FX_ECHO_FREEZE,
                          FX_REVERB, FX_SHIMMER, FX_DARK_VERB, FX_SPRING };
    for (int i = 0; i < 8; i++) {
        int slot = space_slots[i];
        pfx_activate(&e, slot, 0.8f);
        ASSERT_EQ_INT(e.slots[slot].active, 1, "space slot active");

        pfx_deactivate(&e, slot);
        ASSERT_EQ_INT(e.slots[slot].active, 0, "space slot deactivated");
        ASSERT_EQ_INT(e.slots[slot].tail_active, 1, "tail_active set");
    }

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 9. Scenes: save/recall with latched pads, params, globals
 * ============================================================ */

static void test_scene_save_recall(void) {
    TEST("Scene save and recall");
    perf_fx_engine_t e = make_engine();

    /* Set up state: latch two FX, set params and globals */
    pfx_activate(&e, FX_RPT_1_8, 0.7f);
    pfx_set_latched(&e, FX_RPT_1_8, 1);
    pfx_set_param(&e, FX_RPT_1_8, 0, 0.42f);

    pfx_activate(&e, FX_REVERB, 0.9f);
    pfx_set_latched(&e, FX_REVERB, 1);
    pfx_set_param(&e, FX_REVERB, 1, 0.88f);

    e.global_hpf = 0.3f;
    e.global_lpf = 0.8f;
    e.dry_wet = 0.7f;
    e.eq_bump_freq = 0.6f;

    pfx_scene_save(&e, 0);
    ASSERT_EQ_INT(e.scenes[0].populated, 1, "scene 0 populated");

    /* Change all state */
    pfx_set_latched(&e, FX_RPT_1_8, 0);
    e.slots[FX_RPT_1_8].pressure = 0.0f;
    pfx_set_latched(&e, FX_REVERB, 0);
    e.slots[FX_REVERB].pressure = 0.0f;
    pfx_set_param(&e, FX_RPT_1_8, 0, 0.99f);
    pfx_set_param(&e, FX_REVERB, 1, 0.11f);
    e.global_hpf = 0.0f;
    e.global_lpf = 1.0f;
    e.dry_wet = 0.3f;
    e.eq_bump_freq = 0.5f;

    /* Recall scene 0 */
    pfx_scene_recall(&e, 0);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_8].latched, 1, "rpt latched restored");
    ASSERT_EQ_INT(e.slots[FX_RPT_1_8].active, 1, "rpt active after recall");
    ASSERT_EQ_INT(e.slots[FX_REVERB].latched, 1, "reverb latched restored");
    ASSERT_EQ_INT(e.slots[FX_REVERB].active, 1, "reverb active after recall");
    ASSERT_NEAR(e.slots[FX_RPT_1_8].params[0], 0.42f, 0.01f, "rpt param restored");
    ASSERT_NEAR(e.slots[FX_REVERB].params[1], 0.88f, 0.01f, "reverb param restored");
    ASSERT_NEAR(e.global_hpf, 0.3f, 0.01f, "global_hpf restored");
    ASSERT_NEAR(e.global_lpf, 0.8f, 0.01f, "global_lpf restored");
    ASSERT_NEAR(e.dry_wet, 0.7f, 0.01f, "dry_wet restored");
    ASSERT_NEAR(e.eq_bump_freq, 0.6f, 0.01f, "eq_bump_freq restored");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_clear(void) {
    TEST("Scene clear");
    perf_fx_engine_t e = make_engine();

    pfx_scene_save(&e, 5);
    ASSERT_EQ_INT(e.scenes[5].populated, 1, "scene 5 populated");

    pfx_scene_clear(&e, 5);
    ASSERT_EQ_INT(e.scenes[5].populated, 0, "scene 5 cleared");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_bounds(void) {
    TEST("Scene bounds checking");
    perf_fx_engine_t e = make_engine();

    /* Should not crash */
    pfx_scene_save(&e, -1);
    pfx_scene_save(&e, 16);
    pfx_scene_recall(&e, -1);
    pfx_scene_recall(&e, 16);
    pfx_scene_clear(&e, -1);
    pfx_scene_clear(&e, 16);

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_recall_empty(void) {
    TEST("Scene recall unpopulated (no-op)");
    perf_fx_engine_t e = make_engine();

    e.dry_wet = 0.42f;
    pfx_scene_recall(&e, 0); /* not populated */
    ASSERT_NEAR(e.dry_wet, 0.42f, 0.01f, "state unchanged");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 10. Step presets: save/recall/clear
 * ============================================================ */

static void test_step_save_recall(void) {
    TEST("Step preset save and recall");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_CHORUS, 0.5f);
    pfx_set_latched(&e, FX_CHORUS, 1);
    pfx_set_param(&e, FX_CHORUS, 0, 0.33f);
    e.dry_wet = 0.65f;

    pfx_step_save(&e, 3);
    ASSERT_EQ_INT(e.step_presets[3].populated, 1, "step 3 populated");

    /* Change state */
    pfx_set_latched(&e, FX_CHORUS, 0);
    e.slots[FX_CHORUS].pressure = 0.0f;
    pfx_set_param(&e, FX_CHORUS, 0, 0.99f);
    e.dry_wet = 0.1f;

    pfx_step_recall(&e, 3);
    ASSERT_NEAR(e.dry_wet, 0.65f, 0.01f, "dry_wet restored");
    ASSERT_EQ_INT(e.slots[FX_CHORUS].latched, 1, "chorus latched after recall");
    ASSERT_EQ_INT(e.slots[FX_CHORUS].active, 1, "chorus active after recall");
    ASSERT_NEAR(e.slots[FX_CHORUS].params[0], 0.33f, 0.01f, "chorus param restored");
    ASSERT_EQ_INT(e.current_step_preset, 3, "current_step_preset updated");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_step_clear(void) {
    TEST("Step preset clear");
    perf_fx_engine_t e = make_engine();

    pfx_step_save(&e, 7);
    ASSERT_EQ_INT(e.step_presets[7].populated, 1, "step 7 populated");

    /* Set current so we can test clearing resets it */
    e.current_step_preset = 7;
    pfx_step_clear(&e, 7);
    ASSERT_EQ_INT(e.step_presets[7].populated, 0, "step 7 cleared");
    ASSERT_EQ_INT(e.current_step_preset, -1, "current_step_preset reset");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_step_bounds(void) {
    TEST("Step preset bounds checking");
    perf_fx_engine_t e = make_engine();

    pfx_step_save(&e, -1);
    pfx_step_save(&e, 16);
    pfx_step_recall(&e, -1);
    pfx_step_recall(&e, 16);
    pfx_step_clear(&e, -1);
    pfx_step_clear(&e, 16);

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 11. Render: bypass produces passthrough, active FX modifies output
 * ============================================================ */

static void test_render_bypass(void) {
    TEST("Render with bypass (passthrough)");
    perf_fx_engine_t e = make_engine();

    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    /* Put a tone in the audio out area */
    int16_t *audio_out = (int16_t *)(fake_mem + 256);
    for (int i = 0; i < 128; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = s;
        audio_out[i * 2 + 1] = s;
    }
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.audio_source = SOURCE_MOVE_MIX;
    e.bypassed = 1;

    int16_t out[256];
    pfx_engine_render(&e, out, 128);

    /* Bypassed should pass through the audio (soft_clip applied but no FX) */
    int has_signal = 0;
    for (int i = 0; i < 256; i++) {
        if (out[i] != 0) { has_signal = 1; break; }
    }
    ASSERT_TRUE(has_signal, "bypass passes signal through");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_render_passthrough(void) {
    TEST("Render passthrough (no FX active)");
    perf_fx_engine_t e = make_engine();

    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    int16_t *audio_out = (int16_t *)(fake_mem + 256);
    for (int i = 0; i < 128; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = s;
        audio_out[i * 2 + 1] = s;
    }
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.audio_source = SOURCE_MOVE_MIX;

    int16_t out[256];
    pfx_engine_render(&e, out, 128);

    /* With no FX active, output should have signal */
    int has_signal = 0;
    for (int i = 0; i < 256; i++) {
        if (out[i] != 0) { has_signal = 1; break; }
    }
    ASSERT_TRUE(has_signal, "passthrough has signal");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_render_with_active_fx(void) {
    TEST("Render with active FX modifies output");
    perf_fx_engine_t e = make_engine();

    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    int16_t *audio_out = (int16_t *)(fake_mem + 256);
    for (int i = 0; i < 128; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = s;
        audio_out[i * 2 + 1] = s;
    }
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.audio_source = SOURCE_MOVE_MIX;

    /* Render passthrough first for comparison */
    int16_t out_dry[256];
    pfx_engine_render(&e, out_dry, 128);

    /* Reload audio (render consumed it) */
    for (int i = 0; i < 128; i++) {
        int16_t s = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = s;
        audio_out[i * 2 + 1] = s;
    }

    /* Activate bitcrush (aggressive: produces audible difference) */
    pfx_activate(&e, FX_BITCRUSH, 1.0f);
    pfx_set_param(&e, FX_BITCRUSH, 0, 0.1f); /* heavy crush */

    int16_t out_fx[256];
    pfx_engine_render(&e, out_fx, 128);

    /* Check that output differs from dry */
    int differ = 0;
    for (int i = 0; i < 256; i++) {
        if (out_fx[i] != out_dry[i]) { differ = 1; break; }
    }
    ASSERT_TRUE(differ, "FX output differs from dry");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_render_silence(void) {
    TEST("Render with silence input and bypass");
    perf_fx_engine_t e = make_engine();

    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.bypassed = 1;

    int16_t out[256];
    pfx_engine_render(&e, out, 128);

    int all_zero = 1;
    for (int i = 0; i < 256; i++) {
        if (out[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_TRUE(all_zero, "silence in = silence out when bypassed");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * 12. Exit cleanup: activate+latch several FX, deactivate all
 * ============================================================ */

static void test_exit_cleanup(void) {
    TEST("Exit cleanup: all FX cleared");
    perf_fx_engine_t e = make_engine();

    /* Activate and latch several FX across rows */
    pfx_activate(&e, FX_RPT_1_4, 0.8f);
    pfx_set_latched(&e, FX_RPT_1_4, 1);
    pfx_activate(&e, FX_LP_SWEEP_DOWN, 0.7f);
    pfx_set_latched(&e, FX_LP_SWEEP_DOWN, 1);
    pfx_activate(&e, FX_DELAY, 0.9f);
    pfx_set_latched(&e, FX_DELAY, 1);
    pfx_activate(&e, FX_BITCRUSH, 0.6f);
    pfx_set_latched(&e, FX_BITCRUSH, 1);

    /* Verify all active */
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "rpt active");
    ASSERT_EQ_INT(e.slots[FX_LP_SWEEP_DOWN].active, 1, "filter active");
    ASSERT_EQ_INT(e.slots[FX_DELAY].active, 1, "delay active");
    ASSERT_EQ_INT(e.slots[FX_BITCRUSH].active, 1, "crush active");

    /* Reset clears everything */
    pfx_engine_reset(&e);

    for (int i = 0; i < PFX_NUM_FX; i++) {
        ASSERT_EQ_INT(e.slots[i].active, 0, "slot cleared");
        ASSERT_EQ_INT(e.slots[i].latched, 0, "latch cleared");
        ASSERT_EQ_INT(e.slots[i].tail_active, 0, "tail cleared");
        ASSERT_EQ_INT(e.slots[i].fading_out, 0, "fade cleared");
    }

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Bonus: Serialization
 * ============================================================ */

static void test_serialize_state(void) {
    TEST("State serialization");
    perf_fx_engine_t e = make_engine();

    e.bpm = 140.0f;
    e.dry_wet = 0.65f;
    e.global_hpf = 0.2f;
    e.eq_bump_freq = 0.7f;

    char buf[8192];
    int len = pfx_serialize_state(&e, buf, sizeof(buf));
    ASSERT_TRUE(len > 0, "serialization produced output");
    ASSERT_TRUE(strstr(buf, "140.0") != NULL, "contains BPM");
    ASSERT_TRUE(strstr(buf, "0.650") != NULL, "contains dry_wet");
    ASSERT_TRUE(strstr(buf, "\"slots\"") != NULL, "contains slots array");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Bonus: FX category macros
 * ============================================================ */

static void test_fx_category_macros(void) {
    TEST("FX category macros");

    ASSERT_TRUE(FX_IS_REPEAT(FX_RPT_1_4), "RPT_1_4 is repeat");
    ASSERT_TRUE(FX_IS_REPEAT(FX_HALF_SPEED), "HALF_SPEED is repeat");
    ASSERT_TRUE(!FX_IS_REPEAT(FX_LP_SWEEP_DOWN), "LP_SWEEP not repeat");

    ASSERT_TRUE(FX_IS_FILTER(FX_LP_SWEEP_DOWN), "LP_SWEEP is filter");
    ASSERT_TRUE(FX_IS_FILTER(FX_AUTO_FILTER), "AUTO_FILTER is filter");
    ASSERT_TRUE(!FX_IS_FILTER(FX_DELAY), "DELAY not filter");

    ASSERT_TRUE(FX_IS_SPACE(FX_DELAY), "DELAY is space");
    ASSERT_TRUE(FX_IS_SPACE(FX_SPRING), "SPRING is space");
    ASSERT_TRUE(!FX_IS_SPACE(FX_BITCRUSH), "BITCRUSH not space");

    ASSERT_TRUE(FX_IS_DISTORT(FX_BITCRUSH), "BITCRUSH is distort");
    ASSERT_TRUE(FX_IS_DISTORT(FX_CHORUS), "CHORUS is distort");
    ASSERT_TRUE(!FX_IS_DISTORT(FX_RPT_1_4), "RPT not distort");

    PASS();
}

/* ============================================================
 * Bonus: Re-activate cancels fade
 * ============================================================ */

static void test_reactivate_cancels_fade(void) {
    TEST("Re-activate cancels fade-out");
    perf_fx_engine_t e = make_engine();

    pfx_activate(&e, FX_RPT_1_4, 1.0f);
    pfx_deactivate(&e, FX_RPT_1_4);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].fading_out, 1, "fading");

    pfx_activate(&e, FX_RPT_1_4, 0.5f);
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].fading_out, 0, "fade cancelled");
    ASSERT_EQ_INT(e.slots[FX_RPT_1_4].active, 1, "re-activated");
    ASSERT_NEAR(e.slots[FX_RPT_1_4].velocity, 0.5f, 0.01f, "new velocity");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void) {
    printf("Performance FX DSP Unit Tests (v2)\n");
    printf("====================================\n\n");

    printf("Core:\n");
    test_clampf();
    test_engine_init_destroy();
    test_engine_reset();
    test_pressure_curve_linear();
    test_pressure_curve_exponential();
    test_pressure_curve_switch();
    test_bpm_to_samples();
    test_bpm_to_samples_8th();
    test_fx_category_macros();

    printf("\nActivation (per row):\n");
    test_activate_repeat();
    test_activate_filter();
    test_activate_space();
    test_activate_distort();
    test_activate_bounds();
    test_reactivate_cancels_fade();

    printf("\nPressure:\n");
    test_pressure_beat_repeat();
    test_pressure_lp_sweep();
    test_pressure_delay();
    test_pressure_bitcrush();

    printf("\nLatch:\n");
    test_latch_basic();
    test_latch_unlatch_fade();
    test_latch_inactive_activates();

    printf("\nLast Touched:\n");
    test_last_touched();

    printf("\nParams:\n");
    test_set_param();

    printf("\nAnimated Phase:\n");
    test_animated_phase();

    printf("\nSpace FX Tail:\n");
    test_space_tail();

    printf("\nScenes:\n");
    test_scene_save_recall();
    test_scene_clear();
    test_scene_bounds();
    test_scene_recall_empty();

    printf("\nStep Presets:\n");
    test_step_save_recall();
    test_step_clear();
    test_step_bounds();

    printf("\nRendering:\n");
    test_render_bypass();
    test_render_passthrough();
    test_render_with_active_fx();
    test_render_silence();

    printf("\nExit Cleanup:\n");
    test_exit_cleanup();

    printf("\nSerialization:\n");
    test_serialize_state();

    printf("\n====================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
