// 01 · Boot / loading. Top: branding. Bottom: progress + Wi-Fi / relay checks.
#include "common.h"
#include "../api.h"
#include "../kbd.h"
#include "../store.h"
#include <stdio.h>
#include <string.h>

static C2D_SpriteSheet s_sheet;
static C2D_Image s_wordmark;
static bool s_wordmark_ok;

typedef enum { ST_INIT, ST_WIFI, ST_RELAY, ST_SESSION, ST_DONE, ST_FAIL_WIFI, ST_FAIL_RELAY } Step;
static Step s_step;
static float s_timer;
static float s_progress;
static bool s_waiting;

// The "dingplay" wordmark (white letters, orange shadow) — the same artwork as the mobile app.
void boot_load_wordmark(void) {
    if (s_wordmark_ok) return;
    s_sheet = C2D_SpriteSheetLoad("romfs:/gfx/wordmark.t3x");
    if (s_sheet) {
        s_wordmark = C2D_SpriteSheetGetImage(s_sheet, 0);
        s_wordmark_ok = true;
    }
}

// Draws the wordmark centred on cx, `w` pixels wide; returns its height.
float boot_draw_wordmark(float cx, float y, float w) {
    if (!s_wordmark_ok) {
        ui_text_shadow(cx, y, w * 0.18f, C_WHITE, C_INK, 3, ALIGN_CENTER, FONT_HEAD, "dingplay");
        return ui_line_height(w * 0.18f);
    }
    float sc = w / s_wordmark.subtex->width;
    C2D_DrawImageAt(s_wordmark, cx - w / 2, y, 0.5f, NULL, sc, sc);
    return s_wordmark.subtex->height * sc;
}

static void go_next(void) {
    if (g_session.logged_in) app_reset_to(SCR_HOME, NULL);
    else app_reset_to(SCR_LOGIN, NULL);
}

static void health_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_waiting = false;
    if (status == 200) {
        s_step = ST_SESSION;
        s_progress = 0.62f;
    } else {
        s_step = ST_FAIL_RELAY;
    }
}

static void me_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_waiting = false;
    s_step = ST_DONE;
    s_progress = 1.0f;
    (void)status;
}

static void enter(void *arg) {
    (void)arg;
    boot_load_wordmark();
    s_step = ST_INIT;
    s_timer = 0;
    s_progress = 0.05f;
    s_waiting = false;
}

static void update(const Input *in) {
    s_timer += in->dt;
    switch (s_step) {
        case ST_INIT:
            if (s_timer > 0.4f) {
                s_step = ST_WIFI;
                s_progress = 0.25f;
                s_timer = 0;
            }
            break;
        case ST_WIFI:
            if (s_timer > 0.3f) {
                if (g_wifi) {
                    s_step = ST_RELAY;
                    s_progress = 0.42f;
                } else {
                    s_step = ST_FAIL_WIFI;
                }
                s_timer = 0;
            }
            break;
        case ST_RELAY:
            if (!s_waiting) {
                s_waiting = true;
                api_health(health_done, NULL);
            }
            break;
        case ST_SESSION:
            if (!s_waiting) {
                api_stats(NULL, NULL);
                if (g_settings.token[0]) {
                    s_waiting = true;
                    api_me(me_done, NULL);
                } else {
                    s_step = ST_DONE;
                    s_progress = 1.0f;
                }
            }
            break;
        case ST_DONE:
            if (s_timer > 0.25f) go_next();
            break;
        case ST_FAIL_WIFI:
        case ST_FAIL_RELAY: {
            // Buttons: set relay / continue offline
            float by = 150;
            if (s_step == ST_FAIL_RELAY && (ui_tap(in, 40, by, 240, 30) || (in->down & KEY_X))) {
                char buf[128];
                strncpy(buf, g_settings.relay, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = 0;
                if (kbd_prompt(KBD_TEXT, tr(S_RELAY_HINT), buf, sizeof(buf), 120)) {
                    strncpy(g_settings.relay, buf, sizeof(g_settings.relay) - 1);
                    store_save_settings(&g_settings);
                    api_apply_settings();
                    s_step = ST_RELAY;
                    s_progress = 0.42f;
                }
            }
            if (ui_tap(in, 40, by + 36, 240, 30) || (in->down & (KEY_A | KEY_B | KEY_START))) {
                g_session.logged_in = false;
                app_reset_to(SCR_LOGIN, NULL);
            }
            if (s_step == ST_FAIL_WIFI && g_wifi) s_step = ST_WIFI;
            break;
        }
    }
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_ORANGE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0xFFFFFF, 77));
    float cx = TOP_W / 2;
    float wh = boot_draw_wordmark(cx, 44, 260);
    float cw = ui_text_width(9, FONT_HEAD, tr(S_FOR_3DS)) + 18;
    ui_chip(cx - cw / 2, 44 + wh + 2, 9, 9, 4, C_INK, C_GOLD, tr(S_FOR_3DS));
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_INK);
    bool failed = s_step == ST_FAIL_WIFI || s_step == ST_FAIL_RELAY;
    const char *title = failed ? (s_step == ST_FAIL_WIFI ? tr(S_NO_WIFI) : tr(S_RELAY_UNREACHABLE)) : tr(S_WAKING);
    ui_text(BOT_W / 2, 52, 12, failed ? C_ORANGE : C_WHITE, ALIGN_CENTER, FONT_HEAD, title);
    ui_progress(40, 78, 240, 16, s_progress, failed ? C_ORANGE : C_GREEN, C_WHITE);
    char sub[160];
    if (s_step == ST_WIFI || s_step == ST_INIT) snprintf(sub, sizeof(sub), "%s", tr(S_CHECK_WIFI));
    else if (s_step == ST_RELAY) snprintf(sub, sizeof(sub), "%s · %s", tr(S_CHECK_WIFI), tr(S_CHECK_RELAY));
    else if (failed) snprintf(sub, sizeof(sub), "%s", s_step == ST_FAIL_RELAY ? g_settings.relay : tr(S_TIP_LOCAL));
    else snprintf(sub, sizeof(sub), "%s · %s", tr(S_CHECK_WIFI), tr(S_LOADING_FRIENDS));
    ui_text(BOT_W / 2, 100, 9, C_STONE, ALIGN_CENTER, FONT_BODY, sub);

    if (failed) {
        float by = 150;
        if (s_step == ST_FAIL_RELAY) {
            ui_card(40, by, 240, 30, 10, 2, C_WHITE, C_INK, 0, 0, 0);
            ui_text_v(160, by, 30, 11, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_SET_RELAY));
        }
        ui_dashed_rrect(40, by + 36, 240, 30, 10, 2, C_WHITE);
        ui_text_v(160, by + 36, 30, 11, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_CONTINUE_OFFLINE));
    } else {
        // TIP card
        float tw = 260, th = 40, tx = (BOT_W - tw) / 2, ty = 124;
        ui_rrect_border(tx, ty, tw, th, 8, 1, RGBA(0xFFFFFF, 20), RGB(0x4A423A));
        ui_text(tx + 11, ty + 6, 8, C_ORANGE, ALIGN_LEFT, FONT_HEAD, tr(S_TIP));
        ui_text_wrap(tx + 11, ty + 17, tw - 22, 9, C_CREAM_TEXT, ALIGN_LEFT, FONT_BODY, tr(S_TIP_LOCAL), 2, 0);
    }
    ui_text(BOT_W / 2, BOT_H - 14 - ui_line_height(8) / 2, 8, C_MUTED, ALIGN_CENTER, FONT_BODY, tr(S_VERSION_LINE));
}

static void leave(void) {}

const ScreenVTable SCREEN_BOOT = {enter, leave, update, draw_top, draw_bottom};
