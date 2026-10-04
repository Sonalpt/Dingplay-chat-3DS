#pragma once
// Cache of phone-sent chat images, served pre-decoded by the relay as raw RGBA
// (letterboxed into a fixed IMG_W x IMG_H canvas — the console has no image decoder).
// Keyed by message id; a small ring, evicting the least-recently-used.
#include <citro2d.h>
#include <stddef.h>

#define CHATIMG_W 140      // chat thumbnail (GET /image/:room/:id)
#define CHATIMG_H 105
#define CHATIMG_BIG_W 400  // fullscreen viewer (GET /image/:room/:id?big=1)
#define CHATIMG_BIG_H 240

void chatimg_init(void);
void chatimg_exit(void);
void chatimg_clear(void);                 // drop all (on leaving a chat)
void chatimg_set_fetcher(void (*fetch)(const char *id));

// NULL until loaded; the first call for an id kicks off a fetch.
C2D_Image *chatimg_get(const char *id);
void chatimg_put(const char *id, const unsigned char *rgba, int w, int h);
void chatimg_mark_failed(const char *id);
bool chatimg_failed(const char *id);  // fetch failed (deleted/unreadable) — show a placeholder

// Fullscreen viewer: a single-slot full-size image.
void chatimg_big_set_fetcher(void (*fetch)(const char *id));
C2D_Image *chatimg_big_get(const char *id);  // NULL until loaded; kicks off a fetch
void chatimg_big_put(const char *id, const unsigned char *rgba, int w, int h);
void chatimg_big_mark_failed(const char *id);
bool chatimg_big_failed(const char *id);
void chatimg_big_clear(void);
