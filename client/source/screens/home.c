// 03 · Home / mode select. Identity and news upstairs; two big mode tiles downstairs.
#include "common.h"
#include "../api.h"
#include "../local/udsnet.h"
#include "../mii.h"
#include <stdio.h>
#include <string.h>

static float s_refresh;

static void enter(void *arg) {
    (void)arg;
    s_refresh = 0;
    if (g_session.logged_in) {
        api_me(NULL, NULL);
        api_news(NULL, NULL);
    }
}

#define TILE_Y 12
#define TILE_H 118
#define BTN_Y 140
#define BTN_H 44

static void update(const Input *in) {
    s_refresh += in->dt;
    if (g_session.logged_in && s_refresh > 30.0f) {
        s_refresh = 0;
        api_me(NULL, NULL);
    }
    if (in->down & KEY_START) {
        app_quit();
        return;
    }
    if (ui_tap(in, 12, TILE_Y, 148, TILE_H)) {
        if (g_session.logged_in) app_go(SCR_FRIENDS, NULL);
        else app_go(SCR_LOGIN, NULL);
    }
    if (ui_tap(in, 160, TILE_Y, 148, TILE_H)) app_go(SCR_LOBBY, NULL);
    if (ui_tap(in, 12, BTN_Y, 148, BTN_H)) {
        if (g_session.logged_in) app_go(SCR_FRIENDS, NULL);
        else app_go(SCR_LOGIN, NULL);
    }
    if (ui_tap(in, 160, BTN_Y, 148, BTN_H)) app_go(SCR_SETTINGS, NULL);
    if (in->down & KEY_B && local_in_room()) app_go(SCR_LOCAL_CHAT, NULL);
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_WHITE);
    ui_dots(0, 22, TOP_W, TOP_H - 22, RGBA(0x241F1A, 43));
    top_brand_bar();
    // identity
    float ax = 16, ay = 22 + (194 - 64) / 2;
    if (g_session.logged_in) {
        ui_avatar(ax, ay, 64, false, g_session.uid, g_session.username, 3, C_INK);
        char hello[80];
        snprintf(hello, sizeof(hello), "%s%s", tr(S_HELLO), g_session.username);
        ui_text(ax + 78, ay - 2, 20, C_INK, ALIGN_LEFT, FONT_HEAD, hello);
        char sub[80];
        snprintf(sub, sizeof(sub), "@%s", g_session.username);
        ui_text(ax + 78, ay + 26, 11, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
        char c1[40], c2[40];
        snprintf(c1, sizeof(c1), "%d %s", g_session.friends_online, tr(S_FRIENDS_ONLINE));
        snprintf(c2, sizeof(c2), "%d %s", g_session.unread, tr(S_UNREAD));
        float w = ui_chip(ax + 78, ay + 47, 9, 8, 3, C_GREEN, C_WHITE, c1);
        ui_chip(ax + 78 + w + 6, ay + 47, 9, 8, 3, C_BLUE, C_WHITE, c2);
    } else {
        const char *mii_name = mii_own_name();
        ui_avatar(ax, ay, 64, false, mii_own_key(), mii_name[0] ? mii_name : "?", 3, C_INK);
        ui_text(ax + 78, ay - 2, 20, C_INK, ALIGN_LEFT, FONT_HEAD, mii_name[0] ? mii_name : tr(S_GUEST));
        ui_text(ax + 78, ay + 26, 11, C_MUTED, ALIGN_LEFT, FONT_BODY, tr(S_GUEST_SUB));
        ui_chip(ax + 78, ay + 47, 9, 8, 3, C_SAND, C_MUTED, tr(S_TIP_LOCAL));
    }
    // news bar
    ui_rect(0, TOP_H - 24, TOP_W, 24, C_ORANGE);
    const char *tag = g_session.news_tag[0] ? g_session.news_tag : tr(S_NEW);
    const char *text = g_session.news_text[0] ? g_session.news_text : (g_lang == LANG_FR ? "Les notes vocales marchent en sans-fil local" : "Voice notes now work over Local Wireless");
    float tw = ui_chip(10, TOP_H - 24 + 5, 8, 6, 2, C_INK, C_GOLD, tag);
    char line[96];
    ui_ellipsize(line, sizeof(line), 10, FONT_HEAD, text, TOP_W - 10 - tw - 8 - 10);
    ui_text_v(10 + tw + 8, TOP_H - 24, 24, 10, C_WHITE, ALIGN_LEFT, FONT_HEAD, line);
}

static void tile(float x, u32 fill, u32 shadow, UiIcon icon, const char *title, const char *sub, u32 sub_col, bool dim) {
    ui_card(x, TILE_Y, 148, TILE_H, 14, 3, dim ? C_SAND : fill, C_INK, 0, 5, dim ? C_INK : shadow);
    ui_icon(icon, x + 10 + 15, TILE_Y + 10 + 15, 30, dim ? C_MUTED : C_WHITE);
    ui_text(x + 10, TILE_Y + TILE_H - 10 - 16 - 3 - ui_line_height(9), 16, dim ? C_MUTED : C_WHITE, ALIGN_LEFT, FONT_HEAD, title);
    ui_text_wrap(x + 10, TILE_Y + TILE_H - 10 - ui_line_height(9), 128, 9, dim ? C_MUTED : sub_col, ALIGN_LEFT, FONT_BODY, sub, 1, 0);
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    tile(12, C_GREEN, C_GREEN_SHADOW, ICON_GLOBE, tr(S_ONLINE), g_session.logged_in ? tr(S_ONLINE_SUB) : tr(S_SIGN_IN), C_GREEN_TINT, false);
    tile(160, C_BLUE, C_BLUE_SHADOW, ICON_PEOPLE, tr(S_LOCAL), local_in_room() ? tr(S_BACK_TO_ROOM) : tr(S_LOCAL_SUB), C_BLUE_TINT, false);
    ui_card(12, BTN_Y, 148, BTN_H, 12, 3, C_WHITE, C_INK, 0, 4, C_INK);
    ui_icon(ICON_USER, 12 + 74 - 30, BTN_Y + 22, 18, C_INK);
    ui_text_v(12 + 74 - 30 + 16, BTN_Y, BTN_H, 13, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_FRIENDS));
    ui_card(160, BTN_Y, 148, BTN_H, 12, 3, C_WHITE, C_INK, 0, 4, C_INK);
    ui_icon(ICON_GEAR, 160 + 74 - 34, BTN_Y + 22, 18, C_INK);
    ui_text_v(160 + 74 - 34 + 16, BTN_Y, BTN_H, 13, C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_SETTINGS));
    ui_text(BOT_W / 2, BTN_Y + BTN_H + 14, 9, C_MUTED2, ALIGN_CENTER, FONT_BODY, tr(S_START_QUIT));
}

static void leave(void) {}

const ScreenVTable SCREEN_HOME = {enter, leave, update, draw_top, draw_bottom};
