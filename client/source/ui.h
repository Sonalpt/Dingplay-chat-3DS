#pragma once
// Flat drawing toolkit recreating the mockup's shape language with citro2d:
// thick borders, hard offset shadows (no blur), generously rounded corners.
#include <citro2d.h>
#include <stdbool.h>
#include "app.h"
#include "theme.h"

typedef enum { FONT_BODY = 0, FONT_HEAD = 1 } UiFont;  // ExtraBold / Black

typedef enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 } UiAlign;

bool ui_init(void);
void ui_exit(void);
void ui_frame_begin(void);   // clears the per-frame text buffer

// ---- Shapes -------------------------------------------------------------------
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_rrect(float x, float y, float w, float h, float r, u32 color);
// Per-corner radii (tl, tr, br, bl) for bubble tails. Opaque colours only (pieces overlap).
void ui_rrect4(float x, float y, float w, float h, float tl, float tr, float br, float bl, u32 color);
// Border drawn inside the box (CSS box-sizing: border-box).
void ui_rrect_border(float x, float y, float w, float h, float r, float bw, u32 fill, u32 border);
// Border + hard shadow offset (dx, dy) in `shadow`.
void ui_card(float x, float y, float w, float h, float r, float bw, u32 fill, u32 border, float dx, float dy, u32 shadow);
void ui_circle(float cx, float cy, float r, u32 color);
void ui_circle_border(float cx, float cy, float r, float bw, u32 fill, u32 border);
// Outlines only (nothing painted inside): use these instead of a transparent fill.
void ui_rrect_outline(float x, float y, float w, float h, float r, float bw, u32 color);
void ui_circle_outline(float cx, float cy, float r, float bw, u32 color);
void ui_dashed_rrect(float x, float y, float w, float h, float r, float bw, u32 color);
void ui_dashed_circle(float cx, float cy, float r, float bw, u32 color);
void ui_line(float x0, float y0, float x1, float y1, float thick, u32 color);
// Stroke with round caps, the primitive drawings are made of.
void ui_stroke_seg(float x0, float y0, float x1, float y1, float width, u32 color);
// Subtle drifting dot grid (14 px pitch) used behind several panels.
void ui_dots(float x, float y, float w, float h, u32 dot_color);
// Pill / chip: filled rounded rect sized to its label. Returns width.
float ui_chip(float x, float y, float px, float padx, float pady, u32 bg, u32 fg, const char *text);
void ui_toggle(float x, float y, bool on);   // 36×19 switch
void ui_progress(float x, float y, float w, float h, float frac, u32 fg, u32 border);

// ---- Text ---------------------------------------------------------------------------
// `px` mirrors the CSS font-size in the mockups.
void ui_text(float x, float y, float px, u32 color, UiAlign align, UiFont font, const char *str);
// Vertically centered inside (y, h).
void ui_text_v(float x, float y, float h, float px, u32 color, UiAlign align, UiFont font, const char *str);
float ui_text_width(float px, UiFont font, const char *str);
float ui_line_height(float px);
// Word-wraps into at most `max_lines`; returns lines drawn. Pass color 0 to only measure.
int ui_text_wrap(float x, float y, float w, float px, u32 color, UiAlign align, UiFont font, const char *str, int max_lines, float line_h);
// Truncates with "…" to fit `w`. Writes into out (size n).
void ui_ellipsize(char *out, size_t n, float px, UiFont font, const char *str, float w);
void ui_text_shadow(float x, float y, float px, u32 color, u32 shadow, float dy, UiAlign align, UiFont font, const char *str);

// ---- Icons (vector, drawn with primitives) -----------------------------------------
typedef enum {
    ICON_GLOBE, ICON_PEOPLE, ICON_USER, ICON_GEAR, ICON_SEND, ICON_PLUS, ICON_SCAN, ICON_LOCK, ICON_MIC,
    ICON_PLAY, ICON_PAUSE, ICON_ADD_FRIEND, ICON_CHECK, ICON_RADIO, ICON_APPLE, ICON_PLAYSTORE, ICON_PENCIL
} UiIcon;
void ui_icon(UiIcon icon, float cx, float cy, float size, u32 color);
void ui_wifi_bars(float x, float y, int bars, u32 on, u32 off);
void ui_battery(float x, float y, float frac, u32 color);

// ---- Avatars ----------------------------------------------------------------------------
// Draws the cached avatar or a placeholder initial. `round` = circle, else rounded square.
void ui_avatar(float x, float y, float size, bool round, const char *uid, const char *name, float bw, u32 border);

// ---- Hit testing ------------------------------------------------------------------------------
bool ui_in(const touchPosition *t, float x, float y, float w, float h);
bool ui_tap(const Input *in, float x, float y, float w, float h);       // released inside, started inside
bool ui_pressed(const Input *in, float x, float y, float w, float h);   // stylus went down inside this frame
bool ui_holding(const Input *in, float x, float y, float w, float h);   // stylus currently down inside

// ---- Misc ----------------------------------------------------------------------------------------
void ui_format_time(char *out, size_t n, int64_t unix_ms);      // "16:24"
void ui_format_ago(char *out, size_t n, int64_t unix_ms);       // "2 h ago"
void ui_format_dur(char *out, size_t n, int ms);                // "0:07"
u32 ui_lerp_color(u32 a, u32 b, float t);
