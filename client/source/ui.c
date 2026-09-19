#include "ui.h"
#include "avatar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEXT_BUF_GLYPHS 8192
#define TEXT_LH_RATIO 1.364f  // Nunito's normal line-height in em, as the CSS mockups lay it out
#define DEPTH 0.5f

static C2D_TextBuf s_textbuf;
// Two atlases per face: 12 pt for small UI text, 24 pt for headings and big glyphs.
// Drawing a 24 pt atlas at 9 px aliases badly on the real screen; a near-1:1 atlas stays crisp.
#define TIER_SMALL 0
#define TIER_LARGE 1
#define TIER_SPLIT_PX 13.0f  // requested px above this use the large atlas
static C2D_Font s_fonts[2][2];    // [UiFont][tier]
static float s_linefeed[2][2];
static float s_textscale[2][2];  // citro2d multiplies every scale by 30 / cellHeight
// Optional artwork: romfs:/gfx/icons/<name>.t3x replaces the vector fallback of ui_icon().
static const char *const ICON_FILES[] = {
    [ICON_GLOBE] = "global",       [ICON_PEOPLE] = "friends", [ICON_USER] = "user",   [ICON_GEAR] = "settings",
    [ICON_SEND] = "send",          [ICON_PLUS] = "add",       [ICON_SCAN] = "scan",   [ICON_ADD_FRIEND] = "add-friend",
    [ICON_GAMEPAD] = "multiplayer", [ICON_CLOSE] = "close",   [ICON_PENCIL] = NULL,  // the rest stay vector
};
#define ICON_COUNT (sizeof(ICON_FILES) / sizeof(ICON_FILES[0]))
static C2D_SpriteSheet s_icon_sheets[ICON_COUNT];
static C2D_Image s_icon_imgs[ICON_COUNT];
static u8 s_icon_state[ICON_COUNT];  // 0 untried, 1 loaded, 2 missing
static C3D_Tex s_dots_tex[2];  // 0 = white dots (orange/blue panels), 1 = ink dots (white/cream panels)
static bool s_dots_ok;

// ---- Init --------------------------------------------------------------------------

static float font_linefeed(C2D_Font font) {
    if (font) {
        FINF_s *finf = C2D_FontGetInfo(font);
        if (finf && finf->lineFeed > 0) return (float)finf->lineFeed;
    }
    // System shared font
    CFNT_s *sys = fontGetSystemFont();
    if (sys && fontGetInfo(sys)->lineFeed > 0) return (float)fontGetInfo(sys)->lineFeed;
    return 30.0f;
}

// citro2d normalises fonts to the system font's 30 px cell: C2D_DrawText and
// C2D_FontCalcGlyphPos multiply the caller's scale by 30 / cellHeight. Our
// sizing has to undo that or a 12 pt atlas draws twice as large as a 24 pt one.
static float font_textscale(C2D_Font font) {
    u8 cell = 30;
    if (font) {
        FINF_s *finf = C2D_FontGetInfo(font);
        if (finf && finf->tglp && finf->tglp->cellHeight > 0) cell = finf->tglp->cellHeight;
    } else {
        CFNT_s *sys = fontGetSystemFont();
        if (sys && fontGetGlyphInfo(sys)->cellHeight > 0) cell = fontGetGlyphInfo(sys)->cellHeight;
    }
    return 30.0f / (float)cell;
}

// 128×128 tile with a 9×9 grid of soft 1.5 px dots (pitch 14.2 px). Colour and
// alpha are baked in (rgba(255,255,255,.3) / rgba(36,31,26,.17) as in the CSS).
static bool build_dots_texture(C3D_Tex *tex, u8 r, u8 g, u8 b, float alpha) {
    const int N = 128;
    if (!C3D_TexInit(tex, N, N, GPU_RGBA8)) return false;
    u8 *px = (u8 *)malloc(N * N * 4);
    if (!px) return false;
    const float pitch = N / 9.0f;
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            float fx = fmodf(x + 0.5f, pitch) - pitch / 2, fy = fmodf(y + 0.5f, pitch) - pitch / 2;
            float d = sqrtf(fx * fx + fy * fy);
            float a = 1.5f + 0.5f - d;  // radius 1.5, 1 px feather
            if (a < 0) a = 0;
            if (a > 1) a = 1;
            u8 *p = px + (y * N + x) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = (u8)(a * alpha * 255.0f);
        }
    }
    tex_upload_rgba(tex, px, N, N, N);
    free(px);
    C3D_TexSetWrap(tex, GPU_REPEAT, GPU_REPEAT);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    return true;
}

bool ui_init(void) {
    s_textbuf = C2D_TextBufNew(TEXT_BUF_GLYPHS);
    if (!s_textbuf) return false;
    s_fonts[FONT_BODY][TIER_SMALL] = C2D_FontLoad("romfs:/nunito-regular-12.bcfnt");
    s_fonts[FONT_BODY][TIER_LARGE] = C2D_FontLoad("romfs:/nunito-regular-24.bcfnt");
    s_fonts[FONT_HEAD][TIER_SMALL] = C2D_FontLoad("romfs:/nunito-bold-12.bcfnt");
    s_fonts[FONT_HEAD][TIER_LARGE] = C2D_FontLoad("romfs:/nunito-bold-24.bcfnt");
    for (int f = 0; f < 2; f++) {
        // fall back across tiers, then to the other face, then to the system font (NULL)
        if (!s_fonts[f][TIER_SMALL]) s_fonts[f][TIER_SMALL] = s_fonts[f][TIER_LARGE];
        if (!s_fonts[f][TIER_LARGE]) s_fonts[f][TIER_LARGE] = s_fonts[f][TIER_SMALL];
    }
    for (int t = 0; t < 2; t++)
        if (!s_fonts[FONT_HEAD][t]) s_fonts[FONT_HEAD][t] = s_fonts[FONT_BODY][t];
    for (int f = 0; f < 2; f++)
        for (int t = 0; t < 2; t++) {
            s_linefeed[f][t] = font_linefeed(s_fonts[f][t]);
            s_textscale[f][t] = font_textscale(s_fonts[f][t]);
        }
    s_dots_ok = build_dots_texture(&s_dots_tex[0], 255, 255, 255, 0.30f) && build_dots_texture(&s_dots_tex[1], 0x24, 0x1F, 0x1A, 0.17f);
    return true;
}

void ui_exit(void) {
    for (size_t i = 0; i < ICON_COUNT; i++)
        if (s_icon_state[i] == 1) C2D_SpriteSheetFree(s_icon_sheets[i]);
    if (s_dots_ok) {
        C3D_TexDelete(&s_dots_tex[0]);
        C3D_TexDelete(&s_dots_tex[1]);
    }
    {
        // handles may be shared through the fallbacks above: free each distinct one once
        C2D_Font freed[4];
        int nf = 0;
        for (int f = 0; f < 2; f++)
            for (int t = 0; t < 2; t++) {
                C2D_Font h = s_fonts[f][t];
                if (!h) continue;
                bool seen = false;
                for (int k = 0; k < nf; k++)
                    if (freed[k] == h) seen = true;
                if (seen) continue;
                freed[nf++] = h;
                C2D_FontFree(h);
            }
    }
    if (s_textbuf) C2D_TextBufDelete(s_textbuf);
}

void ui_frame_begin(void) { C2D_TextBufClear(s_textbuf); }

// ---- Shapes ------------------------------------------------------------------------

void ui_rect(float x, float y, float w, float h, u32 color) {
    if (w <= 0 || h <= 0) return;
    C2D_DrawRectSolid(x, y, DEPTH, w, h, color);
}

void ui_circle(float cx, float cy, float r, u32 color) {
    if (r <= 0) return;
    C2D_DrawCircleSolid(cx, cy, DEPTH, r, color);
}

// Quarter disc as a small triangle fan. quadrant: 0 = top-left, 1 = top-right,
// 2 = bottom-right, 3 = bottom-left. Non-overlapping so translucent fills stay flat.
static void quarter(float cx, float cy, float r, int quadrant, u32 color) {
    const int segs = r > 8 ? 8 : 5;
    float a0 = (float)M_PI * (0.5f * (quadrant + 2));  // 0: π, 1: 3π/2, 2: 0/2π, 3: π/2
    for (int i = 0; i < segs; i++) {
        float t0 = a0 + (float)M_PI * 0.5f * i / segs;
        float t1 = a0 + (float)M_PI * 0.5f * (i + 1) / segs;
        C2D_DrawTriangle(cx, cy, color, cx + cosf(t0) * r, cy + sinf(t0) * r, color, cx + cosf(t1) * r, cy + sinf(t1) * r, color, DEPTH);
    }
}

void ui_rrect(float x, float y, float w, float h, float r, u32 color) {
    if (w <= 0 || h <= 0) return;
    float m = (w < h ? w : h) / 2;
    if (r > m) r = m;
    if (r < 1.0f) {
        ui_rect(x, y, w, h, color);
        return;
    }
    ui_rect(x + r, y, w - 2 * r, h, color);
    ui_rect(x, y + r, r, h - 2 * r, color);
    ui_rect(x + w - r, y + r, r, h - 2 * r, color);
    quarter(x + r, y + r, r, 0, color);
    quarter(x + w - r, y + r, r, 1, color);
    quarter(x + w - r, y + h - r, r, 2, color);
    quarter(x + r, y + h - r, r, 3, color);
}

void ui_rrect4(float x, float y, float w, float h, float tl, float tr, float br, float bl, u32 color) {
    float R = fmaxf(fmaxf(tl, tr), fmaxf(br, bl));
    float m = (w < h ? w : h) / 2;
    if (R > m) R = m;
    if (R < 1) {
        ui_rect(x, y, w, h, color);
        return;
    }
    ui_rect(x + R, y, w - 2 * R, h, color);
    ui_rect(x, y + R, w, h - 2 * R, color);
    // corners: rounded rc×rc square + the rest of the R×R corner region
    const float rs[4] = {tl, tr, br, bl};
    const float cxs[4] = {x, x + w - R, x + w - R, x};
    const float cys[4] = {y, y, y + h - R, y + h - R};
    for (int c = 0; c < 4; c++) {
        float rc = rs[c] > R ? R : rs[c];
        float cx0 = cxs[c], cy0 = cys[c];
        // the rc×rc square sits at the outer corner
        float sqx = (c == 0 || c == 3) ? cx0 : cx0 + R - rc;
        float sqy = (c == 0 || c == 1) ? cy0 : cy0 + R - rc;
        if (rc >= 1) {
            float qx = (c == 0 || c == 3) ? sqx + rc : sqx;
            float qy = (c == 0 || c == 1) ? sqy + rc : sqy;
            quarter(qx, qy, rc, c, color);
        } else {
            ui_rect(sqx, sqy, rc > 0 ? rc : 1, rc > 0 ? rc : 1, color);
        }
        // fill the remainder of the corner region (two strips)
        if (c == 0) { ui_rect(cx0 + rc, cy0, R - rc, rc, color); ui_rect(cx0, cy0 + rc, R, R - rc, color); }
        if (c == 1) { ui_rect(cx0, cy0, R - rc, rc, color); ui_rect(cx0, cy0 + rc, R, R - rc, color); }
        if (c == 2) { ui_rect(cx0, cy0 + R - rc, R - rc, rc, color); ui_rect(cx0, cy0, R, R - rc, color); }
        if (c == 3) { ui_rect(cx0 + rc, cy0 + R - rc, R - rc, rc, color); ui_rect(cx0, cy0, R, R - rc, color); }
    }
}

void ui_rrect_border(float x, float y, float w, float h, float r, float bw, u32 fill, u32 border) {
    ui_rrect(x, y, w, h, r, border);
    float ir = r - bw;
    if (ir < 0) ir = 0;
    ui_rrect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, ir, fill);
}

void ui_card(float x, float y, float w, float h, float r, float bw, u32 fill, u32 border, float dx, float dy, u32 shadow) {
    if (dx != 0 || dy != 0) ui_rrect(x + dx, y + dy, w, h, r, shadow);
    ui_rrect_border(x, y, w, h, r, bw, fill, border);
}

void ui_circle_border(float cx, float cy, float r, float bw, u32 fill, u32 border) {
    ui_circle(cx, cy, r, border);
    ui_circle(cx, cy, r - bw, fill);
}

void ui_circle_outline(float cx, float cy, float r, float bw, u32 color) {
    const int n = (int)(r * 0.8f) + 12;
    float rr = r - bw / 2;
    for (int i = 0; i < n; i++) {
        float a0 = (float)M_PI * 2 * i / n, a1 = (float)M_PI * 2 * (i + 1) / n;
        C2D_DrawLine(cx + cosf(a0) * rr, cy + sinf(a0) * rr, color, cx + cosf(a1) * rr, cy + sinf(a1) * rr, color, bw, DEPTH);
    }
}

void ui_rrect_outline(float x, float y, float w, float h, float r, float bw, u32 color) {
    float m = (w < h ? w : h) / 2;
    if (r > m) r = m;
    // straight edges
    ui_rect(x + r, y, w - 2 * r, bw, color);
    ui_rect(x + r, y + h - bw, w - 2 * r, bw, color);
    ui_rect(x, y + r, bw, h - 2 * r, color);
    ui_rect(x + w - bw, y + r, bw, h - 2 * r, color);
    if (r < 1) return;
    // corner arcs
    float cs[4][2] = {{x + r, y + r}, {x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}};
    float base[4] = {(float)M_PI, (float)M_PI * 1.5f, 0, (float)M_PI * 0.5f};
    int segs = r > 8 ? 6 : 4;
    float rr = r - bw / 2;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < segs; i++) {
            float a0 = base[c] + (float)M_PI * 0.5f * i / segs, a1 = base[c] + (float)M_PI * 0.5f * (i + 1) / segs;
            C2D_DrawLine(cs[c][0] + cosf(a0) * rr, cs[c][1] + sinf(a0) * rr, color, cs[c][0] + cosf(a1) * rr, cs[c][1] + sinf(a1) * rr, color, bw, DEPTH);
        }
}

void ui_line(float x0, float y0, float x1, float y1, float thick, u32 color) {
    C2D_DrawLine(x0, y0, color, x1, y1, color, thick, DEPTH);
}

void ui_stroke_seg(float x0, float y0, float x1, float y1, float width, u32 color) {
    float dx = x1 - x0, dy = y1 - y0;
    if (dx * dx + dy * dy < 0.25f) {
        ui_circle(x0, y0, width / 2, color);
        return;
    }
    C2D_DrawLine(x0, y0, color, x1, y1, color, width, DEPTH);
    ui_circle(x1, y1, width / 2, color);
}

void ui_dashed_rrect(float x, float y, float w, float h, float r, float bw, u32 color) {
    const float dash = 4, gap = 3;
    // straight edges only; corners get a short arc feel from the end dashes
    for (float t = r; t < w - r; t += dash + gap) {
        float len = fminf(dash, w - r - t);
        ui_rect(x + t, y, len, bw, color);
        ui_rect(x + t, y + h - bw, len, bw, color);
    }
    for (float t = r; t < h - r; t += dash + gap) {
        float len = fminf(dash, h - r - t);
        ui_rect(x, y + t, bw, len, color);
        ui_rect(x + w - bw, y + t, bw, len, color);
    }
    if (r >= 3) {
        // corner arcs as 2 short segments each
        float cs[4][2] = {{x + r, y + r}, {x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}};
        float base[4] = {(float)M_PI, (float)M_PI * 1.5f, 0, (float)M_PI * 0.5f};
        for (int c = 0; c < 4; c++) {
            for (int s = 0; s < 2; s++) {
                float a0 = base[c] + (float)M_PI * 0.5f * (s * 0.5f + 0.1f);
                float a1 = base[c] + (float)M_PI * 0.5f * (s * 0.5f + 0.4f);
                float rr = r - bw / 2;
                ui_line(cs[c][0] + cosf(a0) * rr, cs[c][1] + sinf(a0) * rr, cs[c][0] + cosf(a1) * rr, cs[c][1] + sinf(a1) * rr, bw, color);
            }
        }
    }
}

void ui_dashed_circle(float cx, float cy, float r, float bw, u32 color) {
    const int n = (int)(r * 0.9f) + 6;
    float rr = r - bw / 2;
    for (int i = 0; i < n; i++) {
        float a0 = (float)M_PI * 2 * i / n;
        float a1 = (float)M_PI * 2 * (i + 0.55f) / n;
        ui_line(cx + cosf(a0) * rr, cy + sinf(a0) * rr, cx + cosf(a1) * rr, cy + sinf(a1) * rr, bw, color);
    }
}

void ui_dots(float x, float y, float w, float h, u32 dot_color) {
    if (!s_dots_ok) return;
    // background-position drifts 28px right / 28px up every 9 s
    float phase = fmodf(g_time / 9.0f, 1.0f) * (28.0f / 128.0f);
    Tex3DS_SubTexture sub = {
        .width = (u16)w, .height = (u16)h,
        .left = -phase, .right = -phase + w / 128.0f,
        .top = 1.0f - phase, .bottom = 1.0f - phase - h / 128.0f,
    };
    // dot_color only selects the variant: light dots on saturated panels, ink dots on light ones
    bool light = ((dot_color >> 0) & 0xFF) > 0x80;  // red channel high → white dots
    C2D_Image img = {&s_dots_tex[light ? 0 : 1], &sub};
    C2D_DrawImageAt(img, x, y, DEPTH, NULL, 1.0f, 1.0f);
}

float ui_chip(float x, float y, float px, float padx, float pady, u32 bg, u32 fg, const char *text) {
    float tw = ui_text_width(px, FONT_HEAD, text);
    float h = ui_line_height(px) + pady * 2;
    float w = tw + padx * 2;
    ui_rrect(x, y, w, h, h * 0.36f, bg);
    ui_text_v(x + padx, y, h, px, fg, ALIGN_LEFT, FONT_HEAD, text);
    return w;
}

void ui_toggle(float x, float y, bool on) {
    ui_rrect_border(x, y, 36, 19, 9.5f, 2, on ? C_GREEN : C_SAND, C_INK);
    float kx = on ? x + 36 - 2 - 2 - 13 : x + 2 + 2;
    if (on) ui_circle(kx + 6.5f, y + 9.5f, 6.5f, C_WHITE);
    else ui_circle_border(kx + 6.5f, y + 9.5f, 6.5f, 1, C_WHITE, C_INK);
}

void ui_progress(float x, float y, float w, float h, float frac, u32 fg, u32 border) {
    ui_rrect(x, y, w, h, h / 2, border);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float iw = (w - 8) * frac;
    if (iw > 0) ui_rrect(x + 4, y + 4, iw, h - 8, (h - 8) / 2 > 3 ? 3 : (h - 8) / 2, fg);
}

// ---- Text ------------------------------------------------------------------------------

// Text sizes are the mockups' (a uniform bump overflowed fixed-width containers);
// legibility comes from the near-1:1 small atlas instead. Hook kept for tuning.
static inline float eff_px(float px) { return px; }
static inline int tier_for(float px) { return eff_px(px) <= TIER_SPLIT_PX ? TIER_SMALL : TIER_LARGE; }
static inline C2D_Font font_for(UiFont font, float px) { return s_fonts[font][tier_for(px)]; }
// Scale handed to citro2d so that the glyph em comes out at `px` on screen (the
// mockups' CSS font-size): line feed is 1.364 em, and citro2d applies textScale on top.
static inline float scale_for(float px, UiFont font) {
    int t = tier_for(px);
    return eff_px(px) * TEXT_LH_RATIO / (s_linefeed[font][t] * s_textscale[font][t]);
}
// Multiplier for raw glyph advances (charWidth) → screen pixels.
static inline float advance_scale(float px, UiFont font) { return scale_for(px, font) * s_textscale[font][tier_for(px)]; }

float ui_line_height(float px) { return eff_px(px) * TEXT_LH_RATIO; }

static void parse(C2D_Text *txt, UiFont font, float px, const char *str) {
    C2D_Font h = font_for(font, px);
    if (h) C2D_TextFontParse(txt, h, s_textbuf, str);
    else C2D_TextParse(txt, s_textbuf, str);
    C2D_TextOptimize(txt);
}

void ui_text(float x, float y, float px, u32 color, UiAlign align, UiFont font, const char *str) {
    if (!str || !*str) return;
    C2D_Text txt;
    parse(&txt, font, px, str);
    u32 flags = C2D_WithColor | (align == ALIGN_CENTER ? C2D_AlignCenter : align == ALIGN_RIGHT ? C2D_AlignRight : C2D_AlignLeft);
    float s = scale_for(px, font);
    C2D_DrawText(&txt, flags, x, y, DEPTH, s, s, color);
}

void ui_text_v(float x, float y, float h, float px, u32 color, UiAlign align, UiFont font, const char *str) {
    ui_text(x, y + (h - ui_line_height(px)) / 2, px, color, align, font, str);
}

void ui_text_shadow(float x, float y, float px, u32 color, u32 shadow, float dy, UiAlign align, UiFont font, const char *str) {
    ui_text(x, y + dy, px, shadow, align, font, str);
    ui_text(x, y, px, color, align, font, str);
}

// Width from glyph advances — no text-buffer usage, cheap enough to call per word.
static float glyph_width(C2D_Font h, u32 cp) {
    if (h) {
        int gi = C2D_FontGlyphIndexFromCodePoint(h, cp);
        charWidthInfo_s *cwi = C2D_FontGetCharWidthInfo(h, gi);
        return cwi ? (float)cwi->charWidth : 0;
    }
    CFNT_s *sys = fontGetSystemFont();
    int gi = fontGlyphIndexFromCodePoint(sys, cp);
    charWidthInfo_s *cwi = fontGetCharWidthInfo(sys, gi);
    return cwi ? (float)cwi->charWidth : 0;
}

static float raw_width(C2D_Font h, const char *str, size_t len) {
    float w = 0;
    const u8 *p = (const u8 *)str;
    const u8 *end = p + len;
    while (p < end && *p) {
        u32 cp;
        ssize_t n = decode_utf8(&cp, p);
        if (n <= 0) break;
        p += n;
        if (cp == '\n') continue;
        w += glyph_width(h, cp);
    }
    return w;
}

float ui_text_width(float px, UiFont font, const char *str) {
    if (!str) return 0;
    return raw_width(font_for(font, px), str, strlen(str)) * advance_scale(px, font);
}

int ui_text_wrap(float x, float y, float w, float px, u32 color, UiAlign align, UiFont font, const char *str, int max_lines, float line_h) {
    if (!str || !*str) return 0;
    float s = advance_scale(px, font);
    C2D_Font h = font_for(font, px);
    if (line_h <= 0) line_h = ui_line_height(px);
    int lines = 0;
    const char *p = str;
    char line[512];
    while (*p && lines < max_lines) {
        // greedy: extend by words while it fits; break long words by character
        const char *line_start = p;
        const char *last_fit = NULL;
        const char *q = p;
        float width = 0;
        bool hard_break = false;
        while (*q) {
            if (*q == '\n') {
                hard_break = true;
                break;
            }
            const char *word_end = q;
            while (*word_end && *word_end != ' ' && *word_end != '\n') word_end++;
            float ww = raw_width(h, q, word_end - q) * s;
            float space = (q > line_start) ? raw_width(h, " ", 1) * s : 0;
            if (width + space + ww <= w || q == line_start) {
                if (q == line_start && ww > w) {
                    // single word wider than the box: cut by characters
                    const char *c = q;
                    float cw = 0;
                    while (c < word_end) {
                        u32 cp;
                        ssize_t n = decode_utf8(&cp, (const u8 *)c);
                        if (n <= 0) break;
                        float gw = glyph_width(h, cp) * s;
                        if (cw + gw > w && c > q) break;
                        cw += gw;
                        c += n;
                    }
                    last_fit = c;
                    width = cw;
                    q = c;
                    break;
                }
                width += space + ww;
                last_fit = word_end;
                q = word_end;
                if (*q == ' ') q++;
            } else {
                break;
            }
        }
        if (!last_fit) last_fit = q;
        size_t len = last_fit - line_start;
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, line_start, len);
        line[len] = 0;
        // ellipsis on the last permitted line if more text follows
        const char *next = last_fit;
        while (*next == ' ') next++;
        if (hard_break && next == q) next = q + 1;
        if (lines == max_lines - 1 && *next) {
            char tmp[512];
            ui_ellipsize(tmp, sizeof(tmp), px, font, line, w);
            strncpy(line, tmp, sizeof(line) - 1);
            line[sizeof(line) - 1] = 0;
        }
        if (color) {
            float tx = align == ALIGN_CENTER ? x + w / 2 : align == ALIGN_RIGHT ? x + w : x;
            ui_text(tx, y + lines * line_h, px, color, align, font, line);
        }
        lines++;
        p = next;
        if (p == line_start) break;  // safety: no progress
    }
    return lines;
}

void ui_ellipsize(char *out, size_t n, float px, UiFont font, const char *str, float w) {
    if (!str) {
        out[0] = 0;
        return;
    }
    if (ui_text_width(px, font, str) <= w) {
        strncpy(out, str, n - 1);
        out[n - 1] = 0;
        return;
    }
    float s = advance_scale(px, font);
    C2D_Font h = font_for(font, px);
    float ell = glyph_width(h, 0x2026) * s;  // "…"
    float acc = 0;
    const u8 *p = (const u8 *)str;
    size_t len = 0;
    while (*p) {
        u32 cp;
        ssize_t k = decode_utf8(&cp, p);
        if (k <= 0) break;
        float gw = glyph_width(h, cp) * s;
        if (acc + gw + ell > w) break;
        acc += gw;
        if (len + k >= n - 4) break;
        memcpy(out + len, p, k);
        len += k;
        p += k;
    }
    memcpy(out + len, "\xE2\x80\xA6", 3);
    out[len + 3] = 0;
}

// ---- Icons ----------------------------------------------------------------------------------

static bool icon_sprite(UiIcon icon, float cx, float cy, float size, u32 color) {
    if (icon >= ICON_COUNT || !ICON_FILES[icon]) return false;
    if (s_icon_state[icon] == 0) {
        char path[64];
        snprintf(path, sizeof(path), "romfs:/gfx/icons/%s.t3x", ICON_FILES[icon]);
        s_icon_sheets[icon] = C2D_SpriteSheetLoad(path);
        if (s_icon_sheets[icon] && C2D_SpriteSheetCount(s_icon_sheets[icon]) > 0) {
            s_icon_imgs[icon] = C2D_SpriteSheetGetImage(s_icon_sheets[icon], 0);
            s_icon_state[icon] = 1;
        } else {
            s_icon_state[icon] = 2;
        }
    }
    if (s_icon_state[icon] != 1) return false;
    const C2D_Image *img = &s_icon_imgs[icon];
    float sc = size / (float)(img->subtex->width > img->subtex->height ? img->subtex->width : img->subtex->height);
    // Artwork is white on transparent; tint it to the requested colour.
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    C2D_DrawImageAt(*img, cx - img->subtex->width * sc / 2, cy - img->subtex->height * sc / 2, DEPTH, &tint, sc, sc);
    return true;
}

void ui_icon(UiIcon icon, float cx, float cy, float size, u32 color) {
    if (icon_sprite(icon, cx, cy, size, color)) return;
    float s = size / 2;  // half size
    switch (icon) {
        case ICON_GLOBE: {
            float t = size > 20 ? 2.2f : 1.6f;
            ui_circle_outline(cx, cy, s, t, color);
            ui_line(cx - s, cy, cx + s, cy, t, color);
            ui_line(cx, cy - s, cx, cy + s, t, color);
            // meridian ellipse approximated with 12 segments
            for (int i = 0; i < 12; i++) {
                float a0 = (float)M_PI * 2 * i / 12, a1 = (float)M_PI * 2 * (i + 1) / 12;
                ui_line(cx + cosf(a0) * s * 0.45f, cy + sinf(a0) * s, cx + cosf(a1) * s * 0.45f, cy + sinf(a1) * s, t, color);
            }
            break;
        }
        case ICON_PEOPLE: {
            ui_circle(cx - s * 0.35f, cy - s * 0.35f, s * 0.32f, color);
            ui_circle(cx + s * 0.4f, cy - s * 0.2f, s * 0.26f, color);
            ui_rrect(cx - s * 0.85f, cy + s * 0.05f, s, s * 0.8f, s * 0.35f, color);
            ui_rrect(cx + s * 0.02f, cy + s * 0.2f, s * 0.8f, s * 0.65f, s * 0.3f, color);
            break;
        }
        case ICON_USER: {
            ui_circle(cx, cy - s * 0.35f, s * 0.38f, color);
            ui_rrect(cx - s * 0.7f, cy + s * 0.15f, s * 1.4f, s * 0.75f, s * 0.35f, color);
            break;
        }
        case ICON_GEAR: {
            for (int i = 0; i < 8; i++) {
                float a = (float)M_PI * 2 * i / 8;
                ui_line(cx, cy, cx + cosf(a) * s, cy + sinf(a) * s, s * 0.5f, color);
            }
            ui_circle(cx, cy, s * 0.62f, color);
            break;
        }
        case ICON_SEND: {
            C2D_DrawTriangle(cx - s, cy - s * 0.85f, color, cx + s, cy, color, cx - s * 0.25f, cy + s * 0.05f, color, DEPTH);
            C2D_DrawTriangle(cx - s, cy + s * 0.85f, color, cx + s, cy, color, cx - s * 0.25f, cy - s * 0.05f, color, DEPTH);
            break;
        }
        case ICON_PLUS: {
            float t = size > 14 ? 3 : 2;
            ui_rect(cx - s, cy - t / 2, size, t, color);
            ui_rect(cx - t / 2, cy - s, t, size, color);
            break;
        }
        case ICON_SCAN: {
            float t = size > 14 ? 2 : 1.5f;
            float r = s * 0.8f;
            for (int i = 1; i < 10; i++) {
                float a0 = (float)M_PI * 2 * i / 12 + 0.6f, a1 = (float)M_PI * 2 * (i + 1) / 12 + 0.6f;
                ui_line(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, t, color);
            }
            float a = 0.6f + (float)M_PI * 2 / 12;
            float ax = cx + cosf(a) * r, ay = cy + sinf(a) * r;
            C2D_DrawTriangle(ax - s * 0.35f, ay - s * 0.4f, color, ax + s * 0.3f, ay - s * 0.05f, color, ax - s * 0.3f, ay + s * 0.35f, color, DEPTH);
            break;
        }
        case ICON_LOCK: {
            ui_rrect(cx - s * 0.75f, cy - s * 0.1f, s * 1.5f, s * 1.1f, s * 0.2f, color);
            float t = s * 0.28f;
            for (int i = 0; i < 6; i++) {
                float a0 = (float)M_PI + (float)M_PI * i / 6, a1 = (float)M_PI + (float)M_PI * (i + 1) / 6;
                ui_line(cx + cosf(a0) * s * 0.45f, cy - s * 0.15f + sinf(a0) * s * 0.5f, cx + cosf(a1) * s * 0.45f, cy - s * 0.15f + sinf(a1) * s * 0.5f, t, color);
            }
            break;
        }
        case ICON_MIC: {
            ui_rrect(cx - s * 0.3f, cy - s, s * 0.6f, s * 1.2f, s * 0.3f, color);
            float t = s * 0.2f;
            for (int i = 0; i < 6; i++) {
                float a0 = (float)M_PI * i / 6, a1 = (float)M_PI * (i + 1) / 6;
                ui_line(cx + cosf(a0) * s * 0.6f, cy + sinf(a0) * s * 0.6f - s * 0.1f, cx + cosf(a1) * s * 0.6f, cy + sinf(a1) * s * 0.6f - s * 0.1f, t, color);
            }
            ui_rect(cx - t / 2, cy + s * 0.5f, t, s * 0.4f, color);
            ui_rect(cx - s * 0.4f, cy + s * 0.85f, s * 0.8f, t, color);
            break;
        }
        case ICON_PLAY:
            C2D_DrawTriangle(cx - s * 0.6f, cy - s * 0.8f, color, cx + s * 0.8f, cy, color, cx - s * 0.6f, cy + s * 0.8f, color, DEPTH);
            break;
        case ICON_PAUSE:
            ui_rect(cx - s * 0.7f, cy - s * 0.75f, s * 0.5f, s * 1.5f, color);
            ui_rect(cx + s * 0.2f, cy - s * 0.75f, s * 0.5f, s * 1.5f, color);
            break;
        case ICON_ADD_FRIEND: {
            ui_circle(cx - s * 0.25f, cy - s * 0.4f, s * 0.35f, color);
            ui_rrect(cx - s * 0.9f, cy + s * 0.05f, s * 1.3f, s * 0.75f, s * 0.35f, color);
            float t = 1.6f;
            ui_rect(cx + s * 0.3f, cy - s * 0.35f, s * 0.7f, t, color);
            ui_rect(cx + s * 0.65f - t / 2, cy - s * 0.7f, t, s * 0.7f, color);
            break;
        }
        case ICON_CHECK: {
            float t = size > 12 ? 2.5f : 2;
            ui_line(cx - s * 0.7f, cy, cx - s * 0.15f, cy + s * 0.55f, t, color);
            ui_line(cx - s * 0.15f, cy + s * 0.55f, cx + s * 0.75f, cy - s * 0.6f, t, color);
            break;
        }
        case ICON_RADIO:
            ui_circle(cx, cy, s, color);
            break;
        case ICON_APPLE: {
            ui_circle(cx - s * 0.05f, cy + s * 0.15f, s * 0.62f, color);
            ui_circle(cx + s * 0.45f, cy - s * 0.55f, s * 0.22f, color);
            break;
        }
        case ICON_PLAYSTORE: {
            C2D_DrawTriangle(cx - s * 0.7f, cy - s, RGB(0x4285F4), cx + s * 0.8f, cy, RGB(0xFFD400), cx - s * 0.7f, cy + s, RGB(0xEA4335), DEPTH);
            break;
        }
        case ICON_GAMEPAD:
            ui_rrect(cx - s, cy - s * 0.55f, size, s * 1.1f, s * 0.5f, color);
            break;
        case ICON_CLOSE:
            ui_line(cx - s * 0.7f, cy - s * 0.7f, cx + s * 0.7f, cy + s * 0.7f, 2, color);
            ui_line(cx + s * 0.7f, cy - s * 0.7f, cx - s * 0.7f, cy + s * 0.7f, 2, color);
            break;
        case ICON_PENCIL: {
            ui_line(cx - s * 0.6f, cy + s * 0.6f, cx + s * 0.5f, cy - s * 0.5f, s * 0.5f, color);
            C2D_DrawTriangle(cx - s * 0.85f, cy + s * 0.85f, color, cx - s * 0.45f, cy + s * 0.75f, color, cx - s * 0.75f, cy + s * 0.45f, color, DEPTH);
            break;
        }
    }
}

void ui_wifi_bars(float x, float y, int bars, u32 on, u32 off) {
    for (int i = 0; i < 4; i++) {
        float h = 3 + i * 2;
        ui_rect(x + i * 4, y + 9 - h, 2.5f, h, i < bars ? on : off);
    }
}

void ui_battery(float x, float y, float frac, u32 color) {
    ui_rrect_outline(x, y, 16, 8, 2, 1.5f, color);
    ui_rect(x + 16, y + 2.5f, 1.5f, 3, color);
    float w = (16 - 5) * frac;
    if (w > 0) ui_rect(x + 2.5f, y + 2.5f, w, 3, color);
}

// ---- Avatars -----------------------------------------------------------------------------------

void ui_avatar(float x, float y, float size, bool round, const char *uid, const char *name, float bw, u32 border) {
    if (bw > 0) {
        if (round) ui_circle(x + size / 2, y + size / 2, size / 2, border);
        else ui_rrect(x, y, size, size, size * 0.28f, border);
    }
    float inner = size - 2 * bw;
    C2D_Image *img = (uid && *uid && strncmp(uid, "node-", 5) != 0) ? avatar_get(uid, round) : NULL;
    if (img) {
        float sc = inner / (float)img->subtex->width;
        C2D_DrawImageAt(*img, x + bw, y + bw, DEPTH, NULL, sc, sc);
        return;
    }
    // Placeholder: sand disc with the first letter.
    u32 bg = C_SAND;
    if (round) ui_circle(x + size / 2, y + size / 2, inner / 2, bg);
    else ui_rrect(x + bw, y + bw, inner, inner, inner * 0.28f, bg);
    char initial[8] = {0};
    if (name && *name) {
        u32 cp;
        ssize_t n = decode_utf8(&cp, (const u8 *)name);
        if (n > 0 && n < 7) memcpy(initial, name, n);
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= 32;
    } else {
        strcpy(initial, "?");
    }
    ui_text_v(x + size / 2, y, size, size * 0.42f, C_MUTED, ALIGN_CENTER, FONT_HEAD, initial);
}

// ---- Hit testing -----------------------------------------------------------------------------------

bool ui_in(const touchPosition *t, float x, float y, float w, float h) {
    return t->px >= x && t->px < x + w && t->py >= y && t->py < y + h;
}
bool ui_tap(const Input *in, float x, float y, float w, float h) {
    return in->touch_up && ui_in(&in->touch_start, x, y, w, h) && ui_in(&in->touch, x, y, w, h);
}
bool ui_pressed(const Input *in, float x, float y, float w, float h) { return in->touch_down && ui_in(&in->touch, x, y, w, h); }
bool ui_holding(const Input *in, float x, float y, float w, float h) { return in->touching && ui_in(&in->touch, x, y, w, h); }

// ---- Misc ------------------------------------------------------------------------------------------

void ui_format_time(char *out, size_t n, int64_t unix_ms) {
    time_t t = (time_t)(unix_ms / 1000);
    struct tm *tm = localtime(&t);
    if (!tm) {
        snprintf(out, n, "--:--");
        return;
    }
    snprintf(out, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

void ui_format_ago(char *out, size_t n, int64_t unix_ms) {
    int64_t now = (int64_t)time(NULL) * 1000;
    int64_t d = now - unix_ms;
    if (unix_ms <= 0 || d < 0) d = 0;
    int mins = (int)(d / 60000);
    if (mins < 1) {
        snprintf(out, n, "%s", tr(S_ONLINE_NOW));
    } else if (mins < 60) {
        snprintf(out, n, tr(S_AGO_MIN), mins);
    } else if (mins < 60 * 48) {
        snprintf(out, n, tr(S_AGO_H), mins / 60);
    } else {
        snprintf(out, n, tr(S_AGO_D), mins / (60 * 24));
    }
}

void ui_format_dur(char *out, size_t n, int ms) {
    int s = (ms + 500) / 1000;
    snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

u32 ui_lerp_color(u32 a, u32 b, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    u32 out = 0;
    for (int i = 0; i < 4; i++) {
        int ca = (a >> (i * 8)) & 0xFF, cb = (b >> (i * 8)) & 0xFF;
        out |= (u32)(ca + (cb - ca) * t) << (i * 8);
    }
    return out;
}
