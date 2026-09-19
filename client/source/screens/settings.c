// 09 · Settings. Status is read-only upstairs; only the toggles live where fingers go.
#include "common.h"
#include "../api.h"
#include "../kbd.h"
#include "../local/udsnet.h"
#include "../mii.h"
#include "../store.h"
#include <stdio.h>
#include <string.h>

static char s_ssid[40];
static float s_ssid_timer;

static void read_ssid(void) {
    s_ssid[0] = 0;
    if (!g_wifi) return;
    char buf[36];
    memset(buf, 0, sizeof(buf));
    if (R_SUCCEEDED(ACU_GetSSID(buf))) {
        buf[32] = 0;
        strncpy(s_ssid, buf, sizeof(s_ssid) - 1);
    }
}

static void enter(void *arg) {
    (void)arg;
    read_ssid();
    s_ssid_timer = 0;
}

#define ROW_X 8
#define ROW_W (BOT_W - 16)
#define ROW_H 32
#define ROW_Y(i) (28 + 8 + (i) * (ROW_H + 6))
#define SIGNOUT_Y (BOT_H - 8 - 34)

static void save(void) {
    store_save_settings(&g_settings);
}

static void update(const Input *in) {
    s_ssid_timer += in->dt;
    if (s_ssid_timer > 5) {
        s_ssid_timer = 0;
        read_ssid();
    }
    if (in->down & KEY_B) {
        app_back();
        return;
    }
    if (ui_tap(in, ROW_X, ROW_Y(0), ROW_W, ROW_H)) {
        g_settings.notif_sound = !g_settings.notif_sound;
        save();
    }
    if (ui_tap(in, ROW_X, ROW_Y(1), ROW_W, ROW_H)) {
        g_settings.discoverable = !g_settings.discoverable;
        save();
    }
    // sync chips: right-aligned, 3 chips
    const int opts[3] = {3, 5, 15};
    float cx = ROW_X + ROW_W - 10;
    for (int i = 2; i >= 0; i--) {
        char l[8];
        snprintf(l, sizeof(l), "%d s", opts[i]);
        float w = ui_text_width(10, FONT_HEAD, l) + 14;
        cx -= w;
        if (ui_tap(in, cx, ROW_Y(2), w, ROW_H)) {
            g_settings.sync_seconds = opts[i];
            save();
        }
        cx -= 4;
    }
    float lx = ROW_X + ROW_W - 10;
    for (int i = 1; i >= 0; i--) {
        float w = ui_text_width(10, FONT_HEAD, i ? "EN" : "FR") + 16;
        lx -= w;
        if (ui_tap(in, lx, ROW_Y(3), w, ROW_H)) {
            g_settings.lang = i ? LANG_EN : LANG_FR;
            i18n_set(g_settings.lang);
            save();
        }
        lx -= 4;
    }
    if (in->down & KEY_SELECT) {
        char buf[128];
        strncpy(buf, g_settings.relay, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        if (kbd_prompt(KBD_TEXT, tr(S_RELAY_HINT), buf, sizeof(buf), 120)) {
            strncpy(g_settings.relay, buf, sizeof(g_settings.relay) - 1);
            save();
            api_apply_settings();
        }
    }
    if (ui_tap(in, ROW_X, SIGNOUT_Y, ROW_W, 34)) {
        if (g_session.logged_in) {
            api_logout();
            app_reset_to(SCR_LOGIN, NULL);
        } else {
            app_go(SCR_LOGIN, NULL);
        }
    }
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_WHITE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0x241F1A, 43));
    ui_text(16, 14, 10, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_ACCOUNT));
    float y = 14 + ui_line_height(10) + 12;
    if (g_session.logged_in) {
        ui_avatar(16, y, 54, false, g_session.uid, g_session.username, 3, C_INK);
        ui_text(82, y + 6, 18, C_INK, ALIGN_LEFT, FONT_HEAD, g_session.username);
        char sub[96];
        snprintf(sub, sizeof(sub), "@%s · %s", g_session.username, tr(S_SIGNED_IN_HERE));
        ui_text(82, y + 32, 10, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
    } else {
        const char *mii_name = mii_own_name();
        ui_avatar(16, y, 54, false, mii_own_key(), mii_name[0] ? mii_name : "?", 3, C_INK);
        ui_text(82, y + 6, 18, C_INK, ALIGN_LEFT, FONT_HEAD, mii_name[0] ? mii_name : tr(S_GUEST));
        ui_text(82, y + 32, 10, C_MUTED, ALIGN_LEFT, FONT_BODY, tr(S_NOT_SIGNED_IN));
    }
    y += 54 + 12;
    float cw = (TOP_W - 32 - 8) / 2.0f;
    ui_rrect_border(16, y, cw, 44, 10, 2, C_WHITE, C_INK);
    ui_text(26, y + 8, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_WIFI));
    char wifi[64];
    if (g_wifi) snprintf(wifi, sizeof(wifi), "%s%s%s", tr(S_CONNECTED), s_ssid[0] ? " · " : "", s_ssid);
    else snprintf(wifi, sizeof(wifi), "%s", tr(S_DISCONNECTED));
    char wl[64];
    ui_ellipsize(wl, sizeof(wl), 12, FONT_HEAD, wifi, cw - 20);
    ui_text(26, y + 21, 12, g_wifi ? C_GREEN : C_RED, ALIGN_LEFT, FONT_HEAD, wl);
    ui_rrect_border(16 + cw + 8, y, cw, 44, 10, 2, C_WHITE, C_INK);
    ui_text(26 + cw + 8, y + 8, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_LOCAL_WIRELESS));
    ui_text(26 + cw + 8, y + 21, 12, g_settings.discoverable ? C_BLUE : C_MUTED, ALIGN_LEFT, FONT_HEAD, g_settings.discoverable ? tr(S_ON_DISCOVERABLE) : tr(S_OFF));
    y += 44 + 12;
    // three lines: the about sentence may wrap once, then the relay line
    ui_rrect_border(16, y, TOP_W - 32, 52, 10, 2, C_CREAM, C_INK);
    char about[160];
    snprintf(about, sizeof(about), "%s\n%s: %s · SELECT", tr(S_ABOUT_LINE), tr(S_RELAY), g_settings.relay);
    ui_text_wrap(26, y + 5, TOP_W - 52, 10, RGB(0x5A5149), ALIGN_LEFT, FONT_BODY, about, 3, 0);
}

static void row(int i, const char *label) {
    ui_rrect_border(ROW_X, ROW_Y(i), ROW_W, ROW_H, 9, 2, C_WHITE, C_INK);
    ui_text_v(ROW_X + 10, ROW_Y(i), ROW_H, 12, C_INK, ALIGN_LEFT, FONT_HEAD, label);
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    ui_rect(0, 0, BOT_W, 28, C_INK);
    ui_icon(ICON_GEAR, 17, 14, 14, C_WHITE);
    ui_text_v(31, 0, 28, 13, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_SETTINGS));

    row(0, tr(S_NOTIF_SOUND));
    ui_toggle(ROW_X + ROW_W - 10 - 36, ROW_Y(0) + (ROW_H - 19) / 2, g_settings.notif_sound);
    row(1, tr(S_STAY_DISCOVERABLE));
    ui_toggle(ROW_X + ROW_W - 10 - 36, ROW_Y(1) + (ROW_H - 19) / 2, g_settings.discoverable);
    row(2, tr(S_SYNC_EVERY));
    const int opts[3] = {3, 5, 15};
    float cx = ROW_X + ROW_W - 10;
    for (int i = 2; i >= 0; i--) {
        char l[8];
        snprintf(l, sizeof(l), "%d s", opts[i]);
        float w = ui_text_width(10, FONT_HEAD, l) + 14;
        cx -= w;
        bool on = g_settings.sync_seconds == opts[i];
        ui_chip(cx, ROW_Y(2) + (ROW_H - ui_line_height(10) - 6) / 2, 10, 7, 3, on ? C_INK : C_SAND, on ? C_WHITE : C_INK, l);
        cx -= 4;
    }
    row(3, tr(S_LANGUAGE));
    float lx = ROW_X + ROW_W - 10;
    for (int i = 1; i >= 0; i--) {
        const char *l = i ? "EN" : "FR";
        float w = ui_text_width(10, FONT_HEAD, l) + 16;
        lx -= w;
        bool on = (g_settings.lang == LANG_EN) == (i == 1);
        ui_chip(lx, ROW_Y(3) + (ROW_H - ui_line_height(10) - 6) / 2, 10, 8, 3, on ? C_INK : C_SAND, on ? C_WHITE : C_INK, l);
        lx -= 4;
    }
    if (g_session.logged_in) {
        ui_card(ROW_X, SIGNOUT_Y, ROW_W, 34, 9, 2, C_RED, C_INK, 0, 3, C_RED_SHADOW);
        ui_text_v(BOT_W / 2, SIGNOUT_Y, 34, 13, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_SIGN_OUT));
    } else {
        ui_card(ROW_X, SIGNOUT_Y, ROW_W, 34, 9, 2, C_GREEN, C_INK, 0, 3, C_GREEN_SHADOW);
        ui_text_v(BOT_W / 2, SIGNOUT_Y, 34, 13, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_SIGN_IN));
    }
}

static void leave(void) { (void)local_available; }

const ScreenVTable SCREEN_SETTINGS = {enter, leave, update, draw_top, draw_bottom};
