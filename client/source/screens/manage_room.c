// 06c · Host controls. Only the host sees this; join requests land as a banner
// row rather than a modal, so a busy room is never blocked by a popup.
#include "common.h"
#include "../local/udsnet.h"
#include <stdio.h>
#include <string.h>

static float s_invite_timer;

static void enter(void *arg) {
    (void)arg;
    s_invite_timer = 0;
}

#define ROW_Y0 32
#define PEND_H 40
#define MEM_H 30
#define GAP 6
#define BTN_Y (BOT_H - 8 - 34)

static void update(const Input *in) {
    if (in->down & KEY_B || !local_is_host()) {
        app_back();
        return;
    }
    if (s_invite_timer > 0) {
        s_invite_timer -= in->dt;
        if (s_invite_timer <= 0) local_set_inviting(false);
    }
    LocalPending pend[LOCAL_MAX_NODES];
    int np = local_pending(pend, LOCAL_MAX_NODES);
    LocalMember mem[LOCAL_MAX_NODES];
    int nm = local_members(mem, LOCAL_MAX_NODES);

    float y = ROW_Y0;
    if (np > 0) {
        // first pending request: ACCEPT / DENY
        float ax = BOT_W - 8 - 8 - 48 - 6 - 56, dx = BOT_W - 8 - 8 - 48;
        if (ui_tap(in, ax, y + 8, 56, 24) || (in->down & KEY_A)) local_accept(pend[0].node);
        else if (ui_tap(in, dx, y + 8, 48, 24) || (in->down & KEY_X)) local_deny(pend[0].node);
        y += PEND_H + GAP;
    }
    for (int i = 0; i < nm; i++) {
        if (mem[i].is_me) continue;
        if (y + MEM_H > BTN_Y - 6) break;
        float kx = BOT_W - 8 - 8 - 48, mx = kx - 6 - 44;
        if (ui_tap(in, mx, y + 4, 44, 22)) local_mute(mem[i].node, !mem[i].muted);
        if (ui_tap(in, kx, y + 4, 48, 22)) local_kick(mem[i].node);
        y += MEM_H + GAP;
    }
    float bw = (BOT_W - 16 - 6) / 2.0f;
    if (ui_tap(in, 8, BTN_Y, bw, 34)) {
        local_set_inviting(true);
        s_invite_timer = 60;
        app_toast(tr(S_INVITING), C_BLUE);
    }
    if (ui_tap(in, 8 + bw + 6, BTN_Y, bw, 34)) {
        local_leave();
        app_reset_to(SCR_HOME, NULL);
        app_go(SCR_LOBBY, NULL);
    }
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_CREAM);
    // header
    ui_rect(0, 0, TOP_W, 24, C_BLUE);
    ui_icon(ICON_PEOPLE, 16, 12, 14, C_WHITE);
    ui_text_v(29, 0, 24, 12, C_WHITE, ALIGN_LEFT, FONT_HEAD, local_room_name());
    float x = 29 + ui_text_width(12, FONT_HEAD, local_room_name()) + 6;
    ui_chip(x, 6, 8, 6, 2, C_INK, C_WHITE, tr(S_YOU_HOST));
    char up[24];
    snprintf(up, sizeof(up), tr(S_UP), local_uptime_s() / 60);
    ui_text_v(TOP_W - 9, 0, 24, 9, C_WHITE, ALIGN_RIGHT, FONT_HEAD, up);
    // roster
    LocalMember mem[LOCAL_MAX_NODES];
    int nm = local_members(mem, LOCAL_MAX_NODES);
    ui_text(12, 35, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_IN_THE_ROOM));
    float ry = 35 + ui_line_height(9) + 6;
    for (int i = 0; i < nm && i < 5; i++) {
        float ax = 12 + i * 50;
        ui_avatar(ax + 2, ry, 40, false, mem[i].avatar_key, mem[i].name, mem[i].is_host ? 3 : 2, mem[i].is_host ? C_GREEN : C_INK);
        char n[16];
        ui_ellipsize(n, sizeof(n), 9, FONT_HEAD, mem[i].name, 48);
        ui_text(ax + 22, ry + 42, 9, mem[i].muted ? C_MUTED2 : C_INK, ALIGN_CENTER, FONT_HEAD, n);
    }
    char msgs[32];
    snprintf(msgs, sizeof(msgs), tr(S_MESSAGES_N), local_msg_count());
    float cy = ry + 42 + ui_line_height(9) + 6;
    float w = ui_chip(12, cy, 9, 7, 3, C_SAND, C_INK, msgs);
    ui_chip(12 + w + 5, cy, 9, 7, 3, C_SAND, C_INK, local_signal() == 2 ? tr(S_SIGNAL_GOOD) : tr(S_SIGNAL_WEAK));
    // waiting panel
    float px = TOP_W - 12 - 132, py = 35, pw = 132, ph = TOP_H - 35 - 12;
    ui_rrect_border(px, py, pw, ph, 12, 3, C_ORANGE, C_INK);
    ui_text(px + 9, py + 9, 9, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_WAITING_TO_JOIN));
    LocalPending pend[LOCAL_MAX_NODES];
    int np = local_pending(pend, LOCAL_MAX_NODES);
    if (np > 0) {
        ui_avatar(px + 9, py + 28, 26, true, pend[0].avatar_key, pend[0].name, 2, C_INK);
        ui_text_v(px + 41, py + 28, 26, 13, C_WHITE, ALIGN_LEFT, FONT_HEAD, pend[0].name);
        if (pend[0].dropped > 0) {
            char line[120];
            snprintf(line, sizeof(line), tr(S_OUT_OF_RANGE_TWICE), pend[0].dropped);
            ui_text_wrap(px + 9, py + 62, pw - 18, 9, C_ORANGE_TINT, ALIGN_LEFT, FONT_BODY, line, 4, 0);
        } else if (np > 1) {
            char more[32];
            snprintf(more, sizeof(more), "+%d", np - 1);
            ui_text(px + 9, py + 62, 9, C_ORANGE_TINT, ALIGN_LEFT, FONT_BODY, more);
        }
    } else {
        ui_text_wrap(px + 9, py + 28, pw - 18, 9, C_ORANGE_TINT, ALIGN_LEFT, FONT_BODY, tr(S_NOBODY_WAITING), 5, 0);
    }
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    ui_rect(0, 0, BOT_W, 24, C_INK);
    char title[48];
    snprintf(title, sizeof(title), "%s%s", tr(S_MANAGE), local_room_name());
    ui_text_v(10, 0, 24, 12, C_WHITE, ALIGN_LEFT, FONT_HEAD, title);
    ui_text_v(BOT_W - 10, 0, 24, 9, C_STONE, ALIGN_RIGHT, FONT_HEAD, tr(S_HOST_ONLY));

    LocalPending pend[LOCAL_MAX_NODES];
    int np = local_pending(pend, LOCAL_MAX_NODES);
    LocalMember mem[LOCAL_MAX_NODES];
    int nm = local_members(mem, LOCAL_MAX_NODES);
    float y = ROW_Y0;
    if (np > 0) {
        ui_rrect_border(8, y, BOT_W - 16, PEND_H, 10, 2, C_ORANGE, C_INK);
        ui_avatar(16, y + 8, 24, true, pend[0].avatar_key, pend[0].name, 2, C_INK);
        char line[64];
        snprintf(line, sizeof(line), "%s%s", pend[0].name, tr(S_WANTS_TO_JOIN));
        ui_text_wrap(47, y + 7, BOT_W - 47 - 130, 11, C_WHITE, ALIGN_LEFT, FONT_HEAD, line, 2, 13);
        float ax = BOT_W - 8 - 8 - 48 - 6 - 56, dx = BOT_W - 8 - 8 - 48;
        ui_rrect_border(ax, y + 8, 56, 24, 7, 2, C_GREEN, C_INK);
        ui_text_v(ax + 28, y + 8, 24, 10, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_ACCEPT));
        ui_rrect_border(dx, y + 8, 48, 24, 7, 2, C_WHITE, C_INK);
        ui_text_v(dx + 24, y + 8, 24, 10, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_DENY));
        y += PEND_H + GAP;
    }
    for (int i = 0; i < nm; i++) {
        if (mem[i].is_me) continue;
        if (y + MEM_H > BTN_Y - 6) break;
        ui_rrect_border(8, y, BOT_W - 16, MEM_H, 9, 2, C_WHITE, C_INK);
        ui_avatar(16, y + 5, 20, true, mem[i].avatar_key, mem[i].name, 0, 0);
        ui_text_v(43, y, MEM_H, 12, C_INK, ALIGN_LEFT, FONT_HEAD, mem[i].name);
        float kx = BOT_W - 8 - 8 - 48, mx = kx - 6 - 44;
        ui_rrect_border(mx, y + 4, 44, 22, 6, 1.5f, mem[i].muted ? C_INK : C_SAND, C_INK);
        ui_text_v(mx + 22, y + 4, 22, 9, mem[i].muted ? C_WHITE : C_INK, ALIGN_CENTER, FONT_HEAD, mem[i].muted ? tr(S_MUTED) : tr(S_MUTE));
        ui_rrect_border(kx, y + 4, 48, 22, 6, 1.5f, C_WHITE, C_RED);
        ui_text_v(kx + 24, y + 4, 22, 9, C_RED, ALIGN_CENTER, FONT_HEAD, tr(S_KICK));
        y += MEM_H + GAP;
    }
    float bw = (BOT_W - 16 - 6) / 2.0f;
    ui_card(8, BTN_Y, bw, 34, 9, 2, s_invite_timer > 0 ? C_BLUE : C_WHITE, C_INK, 0, 3, C_INK);
    ui_text_v(8 + bw / 2, BTN_Y, 34, 11, s_invite_timer > 0 ? C_WHITE : C_INK, ALIGN_CENTER, FONT_HEAD, s_invite_timer > 0 ? tr(S_INVITING) : tr(S_INVITE_NEARBY));
    ui_card(8 + bw + 6, BTN_Y, bw, 34, 9, 2, C_RED, C_INK, 0, 3, C_RED_SHADOW);
    ui_text_v(8 + bw + 6 + bw / 2, BTN_Y, 34, 11, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_CLOSE_ROOM));
}

static void leave(void) {}

const ScreenVTable SCREEN_MANAGE_ROOM = {enter, leave, update, draw_top, draw_bottom};
