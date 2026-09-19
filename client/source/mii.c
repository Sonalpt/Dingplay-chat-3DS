#include "mii.h"
#include <stdio.h>
#include <string.h>

#define MII_SLOTS 20

typedef struct {
    char key[MII_KEY_LEN];
    u8 data[MII_LEN];
} Entry;
static Entry s_table[MII_SLOTS];
static int s_next;
static u8 s_own[MII_LEN];
static bool s_own_loaded, s_own_ok;
static char s_own_key[MII_KEY_LEN], s_own_name[32];

static u32 hash(const u8 *d, size_t n) {
    u32 h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    return h;
}

static bool is_mii(const u8 *d) {
    if (d[0] != 3) return false;
    for (int i = 1; i < MII_LEN; i++)
        if (d[i]) return true;
    return false;
}

const char *mii_register(const u8 mii[MII_LEN], char out[MII_KEY_LEN]) {
    out[0] = 0;
    if (!mii || !is_mii(mii)) return out;
    // Hash only the appearance bytes (0x18..0x48): same face → same key.
    snprintf(out, MII_KEY_LEN, "mii:%08x", (unsigned)hash(mii + 0x18, 0x48 - 0x18));
    for (int i = 0; i < MII_SLOTS; i++)
        if (strcmp(s_table[i].key, out) == 0) return out;
    Entry *e = &s_table[s_next];
    s_next = (s_next + 1) % MII_SLOTS;
    strncpy(e->key, out, MII_KEY_LEN - 1);
    memcpy(e->data, mii, MII_LEN);
    return out;
}

const u8 *mii_lookup(const char *key) {
    if (!key || strncmp(key, "mii:", 4) != 0) return NULL;
    for (int i = 0; i < MII_SLOTS; i++)
        if (strcmp(s_table[i].key, key) == 0) return s_table[i].data;
    return NULL;
}

static void load_own(void) {
    if (s_own_loaded) return;
    s_own_loaded = true;
    memset(s_own, 0, sizeof(s_own));
    if (R_SUCCEEDED(cfguInit())) {
        // Config block 0x000A0000: the owner's Mii as CFLStoreData (0x60 bytes).
        if (R_SUCCEEDED(CFGU_GetConfigInfoBlk2(MII_LEN, 0x000A0000, s_own))) s_own_ok = is_mii(s_own);
        cfguExit();
    }
    if (s_own_ok) {
        mii_register(s_own, s_own_key);
        u16 name[11] = {0};
        memcpy(name, s_own + 0x1A, 20);
        ssize_t n = utf16_to_utf8((u8 *)s_own_name, name, sizeof(s_own_name) - 1);
        s_own_name[n > 0 ? n : 0] = 0;
    }
}

bool mii_own(u8 out[MII_LEN]) {
    load_own();
    if (out) memcpy(out, s_own, MII_LEN);
    return s_own_ok;
}

const char *mii_own_key(void) {
    load_own();
    return s_own_key;
}

const char *mii_own_name(void) {
    load_own();
    return s_own_name;
}
