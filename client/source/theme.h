#pragma once
// Design tokens lifted from the mockups (Dingplay Chat 3DS.dc.html).
#include <citro2d.h>

#define RGB(hex) C2D_Color32(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF, 0xFF)
#define RGBA(hex, a) C2D_Color32(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF, (a))

#define C_INK        RGB(0x241F1A)
#define C_CREAM      RGB(0xFFF3E0)
#define C_ORANGE     RGB(0xF7941E)
#define C_GREEN      RGB(0x2FB86E)
#define C_BLUE       RGB(0x37A8EE)
#define C_RED        RGB(0xEF4444)
#define C_SAND       RGB(0xEBDFC9)   // inactive tabs, chips
#define C_MUTED      RGB(0x6B6357)   // secondary text
#define C_MUTED2     RGB(0x8A8076)   // tertiary text
#define C_STONE      RGB(0xB9AE9E)   // offline dot, dashed borders
#define C_WHITE      RGB(0xFFFFFF)
#define C_GOLD       RGB(0xFFD98A)   // "FOR NEW NINTENDO 3DS" badge text
#define C_PANEL      RGB(0x101010)
#define C_NAVY       RGB(0x0B1836)   // online chat log background
#define C_NAVY_BUBBLE RGBA(0x09142E, 199)
#define C_NAVY_NAME  RGB(0xB9CBEA)
#define C_NAVY_BARS  RGB(0x93A9D0)
#define C_CREAM_TEXT RGB(0xE7DDCE)
#define C_LINE       RGB(0xE0D3BB)   // thin separators
#define C_DASH       RGB(0xC9BCA4)

// Hard shadows are a darker tone of the surface, never blurred.
#define C_GREEN_SHADOW  RGB(0x145C36)
#define C_BLUE_SHADOW   RGB(0x1A6899)
#define C_ORANGE_SHADOW RGB(0xA8620A)
#define C_RED_SHADOW    RGB(0x8C1F1F)
#define C_GREEN_TINT    RGB(0xD8F5E6)
#define C_BLUE_TINT     RGB(0xD6EEFC)
#define C_ORANGE_TINT   RGB(0x3D2A0D)

// Drawing palette (index order shared with the relay: relay/src/lib/png.js).
#define DRAW_COLOR_COUNT 6
static const u32 DRAW_COLORS[DRAW_COLOR_COUNT] = {
    0x241F1A, 0xEF4444, 0xF7941E, 0x2FB86E, 0x37A8EE, 0xFFFFFF,
};
static const float DRAW_PEN_WIDTHS[3] = {2.0f, 4.0f, 7.0f};

// Screens
#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240
