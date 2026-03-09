/*
 * pfx_bungee.cpp - C wrapper implementation for Bungee time-stretcher
 *
 * Provides a streaming interface around Bungee's grain-based API.
 * Audio is written into a circular buffer; Bungee reads from it at
 * the requested speed while preserving pitch.
 */

#include <bungee/Bungee.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

extern "C" {
#include "pfx_bungee.h"
}

/* Circular input buffer: 4 seconds at 44100 Hz stereo */
#define INPUT_BUF_SECS  4
#define INPUT_BUF_FRAMES(sr) ((sr) * INPUT_BUF_SECS)

/* Output accumulator: enough for a couple of render blocks */
#define OUT_BUF_FRAMES 1024

struct pfx_bungee {
    Bungee::Stretcher<Bungee::Basic> *stretcher;
    int sample_rate;
    int max_grain;

    /* Circular input buffer (non-interleaved: L then R) */
    float *input_buf;       /* [max_input * 2] — L block then R block */
    int input_buf_frames;   /* total frames capacity */
    int64_t write_pos;      /* monotonic write position (frame count) */

    /* Grain scratch buffer (non-interleaved) */
    float *grain_buf;

    /* Bungee request state */
    Bungee::Request req;

    /* Output accumulator (interleaved stereo) */
    float *out_buf;
    int out_count;          /* frames available in out_buf */
    int out_read;           /* read cursor in out_buf */

    float speed;
    bool needs_reset;
};

extern "C" pfx_bungee_t *pfx_bungee_create(int sample_rate) {
    auto *b = new pfx_bungee_t();
    b->sample_rate = sample_rate;
    b->speed = 0.5f;

    b->stretcher = new Bungee::Stretcher<Bungee::Basic>(
        Bungee::SampleRates{sample_rate, sample_rate}, 2, 0);

    b->max_grain = b->stretcher->maxInputFrameCount();
    b->grain_buf = (float *)calloc(b->max_grain * 2, sizeof(float));

    b->input_buf_frames = INPUT_BUF_FRAMES(sample_rate);
    b->input_buf = (float *)calloc(b->input_buf_frames * 2, sizeof(float));
    b->write_pos = 0;

    b->out_buf = (float *)calloc(OUT_BUF_FRAMES * 2, sizeof(float));
    b->out_count = 0;
    b->out_read = 0;

    b->req.position = 0.0;
    b->req.speed = (double)b->speed;
    b->req.pitch = 1.0;
    b->req.reset = true;
    b->req.resampleMode = resampleMode_autoOut;
    b->needs_reset = true;

    return b;
}

extern "C" void pfx_bungee_destroy(pfx_bungee_t *b) {
    if (!b) return;
    delete b->stretcher;
    free(b->input_buf);
    free(b->grain_buf);
    free(b->out_buf);
    delete b;
}

extern "C" void pfx_bungee_set_speed(pfx_bungee_t *b, float speed) {
    if (!b) return;
    b->speed = speed;
    b->req.speed = (double)speed;
}

extern "C" void pfx_bungee_reset(pfx_bungee_t *b) {
    if (!b) return;
    b->needs_reset = true;
    b->out_count = 0;
    b->out_read = 0;
}

extern "C" void pfx_bungee_write(pfx_bungee_t *b, const float *in_lr, int frames) {
    if (!b || !in_lr) return;

    /* Deinterleave into circular buffer */
    for (int i = 0; i < frames; i++) {
        int idx = (int)(b->write_pos % b->input_buf_frames);
        b->input_buf[idx] = in_lr[i * 2];                              /* L */
        b->input_buf[idx + b->input_buf_frames] = in_lr[i * 2 + 1];   /* R */
        b->write_pos++;
    }
}

/* Feed a grain from the circular buffer based on Bungee's InputChunk request */
static void feed_grain(pfx_bungee_t *b, const Bungee::InputChunk &chunk) {
    int len = chunk.end - chunk.begin;
    int stride = b->max_grain;

    memset(b->grain_buf, 0, stride * 2 * sizeof(float));

    for (int i = 0; i < len; i++) {
        int64_t src_pos = (int64_t)chunk.begin + i;
        /* Map absolute position to circular buffer index */
        int idx = (int)(((src_pos % b->input_buf_frames) + b->input_buf_frames) % b->input_buf_frames);

        /* Only feed if we actually have this data (not too far ahead of write) */
        if (src_pos >= 0 && src_pos < b->write_pos) {
            b->grain_buf[i] = b->input_buf[idx];
            b->grain_buf[stride + i] = b->input_buf[idx + b->input_buf_frames];
        }
    }

    int muteHead = std::max(0, (int)(-chunk.begin));
    int muteTail = 0;
    /* Mute frames we don't have yet */
    int64_t avail_end = b->write_pos;
    if ((int64_t)chunk.end > avail_end) {
        muteTail = (int)((int64_t)chunk.end - avail_end);
    }

    b->stretcher->analyseGrain(b->grain_buf, stride, muteHead, muteTail);
}

extern "C" int pfx_bungee_read(pfx_bungee_t *b, float *out_lr, int max_frames) {
    if (!b || !out_lr) return 0;

    /* On first call or after reset, initialize position near current write pos */
    if (b->needs_reset) {
        /* Start stretching from exactly the current playback position.
         * Primed audio behind this point gives bungee the look-back it needs
         * for its grain window. New audio flowing in provides the look-ahead. */
        int64_t start = b->write_pos;
        b->req.position = (double)start;
        b->req.speed = (double)b->speed;
        b->req.pitch = 1.0;
        b->req.reset = true;
        b->req.resampleMode = resampleMode_autoOut;

        /* Skip preroll — it pushes position backward which would desync.
         * reset=true on the first grain handles initialization. */
        b->needs_reset = false;
        b->out_count = 0;
        b->out_read = 0;
    }

    b->req.speed = (double)b->speed;

    /* Generate grains until we have enough output */
    int produced = 0;
    int safety = 128;

    while (produced < max_frames && safety-- > 0) {
        /* Check if accumulator has frames to drain */
        if (b->out_read < b->out_count) {
            int avail = b->out_count - b->out_read;
            int need = max_frames - produced;
            int take = std::min(avail, need);
            memcpy(out_lr + produced * 2,
                   b->out_buf + b->out_read * 2,
                   take * 2 * sizeof(float));
            produced += take;
            b->out_read += take;
            continue;
        }

        /* Accumulator empty — generate a new grain */
        b->out_count = 0;
        b->out_read = 0;

        /* Don't read ahead of what's been written */
        if (b->req.position >= (double)b->write_pos) {
            break; /* underrun — wait for more input */
        }

        Bungee::InputChunk chunk = b->stretcher->specifyGrain(b->req);
        feed_grain(b, chunk);

        Bungee::OutputChunk output{};
        b->stretcher->synthesiseGrain(output);

        int to_store = std::min(output.frameCount, OUT_BUF_FRAMES);
        for (int i = 0; i < to_store; i++) {
            b->out_buf[i * 2]     = output.data[i];
            b->out_buf[i * 2 + 1] = output.data[i + output.channelStride];
        }
        b->out_count = to_store;
        b->out_read = 0;

        b->stretcher->next(b->req);
        b->req.reset = false;
    }

    /* Zero-fill if we didn't produce enough */
    if (produced < max_frames) {
        memset(out_lr + produced * 2, 0, (max_frames - produced) * 2 * sizeof(float));
    }

    return produced;
}
