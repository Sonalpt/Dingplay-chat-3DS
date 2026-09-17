#pragma once
// IMA ADPCM, 4 bits/sample, mono. Twin of relay/src/lib/adpcm.js — keep in sync.
// Stream layout: int16 predictor, u8 step index, u8 pad, then packed nibbles
// (low nibble first). The DPV1 container (voice.h) wraps it with rate + count.
#include <3ds.h>

typedef struct {
    s16 predictor;
    u8 index;
} AdpcmState;

void adpcm_encoder_init(AdpcmState *st, s16 first_sample);
u8 adpcm_encode_sample(AdpcmState *st, s16 sample);
void adpcm_decoder_init(AdpcmState *st, const u8 *header4);
s16 adpcm_decode_nibble(AdpcmState *st, u8 code);
void adpcm_write_header(u8 *out4, const AdpcmState *st);
