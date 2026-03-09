/*
 * Performance FX Plugin v2 (Audio FX API v2)
 *
 * Wrapper around the perf_fx_dsp engine.
 * 32 unified punch-in FX with latch support.
 * Exports as an audio FX plugin (in-place processing).
 */

#include "perf_fx_dsp.h"
#include "plugin_api_v1.h"
#include "audio_fx_api_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "pfx_track_shm.h"

#define clampf pfx_clampf

#define SAFE_SNPRINTF(buf, n, len, ...) do { \
    n += snprintf((buf) + (n), (n) < (len) ? (len) - (n) : 0, __VA_ARGS__); \
    if ((n) >= (len)) return (len) - 1; \
} while(0)

static const host_api_v1_t *g_host = NULL;
static audio_fx_api_v2_t g_fx_api;

/* FX names for all 32 slots */
static const char *FX_NAMES[PFX_NUM_FX] = {
    /* Row 4: Time/Repeat (slots 0-7) */
    "RPT 1/4", "RPT 1/8", "RPT 1/16", "RPT Trip",
    "Stutter", "Scatter", "Reverse", "Timestretch",
    /* Row 3: Filter Sweeps (slots 8-15) */
    "LP Sweep", "HP Sweep", "BP Rise", "BP Fall",
    "Reso Sweep", "Phaser", "Flanger", "Auto Filter",
    /* Row 2: Space Throws (slots 16-23) */
    "Delay 1/4", "Delay D8", "PingPong 1/4", "PingPong D8",
    "Room", "Hall", "Dark Verb", "Spring",
    /* Row 1: Distortion & Rhythm (slots 24-31) */
    "Bitcrush", "Downsample", "Saturate", "Gate/Duck",
    "Tremolo", "Octave Down", "Vinyl Sim", "Vinyl Brake"
};

/* Per-FX param names (4 params each, mapped to E1-E4) */
static const char *FX_PARAM_NAMES[PFX_NUM_FX][PFX_SLOT_PARAMS] = {
    /* Row 4: Time/Repeat */
    {"Speed",  "Filter", "Gate"},           /* RPT 1/4 */
    {"Speed",  "Filter", "Gate"},           /* RPT 1/8 */
    {"Speed",  "Filter", "Gate"},           /* RPT 1/16 */
    {"Speed",  "Filter", "Gate"},           /* RPT Trip */
    {"Speed",  "Filter", "Gate"},           /* Stutter */
    {"Pattern", "Gate",  "Revrse"},         /* Scatter */
    {"Feedbk", "Filter", "Mix"},            /* Reverse */
    {"Tone",   "WowFlt", "Mix"},            /* Half Speed */
    /* Row 3: Filter Sweeps */
    {"Speed",  "Reso",   "Depth"},          /* LP Sweep Down */
    {"Speed",  "Reso",   "Depth"},          /* HP Sweep Up */
    {"Speed",  "Reso",   "Width"},          /* BP Rise */
    {"Speed",  "Reso",   "Width"},          /* BP Fall */
    {"Speed",  "Reso",   "Mix"},            /* Reso Sweep */
    {"Depth",  "Feedbk", "Mix"},            /* Phaser */
    {"Depth",  "Feedbk", "Mix"},            /* Flanger */
    {"Depth",  "Center", "Reso"},           /* Auto Filter */
    /* Row 2: Space Throws */
    {"Feedbk", "Tone",   "Level"},          /* Delay 1/4 */
    {"Feedbk", "Tone",   "Level"},          /* Delay Dot8 */
    {"Feedbk", "Tone",   "Level"},          /* Ping Pong */
    {"Feedbk", "Tone",   "Level"},          /* PP Dot8 */
    {"Time",   "Tone",   "Level"},          /* Reverb */
    {"Time",   "Tone",   "Level"},          /* Hall */
    {"Time",   "Dark",   "Level"},          /* Dark Verb */
    {"Time",   "Tone",   "Level"},          /* Spring */
    /* Row 1: Distortion & Rhythm */
    {"Filter", "Tone",   "Mix"},            /* Bitcrush */
    {"Filter", "Tone",   "Mix"},            /* Downsample */
    {"Drive",  "Tone",   "Mix"},            /* Saturate */
    {"Speed",  "Shape",  "Depth"},          /* Gate/Duck */
    {"Rate",   "Depth",  "Wave"},           /* Tremolo */
    {"Tone",   "WowFlt", "Mix"},            /* Octave Down */
    {"Noise",  "WowFlt", "Tone"},           /* Vinyl Sim */
    {"Speed",  "Tone",   "Mix"}             /* Tape Stop */
};

typedef struct {
    perf_fx_engine_t engine;
    char module_dir[256];
    pfx_track_audio_shm_t *track_shm;
    uint32_t last_track_seq;
    int16_t track_bufs[4][PFX_TRACK_SHM_FRAMES * 2];
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

static void *fx_create(const char *module_dir, const char *config_json) {
    (void)config_json;
    pfx_instance_t *inst = (pfx_instance_t *)calloc(1, sizeof(pfx_instance_t));
    if (!inst) return NULL;

    if (module_dir)
        snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", module_dir);

    pfx_engine_init(&inst->engine);

    /* Wire up log callback so engine can log diagnostics */
    inst->engine.log_fn = (g_host && g_host->log) ? g_host->log : NULL;

    if (g_host) {
        inst->engine.mapped_memory = g_host->mapped_memory;
        inst->engine.audio_out_offset = g_host->audio_out_offset;
        inst->engine.audio_in_offset = g_host->audio_in_offset;
    }

    /* Load vinyl crackle sample */
    if (module_dir) {
        char crackle_path[512];
        snprintf(crackle_path, sizeof(crackle_path), "%s/vinyl_crackle.wav", module_dir);
        pfx_engine_load_vinyl_crackle(&inst->engine, crackle_path);
        log_msg("pfx: vinyl crackle: %s (%d samples)",
                inst->engine.vinyl_crackle_buf ? "loaded" : "not found",
                inst->engine.vinyl_crackle_len);
    }

    log_msg("pfx: Performance FX v2 engine initialized (32 unified FX)");

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
    }

    return inst;
}

static void fx_destroy(void *instance) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst) return;
    if (inst->track_shm) {
        munmap(inst->track_shm, sizeof(pfx_track_audio_shm_t));
    }
    pfx_engine_destroy(&inst->engine);
    free(inst);
    log_msg("pfx: engine destroyed");
}

/* ---- Audio (in-place processing) ---- */

static void fx_process_block(void *instance, int16_t *audio_inout, int frames) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst) return;

    /* Copy per-track audio from shm if available (disabled — always Move Mix for now) */
    if (inst->track_shm) {
        uint32_t cur_seq = inst->track_shm->sequence;
        if (cur_seq != inst->last_track_seq) {
            inst->last_track_seq = cur_seq;
            int ch = inst->track_shm->channel_count;
            if (ch > 4) ch = 4;
            for (int t = 0; t < ch; t++) {
                memcpy(inst->track_bufs[t], inst->track_shm->audio[t],
                       frames * 2 * sizeof(int16_t));
                inst->engine.track_audio[t] = inst->track_bufs[t];
            }
        }
        inst->engine.track_audio_valid = (inst->engine.track_audio[0] != NULL);
    } else {
        inst->engine.track_audio_valid = 0;
    }

    inst->engine.direct_input = audio_inout;
    pfx_engine_render(&inst->engine, audio_inout, frames);
    inst->engine.direct_input = NULL;
}

/* ---- Parameters ---- */

static void fx_set_param(void *instance, const char *key, const char *val) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || !key || !val) return;
    perf_fx_engine_t *e = &inst->engine;
    float fval = (float)atof(val);
    int ival = atoi(val);

    /* Global params (E6-E8) */
    if (strcmp(key, "dj_filter") == 0) { e->dj_filter = clampf(fval, 0, 1); return; }
    if (strcmp(key, "tilt_eq") == 0) { e->tilt_eq = clampf(fval, 0, 1); return; }
    if (strcmp(key, "dry_wet") == 0) { e->dry_wet = clampf(fval, 0, 1); return; }
    if (strcmp(key, "repeat_rate") == 0) { e->repeat_rate = clampf(fval, 0, 1); return; }
    if (strcmp(key, "repeat_speed") == 0) { e->repeat_speed = clampf(fval, 0, 1); return; }

    /* Engine params */
    if (strcmp(key, "bpm") == 0) { e->bpm = clampf(fval, 20, 300); return; }
    if (strcmp(key, "bypass") == 0) { e->bypassed = ival; return; }
    if (strcmp(key, "pressure_curve") == 0) { e->pressure_curve = ival; return; }
    if (strcmp(key, "audio_source") == 0) { e->audio_source = ival; return; }
    if (strcmp(key, "track_mask") == 0) { e->track_mask = ival & 0x0F; return; }
    if (strcmp(key, "transport_running") == 0) { e->transport_running = ival; return; }

    /* Unified punch FX: punch_N_on, punch_N_off, punch_N_pressure,
     * punch_N_param_M, punch_N_latch */
    if (strncmp(key, "punch_", 6) == 0) {
        int slot = atoi(key + 6);
        const char *suffix = strchr(key + 6, '_');
        if (!suffix) return;
        suffix++;
        if (strcmp(suffix, "on") == 0) {
            pfx_activate(e, slot, fval > 0.0f ? fval : 0.7f);
            log_msg("pfx: ON slot=%d vel=%.3f", slot, fval);
        } else if (strcmp(suffix, "off") == 0) {
            pfx_deactivate(e, slot);
            log_msg("pfx: OFF slot=%d", slot);
        } else if (strcmp(suffix, "pressure") == 0) {
            pfx_set_pressure(e, slot, fval);
            /* Log pressure with settling state */
            pfx_slot_t *s = &e->slots[slot];
            log_msg("pfx: PRES slot=%d p=%.3f vel=%.3f settle=%d pr=%.3f",
                    slot, s->pressure, s->velocity, s->settle_counter,
                    pressure_relative(s->pressure, s->velocity, s->settle_counter));
        } else if (strcmp(suffix, "latch") == 0) {
            pfx_set_latched(e, slot, ival);
        } else if (strncmp(suffix, "param_", 6) == 0) {
            int param = atoi(suffix + 6);
            pfx_set_param(e, slot, param, fval);
        }
        return;
    }

}

static int fx_get_param(void *instance, const char *key, char *buf, int buf_len) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || !key) return -1;
    perf_fx_engine_t *e = &inst->engine;

    if (strcmp(key, "name") == 0)
        return snprintf(buf, buf_len, "Performance FX");

    /* Global params */
    if (strcmp(key, "dj_filter") == 0) return snprintf(buf, buf_len, "%.3f", e->dj_filter);
    if (strcmp(key, "tilt_eq") == 0) return snprintf(buf, buf_len, "%.3f", e->tilt_eq);
    if (strcmp(key, "dry_wet") == 0) return snprintf(buf, buf_len, "%.3f", e->dry_wet);
    if (strcmp(key, "repeat_rate") == 0) return snprintf(buf, buf_len, "%.3f", e->repeat_rate);
    if (strcmp(key, "repeat_speed") == 0) return snprintf(buf, buf_len, "%.3f", e->repeat_speed);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, buf_len, "%.1f", e->bpm);
    if (strcmp(key, "bypass") == 0) return snprintf(buf, buf_len, "%d", e->bypassed);
    if (strcmp(key, "pressure_curve") == 0) return snprintf(buf, buf_len, "%d", e->pressure_curve);
    if (strcmp(key, "audio_source") == 0) return snprintf(buf, buf_len, "%d", e->audio_source);
    if (strcmp(key, "track_mask") == 0) return snprintf(buf, buf_len, "%d", e->track_mask);
    if (strcmp(key, "track_audio_available") == 0) {
        return snprintf(buf, buf_len, "%d", inst->track_shm ? 1 : 0);
    }
    if (strcmp(key, "last_touched") == 0) {
        return snprintf(buf, buf_len, "%d", e->last_touched_slot);
    }

    /* FX names (all 32) */
    if (strcmp(key, "fx_names") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_FX; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s\"%s\"", i ? "," : "", FX_NAMES[i]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* FX active state (all 32) */
    if (strcmp(key, "fx_active") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_FX; i++) {
            pfx_slot_t *s = &e->slots[i];
            SAFE_SNPRINTF(buf, n, buf_len, "%s%d", i ? "," : "",
                          s->active || s->tail_active);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* FX latched state (all 32) */
    if (strcmp(key, "fx_latched") == 0) {
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_NUM_FX; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s%d", i ? "," : "",
                          e->slots[i].latched);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Per-FX param names */
    if (strncmp(key, "fx_param_names_", 15) == 0) {
        int slot = atoi(key + 15);
        if (slot < 0 || slot >= PFX_NUM_FX) return -1;
        int n = snprintf(buf, buf_len, "[");
        for (int i = 0; i < PFX_SLOT_PARAMS; i++) {
            SAFE_SNPRINTF(buf, n, buf_len, "%s\"%s\"", i ? "," : "",
                         FX_PARAM_NAMES[slot][i]);
        }
        SAFE_SNPRINTF(buf, n, buf_len, "]");
        return n;
    }

    /* Full state */
    if (strcmp(key, "state") == 0)
        return pfx_serialize_state(e, buf, buf_len);

    return -1;
}

/* ---- MIDI ---- */

static void fx_on_midi(void *instance, const uint8_t *msg, int len, int source) {
    pfx_instance_t *inst = (pfx_instance_t *)instance;
    if (!inst || len < 3) return;
    (void)source;

    uint8_t status = msg[0] & 0xF0;
    uint8_t d1 = msg[1];
    uint8_t d2 = msg[2];

    /* Polyphonic aftertouch for pad pressure — all 4 rows */
    if (status == 0xA0) {
        int note = d1;
        float pressure = (float)d2 / 127.0f;

        /* Row 4: pads 92-99 -> slots 0-7 */
        if (note >= 92 && note <= 99)
            pfx_set_pressure(&inst->engine, note - 92, pressure);
        /* Row 3: pads 84-91 -> slots 8-15 */
        else if (note >= 84 && note <= 91)
            pfx_set_pressure(&inst->engine, note - 84 + 8, pressure);
        /* Row 2: pads 76-83 -> slots 16-23 */
        else if (note >= 76 && note <= 83)
            pfx_set_pressure(&inst->engine, note - 76 + 16, pressure);
        /* Row 1: pads 68-75 -> slots 24-31 */
        else if (note >= 68 && note <= 75)
            pfx_set_pressure(&inst->engine, note - 68 + 24, pressure);
    }
}

/* ---- Entry point (Audio FX API v2) ---- */

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    memset(&g_fx_api, 0, sizeof(g_fx_api));
    g_fx_api.api_version = AUDIO_FX_API_VERSION_2;
    g_fx_api.create_instance = fx_create;
    g_fx_api.destroy_instance = fx_destroy;
    g_fx_api.process_block = fx_process_block;
    g_fx_api.set_param = fx_set_param;
    g_fx_api.get_param = fx_get_param;
    g_fx_api.on_midi = fx_on_midi;
    return &g_fx_api;
}
