// 02 · Login. Top: branding + "no account" nudge. Bottom: username / password / stay signed in.
#include "common.h"
#include "../api.h"
#include "../kbd.h"
#include "../store.h"
#include <stdio.h>
#include <string.h>

float boot_draw_wordmark(float cx, float y, float w);

static char s_user[64], s_pass[64];
static bool s_show_pass;
static bool s_busy;
static int s_focus;  // 0 user, 1 pass
static float s_caret;

static void login_done(int status, cJSON *json, void *user) {
    (void)user;
    s_busy = false;
    if (status == 200) {
        store_save_settings(&g_settings);
        app_reset_to(SCR_HOME, NULL);
        return;
    }
    const char *err = tr(S_ERR_RELAY);
    if (status < 0) err = tr(S_ERR_NETWORK);
    else if (status == 401 && json) {
        cJSON *e = cJSON_GetObjectItemCaseSensitive(json, "error");
        if (cJSON_IsString(e) && strcmp(e->valuestring, "unknown_user") == 0) err = tr(S_ERR_UNKNOWN_USER);
        else err = tr(S_ERR_BAD_CREDENTIALS);
    }
    app_toast(err, C_RED);
}

static void enter(void *arg) {
    (void)arg;
    s_busy = false;
    s_show_pass = false;
    s_focus = s_user[0] ? 1 : 0;
    if (!g_session.players_online) api_stats(NULL, NULL);
}

static void do_login(void) {
    if (s_busy) return;
    if (!s_user[0]) {
        s_focus = 0;
        return;
    }
    if (!s_pass[0]) {
        s_focus = 1;
        return;
    }
    if (!g_wifi) {
        app_toast(tr(S_ERR_NETWORK), C_RED);
        return;
    }
    s_busy = true;
    api_login(s_user, s_pass, login_done, NULL);
}

// layout (bottom): padding 12, gap 8
#define FY_USER 27
#define FY_PASS 74
#define FY_CHECK 114
#define FY_LOGIN 138
#define FY_LOCAL 184

static void update(const Input *in) {
    s_caret += in->dt;
    if (ui_tap(in, 12, FY_USER, 296, 32)) {
        s_focus = 0;
        if (kbd_prompt(KBD_USERNAME, tr(S_USERNAME), s_user, sizeof(s_user), 40)) s_focus = 1;
    }
    if (ui_tap(in, 12, FY_PASS, 240, 32)) {
        s_focus = 1;
        kbd_prompt(KBD_PASSWORD, tr(S_PASSWORD), s_pass, sizeof(s_pass), 60);
    }
    if (ui_tap(in, 252, FY_PASS, 56, 32)) s_show_pass = !s_show_pass;
    if (ui_tap(in, 12, FY_CHECK, 200, 18)) {
        g_settings.stay_signed_in = !g_settings.stay_signed_in;
        store_save_settings(&g_settings);
    }
    if (ui_tap(in, 12, FY_LOGIN, 296, 38) || (in->down & KEY_A)) do_login();
    if (ui_tap(in, 12, FY_LOCAL, 296, 30) || (in->down & KEY_Y)) {
        g_session.logged_in = false;
        app_reset_to(SCR_HOME, NULL);
    }
    if (in->down & KEY_B) {
        g_session.logged_in = false;
        app_reset_to(SCR_HOME, NULL);
    }
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_ORANGE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0xFFFFFF, 77));
    float cx = TOP_W / 2;
    boot_draw_wordmark(cx, 8, 120);
    // sign-in card
    float cw = 300, ch = 56, cy0 = 70;
    ui_card(cx - cw / 2, cy0, cw, ch, 14, 3, C_WHITE, C_INK, 4, 4, C_INK);
    ui_text(cx, cy0 + 11, 14, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_SIGN_IN_TITLE));
    ui_text_wrap(cx - cw / 2 + 16, cy0 + 31, cw - 32, 10, C_MUTED, ALIGN_CENTER, FONT_BODY, tr(S_SIGN_IN_SUB), 2, 0);
    // no-account nudge
    float nw = 330, nh = 52, ny = 138;
    ui_rrect(cx - nw / 2, ny, nw, nh, 12, C_INK);
    ui_text(cx - nw / 2 + 11, ny + 8, 11, C_GOLD, ALIGN_LEFT, FONT_HEAD, tr(S_NO_ACCOUNT));
    ui_text_wrap(cx - nw / 2 + 11, ny + 23, nw - 22 - 84, 10, C_CREAM_TEXT, ALIGN_LEFT, FONT_BODY, tr(S_NO_ACCOUNT_SUB), 2, 12);
    float bx = cx + nw / 2 - 11 - 72;
    ui_rrect(bx, ny + 8, 72, 16, 6, C_WHITE);
    ui_icon(ICON_APPLE, bx + 10, ny + 16, 11, C_INK);
    ui_text_v(bx + 19, ny + 8, 16, 9, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_APP_STORE));
    ui_rrect(bx, ny + 28, 72, 16, 6, C_WHITE);
    ui_icon(ICON_PLAYSTORE, bx + 10, ny + 36, 11, C_INK);
    ui_text_v(bx + 19, ny + 28, 16, 9, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_GOOGLE_PLAY));
    // players online
    char po[48];
    if (g_session.players_online > 0) {
        char n[16];
        int v = g_session.players_online;
        if (v >= 1000) snprintf(n, sizeof(n), "%d %03d", v / 1000, v % 1000);
        else snprintf(n, sizeof(n), "%d", v);
        snprintf(po, sizeof(po), "%s %s", n, tr(S_PLAYERS_ONLINE));
        float pw = ui_text_width(9, FONT_HEAD, po) + 16;
        ui_chip(cx - pw / 2, 204, 9, 8, 4, C_INK, C_WHITE, po);
    }
}

static void field(float y, const char *label, const char *value, bool focused, bool password, u32 label_col) {
    ui_text(12, y - 15, 9, label_col, ALIGN_LEFT, FONT_HEAD, label);
    if (focused) {
        ui_rrect(12 - 2, y - 2, 296 + 4, 36, 11, RGBA(0x2FB86E, 64));
        ui_rrect_border(12, y, 296, 32, 9, 3, C_WHITE, C_GREEN);
    } else {
        ui_rrect_border(12, y, 296, 32, 9, 2, C_WHITE, C_INK);
    }
    char shown[80];
    if (password && !s_show_pass) {
        size_t n = strlen(value);
        if (n > 24) n = 24;
        for (size_t i = 0; i < n; i++) memcpy(shown + i * 3, "\xE2\x80\xA2", 3);
        shown[n * 3] = 0;
    } else {
        ui_ellipsize(shown, sizeof(shown), 13, FONT_BODY, value, password ? 210 : 270);
    }
    float tx = 22;
    ui_text_v(tx, y, 32, password ? 14 : 13, C_INK, ALIGN_LEFT, FONT_BODY, shown);
    if (focused && ((int)(s_caret * 2) & 1)) {
        float w = ui_text_width(password ? 14 : 13, FONT_BODY, shown);
        ui_rect(tx + w + 1, y + 9, 2, 14, C_GREEN);
    }
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    char label[64];
    field(FY_USER, tr(S_USERNAME), s_user, s_focus == 0, false, C_MUTED);
    snprintf(label, sizeof(label), "%s · %s", tr(S_PASSWORD), tr(S_TAP_KEYBOARD));
    field(FY_PASS, label, s_pass, s_focus == 1, true, C_GREEN);
    ui_text_v(252 + 46, FY_PASS, 32, 9, C_MUTED, ALIGN_RIGHT, FONT_HEAD, s_show_pass ? tr(S_HIDE) : tr(S_SHOW));
    // checkbox
    ui_rrect_border(12, FY_CHECK, 16, 16, 5, 2, g_settings.stay_signed_in ? C_GREEN : C_WHITE, C_INK);
    if (g_settings.stay_signed_in) ui_icon(ICON_CHECK, 20, FY_CHECK + 8, 10, C_WHITE);
    ui_text_v(35, FY_CHECK, 16, 11, C_INK, ALIGN_LEFT, FONT_BODY, tr(S_STAY_SIGNED));
    // LOG IN
    ui_card(12, FY_LOGIN, 296, 38, 11, 3, s_busy ? C_SAND : C_GREEN, C_INK, 0, 4, s_busy ? C_INK : C_GREEN_SHADOW);
    ui_text_v(BOT_W / 2, FY_LOGIN, 38, 16, s_busy ? C_MUTED : C_WHITE, ALIGN_CENTER, FONT_HEAD, s_busy ? tr(S_LOGGING_IN) : tr(S_LOG_IN));
    // local escape hatch
    ui_rrect(12, FY_LOCAL, 296, 30, 10, C_WHITE);
    ui_dashed_rrect(12, FY_LOCAL, 296, 30, 10, 2, C_INK);
    ui_text_v(BOT_W / 2, FY_LOCAL, 30, 11, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_PLAY_LOCAL_NO_ACCOUNT));
}

static void leave(void) {}

const ScreenVTable SCREEN_LOGIN = {enter, leave, update, draw_top, draw_bottom};
