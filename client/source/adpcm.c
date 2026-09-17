#include "adpcm.h"

static const s16 STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
    7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const s8 INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static inline s16 clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (s16)v; }

void adpcm_encoder_init(AdpcmState *st, s16 first_sample) {
    st->predictor = first_sample;
    st->index = 0;
}

u8 adpcm_encode_sample(AdpcmState *st, s16 sample) {
    int step = STEP_TABLE[st->index];
    int diff = sample - st->predictor;
    u8 code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    int delta = step >> 3;
    if (diff >= step) {
        code |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2)) {
        code |= 1;
        delta += step >> 2;
    }
    st->predictor = clamp16((code & 8) ? st->predictor - delta : st->predictor + delta);
    int idx = st->index + INDEX_TABLE[code];
    st->index = idx < 0 ? 0 : idx > 88 ? 88 : (u8)idx;
    return code;
}

void adpcm_decoder_init(AdpcmState *st, const u8 *h) {
    st->predictor = (s16)(h[0] | (h[1] << 8));
    st->index = h[2] > 88 ? 88 : h[2];
}

s16 adpcm_decode_nibble(AdpcmState *st, u8 code) {
    int step = STEP_TABLE[st->index];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    st->predictor = clamp16((code & 8) ? st->predictor - delta : st->predictor + delta);
    int idx = st->index + INDEX_TABLE[code & 0x0F];
    st->index = idx < 0 ? 0 : idx > 88 ? 88 : (u8)idx;
    return st->predictor;
}

void adpcm_write_header(u8 *out4, const AdpcmState *st) {
    out4[0] = (u8)(st->predictor & 0xFF);
    out4[1] = (u8)((st->predictor >> 8) & 0xFF);
    out4[2] = st->index;
    out4[3] = 0;
}
