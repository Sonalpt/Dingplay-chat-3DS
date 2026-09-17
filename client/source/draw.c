#include "draw.h"
#include "ui.h"
#include <math.h>
#include <string.h>

// Minimum stylus travel before a new point is recorded (keeps stroke lists small).
#define MIN_STEP_SQ 2.25f

void canvas_init(Canvas *c, float x, float y, float w, float h) {
    memset(c, 0, sizeof(*c));
    c->cx = x;
    c->cy = y;
    c->cw = w;
    c->ch = h;
    c->d.w = (uint16_t)w;
    c->d.h = (uint16_t)h;
    c->pen = 1;
}

void canvas_clear(Canvas *c) {
    c->d.nstrokes = 0;
    c->d.npoints = 0;
    c->stroking = false;
    c->flushed_points = 0;
}

bool canvas_empty(const Canvas *c) { return c->d.nstrokes == 0; }

static bool add_point(Canvas *c, float x, float y) {
    if (c->d.npoints >= DRAW_MAX_POINTS) return false;
    Stroke *s = &c->d.strokes[c->d.nstrokes - 1];
    float lx = x - c->cx, ly = y - c->cy;
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (lx > c->cw) lx = c->cw;
    if (ly > c->ch) ly = c->ch;
    c->d.pts[c->d.npoints * 2] = (uint16_t)(lx + 0.5f);
    c->d.pts[c->d.npoints * 2 + 1] = (uint16_t)(ly + 0.5f);
    c->d.npoints++;
    s->count++;
    c->last_x = x;
    c->last_y = y;
    return true;
}

bool canvas_update(Canvas *c, const Input *in) {
    bool changed = false;
    bool inside = in->touching && ui_in(&in->touch, c->cx, c->cy, c->cw, c->ch);
    if (in->touch_down && inside) {
        if (c->d.nstrokes < DRAW_MAX_STROKES && c->d.npoints < DRAW_MAX_POINTS) {
            Stroke *s = &c->d.strokes[c->d.nstrokes++];
            s->color = c->eraser ? DRAW_COLOR_COUNT - 1 : c->color;
            s->pen = c->eraser ? 2 : c->pen;
            s->start = c->d.npoints;
            s->count = 0;
            add_point(c, in->touch.px, in->touch.py);
            c->stroking = true;
            changed = true;
        }
    } else if (c->stroking && in->touching) {
        float dx = in->touch.px - c->last_x, dy = in->touch.py - c->last_y;
        if (dx * dx + dy * dy >= MIN_STEP_SQ) {
            if (add_point(c, in->touch.px, in->touch.py)) changed = true;
            else c->stroking = false;
        }
    }
    if (!in->touching && c->stroking) c->stroking = false;
    return changed;
}

static void render_strokes(const Stroke *strokes, int nstrokes, const uint16_t *pts, float ox, float oy, float scale) {
    for (int i = 0; i < nstrokes; i++) {
        const Stroke *s = &strokes[i];
        u32 color = RGB(DRAW_COLORS[s->color < DRAW_COLOR_COUNT ? s->color : 0]);
        float width = DRAW_PEN_WIDTHS[s->pen > 2 ? 1 : s->pen] * scale;
        if (width < 1.0f) width = 1.0f;
        const uint16_t *p = pts + s->start * 2;
        if (s->count == 1) {
            ui_circle(ox + p[0] * scale, oy + p[1] * scale, width / 2, color);
            continue;
        }
        for (int k = 1; k < s->count; k++) {
            ui_stroke_seg(ox + p[(k - 1) * 2] * scale, oy + p[(k - 1) * 2 + 1] * scale, ox + p[k * 2] * scale, oy + p[k * 2 + 1] * scale, width, color);
        }
        if (s->count > 1) ui_circle(ox + p[0] * scale, oy + p[1] * scale, width / 2, color);
    }
}

void canvas_draw(const Canvas *c) { render_strokes(c->d.strokes, c->d.nstrokes, c->d.pts, c->cx, c->cy, 1.0f); }

void drawing_render_full(const Drawing *d, float x, float y, float scale) { render_strokes(d->strokes, d->nstrokes, d->pts, x, y, scale); }

void drawing_render(const DrawingRef *d, float x, float y, float w, float h) {
    if (!d || !d->w || !d->h) return;
    float sx = w / d->w, sy = h / d->h;
    float s = sx < sy ? sx : sy;
    float ox = x + (w - d->w * s) / 2, oy = y + (h - d->h * s) / 2;
    render_strokes(d->strokes, d->nstrokes, d->pts, ox, oy, s);
}
