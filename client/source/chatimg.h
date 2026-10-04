#pragma once
// Cache of phone-sent chat images, served pre-decoded by the relay as raw RGBA
// (letterboxed into a fixed IMG_W x IMG_H canvas — the console has no image decoder).
// Keyed by message id; a small ring, evicting the least-recently-used.
#include <citro2d.h>
#include <stddef.h>

#define CHATIMG_W 140   // relay canvas (GET /image/:room/:id)
#define CHATIMG_H 105

void chatimg_init(void);
void chatimg_exit(void);
void chatimg_clear(void);                 // drop all (on leaving a chat)
void chatimg_set_fetcher(void (*fetch)(const char *id));

// NULL until loaded; the first call for an id kicks off a fetch.
C2D_Image *chatimg_get(const char *id);
void chatimg_put(const char *id, const unsigned char *rgba, int w, int h);
void chatimg_mark_failed(const char *id);
bool chatimg_failed(const char *id);  // fetch failed (deleted/unreadable) — show a placeholder
