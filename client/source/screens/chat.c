// 05 · Online chat (hero). Log on the top screen at full width; the touch screen
// is input and contact switching only. "Syncing · Ns" counts down to the next
// poll and Y forces one.
#include "common.h"
#include "../api.h"
#include "../kbd.h"
#include "../net.h"
#include "../store.h"
#include "../dbg.h"
#include "../voice.h"
#include <stdio.h>
#include <string.h>

static ChatArg s_arg;
static char s_compose[MSG_TEXT_LEN];
static float s_countdown;
static bool s_polling;
static int s_voice_sel;      // D-pad cursor over voice messages
static bool s_voice_mode;    // recording panel replaces the compose area
static bool s_emoji_open;
static float s_caret;
static int s_last_count;
static bool s_first_poll;
static float s_close_arm;   // > 0: "Close room" was tapped once, waiting for the confirming tap

static const char *EMOTICONS[8] = {":)", ":D", ";)", ":(", "<3", "^^", "xD", "!!"};

// Contacts shown in "JUMP TO": global + the 2 most recent friends.
static const Friend *jump_friend(int i) {
    int found = 0;
    for (int k = 0; k < g_friends_count; k++) {
        const Friend *f = &g_friends[k];
        if (s_arg.kind == 1 && strcmp(f->uid, s_arg.peer_uid) == 0) continue;  // already open
        if (found == i) return f;
        found++;
    }
    return NULL;
}

static void poll_done(int added, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_polling = false;
    s_countdown = (float)g_settings.sync_seconds;
    if (added > 0 && !s_first_poll && g_settings.notif_sound) {
        // only blip for other people's messages
        bool other = false;
        for (int i = g_chat.count - 1; i >= 0 && i >= g_chat.count - added; i--)
            if (!g_chat.items[i].mine) other = true;
        if (other) voice_beep();
    }
    s_first_poll = false;
    if (added < 0 && added != -401) {
        // transport / relay error: keep the cached log, retry on the next tick
    }
    if (added == -401) {
        app_toast(tr(S_ERR_RELAY), C_RED);
        app_reset_to(SCR_LOGIN, NULL);
    }
}

static void poll_now(void) {
    if (s_polling) return;
    s_polling = true;
    api_chat_poll(poll_done, NULL);
}

static void send_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    if (status != 200) app_toast(status == 429 ? tr(S_ERR_RELAY) : tr(S_SEND_FAILED), C_RED);
    else s_countdown = 0.3f;  // pull the confirmed copy soon
}

static void media_done(int status, cJSON *json, void *user) {
    (void)json;
    int idx = (int)(intptr_t)user;
    if (status == 200 && idx >= 0 && idx < g_chat.count && g_chat.items[idx].voice) {
        g_chat.items[idx].played = true;
        voice_play(g_chat.items[idx].voice, g_chat.items[idx].voice_len, g_chat.items[idx].id);
    } else if (status == 410) {
        app_toast(tr(S_VOICE_EXPIRED), C_MUTED);
    } else if (status == 415) {
        app_toast(tr(S_VOICE_NO_TRANSCODE), C_MUTED);
    }
}

static void play_selected(void) {
    if (s_voice_sel < 0 || s_voice_sel >= g_chat.count) return;
    Message *m = &g_chat.items[s_voice_sel];
    if (m->type != MSG_VOICE) return;
    if (voice_is_playing(m->id)) {
        voice_stop();
        return;
    }
    if (m->expired) {
        app_toast(tr(S_VOICE_EXPIRED), C_MUTED);
        return;
    }
    if (m->voice) {
        m->played = true;
        voice_play(m->voice, m->voice_len, m->id);
    } else {
        api_media_fetch(m, media_done, (void *)(intptr_t)s_voice_sel);
    }
}

static C2D_SpriteSheet s_bg_sheet;  // Global Room only: the world-chat artwork behind the log
static C2D_Image s_bg;

static void enter(void *arg) {
    if (arg) s_arg = *(ChatArg *)arg;
    dbg_log("chat: enter room=%s kind=%d", s_arg.room, s_arg.kind);
    if (s_arg.kind == 0 && !s_bg_sheet) {
        s_bg_sheet = C2D_SpriteSheetLoad("romfs:/gfx/world-bg.t3x");
        if (s_bg_sheet) s_bg = C2D_SpriteSheetGetImage(s_bg_sheet, 0);
    }
    api_chat_open(s_arg.room);
    s_compose[0] = 0;
    s_countdown = 0;
    s_polling = false;
    s_voice_sel = -1;
    s_voice_mode = false;
    s_emoji_open = false;
    s_first_poll = true;
    s_close_arm = 0;
    s_last_count = g_chat.count;
    voice_panel_reset();
    if (g_friends_count == 0) api_friends(NULL, NULL);
    poll_now();
}

static void send_text(void) {
    dbg_log("chat: send_text '%s'", s_compose);
    if (!s_compose[0]) return;
    api_chat_send_text(s_compose, send_done, NULL);
    s_compose[0] = 0;
}

static void close_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    if (status == 200) {
        app_toast(tr(S_ROOM_CLOSED), C_GREEN);
        app_back();
    } else {
        app_toast(tr(S_ERR_RELAY), C_RED);
    }
}

// Host-only "Close room" chip, top-right of the JUMP TO strip (where the global room has FR / EN).
#define CLOSE_W 78
#define CLOSE_X (BOT_W - 10 - CLOSE_W)
static bool show_close(void) { return s_arg.kind == 2 && s_arg.is_host && strncmp(s_arg.room, "room-", 5) == 0; }

// layout
#define JUMP_H 62
#define FIELD_Y (JUMP_H + 9)
#define FIELD_H 44
#define BTN_Y (FIELD_Y + FIELD_H + 7)
#define BTN_H 36
#define FOOT_Y (BOT_H - 28)

static void update(const Input *in) {
    s_caret += in->dt;
    if (in->down & KEY_B) {
        if (s_voice_mode) {
            voice_panel_reset();
            s_voice_mode = false;
            return;
        }
        if (s_emoji_open) {
            s_emoji_open = false;
            return;
        }
        app_back();
        return;
    }
    // polling
    s_countdown -= in->dt;
    if (s_countdown <= 0 && !s_polling) poll_now();
    if ((in->down & KEY_Y) && !s_polling) {
        s_countdown = 0;
        poll_now();
    }
    // voice cursor
    if (in->down & KEY_UP) s_voice_sel = chatlog_step_voice(&g_chat, s_voice_sel, -1);
    if (in->down & KEY_DOWN) s_voice_sel = chatlog_step_voice(&g_chat, s_voice_sel, +1);
    if (s_voice_sel >= g_chat.count) s_voice_sel = -1;

    if (s_voice_mode) {
        VoicePanelAction a = voice_panel_update(in, 0, JUMP_H, BOT_W, FOOT_Y - JUMP_H);
        if (a == VP_SEND) {
            size_t len;
            const u8 *dpv = voice_rec_data(&len);
            if (dpv) api_chat_send_voice(dpv, len, send_done, NULL);
            voice_rec_discard();
            s_voice_mode = false;
        } else if (a == VP_CANCEL) {
            s_voice_mode = false;
        }
        return;
    }
    if (in->down & KEY_A && !s_emoji_open) {
        if (s_voice_sel >= 0) play_selected();
    }

    // FR / EN world-chat switch (global room only), top-right of the JUMP TO strip
    if (s_arg.kind == 0) {
        for (int i = 0; i < 2; i++) {
            float cx = BOT_W - 10 - 2 * 30 + i * 32;
            if (ui_tap(in, cx, 6, 30, 18)) {
                Lang want = i == 0 ? LANG_FR : LANG_EN;
                if (want != g_settings.chat_lang) {
                    g_settings.chat_lang = want;
                    store_save_settings(&g_settings);
                    ChatArg a = s_arg;
                    strncpy(a.room, api_global_room(), sizeof(a.room) - 1);
                    app_replace(SCR_CHAT, &a);
                }
                return;
            }
        }
    }
    if (show_close()) {
        if (s_close_arm > 0) s_close_arm -= in->dt;
        if (ui_tap(in, CLOSE_X, 6, CLOSE_W, 18)) {
            if (s_close_arm > 0) {
                s_close_arm = 0;
                api_room_close(s_arg.room + 5, close_done, NULL);
            } else {
                s_close_arm = 4.0f;
                app_toast(tr(S_TAP_AGAIN_CLOSE), C_ORANGE);
            }
            return;
        }
    }
    // JUMP TO strip
    float jx = 10, jy = 8 + ui_line_height(9) + 5;
    for (int i = 0; i < 4; i++) {
        if (!ui_tap(in, jx + i * 48, jy, 40, 46)) continue;
        if (i == 0) {
            if (s_arg.kind != 0) {
                ChatArg a;
                memset(&a, 0, sizeof(a));
                strncpy(a.room, api_global_room(), sizeof(a.room) - 1);
                strncpy(a.title, tr(S_GLOBAL_ROOM), sizeof(a.title) - 1);
                a.count = g_session.players_online;
                app_replace(SCR_CHAT, &a);
                return;
            }
        } else if (i < 3) {
            const Friend *f = jump_friend(i - 1);
            if (f) {
                ChatArg a;
                memset(&a, 0, sizeof(a));
                snprintf(a.room, sizeof(a.room), "dm-%.40s", f->uid);
                strncpy(a.title, f->username, sizeof(a.title) - 1);
                strncpy(a.peer_uid, f->uid, sizeof(a.peer_uid) - 1);
                a.kind = 1;
                app_replace(SCR_CHAT, &a);
                return;
            }
        } else {
            int tab = 0;
            app_go(SCR_FRIENDS, &tab);
            return;
        }
    }
    // emoji popover
    if (s_emoji_open) {
        float px = 10, py = FIELD_Y - 8 - 36, pw = BOT_W - 20, ph = 36;
        for (int i = 0; i < 8; i++) {
            if (ui_tap(in, px + 4 + i * (pw - 8) / 8, py + 4, (pw - 8) / 8, ph - 8)) {
                size_t n = strlen(s_compose);
                if (n + 3 < sizeof(s_compose)) {
                    if (n && s_compose[n - 1] != ' ') s_compose[n++] = ' ';
                    strcpy(s_compose + n, EMOTICONS[i]);
                }
                s_emoji_open = false;
            }
        }
        if (in->touch_up && !ui_in(&in->touch, px, py, pw, ph)) s_emoji_open = false;
        return;
    }
    // compose field
    if (ui_tap(in, 10, FIELD_Y, BOT_W - 20, FIELD_H)) {
        kbd_prompt(KBD_TEXT, tr(S_TYPE_MESSAGE), s_compose, sizeof(s_compose), 200);
    }
    float bw = (BOT_W - 20 - 14) / 3.2f;
    if (ui_tap(in, 10, BTN_Y, bw, BTN_H)) s_emoji_open = true;
    if (ui_tap(in, 10 + bw + 7, BTN_Y, bw, BTN_H) || (in->down & KEY_X)) {
        if (voice_can_record()) {
            s_voice_mode = true;
            voice_panel_reset();
        } else {
            app_toast("mic unavailable", C_RED);
        }
    }
    if (ui_tap(in, 10 + 2 * (bw + 7), BTN_Y, bw * 1.2f, BTN_H) || (in->down & KEY_A && s_voice_sel < 0)) send_text();
}

// ---- Top ---------------------------------------------------------------------------------------

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_NAVY);
    if (s_arg.kind == 0 && s_bg_sheet) C2D_DrawImageAt(s_bg, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
    // a little depth: darker band at the bottom like the photo gradient
    ui_rect(0, TOP_H - 60, TOP_W, 60, RGBA(0x05101F, 90));
    // header
    ui_rect(0, 0, TOP_W, 24, C_INK);
    float x = 9;
    if (s_arg.kind == 1) {
        ui_avatar(x, 5, 14, true, s_arg.peer_uid, s_arg.title, 0, 0);
    } else {
        ui_icon(s_arg.kind == 2 ? ICON_PEOPLE : ICON_GLOBE, x + 7, 12, 14, C_WHITE);
    }
    x += 21;
    char title[48];
    if (s_arg.kind == 0) snprintf(title, sizeof(title), "%s · %s", s_arg.title, g_settings.chat_lang == LANG_FR ? "FR" : "EN");
    else snprintf(title, sizeof(title), "%s", s_arg.title);
    ui_text_v(x, 0, 24, 12, C_WHITE, ALIGN_LEFT, FONT_HEAD, title);
    x += ui_text_width(12, FONT_HEAD, title) + 7;
    if (s_arg.kind != 1) {
        char n[16];
        snprintf(n, sizeof(n), "%d", s_arg.kind == 0 ? g_session.players_online : s_arg.count);
        ui_chip(x, 6, 8, 6, 2, s_arg.kind == 2 ? C_BLUE : C_GREEN, C_WHITE, n);
    }
    // syncing pill
    char sync[32];
    int secs = (int)(s_countdown + 0.99f);
    if (secs < 0) secs = 0;
    snprintf(sync, sizeof(sync), "%s · %d s", tr(S_SYNCING), s_polling ? 0 : secs);
    float sw = ui_text_width(9, FONT_HEAD, sync) + 6 + 5 + 14;
    float sx = TOP_W - 9 - sw;
    ui_rrect(sx, 4, sw, 16, 6, RGBA(0xFFFFFF, 31));
    ui_circle(sx + 10, 12, 3, s_polling ? C_GREEN : C_ORANGE);
    ui_text_v(sx + 18, 4, 16, 9, C_GOLD, ALIGN_LEFT, FONT_HEAD, sync);
    // log
    chatlog_draw(&g_chat, 10, 24 + 9, TOP_W - 20, TOP_H - 24 - 18, LOG_NAVY, s_voice_sel);
}

// ---- Bottom ------------------------------------------------------------------------------------

static void jump_item(float x, float y, const char *label, const char *uid, bool active, bool is_global, bool dashed) {
    if (dashed) {
        ui_dashed_circle(x + 20, y + 17, 17, 2, C_STONE);
        ui_text_v(x + 20, y, 34, 15, C_STONE, ALIGN_CENTER, FONT_HEAD, "+");
    } else if (is_global) {
        ui_circle_border(x + 20, y + 17, 17, active ? 3 : 2, C_WHITE, active ? C_ORANGE : C_INK);
        ui_icon(ICON_GLOBE, x + 20, y + 17, 18, C_INK);
    } else {
        ui_avatar(x + 3, y, 34, true, uid, label, active ? 3 : 2, active ? C_ORANGE : C_INK);
    }
    char l[16];
    ui_ellipsize(l, sizeof(l), 8, FONT_HEAD, label, 44);
    ui_text(x + 20, y + 36, 8, dashed ? C_MUTED2 : C_INK, ALIGN_CENTER, FONT_HEAD, l);
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    // JUMP TO
    ui_text(10, 8, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_JUMP_TO));
    if (s_arg.kind == 0) {
        // FR / EN chips
        for (int i = 0; i < 2; i++) {
            bool on = (g_settings.chat_lang == LANG_FR) == (i == 0);
            float cx = BOT_W - 10 - 2 * 30 + i * 32;
            ui_rrect_border(cx, 6, 30, 18, 6, 2, on ? C_INK : C_WHITE, C_INK);
            ui_text_v(cx + 15, 6, 18, 9, on ? C_WHITE : C_INK, ALIGN_CENTER, FONT_HEAD, i == 0 ? "FR" : "EN");
        }
    }
    if (show_close()) {
        bool armed = s_close_arm > 0;
        ui_rrect_border(CLOSE_X, 6, CLOSE_W, 18, 6, 2, armed ? C_RED : C_WHITE, armed ? C_RED : C_INK);
        ui_icon(ICON_CLOSE, CLOSE_X + 11, 15, 8, armed ? C_WHITE : C_RED);
        ui_text_v(CLOSE_X + 20, 6, 18, 9, armed ? C_WHITE : C_INK, ALIGN_LEFT, FONT_HEAD, tr(S_CLOSE_ROOM));
    }
    float jy = 8 + ui_line_height(9) + 5;
    jump_item(10, jy, "Global", NULL, s_arg.kind == 0, true, false);
    for (int i = 0; i < 2; i++) {
        const Friend *f = jump_friend(i);
        if (f) jump_item(10 + 48 * (i + 1), jy, f->username, f->uid, false, false, false);
    }
    jump_item(10 + 48 * 3, jy, tr(S_NEW), NULL, false, false, true);
    ui_rect(0, JUMP_H, BOT_W, 2, C_LINE);

    if (s_voice_mode) {
        voice_panel_draw(0, JUMP_H, BOT_W, FOOT_Y - JUMP_H);
    } else {
        // compose field
        ui_rrect_border(10, FIELD_Y, BOT_W - 20, FIELD_H, 11, 3, C_WHITE, s_compose[0] ? C_GREEN : C_INK);
        char shown[MSG_TEXT_LEN];
        if (s_compose[0]) {
            ui_ellipsize(shown, sizeof(shown), 13, FONT_BODY, s_compose, BOT_W - 20 - 26);
            ui_text_v(20, FIELD_Y, FIELD_H, 13, C_INK, ALIGN_LEFT, FONT_BODY, shown);
            if ((int)(s_caret * 2) & 1) ui_rect(20 + ui_text_width(13, FONT_BODY, shown) + 1, FIELD_Y + 14, 2, 16, C_GREEN);
        } else {
            ui_text_v(20, FIELD_Y, FIELD_H, 13, C_MUTED2, ALIGN_LEFT, FONT_BODY, tr(S_TYPE_MESSAGE));
        }
        float bw = (BOT_W - 20 - 14) / 3.2f;
        ui_card(10, BTN_Y, bw, BTN_H, 10, 2, C_WHITE, C_INK, 0, 0, 0);
        char em[24];
        snprintf(em, sizeof(em), ":) %s", tr(S_EMOJI));
        ui_text_v(10 + bw / 2, BTN_Y, BTN_H, 11, C_INK, ALIGN_CENTER, FONT_HEAD, em);
        ui_card(10 + bw + 7, BTN_Y, bw, BTN_H, 10, 2, C_BLUE, C_INK, 0, 3, C_BLUE_SHADOW);
        ui_icon(ICON_MIC, 10 + bw + 7 + bw / 2 - 20, BTN_Y + BTN_H / 2, 12, C_WHITE);
        ui_text_v(10 + bw + 7 + bw / 2 + 4, BTN_Y, BTN_H, 11, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_VOICE));
        float sx = 10 + 2 * (bw + 7), sw = bw * 1.2f;
        ui_card(sx, BTN_Y, sw, BTN_H, 10, 2, C_GREEN, C_INK, 0, 3, C_GREEN_SHADOW);
        ui_icon(ICON_SEND, sx + sw / 2 - 22, BTN_Y + BTN_H / 2, 15, C_WHITE);
        ui_text_v(sx + sw / 2 + 4, BTN_Y, BTN_H, 12, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_SEND));
        if (s_emoji_open) {
            float px = 10, py = FIELD_Y - 8 - 36, pw = BOT_W - 20, ph = 36;
            ui_card(px, py, pw, ph, 10, 2, C_WHITE, C_INK, 3, 3, C_INK);
            for (int i = 0; i < 8; i++) ui_text_v(px + 4 + (i + 0.5f) * (pw - 8) / 8, py, ph, 12, C_INK, ALIGN_CENTER, FONT_HEAD, EMOTICONS[i]);
        }
    }
    const char *right = s_voice_sel >= 0 ? (g_lang == LANG_FR ? "A · Lire le vocal" : "A · Play voice") : tr(S_REFRESH_NOW);
    footer_bar(FOOT_Y, 28, tr(S_BACK), right, C_SAND, C_INK, C_MUTED);
}

static void leave(void) {
    if (s_bg_sheet) {
        C2D_SpriteSheetFree(s_bg_sheet);
        s_bg_sheet = NULL;
    }
    net_cancel_tag(TAG_CHAT_POLL);
    net_cancel_tag(TAG_MEDIA);
    voice_panel_reset();
    voice_stop();
    if (s_arg.kind == 2 && strncmp(s_arg.room, "room-", 5) == 0) api_room_leave(s_arg.room + 5);
}

const ScreenVTable SCREEN_CHAT = {enter, leave, update, draw_top, draw_bottom};
