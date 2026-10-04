#include "chatimg.h"
#include "avatar.h"   // tex_upload_rgba
#include "app.h"
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
        s->failed = true;
        return;
    }
    tex_upload_rgba(&s->tex, rgba, w, h, w);
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
