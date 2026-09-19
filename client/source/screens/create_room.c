// 06b · Create a Local Wireless room: name, size, lock, go. The top screen previews
// the exact beacon card other consoles will see.
#include "common.h"
#include "../kbd.h"
#include "../local/udsnet.h"
#include "../mii.h"
#include <stdio.h>
#include <string.h>

static char s_name[ROOM_NAME_LEN];
static int s_slots = 8;
static bool s_locked;
static char s_code[8];
static float s_caret;

#define NAME_MAX 12

static void enter(void *arg) {
    (void)arg;
    if (!s_name[0]) strncpy(s_name, g_lang == LANG_FR ? "RÉCRÉ" : "ROOM", sizeof(s_name) - 1);
    s_locked = false;
    s_code[0] = 0;
}

// bottom layout: header 24, padding 8, gap 7
#define NAME_LABEL_Y 32
#define NAME_Y (NAME_LABEL_Y + 13)
#define SLOTS_LABEL_Y (NAME_Y + 30 + 7)
#define SLOTS_Y (SLOTS_LABEL_Y + 14)
#define PASS_LABEL_Y (SLOTS_Y + 28 + 7)
#define PASS_Y (PASS_LABEL_Y + 14)
#define CREATE_Y (PASS_Y + 28 + 7)

static int utf8_len(const char *s) {
    int n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) n++;
    return n;
}

static void create(void) {
    if (utf8_len(s_name) < 1) return;
    if (s_locked && strlen(s_code) != 4) {
        app_toast(tr(S_ENTER_PASSCODE), C_RED);
        return;
    }
    if (!local_available() && !local_init()) {
        app_toast(g_lang == LANG_FR ? "Sans-fil local indisponible" : "Local Wireless unavailable", C_RED);
        return;
    }
    if (!local_host(s_name, s_slots, s_locked ? s_code : NULL)) {
        app_toast(tr(S_ERR_RELAY), C_RED);
        return;
    }
    app_replace(SCR_LOCAL_CHAT, NULL);
}

static void update(const Input *in) {
    s_caret += in->dt;
    if (in->down & KEY_B) {
        app_back();
        return;
    }
    if (ui_tap(in, 8, NAME_Y, BOT_W - 16, 30)) kbd_prompt(KBD_TEXT, tr(S_ROOM_NAME), s_name, sizeof(s_name), NAME_MAX);
    const int opts[3] = {4, 8, 16};
    float sw = (BOT_W - 16 - 12) / 3.0f;
    for (int i = 0; i < 3; i++)
        if (ui_tap(in, 8 + i * (sw + 6), SLOTS_Y, sw, 28)) s_slots = opts[i];
    if (ui_tap(in, 8, PASS_Y + 4, 34, 19)) {
        s_locked = !s_locked;
        if (s_locked && !s_code[0]) kbd_prompt(KBD_NUMPAD, tr(S_ENTER_PASSCODE), s_code, sizeof(s_code), 4);
    }
    if (s_locked && ui_tap(in, 50, PASS_Y, 100, 28)) kbd_prompt(KBD_NUMPAD, tr(S_ENTER_PASSCODE), s_code, sizeof(s_code), 4);
    if (ui_tap(in, 8, CREATE_Y, BOT_W - 16, 40) || (in->down & KEY_A)) create();
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_BLUE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0xFFFFFF, 71));
    ui_text(14, 14, 10, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_HOW_LOOK));
    float cx = 14, cy = 14 + ui_line_height(10) + 9, cw = TOP_W - 28, ch = 122;
    ui_card(cx, cy, cw, ch, 13, 3, C_WHITE, C_INK, 4, 4, C_BLUE_SHADOW);
    const char *me = local_my_name();
    ui_avatar(cx + 10, cy + 10, 34, false, local_my_avatar_key(), me, 2, C_INK);
    ui_text(cx + 53, cy + 8, 17, C_INK, ALIGN_LEFT, FONT_HEAD, s_name);
    char host[64];
    snprintf(host, sizeof(host), "%s%s", tr(S_HOSTED_BY), me);
    ui_text(cx + 53, cy + 31, 9, C_MUTED, ALIGN_LEFT, FONT_BODY, host);
    char cnt[12];
    snprintf(cnt, sizeof(cnt), "1/%d", s_slots);
    float cw2 = ui_text_width(10, FONT_HEAD, cnt) + 16;
    ui_chip(cx + cw - 10 - cw2, cy + 14, 10, 8, 3, C_GREEN, C_WHITE, cnt);
    // slots row
    float sy = cy + 10 + 34 + 8;
    int per_row = s_slots > 8 ? 16 : s_slots;
    float sw = per_row > 8 ? 19 : 26, sh = per_row > 8 ? 18 : 24, gap = 4;
    for (int i = 0; i < s_slots; i++) {
        float sx = cx + 10 + i * (sw + gap);
        if (i == 0) ui_avatar(sx, sy, sw, false, local_my_avatar_key(), me, 2, C_INK);
        else ui_dashed_rrect(sx, sy, sw, sh, 6, 2, C_STONE);
    }
    // chips
    float chy = sy + sh + 8;
    float w = ui_chip(cx + 10, chy, 9, 7, 3, C_SAND, C_INK, s_locked ? tr(S_PASSCODE_ON) : tr(S_OPEN_NO_PASSCODE));
    w += 5 + ui_chip(cx + 10 + w + 5, chy, 9, 7, 3, C_SAND, C_INK, tr(S_TEXT_DRAW_VOICE));
    ui_chip(cx + 10 + w + 5, chy, 9, 7, 3, C_SAND, C_INK, tr(S_RANGE));
    ui_text_wrap(14, cy + ch + 9, cw, 10, C_WHITE, ALIGN_LEFT, FONT_BODY, tr(S_EVERY_CONSOLE_SEES), 2, 0);
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    ui_rect(0, 0, BOT_W, 24, C_INK);
    ui_text_v(10, 0, 24, 12, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_CREATE_A_ROOM));
    // name
    char label[64];
    snprintf(label, sizeof(label), "%s · %s", tr(S_ROOM_NAME), tr(S_TAP_KEYBOARD));
    ui_text(8, NAME_LABEL_Y, 9, C_GREEN, ALIGN_LEFT, FONT_HEAD, label);
    ui_rrect_border(8, NAME_Y, BOT_W - 16, 30, 9, 3, C_WHITE, C_GREEN);
    ui_text_v(17, NAME_Y, 30, 14, C_INK, ALIGN_LEFT, FONT_HEAD, s_name);
    if ((int)(s_caret * 2) & 1) ui_rect(17 + ui_text_width(14, FONT_HEAD, s_name) + 1, NAME_Y + 8, 2, 14, C_GREEN);
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%d/%d", utf8_len(s_name), NAME_MAX);
    ui_text_v(BOT_W - 17, NAME_Y, 30, 9, C_MUTED2, ALIGN_RIGHT, FONT_BODY, cnt);
    // slots
    ui_text(8, SLOTS_LABEL_Y, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_SLOTS));
    const int opts[3] = {4, 8, 16};
    float sw = (BOT_W - 16 - 12) / 3.0f;
    for (int i = 0; i < 3; i++) {
        bool on = s_slots == opts[i];
        char n[4];
        snprintf(n, sizeof(n), "%d", opts[i]);
        ui_rrect_border(8 + i * (sw + 6), SLOTS_Y, sw, 28, 8, 2, on ? C_INK : C_WHITE, C_INK);
        ui_text_v(8 + i * (sw + 6) + sw / 2, SLOTS_Y, 28, 12, on ? C_WHITE : C_INK, ALIGN_CENTER, FONT_HEAD, n);
    }
    // passcode
    ui_text(8, PASS_LABEL_Y, 9, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_PASSCODE));
    ui_toggle(8, PASS_Y + 4, s_locked);
    for (int i = 0; i < 4; i++) {
        float bx = 50 + i * 26;
        ui_rrect_border(bx, PASS_Y + 1, 22, 26, 6, 2, C_WHITE, s_locked ? C_INK : RGBA(0x241F1A, 115));
        if (s_locked && (int)strlen(s_code) > i) {
            char d[2] = {s_code[i], 0};
            ui_text_v(bx + 11, PASS_Y + 1, 26, 12, C_INK, ALIGN_CENTER, FONT_HEAD, d);
        }
    }
    ui_text_wrap(160, PASS_Y + 3, BOT_W - 168, 9, C_MUTED2, ALIGN_LEFT, FONT_BODY, s_locked ? tr(S_PASSCODE_ON_HINT) : tr(S_PASSCODE_OFF_HINT), 2, 11);
    // create
    ui_card(8, CREATE_Y, BOT_W - 16, 40, 11, 3, C_GREEN, C_INK, 0, 5, C_GREEN_SHADOW);
    ui_text_v(BOT_W / 2, CREATE_Y, 40, 15, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_CREATE_ROOM));
    ui_text(BOT_W / 2, CREATE_Y + 48, 9, C_MUTED2, ALIGN_CENTER, FONT_BODY, tr(S_BACK_TO_LOBBY));
}

static void leave(void) {}

const ScreenVTable SCREEN_CREATE_ROOM = {enter, leave, update, draw_top, draw_bottom};
