#pragma once
// The console's own Mii (CFLStoreData from the system config) and a small
// registry of Mii blobs known from Local Wireless peers, keyed for the avatar
// cache as "mii:<hash>". Rendering happens on the relay (Mii Studio) — see
// api.c avatar_fetch — so Mii faces need Wi-Fi; without it the placeholder
// initial is shown.
#include <3ds.h>
#include <stdbool.h>

#define MII_LEN 0x60
#define MII_KEY_LEN 20

bool mii_own(u8 out[MII_LEN]);            // false if the console has no personal Mii
const char *mii_own_key(void);            // "mii:xxxxxxxx" or "" — registers the own Mii
const char *mii_own_name(void);           // Mii name (UTF-8) or ""
// Registers a peer's Mii and returns its avatar key ("" for an all-zero blob).
const char *mii_register(const u8 mii[MII_LEN], char out[MII_KEY_LEN]);
const u8 *mii_lookup(const char *key);    // blob for a key, NULL if unknown
