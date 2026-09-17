#include "voice.h"
#include "adpcm.h"
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MIC_BUF_SIZE 0x8000                            // ~1 s ring at 16 360 Hz, 16-bit
#define MAX_SAMPLES ((VOICE_RATE * VOICE_MAX_MS) / 1000)  // 163 600
#define DPV_HEADER 12
#define DPV_CAPACITY (DPV_HEADER + 4 + (MAX_SAMPLES + 1) / 2)

static u8 *s_micbuf;
static bool s_mic_ok, s_ndsp_ok;

// recording
static bool s_recording;
static u32 s_read_off;
static u32 s_samples;
static AdpcmState s_enc;
static u8 *s_dpv;          // DPV_CAPACITY
static size_t s_dpv_len;   // valid after stop
static bool s_have_note;
static float s_levels[VOICE_LEVELS];
static float s_level_acc;
static u32 s_level_n;
static u64 s_rec_start_tick;

// playback
static ndspWaveBuf s_wave;
static s16 *s_pcm;          // linearAlloc
static u32 s_pcm_samples;
static char s_play_id[48];
static bool s_playing;
static s16 *s_beep;
static ndspWaveBuf s_beep_wave;

bool voice_init(void) {
    s_micbuf = (u8 *)memalign(0x1000, MIC_BUF_SIZE);
    if (s_micbuf) {
        s_mic_ok = R_SUCCEEDED(micInit(s_micbuf, MIC_BUF_SIZE));
        if (!s_mic_ok) {
            free(s_micbuf);
            s_micbuf = NULL;
        }
    }
    // ndsp needs the DSP firmware dump (sdmc:/3ds/dspfirm.cdc); without it we can
    // still record and send, just not play.
    s_ndsp_ok = R_SUCCEEDED(ndspInit());
    if (s_ndsp_ok) {
        ndspSetOutputMode(NDSP_OUTPUT_MONO);
        ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetInterp(1, NDSP_INTERP_LINEAR);
        ndspChnSetRate(1, 32000);
        ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
        float mix[12] = {1.0f, 1.0f};
        ndspChnSetMix(0, mix);
        ndspChnSetMix(1, mix);
        // 80 ms 880 Hz blip with a quick decay
        const int n = 32000 * 80 / 1000;
        s_beep = (s16 *)linearAlloc(n * 2);
        if (s_beep) {
            for (int i = 0; i < n; i++) {
                float env = 1.0f - (float)i / n;
                s_beep[i] = (s16)(sinf(i * 2.0f * (float)M_PI * 880.0f / 32000.0f) * 9000.0f * env * env);
            }
            DSP_FlushDataCache(s_beep, n * 2);
        }
    }
    s_dpv = (u8 *)malloc(DPV_CAPACITY);
    return s_mic_ok || s_ndsp_ok;
}

void voice_exit(void) {
    if (s_recording) voice_rec_cancel();
    voice_stop();
    if (s_ndsp_ok) {
        ndspChnWaveBufClear(1);
        ndspExit();
    }
    if (s_beep) linearFree(s_beep);
    if (s_pcm) linearFree(s_pcm);
    if (s_mic_ok) micExit();
    free(s_micbuf);
    free(s_dpv);
}

bool voice_can_record(void) { return s_mic_ok && s_dpv != NULL; }
bool voice_can_play(void) { return s_ndsp_ok; }

// ---- Recording ------------------------------------------------------------------------------------

static void write_u32(u8 *p, u32 v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

bool voice_rec_start(void) {
    if (!voice_can_record() || s_recording) return false;
    s_have_note = false;
    s_samples = 0;
    s_dpv_len = 0;
    s_level_acc = 0;
    s_level_n = 0;
    memset(s_levels, 0, sizeof(s_levels));
    MICU_SetPower(true);
    MICU_SetGain(0x50);
    Result rc = MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_16360, 0, micGetSampleDataSize(), true);
    if (R_FAILED(rc)) {
        MICU_SetPower(false);
        return false;
    }
    s_read_off = micGetLastSampleOffset();
    s_recording = true;
    s_rec_start_tick = svcGetSystemTick();
    memcpy(s_dpv, "DPV1", 4);
    return true;
}

static void encode_sample(s16 v) {
    if (s_samples >= MAX_SAMPLES) return;
    if (s_samples == 0) {
        adpcm_encoder_init(&s_enc, v);
        adpcm_write_header(s_dpv + DPV_HEADER, &s_enc);
    }
    u8 code = adpcm_encode_sample(&s_enc, v);
    u8 *nib = s_dpv + DPV_HEADER + 4 + (s_samples >> 1);
    if (s_samples & 1) *nib |= (u8)(code << 4);
    else *nib = code;
    s_samples++;
    float a = fabsf((float)v) / 32768.0f;
    if (a > s_level_acc) s_level_acc = a;
    if (++s_level_n >= VOICE_RATE / 12) {  // ~12 level samples per second
        memmove(s_levels, s_levels + 1, sizeof(float) * (VOICE_LEVELS - 1));
        s_levels[VOICE_LEVELS - 1] = s_level_acc;
        s_level_acc = 0;
        s_level_n = 0;
    }
}

static void drain_mic(void) {
    u32 data_size = micGetSampleDataSize();
    u32 end = micGetLastSampleOffset();
    if (end >= data_size) end = data_size - 2;
    u32 off = s_read_off;
    while (off != end) {
        if (off + 1 >= data_size) {
            off = 0;
            if (off == end) break;
        }
        s16 v = (s16)(s_micbuf[off] | (s_micbuf[off + 1] << 8));
        encode_sample(v);
        off += 2;
        if (off >= data_size) off = 0;
    }
    s_read_off = off;
}

void voice_update(void) {
    if (s_recording) {
        drain_mic();
        if (s_samples >= MAX_SAMPLES) voice_rec_stop();
    }
    if (s_playing && s_wave.status == NDSP_WBUF_DONE) {
        s_playing = false;
        s_play_id[0] = 0;
    }
}

void voice_rec_stop(void) {
    if (!s_recording) return;
    drain_mic();
    MICU_StopSampling();
    MICU_SetPower(false);
    s_recording = false;
    if (s_samples < VOICE_RATE / 5) {  // shorter than 0.2 s: treat as an accidental tap
        s_have_note = false;
        return;
    }
    write_u32(s_dpv + 4, VOICE_RATE);
    write_u32(s_dpv + 8, s_samples);
    s_dpv_len = DPV_HEADER + 4 + (s_samples + 1) / 2;
    s_have_note = true;
}

void voice_rec_cancel(void) {
    if (s_recording) {
        MICU_StopSampling();
        MICU_SetPower(false);
        s_recording = false;
    }
    s_have_note = false;
    s_samples = 0;
}

bool voice_rec_active(void) { return s_recording; }

int voice_rec_ms(void) {
    if (!s_recording && !s_have_note) return 0;
    return (int)((u64)s_samples * 1000 / VOICE_RATE);
}

float voice_rec_level(int i) {
    if (i < 0 || i >= VOICE_LEVELS) return 0;
    return s_levels[i];
}

const u8 *voice_rec_data(size_t *len) {
    if (!s_have_note) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = s_dpv_len;
    return s_dpv;
}

void voice_rec_discard(void) {
    s_have_note = false;
    s_samples = 0;
}

// ---- Playback ---------------------------------------------------------------------------------------------

void voice_stop(void) {
    if (!s_ndsp_ok) return;
    ndspChnWaveBufClear(0);
    s_playing = false;
    s_play_id[0] = 0;
    if (s_pcm) {
        linearFree(s_pcm);
        s_pcm = NULL;
    }
}

bool voice_play(const u8 *dpv, size_t len, const char *id) {
    if (!s_ndsp_ok || !dpv || len < 16 || memcmp(dpv, "DPV1", 4) != 0) return false;
    u32 rate = dpv[4] | (dpv[5] << 8) | (dpv[6] << 16) | ((u32)dpv[7] << 24);
    u32 count = dpv[8] | (dpv[9] << 8) | (dpv[10] << 16) | ((u32)dpv[11] << 24);
    if (rate < 8000 || rate > 48000 || count == 0 || count > MAX_SAMPLES || count > (len - 16) * 2) return false;
    voice_stop();
    s_pcm = (s16 *)linearAlloc(count * 2);
    if (!s_pcm) return false;
    AdpcmState st;
    adpcm_decoder_init(&st, dpv + 12);
    const u8 *nib = dpv + 16;
    for (u32 i = 0; i < count; i++) {
        u8 byte = nib[i >> 1];
        s_pcm[i] = adpcm_decode_nibble(&st, (i & 1) ? (byte >> 4) : (byte & 0x0F));
    }
    DSP_FlushDataCache(s_pcm, count * 2);
    s_pcm_samples = count;
    ndspChnSetRate(0, (float)rate);
    memset(&s_wave, 0, sizeof(s_wave));
    s_wave.data_vaddr = s_pcm;
    s_wave.nsamples = count;
    s_wave.looping = false;
    ndspChnWaveBufAdd(0, &s_wave);
    s_playing = true;
    strncpy(s_play_id, id ? id : "", sizeof(s_play_id) - 1);
    return true;
}

bool voice_is_playing(const char *id) {
    if (!s_playing) return false;
    if (!id) return true;
    return strcmp(s_play_id, id) == 0;
}

float voice_play_progress(void) {
    if (!s_playing || !s_pcm_samples) return 0;
    u32 pos = ndspChnGetSamplePos(0);
    float p = (float)pos / (float)s_pcm_samples;
    return p > 1 ? 1 : p;
}

void voice_beep(void) {
    if (!s_ndsp_ok || !s_beep) return;
    ndspChnWaveBufClear(1);
    memset(&s_beep_wave, 0, sizeof(s_beep_wave));
    s_beep_wave.data_vaddr = s_beep;
    s_beep_wave.nsamples = 32000 * 80 / 1000;
    ndspChnWaveBufAdd(1, &s_beep_wave);
}
