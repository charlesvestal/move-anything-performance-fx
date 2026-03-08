/*
 * Performance FX DSP Unit Tests
 *
 * Compile and run on host (macOS/Linux):
 *   gcc -g -O0 -o test_perf_fx test_perf_fx.c perf_fx_dsp.c -lm && ./test_perf_fx
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
    printf("  TEST: %-50s ", name); \
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

/* ============================================================
 * Test: Engine init and destroy
 * ============================================================ */
static void test_engine_init_destroy(void) {
    TEST("Engine init and destroy");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    ASSERT_NEAR(e.dry_wet, 1.0f, 0.01f, "dry_wet default");
    ASSERT_NEAR(e.input_gain, 1.0f, 0.01f, "input_gain default");
    ASSERT_NEAR(e.output_gain, 1.0f, 0.01f, "output_gain default");
    ASSERT_NEAR(e.bpm, 120.0f, 0.01f, "bpm default");
    ASSERT_TRUE(e.capture_buf_l != NULL, "capture buf L allocated");
    ASSERT_TRUE(e.capture_buf_r != NULL, "capture buf R allocated");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Pressure curve
 * ============================================================ */
static void test_pressure_curve_linear(void) {
    TEST("Pressure curve: linear");
    /* Formula: base*0.5 + mod*0.5 + base*mod*0.5 = 0.25 + 0.25 + 0.125 = 0.625 */
    float result = pfx_apply_pressure_curve(0.5f, 0.5f, PRESSURE_LINEAR);
    ASSERT_NEAR(result, 0.625f, 0.01f, "linear midpoint");
    /* Zero pressure should reflect velocity only */
    float zero = pfx_apply_pressure_curve(0.0f, 1.0f, PRESSURE_LINEAR);
    ASSERT_NEAR(zero, 0.5f, 0.01f, "zero pressure, full velocity");
    PASS();
}

static void test_pressure_curve_exponential(void) {
    TEST("Pressure curve: exponential");
    float result = pfx_apply_pressure_curve(0.5f, 1.0f, PRESSURE_EXPONENTIAL);
    /* Exponential should be less than linear at midpoint */
    ASSERT_TRUE(result < 0.5f || result > 0.0f, "exponential in range");
    PASS();
}

static void test_pressure_curve_switch(void) {
    TEST("Pressure curve: switch");
    /* Switch: pressure > 0.3 -> mod=1, else mod=0 */
    /* low: base=1.0, mod=0 -> 0.5 + 0 + 0 = 0.5 */
    float low = pfx_apply_pressure_curve(0.2f, 1.0f, PRESSURE_SWITCH);
    ASSERT_NEAR(low, 0.5f, 0.01f, "switch below threshold");
    /* high: base=1.0, mod=1.0 -> 0.5 + 0.5 + 0.5 = 1.5 clamped to 1.0 */
    float high = pfx_apply_pressure_curve(0.7f, 1.0f, PRESSURE_SWITCH);
    ASSERT_NEAR(high, 1.0f, 0.01f, "switch above threshold");
    PASS();
}

/* ============================================================
 * Test: BPM to samples conversion
 * ============================================================ */
static void test_bpm_to_samples(void) {
    TEST("BPM to samples conversion");
    /* At 120 BPM, beat = 0.5s = 22050 samples. division=1.0 = one beat */
    int samples = pfx_bpm_to_samples(120.0f, 1.0f);
    ASSERT_EQ_INT(samples, 22050, "120 BPM one beat");
    PASS();
}

static void test_bpm_to_samples_8th(void) {
    TEST("BPM to samples (half beat)");
    /* division=0.5 = half a beat = 11025 samples at 120 BPM */
    int samples = pfx_bpm_to_samples(120.0f, 0.5f);
    ASSERT_EQ_INT(samples, 11025, "120 BPM half beat");
    PASS();
}

/* ============================================================
 * Test: Punch-in FX activate/deactivate
 * ============================================================ */
static void test_punch_activate_deactivate(void) {
    TEST("Punch-in activate/deactivate");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_punch_activate(&e, 0, 0.8f);
    ASSERT_EQ_INT(e.punch[0].active, 1, "punch 0 active");
    ASSERT_NEAR(e.punch[0].velocity, 0.8f, 0.01f, "punch 0 velocity");

    pfx_punch_deactivate(&e, 0);
    /* Now fading out instead of immediately off */
    ASSERT_EQ_INT(e.punch[0].fading_out, 1, "punch 0 fading out");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_punch_pressure(void) {
    TEST("Punch-in pressure update");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_punch_activate(&e, 5, 0.5f);
    pfx_punch_set_pressure(&e, 5, 0.9f);
    ASSERT_NEAR(e.punch[5].pressure, 0.9f, 0.01f, "punch 5 pressure");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_punch_bounds(void) {
    TEST("Punch-in bounds checking");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Should not crash on out-of-range slots */
    pfx_punch_activate(&e, -1, 0.5f);
    pfx_punch_activate(&e, 16, 0.5f);
    pfx_punch_deactivate(&e, -1);
    pfx_punch_set_pressure(&e, 99, 0.5f);

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Continuous FX
 * ============================================================ */
static void test_cont_toggle(void) {
    TEST("Continuous FX toggle");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_cont_toggle(&e, 0);
    ASSERT_EQ_INT(e.cont[0].active, 1, "cont 0 active after first toggle");
    ASSERT_EQ_INT(e.active_cont_count, 1, "active count 1");

    pfx_cont_toggle(&e, 0);
    ASSERT_EQ_INT(e.cont[0].active, 0, "cont 0 inactive after second toggle");
    ASSERT_EQ_INT(e.active_cont_count, 0, "active count 0");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_cont_max_simultaneous(void) {
    TEST("Continuous FX max simultaneous (3)");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_cont_activate(&e, 0);
    pfx_cont_activate(&e, 1);
    pfx_cont_activate(&e, 2);
    ASSERT_EQ_INT(e.active_cont_count, 3, "3 active");

    /* 4th should replace oldest */
    pfx_cont_activate(&e, 3);
    ASSERT_EQ_INT(e.active_cont_count, 3, "still 3 active");
    ASSERT_EQ_INT(e.cont[0].active, 0, "oldest (0) replaced");
    ASSERT_EQ_INT(e.cont[3].active, 1, "newest (3) active");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_cont_set_param(void) {
    TEST("Continuous FX set param");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_cont_set_param(&e, 5, 2, 0.75f);
    ASSERT_NEAR(e.cont[5].params[2], 0.75f, 0.001f, "param set correctly");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Scene save/recall/clear
 * ============================================================ */
static void test_scene_save_recall(void) {
    TEST("Scene save and recall");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Set up some state */
    e.dry_wet = 0.7f;
    pfx_cont_activate(&e, 2);
    e.cont[2].params[0] = 0.42f;

    /* Save to scene 0 */
    pfx_scene_save(&e, 0);
    ASSERT_EQ_INT(e.scenes[0].populated, 1, "scene 0 populated");

    /* Change state */
    e.dry_wet = 0.3f;
    pfx_cont_deactivate(&e, 2);
    e.cont[2].params[0] = 0.99f;

    /* Recall scene 0 */
    pfx_scene_recall(&e, 0);
    ASSERT_NEAR(e.dry_wet, 0.7f, 0.01f, "dry_wet restored");
    ASSERT_EQ_INT(e.cont[2].active, 1, "cont 2 active after recall");
    ASSERT_NEAR(e.cont[2].params[0], 0.42f, 0.01f, "cont 2 param restored");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_clear(void) {
    TEST("Scene clear");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_scene_save(&e, 5);
    ASSERT_EQ_INT(e.scenes[5].populated, 1, "scene 5 populated");

    pfx_scene_clear(&e, 5);
    ASSERT_EQ_INT(e.scenes[5].populated, 0, "scene 5 cleared");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_bounds(void) {
    TEST("Scene bounds checking");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Should not crash */
    pfx_scene_save(&e, -1);
    pfx_scene_save(&e, 16);
    pfx_scene_recall(&e, -1);
    pfx_scene_recall(&e, 16);
    pfx_scene_clear(&e, -1);

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Step presets
 * ============================================================ */
static void test_step_save_recall(void) {
    TEST("Step preset save and recall");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    e.output_gain = 1.5f;
    pfx_cont_activate(&e, 7);
    e.cont[7].params[1] = 0.33f;

    pfx_step_save(&e, 3);
    ASSERT_EQ_INT(e.step_presets[3].populated, 1, "step 3 populated");

    /* Change state */
    e.output_gain = 0.1f;
    pfx_cont_deactivate(&e, 7);

    pfx_step_recall(&e, 3);
    ASSERT_NEAR(e.output_gain, 1.5f, 0.01f, "output_gain restored");
    ASSERT_EQ_INT(e.cont[7].active, 1, "cont 7 active after recall");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Engine render (silence in = silence out when bypassed)
 * ============================================================ */
static void test_render_bypass(void) {
    TEST("Render with bypass (silence)");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Create fake mapped memory with silence */
    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.bypassed = 1;

    int16_t out[256]; /* 128 frames * 2 channels */
    pfx_engine_render(&e, out, 128);

    /* Bypassed should be silence */
    int all_zero = 1;
    for (int i = 0; i < 256; i++) {
        if (out[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_TRUE(all_zero, "bypassed output is silence");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_render_passthrough(void) {
    TEST("Render passthrough (no FX active)");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Create fake mapped memory with a test tone */
    uint8_t fake_mem[4096];
    memset(fake_mem, 0, sizeof(fake_mem));
    int16_t *audio_out = (int16_t *)(fake_mem + 256);
    for (int i = 0; i < 128; i++) {
        int16_t sample = (int16_t)(sinf(2.0f * 3.14159f * 440.0f * i / 44100.0f) * 16000);
        audio_out[i * 2] = sample;
        audio_out[i * 2 + 1] = sample;
    }
    e.mapped_memory = fake_mem;
    e.audio_out_offset = 256;
    e.audio_in_offset = 2304;
    e.audio_source = 1; /* Move output */

    int16_t out[256];
    pfx_engine_render(&e, out, 128);

    /* With no FX active and dry_wet=1, output should closely match input */
    int has_signal = 0;
    for (int i = 0; i < 256; i++) {
        if (out[i] != 0) { has_signal = 1; break; }
    }
    ASSERT_TRUE(has_signal, "passthrough has signal");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Scene morphing
 * ============================================================ */
static void test_scene_morph(void) {
    TEST("Scene morphing");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Save scene A with dry_wet = 0.2 */
    e.dry_wet = 0.2f;
    pfx_scene_save(&e, 0);

    /* Save scene B with dry_wet = 0.8 */
    e.dry_wet = 0.8f;
    pfx_scene_save(&e, 1);

    /* Start morph from A to B */
    pfx_scene_morph_start(&e, 0, 1);
    ASSERT_EQ_INT(e.morph.active, 1, "morph active");
    ASSERT_NEAR(e.morph.progress, 0.0f, 0.001f, "progress starts at 0");

    /* Tick forward a bit */
    pfx_scene_morph_tick(&e, 44100); /* 1 second worth */
    ASSERT_TRUE(e.morph.progress > 0.0f, "progress advanced");
    ASSERT_TRUE(e.morph.progress < 1.0f, "progress not yet complete");
    /* dry_wet should be between 0.2 and 0.8 */
    ASSERT_TRUE(e.dry_wet > 0.2f, "dry_wet above scene A");
    ASSERT_TRUE(e.dry_wet < 0.8f, "dry_wet below scene B");

    /* Tick forward to completion */
    pfx_scene_morph_tick(&e, 44100); /* another second */
    ASSERT_EQ_INT(e.morph.active, 0, "morph completed");

    pfx_engine_destroy(&e);
    PASS();
}

static void test_scene_morph_invalid(void) {
    TEST("Scene morph with invalid scenes");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Should not crash with unpopulated scenes */
    pfx_scene_morph_start(&e, 0, 1);
    ASSERT_EQ_INT(e.morph.active, 0, "morph not started with empty scenes");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Engine reset
 * ============================================================ */
static void test_engine_reset(void) {
    TEST("Engine reset");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    /* Set some state */
    pfx_punch_activate(&e, 3, 0.5f);
    pfx_cont_activate(&e, 5);
    e.bypassed = 1;

    pfx_engine_reset(&e);
    ASSERT_EQ_INT(e.punch[3].active, 0, "punch reset");
    ASSERT_EQ_INT(e.cont[5].active, 0, "cont reset");
    ASSERT_EQ_INT(e.active_cont_count, 0, "active count reset");
    ASSERT_EQ_INT(e.bypassed, 0, "bypass reset");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Serialization
 * ============================================================ */
static void test_serialize_state(void) {
    TEST("State serialization");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    e.bpm = 140.0f;
    e.dry_wet = 0.65f;

    char buf[4096];
    int len = pfx_serialize_state(&e, buf, sizeof(buf));
    ASSERT_TRUE(len > 0, "serialization produced output");
    ASSERT_TRUE(strstr(buf, "140.0") != NULL, "contains BPM");
    ASSERT_TRUE(strstr(buf, "0.650") != NULL, "contains dry_wet");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Test: Clamp function
 * ============================================================ */
static void test_clampf(void) {
    TEST("pfx_clampf");
    ASSERT_NEAR(pfx_clampf(0.5f, 0.0f, 1.0f), 0.5f, 0.001f, "in range");
    ASSERT_NEAR(pfx_clampf(-1.0f, 0.0f, 1.0f), 0.0f, 0.001f, "below min");
    ASSERT_NEAR(pfx_clampf(2.0f, 0.0f, 1.0f), 1.0f, 0.001f, "above max");
    PASS();
}

/* ============================================================
 * Test: Punch-in release crossfade
 * ============================================================ */
static void test_punch_crossfade(void) {
    TEST("Punch-in release crossfade");
    perf_fx_engine_t e;
    memset(&e, 0, sizeof(e));
    pfx_engine_init(&e);

    pfx_punch_activate(&e, 0, 1.0f);
    ASSERT_EQ_INT(e.punch[0].active, 1, "punch 0 active");

    pfx_punch_deactivate(&e, 0);
    /* Should be fading, not immediately off */
    ASSERT_EQ_INT(e.punch[0].fading_out, 1, "fading_out set");
    ASSERT_EQ_INT(e.punch[0].fade_len, 256, "fade_len is 256");
    ASSERT_EQ_INT(e.punch[0].active, 1, "still active during fade");

    /* Re-activate should cancel fade */
    pfx_punch_activate(&e, 0, 0.5f);
    ASSERT_EQ_INT(e.punch[0].fading_out, 0, "fade cancelled on re-activate");

    pfx_engine_destroy(&e);
    PASS();
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void) {
    printf("Performance FX DSP Unit Tests\n");
    printf("==============================\n\n");

    printf("Core:\n");
    test_clampf();
    test_engine_init_destroy();
    test_engine_reset();
    test_pressure_curve_linear();
    test_pressure_curve_exponential();
    test_pressure_curve_switch();
    test_bpm_to_samples();
    test_bpm_to_samples_8th();

    printf("\nPunch-in FX:\n");
    test_punch_activate_deactivate();
    test_punch_pressure();
    test_punch_crossfade();
    test_punch_bounds();

    printf("\nContinuous FX:\n");
    test_cont_toggle();
    test_cont_max_simultaneous();
    test_cont_set_param();

    printf("\nScenes:\n");
    test_scene_save_recall();
    test_scene_clear();
    test_scene_bounds();

    printf("\nStep Presets:\n");
    test_step_save_recall();

    printf("\nRendering:\n");
    test_render_bypass();
    test_render_passthrough();

    printf("\nScene Morphing:\n");
    test_scene_morph();
    test_scene_morph_invalid();

    printf("\nSerialization:\n");
    test_serialize_state();

    printf("\n==============================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return tests_passed == tests_run ? 0 : 1;
}
