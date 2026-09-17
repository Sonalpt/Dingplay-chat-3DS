#pragma once
// Voice notes: mic capture → IMA-ADPCM as it records (no 10 s PCM buffer held),
// packed in the DPV1 container; playback through the DSP (ndsp).
//
// DPV1: "DPV1" | u32 sampleRate | u32 sampleCount | adpcm(header 4 + nibbles)
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

#define VOICE_MAX_MS 10000
#define VOICE_RATE 16360
#define VOICE_LEVELS 12

bool voice_init(void);
void voice_exit(void);
void voice_update(void);              // once per frame (feeds the encoder, reclaims buffers)
bool voice_can_record(void);
bool voice_can_play(void);

// Recording (hold-to-record UI)
bool voice_rec_start(void);
void voice_rec_stop(void);            // keeps the note for voice_rec_data()
void voice_rec_cancel(void);
bool voice_rec_active(void);
int voice_rec_ms(void);
float voice_rec_level(int i);         // 0..1, i in [0, VOICE_LEVELS) — newest last
const u8 *voice_rec_data(size_t *len);
void voice_rec_discard(void);

// Playback
bool voice_play(const u8 *dpv, size_t len, const char *id);
void voice_stop(void);
bool voice_is_playing(const char *id);  // NULL = anything playing
float voice_play_progress(void);        // 0..1
void voice_beep(void);                  // notification blip
