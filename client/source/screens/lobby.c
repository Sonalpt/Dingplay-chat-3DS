// 06 · Local Wireless lobby. Radar upstairs; three 40 px rows and one host button downstairs.
// Reachable from inside a chatroom too: the UDS session stays alive while browsing.
#include "common.h"
#include "../kbd.h"
#include "../local/udsnet.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float s_scan_timer;
static bool s_scanned_once;
static int s_sel;
static float s_join_wait;
static bool s_joining;
static LocalRoom s_join_target;
static float s_pulse;

#define ROW_Y0 (10 + 20 + 7)
#define ROW_H 40
#define ROW_GAP 7
#define HOST_Y (BOT_H - 10 - 42)

static void do_scan(void) {
    if (!local_available()) return;
    local_scan();
    s_scanned_once = true;
    s_scan_timer = 0;
    if (s_sel >= g_local_room_count) s_sel = g_local_room_count - 1;
}

static void enter(void *arg) {
    (void)arg;
    s_scan_timer = 10;  // scan right away
    s_sel = g_local_room_count ? 0 : -1;
    s_joining = false;
    if (!local_available()) local_init();
}

static void try_join(const LocalRoom *r) {
    char code[8] = "";
    if (r->flags & 1) {
        if (!kbd_prompt(KBD_NUMPAD, tr(S_ENTER_PASSCODE), code, sizeof(code), 4)) return;
    }
    if (r->members >= r->slots && r->slots) {
        app_toast(tr(S_ROOM_FULL), C_RED);
        return;
    }
    s_join_target = *r;
    if (!local_join(r, code)) {
        app_toast(tr(S_WRONG_PASSCODE), C_RED);
        return;
    }
    s_joining = true;
    s_join_wait = 0;
}

static void update(const Input *in) {
    s_pulse += in->dt;
    s_scan_timer += in->dt;
    if (in->down & KEY_B) {
        if (s_joining) {
            local_leave();
            s_joining = false;
            return;
        }
        app_back();
        return;
    }
    if (s_joining) {
        s_join_wait += in->dt;
        LocalState st = local_state();
        if (st == LOCAL_JOINED) {
            s_joining = false;
            app_replace(SCR_LOCAL_CHAT, NULL);
            return;
        }
        if (st == LOCAL_DENIED || st == LOCAL_FULL || st == LOCAL_LOST || st == LOCAL_ERROR || st == LOCAL_CLOSED) {
            app_toast(st == LOCAL_DENIED ? tr(S_JOIN_DENIED) : st == LOCAL_FULL ? tr(S_ROOM_FULL) : tr(S_WRONG_PASSCODE), C_RED);
            local_leave();
            s_joining = false;
        }
        if (ui_tap(in, 40, 170, 240, 30)) {
            local_leave();
            s_joining = false;
        }
        return;
    }
    // Auto-scan every 5 s while browsing (the scan blocks briefly).
    if (s_scan_timer > 5.0f) do_scan();
    if (ui_tap(in, BOT_W - 10 - 56, 10, 56, 20) || (in->down & KEY_Y)) do_scan();

    if (in->down & KEY_DOWN && g_local_room_count) s_sel = s_sel < g_local_room_count - 1 ? s_sel + 1 : s_sel;
    if (in->down & KEY_UP && g_local_room_count) s_sel = s_sel > 0 ? s_sel - 1 : 0;
    if (in->down & KEY_A && s_sel >= 0 && s_sel < g_local_room_count) try_join(&g_local_rooms[s_sel]);

    int visible = local_in_room() ? 2 : 3;
    for (int i = 0; i < visible && i < g_local_room_count; i++) {
        float y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        if (ui_tap(in, 10, y, BOT_W - 20, ROW_H)) {
            if (s_sel == i) try_join(&g_local_rooms[i]);
            else s_sel = i;
        }
    }
    if (local_in_room()) {
        if (ui_tap(in, 10, HOST_Y - 42 - 7, BOT_W - 20, 42) || (in->down & KEY_X)) {
            app_replace(SCR_LOCAL_CHAT, NULL);
            return;
        }
    }
    if (!local_in_room() && (ui_tap(in, 10, HOST_Y, BOT_W - 20, 42) || (in->down & KEY_X))) app_go(SCR_CREATE_ROOM, NULL);
}

// ---- Top -------------------------------------------------------------------------------------------

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_BLUE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0xFFFFFF, 71));
    // radar
    float cx = 16 + 75, cy = TOP_H / 2;
    ui_circle(cx, cy, 75, RGBA(0xFFFFFF, 31));
    ui_circle_outline(cx, cy, 75, 3, C_WHITE);
    ui_circle_outline(cx, cy, 50, 2, RGBA(0xFFFFFF, 153));
    ui_circle_outline(cx, cy, 26, 2, RGBA(0xFFFFFF, 153));
    // sweep
    float a = fmodf(s_pulse * 1.2f, (float)M_PI * 2);
    ui_line(cx, cy, cx + cosf(a) * 73, cy + sinf(a) * 73, 2, RGBA(0xFFFFFF, 120));
    ui_circle_border(cx, cy, 10, 2, C_INK, C_WHITE);
    for (int i = 0; i < g_local_room_count && i < 8; i++) {
        const LocalRoom *r = &g_local_rooms[i];
        // distance ring from signal (misses), angle from index
        float dist = 30 + r->misses * 18 + (i % 3) * 6;
        float ang = 0.9f + i * 2.2f;
        float px = cx + cosf(ang) * dist, py = cy + sinf(ang) * dist;
        u32 col = i == s_sel ? C_GREEN : (r->flags & 1) ? C_ORANGE : C_WHITE;
        ui_circle_border(px, py, 7, 2, col, C_INK);
    }
    // text
    float tx = 16 + 150 + 16;
    char title[48];
    if (g_local_room_count == 0) snprintf(title, sizeof(title), "%s", tr(S_NO_ROOM_NEARBY));
    else if (g_local_room_count == 1) snprintf(title, sizeof(title), "%s", tr(S_ONE_ROOM_NEARBY));
    else snprintf(title, sizeof(title), tr(S_ROOMS_NEARBY), g_local_room_count);
    ui_text_shadow(tx, 42, 24, C_WHITE, C_BLUE_SHADOW, 3, ALIGN_LEFT, FONT_HEAD, title);
    ui_text_wrap(tx, 42 + ui_line_height(24) + 3, TOP_W - tx - 16, 11, RGB(0xE3F3FF), ALIGN_LEFT, FONT_BODY,
                 local_available() ? tr(S_LOCAL_ON) : (g_lang == LANG_FR ? "Sans-fil local indisponible — vérifie l'interrupteur Wi-Fi." : "Local Wireless unavailable — check the wireless switch."), 3, 0);
    float cy2 = 42 + ui_line_height(24) + 3 + 3 * ui_line_height(11) + 10;
    for (int i = 0; i < 2 && i < g_local_room_count; i++) {
        const LocalRoom *r = &g_local_rooms[i];
        char line[96];
        const char *q = r->misses ? tr(S_WEAK_SIGNAL) : (r->flags & 1) ? tr(S_LOCKED) : tr(S_STRONG_SIGNAL);
        snprintf(line, sizeof(line), "%s · %d %s · %s", r->name, r->members, tr(S_PLAYERS), q);
        ui_chip(tx, cy2 + i * 25, 10, 8, 4, i == 0 ? C_INK : RGBA(0x241F1A, 153), C_WHITE, line);
    }
}

// ---- Bottom -------------------------------------------------------------------------------------------

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    if (s_joining) {
        ui_text(BOT_W / 2, 70, 13, C_INK, ALIGN_CENTER, FONT_HEAD, s_join_target.name);
        LocalState st = local_state();
        ui_text_wrap(30, 100, BOT_W - 60, 11, C_MUTED, ALIGN_CENTER, FONT_BODY, st == LOCAL_WAITING ? tr(S_WAITING_HOST) : tr(S_JOINING), 2, 0);
        float a = fmodf(s_pulse * 3, (float)M_PI * 2);
        for (int i = 0; i < 8; i++) {
            float aa = a + i * (float)M_PI / 4;
            ui_circle(BOT_W / 2 + cosf(aa) * 14, 150 + sinf(aa) * 14, 3, i == 0 ? C_BLUE : C_SAND);
        }
        ui_card(40, 170, 240, 30, 10, 2, C_WHITE, C_INK, 0, 0, 0);
        ui_text_v(160, 170, 30, 11, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_CANCEL));
        return;
    }
    ui_text_v(10, 10, 20, 11, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_NEARBY_ROOMS));
    // scan button
    bool scanning = s_scan_timer < 0.35f;
    float sw = 56;
    ui_rrect_border(BOT_W - 10 - sw, 10, sw, 20, 8, 2, C_WHITE, C_INK);
    ui_icon(ICON_SCAN, BOT_W - 10 - sw + 12, 20, 11, C_INK);
    ui_text_v(BOT_W - 10 - sw + 21, 10, 20, 9, C_INK, ALIGN_LEFT, FONT_HEAD, scanning ? tr(S_SCANNING) : tr(S_SCAN));

    int visible = local_in_room() ? 2 : 3;
    for (int i = 0; i < visible; i++) {
        float y = ROW_Y0 + i * (ROW_H + ROW_GAP);
        if (i >= g_local_room_count) {
            if (i == 0 && s_scanned_once) {
                ui_dashed_rrect(10, y, BOT_W - 20, ROW_H, 11, 2, C_STONE);
                ui_text_v(BOT_W / 2, y, ROW_H, 10, C_MUTED2, ALIGN_CENTER, FONT_BODY, tr(S_NO_ROOM_NEARBY));
            }
            continue;
        }
        const LocalRoom *r = &g_local_rooms[i];
        bool sel = i == s_sel;
        bool weak = r->misses > 0;
        u32 alpha = weak ? 166 : 255;
        ui_rrect_border(10, y, BOT_W - 20, ROW_H, 11, sel ? 3 : 2, C_WHITE, sel ? C_BLUE : C_INK);
        ui_avatar(19, y + 7, 26, false, NULL, r->host, 0, 0);
        char name[40];
        snprintf(name, sizeof(name), "%s%s", r->name, (r->flags & 1) ? " " : "");
        ui_text(53, y + 5, 13, RGBA(0x241F1A, alpha), ALIGN_LEFT, FONT_HEAD, name);
        if (r->flags & 1) ui_icon(ICON_LOCK, 53 + ui_text_width(13, FONT_HEAD, name) + 6, y + 13, 11, C_INK);
        char sub[64];
        if (weak) snprintf(sub, sizeof(sub), "%s", tr(S_WEAK_SIGNAL));
        else if (r->flags & 1) snprintf(sub, sizeof(sub), "%s", tr(S_PASSCODE_REQUIRED));
        else if (r->flags & 2) snprintf(sub, sizeof(sub), "%s · %s", tr(S_HOSTED_BY), r->host);
        else snprintf(sub, sizeof(sub), "%s%s", tr(S_HOSTED_BY), r->host);
        ui_text(53, y + 22, 9, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
        char n[16];
        snprintf(n, sizeof(n), "%d/%d", r->members, r->slots);
        bool joinable = !(r->flags & 1) && !weak && r->members < r->slots;
        float cw = ui_text_width(10, FONT_HEAD, n) + 14;
        ui_chip(BOT_W - 10 - 9 - cw, y + (ROW_H - ui_line_height(10) - 6) / 2, 10, 7, 3, joinable ? C_GREEN : C_SAND, joinable ? C_WHITE : C_INK, n);
    }
    if (local_in_room()) {
        ui_card(10, HOST_Y - 42 - 7, BOT_W - 20, 42, 12, 3, C_BLUE, C_INK, 0, 5, C_BLUE_SHADOW);
        char back[64];
        snprintf(back, sizeof(back), "%s · %s", tr(S_BACK_TO_ROOM), local_room_name());
        ui_text_v(BOT_W / 2, HOST_Y - 42 - 7, 42, 13, C_WHITE, ALIGN_CENTER, FONT_HEAD, back);
        ui_card(10, HOST_Y, BOT_W - 20, 42, 12, 3, C_SAND, C_INK, 0, 0, 0);
        ui_text_v(BOT_W / 2, HOST_Y, 42, 12, C_MUTED, ALIGN_CENTER, FONT_HEAD, tr(S_HOST_A_ROOM));
    } else {
        ui_card(10, HOST_Y, BOT_W - 20, 42, 12, 3, C_GREEN, C_INK, 0, 5, C_GREEN_SHADOW);
        float tw = ui_text_width(15, FONT_HEAD, tr(S_HOST_A_ROOM));
        ui_icon(ICON_PLUS, BOT_W / 2 - tw / 2 - 12, HOST_Y + 21, 16, C_WHITE);
        ui_text_v(BOT_W / 2 + 10, HOST_Y, 42, 15, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_HOST_A_ROOM));
    }
}

static void leave(void) {}

const ScreenVTable SCREEN_LOBBY = {enter, leave, update, draw_top, draw_bottom};
