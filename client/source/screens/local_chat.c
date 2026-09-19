// 07 · Local Wireless chatroom. One tab strip owns the three input modes so the
// canvas keeps the whole remaining screen; L/R cycle modes without leaving the stylus.
#include "common.h"
#include "../draw.h"
#include "../kbd.h"
#include "../local/udsnet.h"
#include "../voice.h"
#include <stdio.h>
#include <string.h>

enum { TAB_TYPE = 0, TAB_DRAW = 1, TAB_VOICE = 2 };
static int s_tab = TAB_DRAW;
static Canvas s_canvas;
static char s_compose[MSG_TEXT_LEN];
static int s_voice_sel;
static float s_caret;

#define TABS_H 31           // 28 + 3 px border
#define FOOT_Y (BOT_H - 24)
#define PANEL_Y TABS_H
#define PANEL_H (FOOT_Y - TABS_H)
// draw layout: padding 7, right column 78, gap 7
#define CANVAS_X 7
#define CANVAS_Y (PANEL_Y + 7)
#define CANVAS_W (BOT_W - 7 - 78 - 7 - 7)
#define CANVAS_H (PANEL_H - 14)
#define COL_X (BOT_W - 7 - 78)

static void enter(void *arg) {
    (void)arg;
    // Coming back from the lobby / manage panel keeps the half-finished drawing;
    // a new room starts clean.
    static char last_room[ROOM_NAME_LEN];
    static int last_uptime;
    if (strcmp(last_room, local_room_name()) != 0 || local_uptime_s() < last_uptime) {
        canvas_init(&s_canvas, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);
        s_compose[0] = 0;
        strncpy(last_room, local_room_name(), sizeof(last_room) - 1);
    }
    last_uptime = local_uptime_s();
    s_voice_sel = -1;
    voice_panel_reset();
}

static void leave_room_and_back(void) {
    local_leave();
    app_reset_to(SCR_HOME, NULL);
    app_go(SCR_LOBBY, NULL);
}

static void play_selected(void) {
    if (s_voice_sel < 0 || s_voice_sel >= g_local_chat.count) return;
    Message *m = &g_local_chat.items[s_voice_sel];
    if (m->type != MSG_VOICE || !m->voice) return;
    if (voice_is_playing(m->id)) voice_stop();
    else {
        m->played = true;
        voice_play(m->voice, m->voice_len, m->id);
    }
}

static void send_text(const char *t) {
    if (!t || !*t) return;
    if (local_muted_me()) {
        app_toast(tr(S_YOU_ARE_MUTED), C_RED);
        return;
    }
    local_send_text(t);
}

static void update(const Input *in) {
    s_caret += in->dt;
    // room state changes
    LocalState st = local_state();
    if (st == LOCAL_KICKED || st == LOCAL_CLOSED || st == LOCAL_LOST || st == LOCAL_DENIED || st == LOCAL_ERROR || st == LOCAL_IDLE) {
        app_toast(st == LOCAL_KICKED ? tr(S_KICKED) : st == LOCAL_CLOSED ? tr(S_HOST_LEFT) : tr(S_WEAK_SIGNAL), C_RED);
        leave_room_and_back();
        return;
    }
    if (local_take_new_message_flag() && g_settings.notif_sound) voice_beep();

    if (in->down & KEY_B) {
        if (s_tab == TAB_VOICE && (voice_rec_active() || voice_rec_data(NULL))) {
            voice_panel_reset();
            return;
        }
        leave_room_and_back();
        return;
    }
    if (in->down & KEY_L) {
        s_tab = (s_tab + 2) % 3;
        voice_panel_reset();
    }
    if (in->down & KEY_R) {
        s_tab = (s_tab + 1) % 3;
        voice_panel_reset();
    }
    for (int t = 0; t < 3; t++)
        if (ui_tap(in, t * (BOT_W / 3.0f), 0, BOT_W / 3.0f, 28) && s_tab != t) {
            s_tab = t;
            voice_panel_reset();
        }
    if (in->down & KEY_Y) {
        app_go(SCR_LOBBY, NULL);
        return;
    }
    if ((in->down & KEY_X) && local_is_host()) {
        app_go(SCR_MANAGE_ROOM, NULL);
        return;
    }
    if (in->down & KEY_UP) s_voice_sel = chatlog_step_voice(&g_local_chat, s_voice_sel, -1);
    if (in->down & KEY_DOWN) s_voice_sel = chatlog_step_voice(&g_local_chat, s_voice_sel, +1);
    if (s_voice_sel >= g_local_chat.count) s_voice_sel = -1;

    if (s_tab == TAB_TYPE) {
        if (in->down & KEY_A && s_voice_sel >= 0) play_selected();
        float fy = PANEL_Y + 8;
        if (ui_tap(in, 8, fy, BOT_W - 16, 44)) kbd_prompt(KBD_TEXT, tr(S_TYPE_MESSAGE), s_compose, sizeof(s_compose), 200);
        // quick replies
        const char *quick[4] = {tr(S_QUICK_OK), ":D", tr(S_QUICK_2MIN), tr(S_QUICK_WHERE)};
        float qx = 8, qy = fy + 44 + 8;
        for (int i = 0; i < 4; i++) {
            float qw = ui_text_width(10, FONT_HEAD, quick[i]) + 20;
            if (ui_tap(in, qx, qy, qw, 26)) send_text(quick[i]);
            qx += qw + 6;
        }
        float qw2 = ui_text_width(10, FONT_HEAD, tr(S_QUICK_COMING)) + 20;
        if (ui_tap(in, 8, qy + 32, qw2, 26)) send_text(tr(S_QUICK_COMING));
        float sy = FOOT_Y - 8 - 36;
        if (ui_tap(in, 8, sy, BOT_W - 16, 36) || (in->down & KEY_A && s_voice_sel < 0)) {
            send_text(s_compose);
            s_compose[0] = 0;
        }
    } else if (s_tab == TAB_DRAW) {
        canvas_update(&s_canvas, in);
        // palette 3×2 (22 px tall, gap 4)
        float cw = (78 - 8) / 3.0f;
        for (int i = 0; i < 6; i++) {
            float px = COL_X + (i % 3) * (cw + 4), py = CANVAS_Y + (i / 3) * 26;
            if (ui_tap(in, px, py, cw, 22)) {
                if (i == 5) s_canvas.eraser = true;
                else {
                    s_canvas.eraser = false;
                    s_canvas.color = (uint8_t)i;
                }
            }
        }
        float py = CANVAS_Y + 2 * 26 + 1;
        for (int i = 0; i < 3; i++)
            if (ui_tap(in, COL_X + i * 26, py, 26, 22)) s_canvas.pen = (uint8_t)i;
        float cy = py + 22 + 5;
        if (ui_tap(in, COL_X, cy, 78, 22)) canvas_clear(&s_canvas);
        float sy = cy + 22 + 5;
        if (ui_tap(in, COL_X, sy, 78, CANVAS_Y + CANVAS_H - sy) || (in->down & KEY_A)) {
            if (canvas_empty(&s_canvas)) {
                if (s_voice_sel >= 0) play_selected();
            } else if (local_muted_me()) {
                app_toast(tr(S_YOU_ARE_MUTED), C_RED);
            } else if (local_send_draw(&s_canvas.d)) {
                canvas_clear(&s_canvas);
            }
        }
    } else {
        VoicePanelAction a = voice_panel_update(in, 0, PANEL_Y, BOT_W, PANEL_H);
        if (a == VP_SEND) {
            size_t len;
            const u8 *dpv = voice_rec_data(&len);
            if (local_muted_me()) app_toast(tr(S_YOU_ARE_MUTED), C_RED);
            else if (dpv) local_send_voice(dpv, len);
            voice_rec_discard();
        }
    }
}

// ---- Top -------------------------------------------------------------------------------------------

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_CREAM);
    ui_rect(0, 0, TOP_W, 24, C_BLUE);
    ui_icon(ICON_PEOPLE, 16, 12, 14, C_WHITE);
    ui_text_v(29, 0, 24, 12, C_WHITE, ALIGN_LEFT, FONT_HEAD, local_room_name());
    float x = 29 + ui_text_width(12, FONT_HEAD, local_room_name()) + 6;
    ui_chip(x, 6, 8, 6, 2, C_INK, C_WHITE, tr(S_LOCAL_WIRELESS));
    // avatar stack + "N here"
    LocalMember mem[LOCAL_MAX_NODES];
    int nm = local_members(mem, LOCAL_MAX_NODES);
    char here[16];
    snprintf(here, sizeof(here), "%d %s", nm, tr(S_HERE));
    float hw = ui_text_width(9, FONT_HEAD, here);
    int shown = nm < 3 ? nm : 3;
    float sx = TOP_W - 9 - hw - 3 - (shown ? 15 + (shown - 1) * 10 : 0);
    for (int i = 0; i < shown; i++) ui_avatar(sx + i * 10, 4.5f, 15, true, mem[i].avatar_key, mem[i].name, 1, C_WHITE);
    ui_text_v(TOP_W - 9, 0, 24, 9, C_WHITE, ALIGN_RIGHT, FONT_HEAD, here);
    int sig = local_signal();
    if (sig < 2) ui_circle(TOP_W - 9 - hw - 6 - (shown ? 15 + (shown - 1) * 10 : 0) - 6, 12, 3, sig == 1 ? C_ORANGE : C_RED);
    chatlog_draw(&g_local_chat, 10, 24 + 8, TOP_W - 20, TOP_H - 24 - 16, LOG_CREAM, s_voice_sel);
}

// ---- Bottom ---------------------------------------------------------------------------------------

static void tab(int i, const char *label) {
    float x = i * (BOT_W / 3.0f), w = BOT_W / 3.0f;
    bool on = s_tab == i;
    ui_rect(x, 0, w, 28, on ? C_INK : C_SAND);
    ui_text_v(x + w / 2, 0, 28, 12, on ? C_WHITE : C_MUTED, ALIGN_CENTER, FONT_HEAD, label);
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_SAND);
    tab(TAB_TYPE, tr(S_TAB_TYPE));
    tab(TAB_DRAW, tr(S_TAB_DRAW));
    tab(TAB_VOICE, tr(S_TAB_VOICE));
    ui_rect(0, 28, BOT_W, 3, C_INK);

    if (s_tab == TAB_TYPE) {
        float fy = PANEL_Y + 8;
        ui_rrect_border(8, fy, BOT_W - 16, 44, 9, 2, C_WHITE, s_compose[0] ? C_GREEN : C_INK);
        if (s_compose[0]) {
            char shown[MSG_TEXT_LEN];
            ui_ellipsize(shown, sizeof(shown), 12, FONT_BODY, s_compose, BOT_W - 16 - 24);
            ui_text_v(18, fy, 44, 12, C_INK, ALIGN_LEFT, FONT_BODY, shown);
            if ((int)(s_caret * 2) & 1) ui_rect(18 + ui_text_width(12, FONT_BODY, shown) + 1, fy + 14, 2, 16, C_GREEN);
        } else {
            ui_text_v(18, fy, 44, 12, C_MUTED2, ALIGN_LEFT, FONT_BODY, tr(S_TYPE_MESSAGE));
        }
        const char *quick[4] = {tr(S_QUICK_OK), ":D", tr(S_QUICK_2MIN), tr(S_QUICK_WHERE)};
        float qx = 8, qy = fy + 44 + 8;
        for (int i = 0; i < 4; i++) {
            float qw = ui_text_width(10, FONT_HEAD, quick[i]) + 20;
            ui_rrect_border(qx, qy, qw, 26, 7, 2, C_WHITE, C_INK);
            ui_text_v(qx + qw / 2, qy, 26, 10, C_INK, ALIGN_CENTER, FONT_HEAD, quick[i]);
            qx += qw + 6;
        }
        float qw2 = ui_text_width(10, FONT_HEAD, tr(S_QUICK_COMING)) + 20;
        ui_rrect_border(8, qy + 32, qw2, 26, 7, 2, C_WHITE, C_INK);
        ui_text_v(8 + qw2 / 2, qy + 32, 26, 10, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_QUICK_COMING));
        float sy = FOOT_Y - 8 - 36;
        ui_card(8, sy, BOT_W - 16, 36, 9, 2, C_GREEN, C_INK, 0, 3, C_GREEN_SHADOW);
        ui_text_v(BOT_W / 2, sy, 36, 12, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_SEND));
    } else if (s_tab == TAB_DRAW) {
        ui_rrect_border(CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H, 8, 2, C_WHITE, C_INK);
        C2D_Flush();
        C3D_SetScissor(GPU_SCISSOR_NORMAL, BOT_H - (CANVAS_Y + CANVAS_H - 2), CANVAS_X + 2, BOT_H - (CANVAS_Y + 2), CANVAS_X + CANVAS_W - 2);
        canvas_draw(&s_canvas);
        C2D_Flush();
        C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
        if (canvas_empty(&s_canvas)) ui_text(CANVAS_X + 6, CANVAS_Y + CANVAS_H - 4 - ui_line_height(8), 8, C_STONE, ALIGN_LEFT, FONT_HEAD, tr(S_DRAW_HINT));
        // palette
        float cw = (78 - 8) / 3.0f;
        for (int i = 0; i < 6; i++) {
            float px = COL_X + (i % 3) * (cw + 4), py = CANVAS_Y + (i / 3) * 26;
            bool on = i == 5 ? s_canvas.eraser : (!s_canvas.eraser && s_canvas.color == i);
            ui_rrect_border(px, py, cw, 22, 6, 2, RGB(DRAW_COLORS[i]), C_INK);
            if (on) ui_rrect_border(px + 2, py + 2, cw - 4, 22 - 4, 4, 2, RGBA(0, 0), i == 0 ? C_WHITE : C_INK);
            if (i == 5) ui_text_v(px + cw / 2, py, 22, 8, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_ERASER));
        }
        float py = CANVAS_Y + 2 * 26 + 1;
        ui_rrect_border(COL_X, py, 78, 22, 6, 2, C_WHITE, C_INK);
        const float dots[3] = {2, 4, 6.5f};
        for (int i = 0; i < 3; i++) {
            float dx = COL_X + 13 + i * 26, dy = py + 11;
            ui_circle(dx, dy, dots[i], C_INK);
            if (s_canvas.pen == i) ui_circle_border(dx, dy, dots[i] + 4, 2, RGBA(0, 0), C_GREEN);
        }
        float cy = py + 22 + 5;
        ui_rrect_border(COL_X, cy, 78, 22, 6, 2, C_WHITE, C_INK);
        ui_text_v(COL_X + 39, cy, 22, 10, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_CLEAR));
        float sy = cy + 22 + 5, sh = CANVAS_Y + CANVAS_H - sy;
        bool can = !canvas_empty(&s_canvas);
        ui_card(COL_X, sy, 78, sh, 8, 2, can ? C_GREEN : C_SAND, C_INK, 0, can ? 3 : 0, C_GREEN_SHADOW);
        ui_icon(ICON_SEND, COL_X + 39, sy + sh / 2 - 8, 16, can ? C_WHITE : C_MUTED);
        ui_text(COL_X + 39, sy + sh / 2 + 3, 11, can ? C_WHITE : C_MUTED, ALIGN_CENTER, FONT_HEAD, tr(S_SEND));
    } else {
        voice_panel_draw(0, PANEL_Y, BOT_W, PANEL_H);
    }
    // footer
    ui_rect(0, FOOT_Y, BOT_W, 24, C_INK);
    ui_text_v(9, FOOT_Y, 24, 9, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_LEAVE_ROOM));
    char right[80];
    if (local_is_host()) snprintf(right, sizeof(right), "X · %s · Y · %s · %s", tr(S_MANAGE_ROOM), tr(S_LOBBY), tr(S_SWITCH_MODE));
    else snprintf(right, sizeof(right), "Y · %s · %s", tr(S_LOBBY), tr(S_SWITCH_MODE));
    ui_text_v(BOT_W - 9, FOOT_Y, 24, 9, C_STONE, ALIGN_RIGHT, FONT_HEAD, right);
    if (local_muted_me()) ui_text(BOT_W / 2, TABS_H + 2, 8, C_RED, ALIGN_CENTER, FONT_HEAD, tr(S_YOU_ARE_MUTED));
}

static void leave(void) {
    voice_panel_reset();
    voice_stop();
}

const ScreenVTable SCREEN_LOCAL_CHAT = {enter, leave, update, draw_top, draw_bottom};
