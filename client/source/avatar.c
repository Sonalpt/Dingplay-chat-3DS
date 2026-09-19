#include "avatar.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AVATAR_PX 48
#define TEX_PX 64          // next power of two
#define AVATAR_SLOTS 20    // 20 × 2 variants × 16 KB = 640 KB of linear heap

// ---- Texture upload ---------------------------------------------------------------------
// The PICA200 wants 8×8-tiled, Morton-ordered texels with v running bottom-up.
// Rather than swizzle by hand, hand a linear top-down image to the GPU's
// display-transfer engine with FLIP_VERT + OUT_TILED — the same layout tex3ds
// produces, so citro2d's usual subtexture (top = 1, bottom = 1 - h/th) is upright.

#define TRANSFER_FLAGS                                                                                 \
    (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |                    \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

void tex_upload_rgba(C3D_Tex *tex, const u8 *rgba, int w, int h, int stride) {
    const int tw = tex->width, th = tex->height;
    u32 *lin = (u32 *)linearAlloc((size_t)tw * th * 4);
    if (!lin) return;
    memset(lin, 0, (size_t)tw * th * 4);
    for (int y = 0; y < h && y < th; y++) {
        const u8 *row = rgba + (size_t)y * stride * 4;
        u32 *dst = lin + (size_t)y * tw;
        for (int x = 0; x < w && x < tw; x++) {
            // Linear RGBA8 for the transfer engine is stored as ABGR in memory (u32 = R<<24|G<<16|B<<8|A).
            dst[x] = ((u32)row[x * 4] << 24) | ((u32)row[x * 4 + 1] << 16) | ((u32)row[x * 4 + 2] << 8) | row[x * 4 + 3];
        }
    }
    GSPGPU_FlushDataCache(lin, (size_t)tw * th * 4);
    C3D_SyncDisplayTransfer(lin, GX_BUFFER_DIM(tw, th), (u32 *)tex->data, GX_BUFFER_DIM(tw, th), TRANSFER_FLAGS);
    linearFree(lin);
}

// ---- Cache ---------------------------------------------------------------------------------

typedef struct {
    char uid[40];
    bool used, loaded, failed, requested;
    u32 last_use;
    C3D_Tex tex[2];  // 0 = rounded square, 1 = circle
    Tex3DS_SubTexture sub;
    C2D_Image img[2];
} Slot;

static Slot s_slots[AVATAR_SLOTS];
static u32 s_tick;
static void (*s_fetch)(const char *uid);

void avatar_set_fetcher(void (*fetch)(const char *uid)) { s_fetch = fetch; }

void avatar_init(void) {
    memset(s_slots, 0, sizeof(s_slots));
}

static void free_slot(Slot *s) {
    if (s->loaded) {
        C3D_TexDelete(&s->tex[0]);
        C3D_TexDelete(&s->tex[1]);
    }
    memset(s, 0, sizeof(*s));
}

void avatar_exit(void) {
    for (int i = 0; i < AVATAR_SLOTS; i++) free_slot(&s_slots[i]);
}

void avatar_clear(void) { avatar_exit(); }

static Slot *find(const char *uid) {
    for (int i = 0; i < AVATAR_SLOTS; i++)
        if (s_slots[i].used && strcmp(s_slots[i].uid, uid) == 0) return &s_slots[i];
    return NULL;
}

static Slot *alloc(const char *uid) {
    Slot *victim = NULL;
    for (int i = 0; i < AVATAR_SLOTS; i++) {
        if (!s_slots[i].used) {
            victim = &s_slots[i];
            break;
        }
        if (!victim || s_slots[i].last_use < victim->last_use) victim = &s_slots[i];
    }
    free_slot(victim);
    victim->used = true;
    strncpy(victim->uid, uid, sizeof(victim->uid) - 1);
    return victim;
}

C2D_Image *avatar_get(const char *uid, bool round) {
    s_tick++;
    Slot *s = find(uid);
    if (!s) {
        s = alloc(uid);
    }
    s->last_use = s_tick;
    if (s->loaded) return &s->img[round ? 1 : 0];
    if (!s->requested && !s->failed && s_fetch) {
        s->requested = true;
        s_fetch(uid);
    }
    return NULL;
}

void avatar_mark_failed(const char *uid) {
    Slot *s = find(uid);
    if (s) s->failed = true;
}

// Alpha-masks a copy of the avatar into a circle or a rounded square (radius 28 %).
static void masked(u8 *dst, const u8 *src, int size, bool round) {
    const float c = size / 2.0f;
    const float r = round ? c : size * 0.28f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float d;
            if (round) {
                d = sqrtf((px - c) * (px - c) + (py - c) * (py - c)) - r;
            } else {
                // signed distance to rounded rect
                float qx = fabsf(px - c) - (c - r), qy = fabsf(py - c) - (c - r);
                float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
                d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r;
            }
            float a = 0.5f - d;  // 1 px antialias
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            const u8 *sp = src + (y * size + x) * 4;
            u8 *dp = dst + (y * size + x) * 4;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = (u8)(sp[3] * a);
        }
    }
}

void avatar_put(const char *uid, const u8 *rgba, int size) {
    if (size != AVATAR_PX) return;
    Slot *s = find(uid);
    if (!s) s = alloc(uid);
    if (s->loaded) {
        C3D_TexDelete(&s->tex[0]);
        C3D_TexDelete(&s->tex[1]);
        s->loaded = false;
    }
    u8 *tmp = (u8 *)malloc(size * size * 4);
    if (!tmp) return;
    for (int v = 0; v < 2; v++) {
        if (!C3D_TexInit(&s->tex[v], TEX_PX, TEX_PX, GPU_RGBA8)) {
            if (v == 1) C3D_TexDelete(&s->tex[0]);
            free(tmp);
            s->failed = true;
            return;
        }
        memset(s->tex[v].data, 0, s->tex[v].size);
        masked(tmp, rgba, size, v == 1);
        tex_upload_rgba(&s->tex[v], tmp, size, size, size);
        C3D_TexSetFilter(&s->tex[v], GPU_LINEAR, GPU_LINEAR);
    }
    free(tmp);
    // FLIP_VERT put image row 0 at the top of v-space: sample from top = 1 down.
    s->sub.width = size;
    s->sub.height = size;
    s->sub.left = 0.0f;
    s->sub.right = (float)size / TEX_PX;
    s->sub.top = 1.0f;
    s->sub.bottom = 1.0f - (float)size / TEX_PX;
    for (int v = 0; v < 2; v++) {
        s->img[v].tex = &s->tex[v];
        s->img[v].subtex = &s->sub;
    }
    s->loaded = true;
    s->failed = false;
}
