#include "common.h"
#include "../draw.h"
#include "../voice.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

// Composite of rgba(9,20,46,.78) over #0B1836 — the bubble tone on a flat navy log.
#define C_NAVY_BUBBLE_OPAQUE RGB(0x091530)
#define C_NAVY_BUBBLE_BORDER RGB(0x6F7A93)  // rgba(255,255,255,.4) over the bubble

// ---- Voice bubble -----------------------------------------------------------------------------

static float voice_bubble_width(const Message *m, float scale, float *dur_w_out, char *dur, size_t n) {
    float pad = 7 * scale, play_r = 9 * scale, bars_w = 8 * 4 * scale - 2 * scale;
    ui_format_dur(dur, n, m->dur_ms);
    float dur_w = ui_text_width(10 * scale, FONT_HEAD, dur);
    if (dur_w_out) *dur_w_out = dur_w;
    return pad + play_r * 2 + 7 * scale + bars_w + 7 * scale + dur_w + pad;
}

float voice_bubble(float x, float y, const Message *m, bool mine, LogStyle style, float scale) {
    // 17 px tall at scale 1: play circle 18, eight 2 px bars, duration label.
    float h = 17 * scale;
    float pad = 7 * scale;
    float play_r = 9 * scale;
    float bars_w = 8 * 4 * scale - 2 * scale;
    char dur[8];
    float dur_w;
    float w = voice_bubble_width(m, scale, &dur_w, dur, sizeof(dur));
    bool playing = voice_is_playing(m->id);
    float progress = playing ? voice_play_progress() : 0;

    u32 bg, border, bar_off, bar_on, play_bg, play_fg, text;
    if (style == LOG_NAVY) {
        bg = mine ? C_GREEN : C_NAVY_BUBBLE_OPAQUE;
        border = mine ? C_GREEN : C_NAVY_BUBBLE_BORDER;
        bar_off = mine ? C_GREEN_TINT : C_NAVY_BARS;
        text = C_WHITE;
        play_bg = mine ? C_WHITE : C_BLUE;
        play_fg = mine ? C_GREEN : C_WHITE;
    } else {
        bg = mine ? C_GREEN : C_WHITE;
        border = C_INK;
        bar_off = mine ? C_GREEN_TINT : C_STONE;
        text = mine ? C_WHITE : C_INK;
        play_bg = mine ? C_WHITE : C_BLUE;
        play_fg = mine ? C_GREEN : C_WHITE;
    }
    bar_on = C_ORANGE;
    if (playing) play_bg = C_ORANGE, play_fg = C_WHITE;

    float bh = h + 2 * pad * 0.85f;
    float r = 11 * scale, tail = 3 * scale;
    if (mine) ui_rrect4(x, y, w, bh, r, r, tail, r, border);
    else ui_rrect4(x, y, w, bh, r, r, r, tail, border);
    float bw = 2 * scale;
    if (mine) ui_rrect4(x + bw, y + bw, w - 2 * bw, bh - 2 * bw, r - bw, r - bw, 1, r - bw, bg);
    else ui_rrect4(x + bw, y + bw, w - 2 * bw, bh - 2 * bw, r - bw, r - bw, r - bw, 1, bg);
    if (!mine && !m->played && !m->expired && style == LOG_NAVY) {
        // unplayed: orange inset ring
        ui_rrect4(x + bw, y + bw, w - 2 * bw, bh - 2 * bw, r - bw, r - bw, r - bw, 1, C_ORANGE);
        ui_rrect4(x + bw * 2.5f, y + bw * 2.5f, w - 5 * bw, bh - 5 * bw, r - bw * 2, r - bw * 2, r - bw * 2, 1, bg);
    }
    float cy = y + bh / 2;
    float cx = x + pad + play_r;
    ui_circle(cx, cy, play_r, play_bg);
    ui_icon(playing ? ICON_PAUSE : ICON_PLAY, cx + (playing ? 0 : 1 * scale), cy, play_r * 0.9f, play_fg);
    float bx = cx + play_r + 7 * scale;
    for (int i = 0; i < 8; i++) {
        float bar_h = (m->bars[i] < 3 ? 3 : m->bars[i]) * scale * (14.0f / 15.0f);
        bool lit = playing && (float)i / 8 < progress;
        ui_rrect(bx + i * 4 * scale, cy - bar_h / 2, 2 * scale, bar_h, scale, lit ? bar_on : bar_off);
    }
    ui_text_v(bx + bars_w + 7 * scale, y, bh, 10 * scale, text, ALIGN_LEFT, FONT_HEAD, dur);
    if (m->expired) {
        // strike the duration to signal the note is gone
        ui_rect(bx + bars_w + 7 * scale, cy, dur_w, 1.5f * scale, text);
    }
    return w;
}

// ---- Chat log ------------------------------------------------------------------------------------

typedef struct {
    float h;        // total block height incl. name line / timestamp
    float bubble_w;
    int lines;
} Metrics;

#define MAX_BUBBLE_W 250.0f
#define DRAW_W 120.0f
#define DRAW_H 52.0f
#define TEXT_PX 12.0f

static void measure(const Message *m, LogStyle style, Metrics *out) {
    memset(out, 0, sizeof(*out));
    float name_h = (style == LOG_NAVY && !m->mine) ? ui_line_height(8) + 2 : 0;
    float meta_h = (style == LOG_NAVY && m->mine) ? ui_line_height(8) + 2 : 0;
    float pad_x = style == LOG_NAVY ? 10 : 9;
    float pad_y = style == LOG_NAVY ? 7 : 6;
    if (m->type == MSG_TEXT || m->type == MSG_IMAGE) {
        char text[MSG_TEXT_LEN + NAME_LEN + 4];
        const char *body = m->type == MSG_IMAGE ? tr(S_IMAGE_PLACEHOLDER) : m->text;
        if (style == LOG_CREAM && !m->mine) snprintf(text, sizeof(text), "%s : %s", m->name, body);
        else snprintf(text, sizeof(text), "%s", body);
        float inner_w = MAX_BUBBLE_W - 2 * pad_x;
        float tw = ui_text_width(TEXT_PX, FONT_BODY, text);
        int lines = tw <= inner_w ? 1 : ui_text_wrap(0, 0, inner_w, TEXT_PX, 0, ALIGN_LEFT, FONT_BODY, text, 6, 0);
        if (lines < 1) lines = 1;
        out->lines = lines;
        out->bubble_w = (lines == 1 ? tw : inner_w) + 2 * pad_x;
        out->h = name_h + meta_h + lines * ui_line_height(TEXT_PX) + 2 * pad_y;
    } else if (m->type == MSG_DRAW) {
        out->bubble_w = DRAW_W + 2 * 4 + 4;
        out->h = name_h + meta_h + DRAW_H + 2 * 4 + 4;
    } else {
        out->bubble_w = 0;  // computed at draw time
        out->h = name_h + meta_h + 17 + 12;
    }
}

int chatlog_last_voice(const MessageList *l) {
    for (int i = l->count - 1; i >= 0; i--)
        if (l->items[i].type == MSG_VOICE) return i;
    return -1;
}

int chatlog_step_voice(const MessageList *l, int current, int dir) {
    int i = current < 0 ? (dir < 0 ? l->count : -1) : current;
    for (;;) {
        i += dir;
        if (i < 0 || i >= l->count) return current;
        if (l->items[i].type == MSG_VOICE) return i;
    }
}

int chatlog_draw(const MessageList *l, float x, float y, float w, float h, LogStyle style, int selected) {
    const float gap = 6;
    const float avatar = style == LOG_NAVY ? 20 : 18;
    const float pad_x = style == LOG_NAVY ? 10 : 9;
    const float pad_y = style == LOG_NAVY ? 7 : 6;

    if (l->count == 0) {
        ui_text_v(x + w / 2, y, h, 11, style == LOG_NAVY ? C_NAVY_NAME : C_MUTED2, ALIGN_CENTER, FONT_BODY, tr(S_NO_MESSAGES));
        return 0;
    }
    // Bottom-up: collect the messages that fit.
    Metrics mt[MSG_HISTORY];
    int first = l->count;
    float used = 0;
    for (int i = l->count - 1; i >= 0; i--) {
        measure(&l->items[i], style, &mt[i]);
        float need = mt[i].h + (used > 0 ? gap : 0);
        if (used + need > h) break;
        used += need;
        first = i;
    }
    if (first == l->count) first = l->count - 1;  // always show the newest, clipped
    float cy = y + h;
    for (int i = l->count - 1; i >= first; i--) {
        const Message *m = &l->items[i];
        Metrics *k = &mt[i];
        if (i != l->count - 1) cy -= gap;
        cy -= k->h;
        float top = cy;
        bool mine = m->mine;
        float bx, by = top;
        float name_h = (style == LOG_NAVY && !mine) ? ui_line_height(8) + 2 : 0;
        float bubble_h = k->h - name_h - ((style == LOG_NAVY && mine) ? ui_line_height(8) + 2 : 0);

        if (!mine) {
            // avatar bottom-aligned with the bubble
            float ax = x, ay = top + name_h + bubble_h - avatar;
            if (style == LOG_NAVY) ui_avatar(ax, ay, avatar, true, m->uid, m->name, 0, 0);
            else ui_avatar(ax, ay, avatar, true, m->uid, m->name, 2, C_INK);
            bx = x + avatar + 6;
            if (name_h) ui_text(bx, top, 8, C_NAVY_NAME, ALIGN_LEFT, FONT_HEAD, m->name);
            by = top + name_h;
        } else {
            bx = x + w;  // right edge; adjusted per bubble width below
        }

        u32 fill, border, text_col;
        if (style == LOG_NAVY) {
            fill = mine ? C_GREEN : C_NAVY_BUBBLE_OPAQUE;
            border = mine ? C_GREEN : C_NAVY_BUBBLE_BORDER;
            text_col = C_WHITE;
        } else {
            fill = mine ? C_GREEN : C_WHITE;
            border = C_INK;
            text_col = mine ? C_WHITE : C_INK;
        }
        float r = style == LOG_NAVY ? 11 : 10, tail = style == LOG_NAVY ? 3 : 2;
        float bw = 2;

        if (m->type == MSG_VOICE) {
            char tmp[8];
            float vw = voice_bubble_width(m, 1.0f, NULL, tmp, sizeof(tmp));
            float vx = mine ? x + w - vw : bx;
            voice_bubble(vx, by, m, mine, style, 1.0f);
            if (i == selected) ui_rrect_outline(vx - 3, by - 3, vw + 6, bubble_h + 6, r + 3, 2, C_ORANGE);
            if (mine && style == LOG_NAVY) {
                char meta[40], t[8];
                ui_format_time(t, sizeof(t), m->ts ? m->ts : (int64_t)time(NULL) * 1000);
                snprintf(meta, sizeof(meta), "%s · %s", t, m->pending ? tr(S_SENDING) : tr(S_SENT));
                ui_text(x + w, by + bubble_h + 2, 8, C_NAVY_NAME, ALIGN_RIGHT, FONT_HEAD, meta);
            }
            if (m->pending) ui_circle(vx + vw - 4, by - 2, 3, C_ORANGE);
            continue;
        }

        float bwid = k->bubble_w;
        if (mine) bx = x + w - bwid;
        if (mine) ui_rrect4(bx, by, bwid, bubble_h, r, r, tail, r, border);
        else ui_rrect4(bx, by, bwid, bubble_h, r, r, r, tail, border);
        if (style == LOG_NAVY && mine) {
            // mine has no distinct border in the mockup: solid green
        } else {
            if (mine) ui_rrect4(bx + bw, by + bw, bwid - 2 * bw, bubble_h - 2 * bw, r - bw, r - bw, 1, r - bw, fill);
            else ui_rrect4(bx + bw, by + bw, bwid - 2 * bw, bubble_h - 2 * bw, r - bw, r - bw, r - bw, 1, fill);
        }

        if (m->type == MSG_DRAW) {
            float dx = bx + 4 + (mine ? 0 : 0), dy = by + 4;
            ui_rect(dx, dy, DRAW_W + 4, DRAW_H + 4, C_WHITE);
            drawing_render(m->draw, dx + 2, dy + 2, DRAW_W, DRAW_H);
        } else {
            char text[MSG_TEXT_LEN + NAME_LEN + 4];
            const char *body = m->type == MSG_IMAGE ? tr(S_IMAGE_PLACEHOLDER) : m->text;
            if (style == LOG_CREAM && !mine) snprintf(text, sizeof(text), "%s : %s", m->name, body);
            else snprintf(text, sizeof(text), "%s", body);
            ui_text_wrap(bx + pad_x, by + pad_y, bwid - 2 * pad_x, TEXT_PX, text_col, ALIGN_LEFT, FONT_BODY, text, 6, 0);
        }
        if (mine && style == LOG_NAVY) {
            char meta[40], t[8];
            ui_format_time(t, sizeof(t), m->ts ? m->ts : (int64_t)time(NULL) * 1000);
            snprintf(meta, sizeof(meta), "%s · %s", t, m->pending ? tr(S_SENDING) : tr(S_SENT));
            ui_text(x + w, by + bubble_h + 2, 8, C_NAVY_NAME, ALIGN_RIGHT, FONT_HEAD, meta);
        } else if (m->pending) {
            ui_circle(bx + bwid - 4, by - 2, 3, C_ORANGE);
        }
    }
    return l->count - first;
}

// ---- Status cluster / bars ------------------------------------------------------------------------

void status_cluster(float right, float y, u32 fg) {
    char t[8];
    ui_format_time(t, sizeof(t), (int64_t)time(NULL) * 1000);
    float tw = ui_text_width(9, FONT_HEAD, t);
    float x = right - 16 - 2;  // battery
    u8 level = 5;
    PTMU_GetBatteryLevel(&level);
    ui_battery(x, y + 7, level / 5.0f, fg);
    x -= 7 + tw;
    ui_text_v(x, y, 22, 9, fg, ALIGN_LEFT, FONT_HEAD, t);
    x -= 7 + 15;
    ui_wifi_bars(x, y + 6.5f, g_wifi ? g_wifi_bars + 1 : 0, C_GREEN, RGBA(0xFFFFFF, 70));
}

void top_brand_bar(void) {
    ui_rect(0, 0, TOP_W, 22, C_INK);
    ui_text_v(9, 0, 22, 10, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_APP_NAME));
    status_cluster(TOP_W - 9, 0, C_WHITE);
}

void footer_bar(float y, float h, const char *left, const char *right, u32 bg, u32 fg_left, u32 fg_right) {
    ui_rect(0, y, BOT_W, h, bg);
    if (bg != C_INK) ui_rect(0, y, BOT_W, 2, C_INK);
    if (left) ui_text_v(10, y, h, 10, fg_left, ALIGN_LEFT, FONT_HEAD, left);
    if (right) ui_text_v(BOT_W - 10, y, h, 10, fg_right, ALIGN_RIGHT, FONT_HEAD, right);
}

// ---- Voice panel ----------------------------------------------------------------------------------

static bool s_vp_holding;

void voice_panel_reset(void) {
    s_vp_holding = false;
    voice_rec_cancel();
}

static void vp_layout(float x, float y, float w, float h, float *bcx, float *bcy, float *btn_y) {
    *bcx = x + w / 2;
    *bcy = y + h * 0.56f;
    *btn_y = *bcy + 38 + 8;
}

VoicePanelAction voice_panel_update(const Input *in, float x, float y, float w, float h) {
    float bcx, bcy, btn_y;
    vp_layout(x, y, w, h, &bcx, &bcy, &btn_y);
    bool have = voice_rec_data(NULL) != NULL;
    bool rec = voice_rec_active();
    // Big button: press (stylus or A) starts, release stops.
    bool on_btn = ui_in(&in->touch, bcx - 38, bcy - 38, 76, 76);
    if (!rec && !have && ((in->touch_down && on_btn) || (in->down & KEY_A))) {
        if (voice_rec_start()) s_vp_holding = true;
    } else if (rec && s_vp_holding) {
        bool released = (in->touch_up || !in->touching) && !(in->held & KEY_A);
        if (released) {
            voice_rec_stop();
            s_vp_holding = false;
        }
    }
    if (rec && !s_vp_holding) {  // hit the 10 s cap
        if (!voice_rec_active()) s_vp_holding = false;
    }
    // CANCEL / SEND
    float bw = 76, bh = 28, gap = 9;
    float cx0 = x + w / 2 - bw - gap / 2, sx0 = x + w / 2 + gap / 2;
    if (ui_tap(in, cx0, btn_y, bw, bh) || (in->down & KEY_B && (have || rec))) {
        voice_rec_cancel();
        s_vp_holding = false;
        return VP_CANCEL;
    }
    if (have && (ui_tap(in, sx0, btn_y, bw, bh) || (in->down & KEY_X))) return VP_SEND;
    return VP_NONE;
}

void voice_panel_draw(float x, float y, float w, float h) {
    float bcx, bcy, btn_y;
    vp_layout(x, y, w, h, &bcx, &bcy, &btn_y);
    bool have = voice_rec_data(NULL) != NULL;
    bool rec = voice_rec_active();
    // 12 level bars
    float bx = x + w / 2 - (12 * 7 - 3) / 2.0f, by = y + 10;
    for (int i = 0; i < VOICE_LEVELS; i++) {
        float lv = voice_rec_level(i);
        float bh = 6 + lv * 30;
        if (!rec && !have) bh = 6;
        ui_rrect(bx + i * 7, by + 18 - bh / 2, 4, bh, 2, rec || have ? C_RED : C_STONE);
    }
    char cur[8], cap[8], line[24];
    ui_format_dur(cur, sizeof(cur), voice_rec_ms());
    ui_format_dur(cap, sizeof(cap), VOICE_MAX_MS);
    float t_y = by + 40;
    float w1 = ui_text_width(14, FONT_HEAD, cur);
    snprintf(line, sizeof(line), " / %s", cap);
    float w2 = ui_text_width(14, FONT_HEAD, line);
    ui_text(x + w / 2 - (w1 + w2) / 2, t_y, 14, C_INK, ALIGN_LEFT, FONT_HEAD, cur);
    ui_text(x + w / 2 - (w1 + w2) / 2 + w1, t_y, 14, C_MUTED, ALIGN_LEFT, FONT_HEAD, line);
    // big button
    u32 fill = rec ? C_RED : have ? C_GREEN : C_RED;
    u32 shadow = rec ? C_RED_SHADOW : have ? C_GREEN_SHADOW : C_RED_SHADOW;
    ui_circle(bcx, bcy + 5, 38, shadow);
    ui_circle_border(bcx, bcy, 38, 4, fill, C_INK);
    if (have) {
        ui_icon(ICON_CHECK, bcx, bcy - 6, 20, C_WHITE);
        ui_text(bcx, bcy + 6, 9, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_SEND));
    } else {
        ui_circle(bcx, bcy - 8, 9, C_WHITE);
        ui_text(bcx, bcy + 4, 11, C_WHITE, ALIGN_CENTER, FONT_HEAD, rec ? tr(S_RELEASE) : tr(S_HOLD_TO_RECORD));
    }
    float bw = 76, bh = 28, gap = 9;
    float cx0 = x + w / 2 - bw - gap / 2, sx0 = x + w / 2 + gap / 2;
    ui_card(cx0, btn_y, bw, bh, 9, 2, C_WHITE, C_INK, 0, 0, 0);
    ui_text_v(cx0 + bw / 2, btn_y, bh, 11, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_CANCEL));
    ui_card(sx0, btn_y, bw, bh, 9, 2, have ? C_GREEN : C_SAND, C_INK, 0, have ? 3 : 0, C_GREEN_SHADOW);
    ui_text_v(sx0 + bw / 2, btn_y, bh, 11, have ? C_WHITE : C_MUTED, ALIGN_CENTER, FONT_HEAD, tr(S_SEND));
    if (!voice_can_record()) ui_text(x + w / 2, y + h - 14, 8, C_RED, ALIGN_CENTER, FONT_HEAD, "mic unavailable");
}
