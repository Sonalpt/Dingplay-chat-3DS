#pragma once
// Avatar textures. The relay serves avatars pre-decoded (raw RGBA8, 48 px) so
// the console never needs an image codec; this module keeps a small LRU of
// GPU textures and asks the network layer for anything it does not have.
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// Uploads linear top-down RGBA8 rows into a (power-of-two) tiled texture, top-aligned
// in v-space: draw with a subtexture of top = 1.0, bottom = 1.0 - h / tex->height.
// `stride` is in pixels.
void tex_upload_rgba(C3D_Tex *tex, const u8 *rgba, int w, int h, int stride);

void avatar_init(void);
void avatar_exit(void);
// Returns the image (subtex = 48×48) or NULL while loading. Triggers a fetch once.
C2D_Image *avatar_get(const char *uid, bool round);
// Called by the network layer with the relay's bytes (size×size×4).
void avatar_put(const char *uid, const u8 *rgba, int size);
void avatar_mark_failed(const char *uid);
// Hook so this module doesn't depend on api.c directly.
void avatar_set_fetcher(void (*fetch)(const char *uid));
void avatar_clear(void);
