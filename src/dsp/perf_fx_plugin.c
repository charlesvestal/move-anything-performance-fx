/*
 * Performance FX Plugin (API v2)
 *
 * Wrapper around the perf_fx_dsp engine.
 * Handles parameter routing, MIDI, and audio rendering.
 */

#include "perf_fx_dsp.h"
#include "plugin_api_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "pfx_track_shm.h"

/* Use pfx_clampf from header */
#define clampf pfx_clampf

/* Overflow-safe snprintf helper */
#define SAFE_SNPRINTF(buf, n, len, ...) do { \
    n += snprintf((buf) + (n), (n) < (len) ? (len) - (n) : 0, __VA_ARGS__); \
    if ((n) >= (len)) return (len) - 1; \
} while(0)

static const host_api_v1_t *g_host = NULL;
static plugin_api_v2_t g_api;

/* Punch-in FX names */
static const char *PUNCH_NAMES[PFX_NUM_PUNCH_IN] = {
    "Beat Rpt 1/4", "Beat Rpt 1/8", "Beat Rpt 1/16", "Beat Rpt Trip",
    "Stutter", "Scatter", "Reverse", "Half-Speed",
    "LP Filter", "HP Filter", "BP Filter", "Reso Peak",
    "Bitcrush", "SR Reduce", "Tape Stop", "Ducker"
};

/* Continuous FX names */
static const char *CONT_NAMES[PFX_NUM_CONTINUOUS] = {
    "Delay", "Ping-Pong", "Tape Echo", "Auto-Filter",
    "Plate Verb", "Dark Verb", "Spring Verb", "Shimmer",
    "Chorus", "Phaser", "Flanger", "Trem/Pan",
    "Compressor", "Saturator", "Pitch Shift", "Ducker"
};

/* Continuous FX parameter names (4 main params per effect) */
static const char *CONT_PARAM_NAMES[PFX_NUM_CONTINUOUS][4] = {
    {"Time", "Feedback", "Filter", "Mix"},         /* Delay */
    {"Time", "Feedback", "Spread", "Mix"},         /* Ping-Pong */
    {"Age", "Wow/Flut", "Feedback", "Mix"},        /* Tape Echo */
    {"Rate", "Depth", "Reso", "Mix"},               /* Auto-Filter */
    {"Decay", "Damping", "Pre-Dly", "Mix"},        /* Plate Reverb */
    {"Decay", "Darkness", "Mod", "Mix"},           /* Dark Reverb */
    {"Decay", "Tone", "Drip", "Mix"},              /* Spring Reverb */
    {"Decay", "Pitch", "Mod", "Mix"},              /* Shimmer */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Chorus */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Phaser */
    {"Rate", "Depth", "Feedbk", "Mix"},            /* Flanger */
    {"Rate", "Depth", "Shape", "Mix"},              /* Trem/Pan */
    {"Thresh", "Ratio", "Attack", "Mix"},          /* Compressor */
    {"Drive", "Tone", "Curve", "Mix"},             /* Saturator */
    {"Pitch", "Grain", "Quality", "Mix"},            /* Pitch Shift */
    {"Rate", "Depth", "Shape", "Mix"}               /* Ducker */
};

typedef struct {
    perf_fx_engine_t engine;
    char module_dir[256];
    pfx_track_audio_shm_t *track_shm;
    uint32_t last_track_seq;
    int16_t track_bufs[4][PFX_TRACK_SHM_FRAMES * 2]; /* local copy */
} pfx_instance_t;

static void log_msg(const char *fmt, ...) {
    if (!g_host || !g_host->log) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_host->log(buf);
}

/* ---- Lifecycle ---- */

static void *v2_create(const char *module_dir, const char *json_defaults) {
    pfx_instance_t *inst = (pfx_instance_t *)calloc(1, sizeof(pfx_instance_t));
    if (!inst) return NULL;

    if (module_dir)
        snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", module_dir);

    pfx_engine_init(&inst->engine);

    /* Connect to host audio memory */
    if (g_host) {
        inst->engine.mapped_memory = g_host->mapped_memory;
        inst->engine.audio_out_offset = g_host->audio_out_offset;
        inst->engine.audio_in_offset = g_host->audio_in_offset;
    }

    log_msg("pfx: Performance FX engine initialized");

    /* Try to map Link Audio track shared memory */
    int shm_fd = shm_open(PFX_TRACK_SHM_NAME, O_RDONLY, 0);
    if (shm_fd >= 0) {
        inst->track_shm = (pfx_track_audio_shm_t *)mmap(NULL,
            sizeof(pfx_track_audio_shm_t), PROT_READ, MAP_SHARED, shm_fd, 0);
        close(shm_fd);
        if (inst->track_shm == MAP_FAILED) {
            inst->track_shm = NULL;
            log_msg("pfx: track shm mmap failed");
        } else {
            log_msg("pfx: track shm mapped OK");
        }
    } else {
        inst->track_shm = NULL;
        log_msg("pfx: track shm not available (Link Audio may be disabled)");
    }

    return inst;
}

static void v2_destroy(void *instance) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst) return;
    if (inst->track_shm) {
        munmap(inst->track_shm, sizeof(pfx_track_audio_shm_t));
    }
    pfx_engine_destroy(&inst->engine);
    free(inst);
    log_msg("pfx: engine destroyed");
}

/* ---- MIDI ---- */

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || len < 3) return;

    uint8_t status = msg[0] & 0xF0;
    uint8_t d1 = msg[1];
    uint8_t d2 = msg[2];

    /* Polyphonic aftertouch for pad pressure */
    if (status == 0xA0) {
        int note = d1;
        float pressure = (float)d2 / 127.0f;
        /* Map pad notes to punch-in slots */
        /* Top row: 92-99 = punch 0-7, Third row: 84-91 = punch 8-15 */
        if (note >= 92 && note <= 99)
            pfx_punch_set_pressure(&inst->engine, note - 92, pressure);
        else if (note >= 84 && note <= 91)
            pfx_punch_set_pressure(&inst->engine, note - 84 + 8, pressure);
    }
}

/* ---- Parameters ---- */

static void v2_set_param(void *instance, const char *key, const char *val) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || !key || !val) return;
    perf_fx_engine_t *e = &inst->engine;
    float fval = (float)atof(val);
    int ival = atoi(val);

    /* Global params */
    if (strcmp(key, "dry_wet") == 0) { e->dry_wet = clampf(fval, 0, 1); return; }
    if (strcmp(key, "input_gain") == 0) { e->input_gain = clampf(fval, 0, 2); return; }
    if (strcmp(key, "output_gain") == 0) { e->output_gain = clampf(fval, 0, 2); return; }
    if (strcmp(key, "global_lp") == 0) { e->global_lp_cutoff = clampf(fval, 0, 1); return; }
    if (strcmp(key, "global_hp") == 0) { e->global_hp_cutoff = clampf(fval, 0, 1); return; }
    if (strcmp(key, "eq_low") == 0) { e->eq_low_gain = clampf(fval, -1, 1); return; }
    if (strcmp(key, "eq_mid") == 0) { e->eq_mid_gain = clampf(fval, -1, 1); return; }
    if (strcmp(key, "eq_high") == 0) { e->eq_high_gain = clampf(fval, -1, 1); return; }
    if (strcmp(key, "bpm") == 0) { e->bpm = clampf(fval, 20, 300); return; }
    if (strcmp(key, "bypass") == 0) { e->bypassed = ival; return; }
    if (strcmp(key, "pressure_curve") == 0) { e->pressure_curve = ival; return; }
    if (strcmp(key, "audio_source") == 0) { e->audio_source = ival; return; }
    if (strcmp(key, "track_mask") == 0) { e->track_mask = ival & 0x0F; return; }

    /* Punch-in FX: punch_N_on, punch_N_off, punch_N_pressure, punch_N_velocity */
    if (strncmp(key, "punch_", 6) == 0) {
        int slot = atoi(key + 6);
        const char *suffix = strchr(key + 6, '_');
        if (!suffix) return;
        suffix++;
        if (strcmp(suffix, "on") == 0) {
            pfx_punch_activate(e, slot, fval);
        } else if (strcmp(suffix, "off") == 0) {
            pfx_punch_deactivate(e, slot);
        } else if (strcmp(suffix, "pressure") == 0) {
            pfx_punch_set_pressure(e, slot, fval);
        } else if (strcmp(suffix, "velocity") == 0) {
            if (slot >= 0 && slot < PFX_NUM_PUNCH_IN)
                e->punch[slot].velocity = clampf(fval, 0, 1);
        }
        return;
    }

    /* Continuous FX: cont_N_toggle, cont_N_on, cont_N_off, cont_N_param_M */
    if (strncmp(key, "cont_", 5) == 0) {
        int slot = atoi(key + 5);
        const char *suffix = strchr(key + 5, '_');
        if (!suffix) return;
        suffix++;
        if (strcmp(suffix, "toggle") == 0) {
            pfx_cont_toggle(e, slot);
        } else if (strcmp(suffix, "on") == 0) {
            pfx_cont_activate(e, slot);
        } else if (strcmp(suffix, "off") == 0) {
            pfx_cont_deactivate(e, slot);
        } else if (strncmp(suffix, "param_", 6) == 0) {
            int param = atoi(suffix + 6);
            pfx_cont_set_param(e, slot, param, fval);
        }
        return;
    }

    /* Scene management: scene_save_N, scene_recall_N, scene_clear_N */
    if (strncmp(key, "scene_", 6) == 0) {
        if (strncmp(key + 6, "save_", 5) == 0) {
            pfx_scene_save(e, atoi(key + 11));
        } else if (strncmp(key + 6, "recall_", 7) == 0) {
            pfx_scene_recall(e, atoi(key + 13));
        } else if (strncmp(key + 6, "clear_", 6) == 0) {
            pfx_scene_clear(e, atoi(key + 12));
        }
        return;
    }

    /* Step preset management: step_save_N, step_recall_N, step_clear_N */
    if (strncmp(key, "step_", 5) == 0) {
        if (strncmp(key + 5, "save_", 5) == 0) {
            pfx_step_save(e, atoi(key + 10));
        } else if (strncmp(key + 5, "recall_", 7) == 0) {
            pfx_step_recall(e, atoi(key + 12));
        } else if (strncmp(key + 5, "clear_", 6) == 0) {
            pfx_step_clear(e, atoi(key + 11));
        }
        return;
    }

    /* Step FX sequencer */
    if (strcmp(key, "step_seq_active") == 0) { e->step_seq_active = ival; return; }
    if (strcmp(key, "step_seq_division") == 0) { e->step_seq_division = ival; return; }

    /* Ducker rate for punch 15 */
    if (strcmp(key, "ducker_rate") == 0) {
        e->punch[PUNCH_DUCKER].ducker.rate_div = ival;
        return;
    }

    /* Transport */
    if (strcmp(key, "transport_running") == 0) { e->transport_running = ival; return; }

    /* Scene morph */
    if (strncmp(key, "scene_morph_", 12) == 0) {
        /* scene_morph_A_B  e.g. scene_morph_0_3 */
        int a = atoi(key + 12);
        const char *bp = strchr(key + 12, '_');
        if (bp) {
            int b = atoi(bp + 1);
            pfx_scene_morph_start(e, a, b);
        }
        return;
    }
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || !key) return -1;
    perf_fx_engine_t *e = &inst->engine;

    if (strcmp(key, "name") == 0)
        return snprintf(buf, buf_len, "Performance FX");

    /* Global params */
    if (strcmp(key, "dry_wet") == 0) return snprintf(buf, buf_len, "%.3f", e->dry_wet);
    if (strcmp(key, "input_gain") == 0) return snprintf(buf, buf_len, "%.3f", e->input_gain);
    if (strcmp(key, "output_gain") == 0) return snprintf(buf, buf_len, "%.3f", e->output_gain);
    if (strcmp(key, "global_lp") == 0) return snprintf(buf, buf_len, "%.3f", e->global_lp_cutoff);
    if (strcmp(key, "global_hp") == 0) return snprintf(buf, buf_len, "%.3f", e->global_hp_cutoff);
    if (strcmp(key, "eq_low") == 0) return snprintf(buf, buf_len, "%.3f", e->eq_low_gain);
    if (strcmp(key, "eq_mid") == 0) return snprintf(buf, buf_len, "%.3f", e->eq_mid_gain);
    if (strcmp(key, "eq_high") == 0) return snprintf(buf, buf_len, "%.3f", e->eq_high_gain);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, buf_len, "%.1f", e->bpm);
    if (strcmp(key, "bypass") == 0) return snprintf(buf, buf_len, "%d", e->bypassed);
    if (strcmp(key, "pressure_curve") == 0) return snprintf(buf, buf_len, "%d", e->pressure_curve);
    if (strcmp(key, "audio_source") == 0) return snprintf(buf, buf_len, "%d", e->audio_source);
    if (strcmp(key, "track_mask") == 0) return snprintf(buf, buf_len, "%d", e->track_mask);
    if (strcmp(key, "track_audio_available") == 0) {
        pfx_instance_t *pi = (pfx_instance_t *)instance;
        return snprintf(buf, buf_len, "%d", pi->track_shm ? 1 : 0);
    }

    /* Punch-in FX info */
    if (strcmp(key, "punch_names") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_PUNCH_IN; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s\"%s\"", i ? "," : "", PUNCH_NAMES[i]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Continuous FX info */
    if (strcmp(key, "cont_names") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s\"%s\"", i ? "," : "", CONT_NAMES[i]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Continuous FX param names for a slot */
    if (strncmp(key, "cont_param_names_", 17) == 0) {
        int slot = atoi(key + 17);
        if (slot < 0 || slot >= PFX_NUM_CONTINUOUS) return -1;
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < 4; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s\"%s\"", i ? "," : "",
                         CONT_PARAM_NAMES[slot][i]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Continuous FX active state */
    if (strcmp(key, "cont_active") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_CONTINUOUS; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s%d", i ? "," : "", e->cont[i].active);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Scene populated state */
    if (strcmp(key, "scene_populated") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_SCENES; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s%d", i ? "," : "", e->scenes[i].populated);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Step preset populated state */
    if (strcmp(key, "step_populated") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_PRESETS; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s%d", i ? "," : "", e->step_presets[i].populated);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    if (strcmp(key, "current_step") == 0)
        return snprintf(buf, buf_len, "%d", e->current_step_preset);

    if (strcmp(key, "step_seq_active") == 0)
        return snprintf(buf, buf_len, "%d", e->step_seq_active);

    /* Scene morph state */
    if (strcmp(key, "morph_active") == 0)
        return snprintf(buf, buf_len, "%d", e->morph.active);
    if (strcmp(key, "morph_progress") == 0)
        return snprintf(buf, buf_len, "%.3f", e->morph.progress);

    /* Full state */
    if (strcmp(key, "state") == 0)
        return pfx_serialize_state(e, buf, buf_len);

    return -1;
}

static int v2_get_error(void *instance, char *buf, int buf_len) {
    (void)instance; (void)buf; (void)buf_len;
    return 0;
}

/* ---- Audio ---- */

static void v2_render(void *instance, int16_t *out_lr, int frames) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst) {
        memset(out_lr, 0, frames * 2 * sizeof(int16_t));
        return;
    }
    /* Copy per-track audio from shm if available */
    if (inst->track_shm && inst->track_shm->sequence != inst->last_track_seq) {
        inst->last_track_seq = inst->track_shm->sequence;
        int ch = inst->track_shm->channel_count;
        if (ch > 4) ch = 4;
        for (int t = 0; t < ch; t++) {
            memcpy(inst->track_bufs[t], inst->track_shm->audio[t],
                   frames * 2 * sizeof(int16_t));
            inst->engine.track_audio[t] = inst->track_bufs[t];
        }
        inst->engine.track_audio_valid = 1;
    } else {
        inst->engine.track_audio_valid = (inst->track_shm != NULL);
    }

    pfx_engine_render(&inst->engine, out_lr, frames);
}

/* ---- Entry point ---- */

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_api.create_instance = v2_create;
    g_api.destroy_instance = v2_destroy;
    g_api.on_midi = v2_on_midi;
    g_api.set_param = v2_set_param;
    g_api.get_param = v2_get_param;
    g_api.get_error = v2_get_error;
    g_api.render_block = v2_render;
    return &g_api;
}
