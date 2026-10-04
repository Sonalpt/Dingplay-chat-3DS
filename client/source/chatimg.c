#include "chatimg.h"
#include "avatar.h"   // tex_upload_rgba
#include "app.h"
#include "dbg.h"
#include <3ds.h>
#include <string.h>

#define TEX_W 256     // next pow2 >= CHATIMG_W / CHATIMG_H
#define TEX_H 128
#define IMG_SLOTS 6   // 6 * 256*128*4 = 768 KB of linear heap

typedef struct {
    char id[UID_LEN];
    bool used, loaded, failed, requested;
    u32 last_use;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} Slot;

static Slot s_slots[IMG_SLOTS];
static u32 s_tick;
static void (*s_fetch)(const char *id);

void chatimg_set_fetcher(void (*fetch)(const char *id)) { s_fetch = fetch; }
void chatimg_init(void) { memset(s_slots, 0, sizeof(s_slots)); }

static void free_slot(Slot *s) {
    if (s->loaded) C3D_TexDelete(&s->tex);
    memset(s, 0, sizeof(*s));
}

void chatimg_clear(void) {
    for (int i = 0; i < IMG_SLOTS; i++) free_slot(&s_slots[i]);
}
void chatimg_exit(void) { chatimg_clear(); }

static Slot *find(const char *id) {
    for (int i = 0; i < IMG_SLOTS; i++)
        if (s_slots[i].used && strncmp(s_slots[i].id, id, sizeof(s_slots[i].id)) == 0) return &s_slots[i];
    return NULL;
}

static Slot *alloc(const char *id) {
    Slot *lru = &s_slots[0];
    for (int i = 0; i < IMG_SLOTS; i++) {
        if (!s_slots[i].used) { lru = &s_slots[i]; break; }
        if (s_slots[i].last_use < lru->last_use) lru = &s_slots[i];
    }
    free_slot(lru);
    lru->used = true;
    strncpy(lru->id, id, sizeof(lru->id) - 1);
    return lru;
}

C2D_Image *chatimg_get(const char *id) {
    s_tick++;
    Slot *s = find(id);
    if (!s) s = alloc(id);
    s->last_use = s_tick;
    if (s->loaded) return &s->img;
    if (!s->requested && !s->failed && s_fetch) {
        s->requested = true;
        s_fetch(id);
    }
    return NULL;
}

void chatimg_put(const char *id, const unsigned char *rgba, int w, int h) {
    Slot *s = find(id);
    if (!s) s = alloc(id);
    if (w > TEX_W) w = TEX_W;
    if (h > TEX_H) h = TEX_H;
    if (!C3D_TexInit(&s->tex, TEX_W, TEX_H, GPU_RGBA8)) {
        dbg_log("chatimg_put %s: C3D_TexInit FAILED", id);
        s->failed = true;
        return;
    }
    tex_upload_rgba(&s->tex, rgba, w, h, w);
    dbg_log("chatimg_put %s: loaded %dx%d", id, w, h);
    C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
    s->sub.width = w;
    s->sub.height = h;
    s->sub.left = 0.0f;
    s->sub.right = (float)w / TEX_W;
    s->sub.top = 1.0f;                       // memory row 0 maps to v = 1 (see tex_upload_rgba)
    s->sub.bottom = 1.0f - (float)h / TEX_H;
    s->img.tex = &s->tex;
    s->img.subtex = &s->sub;
    s->loaded = true;
    s->failed = false;
    s->requested = false;
}

void chatimg_mark_failed(const char *id) {
    Slot *s = find(id);
    if (!s) s = alloc(id);
    s->failed = true;
    s->requested = false;
}

bool chatimg_failed(const char *id) {
    Slot *s = find(id);
    return s && s->failed;
}

// ---- Fullscreen viewer: one full-size image at a time -----------------------
#define BIG_TEX_W 512   // pow2 >= CHATIMG_BIG_W / CHATIMG_BIG_H
#define BIG_TEX_H 256
static struct {
    char id[UID_LEN];
    bool loaded, failed, requested;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} s_big;
static void (*s_big_fetch)(const char *id);

void chatimg_big_set_fetcher(void (*fetch)(const char *id)) { s_big_fetch = fetch; }

void chatimg_big_clear(void) {
    if (s_big.loaded) C3D_TexDelete(&s_big.tex);
    memset(&s_big, 0, sizeof(s_big));
}

C2D_Image *chatimg_big_get(const char *id) {
    if (strncmp(s_big.id, id, sizeof(s_big.id)) != 0) {
        chatimg_big_clear();
        strncpy(s_big.id, id, sizeof(s_big.id) - 1);
    }
    if (s_big.loaded) return &s_big.img;
    if (!s_big.requested && !s_big.failed && s_big_fetch) {
        s_big.requested = true;
        s_big_fetch(id);
    }
    return NULL;
}

void chatimg_big_put(const char *id, const unsigned char *rgba, int w, int h) {
    if (strncmp(s_big.id, id, sizeof(s_big.id)) != 0) return;  // viewer moved on
    if (w > BIG_TEX_W) w = BIG_TEX_W;
    if (h > BIG_TEX_H) h = BIG_TEX_H;
    if (!C3D_TexInit(&s_big.tex, BIG_TEX_W, BIG_TEX_H, GPU_RGBA8)) {
        s_big.failed = true;
        return;
    }
    tex_upload_rgba(&s_big.tex, rgba, w, h, w);
    C3D_TexSetFilter(&s_big.tex, GPU_LINEAR, GPU_LINEAR);
    s_big.sub.width = w;
    s_big.sub.height = h;
    s_big.sub.left = 0.0f;
    s_big.sub.right = (float)w / BIG_TEX_W;
    s_big.sub.top = 1.0f;
    s_big.sub.bottom = 1.0f - (float)h / BIG_TEX_H;
    s_big.img.tex = &s_big.tex;
    s_big.img.subtex = &s_big.sub;
    s_big.loaded = true;
    s_big.failed = false;
    s_big.requested = false;
}

void chatimg_big_mark_failed(const char *id) {
    if (strncmp(s_big.id, id, sizeof(s_big.id)) == 0) { s_big.failed = true; s_big.requested = false; }
}

bool chatimg_big_failed(const char *id) {
    return strncmp(s_big.id, id, sizeof(s_big.id)) == 0 && s_big.failed;
}
