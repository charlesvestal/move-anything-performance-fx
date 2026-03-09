/*
 * pfx_bungee.h - C wrapper for Bungee time-stretcher in Performance FX
 *
 * Provides a streaming interface: write audio in, read stretched audio out.
 * Uses Bungee Basic (open-source) via its C function table API.
 */
#ifndef PFX_BUNGEE_H
#define PFX_BUNGEE_H

#include <stdint.h>

typedef struct pfx_bungee pfx_bungee_t;

/* Create a streaming stretcher. sample_rate = 44100 typically. */
pfx_bungee_t *pfx_bungee_create(int sample_rate);

/* Destroy and free all resources. */
void pfx_bungee_destroy(pfx_bungee_t *b);

/* Set playback speed (0.5 = half speed, 1.0 = normal). Pitch is preserved. */
void pfx_bungee_set_speed(pfx_bungee_t *b, float speed);

/* Reset the stretcher (call when activating the effect). */
void pfx_bungee_reset(pfx_bungee_t *b);

/* Feed input audio (interleaved stereo float, `frames` stereo frames).
 * Call this each block with the incoming audio. */
void pfx_bungee_write(pfx_bungee_t *b, const float *in_lr, int frames);

/* Read stretched output (interleaved stereo float, up to `max_frames`).
 * Returns number of frames actually produced. */
int pfx_bungee_read(pfx_bungee_t *b, float *out_lr, int max_frames);

#endif /* PFX_BUNGEE_H */
