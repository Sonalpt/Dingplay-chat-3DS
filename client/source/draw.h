#pragma once
// Stylus drawing: raw touch samples become a vector stroke list in real time,
// rendered locally at once (never waiting on a round-trip) and shipped as-is.
#include "app.h"

typedef struct {
    Drawing d;
    uint8_t color;      // current palette index
    uint8_t pen;        // 0..2
    bool eraser;
    bool stroking;      // stylus is down inside the canvas
    float cx, cy, cw, ch;  // canvas rect on the bottom screen
    float last_x, last_y;
    // Batching for local wireless: points appended since the last flush.
    uint16_t flushed_points;
} Canvas;

void canvas_init(Canvas *c, float x, float y, float w, float h);
void canvas_clear(Canvas *c);
// Feed this frame's input; returns true when the stroke list changed.
bool canvas_update(Canvas *c, const Input *in);
void canvas_draw(const Canvas *c);   // canvas contents at native position
bool canvas_empty(const Canvas *c);

// Render a stored drawing scaled into (x, y, w, h), preserving aspect.
void drawing_render(const DrawingRef *d, float x, float y, float w, float h);
void drawing_render_full(const Drawing *d, float x, float y, float scale);
