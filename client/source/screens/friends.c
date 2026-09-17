// 04 · Online — friends, requests, and console-only themed rooms. The selected
// row mirrors to the top screen so detail never has to fit in the list.
#include "common.h"
#include "../api.h"
#include "../kbd.h"
#include "../net.h"
#include <stdio.h>
#include <string.h>

enum { TAB_FRIENDS = 0, TAB_REQUESTS = 1, TAB_ROOMS = 2 };
static int s_tab;
static int s_sel;          // index in the current list; -1 = global room row (friends tab) / new room (rooms tab)
static float s_scroll;     // pixels
static float s_drag_start_scroll;
static bool s_dragging;
static float s_refresh;
static bool s_loading;

#define LIST_Y 33
#define LIST_H (BOT_H - 33 - 30)
#define ROW_H 34
#define ROW_GAP 6

static void friends_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_loading = false;
    if (status < 0) app_toast(tr(S_ERR_NETWORK), C_RED);
}

static void rooms_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_loading = false;
    (void)status;
}

static void refresh(void) {
    if (!g_session.logged_in) return;
    s_loading = true;
    if (s_tab == TAB_ROOMS) api_rooms_list(rooms_done, NULL);
    else api_friends(friends_done, NULL);
}

static void enter(void *arg) {
    if (arg) s_tab = *(int *)arg;
    s_sel = -1;
    s_scroll = 0;
    s_refresh = 0;
    refresh();
}

static int list_count(void) {
    if (s_tab == TAB_FRIENDS) return g_friends_count;
    if (s_tab == TAB_REQUESTS) return g_requests_count;
    return g_rooms_count;
}

// rows: index -1 is the pinned first row (Global Room / New room); requests have none
static int row_count(void) { return list_count() + (s_tab == TAB_REQUESTS ? 0 : 1); }

static float content_h(void) { return 8 + row_count() * (ROW_H + ROW_GAP); }

static void clamp_scroll(void) {
    float max = content_h() - LIST_H;
    if (max < 0) max = 0;
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > max) s_scroll = max;
}

static void open_chat_global(void) {
    ChatArg a;
    memset(&a, 0, sizeof(a));
    strcpy(a.room, "global");
    strncpy(a.title, tr(S_GLOBAL_ROOM), sizeof(a.title) - 1);
    a.kind = 0;
    a.count = g_session.players_online;
    app_go(SCR_CHAT, &a);
}

static void open_chat_friend(const Friend *f) {
    ChatArg a;
    memset(&a, 0, sizeof(a));
    snprintf(a.room, sizeof(a.room), "dm-%s", f->uid);
    strncpy(a.title, f->username, sizeof(a.title) - 1);
    strncpy(a.peer_uid, f->uid, sizeof(a.peer_uid) - 1);
    a.kind = 1;
    app_go(SCR_CHAT, &a);
}

static void join_done(int status, cJSON *json, void *user) {
    (void)user;
    if (status == 200 && json) {
        ChatArg a;
        memset(&a, 0, sizeof(a));
        cJSON *r = cJSON_GetObjectItemCaseSensitive(json, "room");
        cJSON *n = cJSON_GetObjectItemCaseSensitive(json, "name");
        if (cJSON_IsString(r)) strncpy(a.room, r->valuestring, sizeof(a.room) - 1);
        if (cJSON_IsString(n)) strncpy(a.title, n->valuestring, sizeof(a.title) - 1);
        a.kind = 2;
        app_go(SCR_CHAT, &a);
    } else {
        app_toast(tr(S_ERR_RELAY), C_RED);
    }
}

static void create_done(int status, cJSON *json, void *user) {
    (void)user;
    if (status == 200 && json) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(json, "id");
        if (cJSON_IsString(id)) api_room_join(id->valuestring, join_done, NULL);
    } else {
        app_toast(tr(S_ERR_RELAY), C_RED);
    }
}

static void new_room(void) {
    char name[32] = "", topic[40] = "";
    if (!kbd_prompt(KBD_TEXT, tr(S_ROOM_NAME), name, sizeof(name), 24)) return;
    kbd_prompt(KBD_TEXT, tr(S_ROOM_TOPIC), topic, sizeof(topic), 32);
    api_room_create(name, topic, create_done, NULL);
}

static void activate(int row) {
    if (s_tab == TAB_FRIENDS) {
        if (row < 0) open_chat_global();
        else if (row < g_friends_count) open_chat_friend(&g_friends[row]);
    } else if (s_tab == TAB_ROOMS) {
        if (row < 0) new_room();
        else if (row < g_rooms_count) api_room_join(g_rooms[row].id, join_done, NULL);
    }
}

static void respond_done(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    if (status == 200) refresh();
    else app_toast(tr(S_ERR_RELAY), C_RED);
}

static void request_done(int status, cJSON *json, void *user) {
    (void)user;
    if (status == 200 && json) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(json, "outcome");
        const char *oc = cJSON_IsString(o) ? o->valuestring : "";
        app_toast(strcmp(oc, "already_friends") == 0 ? tr(S_ALREADY_FRIENDS) : tr(S_REQUEST_SENT), C_GREEN);
        refresh();
    } else if (status == 404) {
        app_toast(tr(S_ERR_UNKNOWN_USER), C_RED);
    } else {
        app_toast(tr(S_ERR_RELAY), C_RED);
    }
}

static void update(const Input *in) {
    s_refresh += in->dt;
    if (s_refresh > 20.0f) {
        s_refresh = 0;
        refresh();
    }
    if (in->down & KEY_B) {
        app_back();
        return;
    }
    // tabs
    for (int t = 0; t < 3; t++) {
        if (ui_tap(in, t * (BOT_W / 3.0f), 0, BOT_W / 3.0f, 30) && s_tab != t) {
            s_tab = t;
            s_sel = -1;
            s_scroll = 0;
            refresh();
        }
    }
    if (in->down & KEY_L) {
        s_tab = (s_tab + 2) % 3;
        s_sel = -1;
        refresh();
    }
    if (in->down & KEY_R) {
        s_tab = (s_tab + 1) % 3;
        s_sel = -1;
        refresh();
    }
    // footer: add friend
    if (ui_tap(in, BOT_W - 110, BOT_H - 30, 110, 30) || (in->down & KEY_X)) {
        char name[40] = "";
        if (kbd_prompt(KBD_USERNAME, tr(S_ADD_FRIEND_HINT), name, sizeof(name), 30)) api_friend_request(name, request_done, NULL);
    }
    // D-pad selection
    int rows = row_count();
    int first = s_tab == TAB_REQUESTS ? 0 : -1;
    int last = list_count() - 1;
    if (in->down & KEY_DOWN && rows) s_sel = s_sel < last ? s_sel + 1 : s_sel;
    if (in->down & KEY_UP && rows) s_sel = s_sel > first ? s_sel - 1 : s_sel;
    if (in->down & (KEY_UP | KEY_DOWN)) {
        // keep the selection visible
        float ry = 8 + (s_sel - first) * (ROW_H + ROW_GAP);
        if (ry < s_scroll) s_scroll = ry;
        if (ry + ROW_H > s_scroll + LIST_H) s_scroll = ry + ROW_H - LIST_H;
        clamp_scroll();
    }
    if (in->down & KEY_A) activate(s_sel);

    // list touch: drag to scroll, tap to select / open
    if (in->touch_down && ui_in(&in->touch, 0, LIST_Y, BOT_W, LIST_H)) {
        s_dragging = true;
        s_drag_start_scroll = s_scroll;
    }
    if (s_dragging && in->touching) {
        s_scroll = s_drag_start_scroll - (in->touch.py - in->touch_start.py);
        clamp_scroll();
    }
    if (in->touch_up) {
        bool was_drag = s_dragging;
        s_dragging = false;
        float moved = in->touch.py - in->touch_start.py;
        if (was_drag && moved < 6 && moved > -6 && ui_in(&in->touch, 0, LIST_Y, BOT_W, LIST_H)) {
            float ly = in->touch.py - LIST_Y + s_scroll - 8;
            int r = (int)(ly / (ROW_H + ROW_GAP));
            float within = ly - r * (ROW_H + ROW_GAP);
            if (r >= 0 && r < rows && within <= ROW_H) {
                int idx = r + first;
                if (s_tab == TAB_REQUESTS) {
                    // ACCEPT / DENY buttons inside the row
                    float x = in->touch.px;
                    if (x >= BOT_W - 8 - 60 - 6 - 52 && x < BOT_W - 8 - 60 - 6) api_friend_respond(g_requests[idx].uid, true, respond_done, NULL);
                    else if (x >= BOT_W - 8 - 60) api_friend_respond(g_requests[idx].uid, false, respond_done, NULL);
                    else s_sel = idx;
                } else if (s_sel == idx) {
                    activate(idx);
                } else {
                    s_sel = idx;
                }
            }
        }
    }
}

// ---- Top ----------------------------------------------------------------------------------------

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_WHITE);
    ui_dots(0, 0, TOP_W, TOP_H, RGBA(0x241F1A, 43));
    ui_text(12, 12, 10, C_MUTED, ALIGN_LEFT, FONT_HEAD, tr(S_SELECTED));
    float cy = 12 + ui_line_height(10) + 10;
    // selected card
    float cw = TOP_W - 24, ch = 84;
    ui_rrect_border(12, cy, cw, ch, 14, 2, C_WHITE, C_INK);
    if (s_tab == TAB_FRIENDS && s_sel >= 0 && s_sel < g_friends_count) {
        const Friend *f = &g_friends[s_sel];
        bool online = f->status != FSTATUS_OFFLINE;
        ui_avatar(24, cy + 14, 56, false, f->uid, f->username, 3, online ? C_GREEN : C_INK);
        ui_text(92, cy + 12, 18, C_INK, ALIGN_LEFT, FONT_HEAD, f->username);
        char sub[96], ago[40];
        if (f->status == FSTATUS_ONLINE) snprintf(ago, sizeof(ago), "%s", tr(S_ONLINE_NOW));
        else if (f->status == FSTATUS_IN_ROOM) snprintf(ago, sizeof(ago), "%s", tr(S_IN_A_ROOM));
        else ui_format_ago(ago, sizeof(ago), f->last_seen);
        snprintf(sub, sizeof(sub), "@%s · %s", f->username, ago);
        ui_text(92, cy + 36, 10, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
        if (f->status == FSTATUS_IN_ROOM && f->lobby_title[0]) ui_chip(92, cy + 54, 9, 8, 3, C_GREEN, C_WHITE, f->lobby_title);
        else if (online) ui_chip(92, cy + 54, 9, 8, 3, C_GREEN, C_WHITE, tr(S_ONLINE_NOW));
        else ui_chip(92, cy + 54, 9, 8, 3, C_SAND, C_MUTED, tr(S_OFFLINE));
        char n[8];
        snprintf(n, sizeof(n), "%d", f->unread);
        ui_text(12 + cw - 30, cy + 20, 20, f->unread ? C_ORANGE : C_STONE, ALIGN_CENTER, FONT_HEAD, n);
        ui_text(12 + cw - 30, cy + 46, 8, C_MUTED, ALIGN_CENTER, FONT_HEAD, tr(S_UNREAD));
    } else if (s_tab == TAB_REQUESTS && s_sel >= 0 && s_sel < g_requests_count) {
        const FriendRequest *r = &g_requests[s_sel];
        ui_avatar(24, cy + 14, 56, false, r->uid, r->username, 3, C_INK);
        ui_text(92, cy + 12, 18, C_INK, ALIGN_LEFT, FONT_HEAD, r->username);
        ui_text(92, cy + 38, 10, C_MUTED, ALIGN_LEFT, FONT_BODY, tr(S_WANTS_FRIEND));
    } else if (s_tab == TAB_ROOMS && s_sel >= 0 && s_sel < g_rooms_count) {
        const ThemedRoom *r = &g_rooms[s_sel];
        ui_rrect_border(24, cy + 14, 56, 56, 16, 3, C_BLUE, C_INK);
        ui_icon(ICON_PEOPLE, 52, cy + 42, 28, C_WHITE);
        ui_text(92, cy + 12, 18, C_INK, ALIGN_LEFT, FONT_HEAD, r->name);
        char sub[96];
        snprintf(sub, sizeof(sub), "%s%s · %s", tr(S_HOSTED_BY), r->host, r->topic[0] ? r->topic : tr(S_CONSOLE_ONLY));
        ui_text(92, cy + 36, 10, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
        char n[24];
        snprintf(n, sizeof(n), "%d %s", r->count, tr(S_HERE));
        ui_chip(92, cy + 54, 9, 8, 3, C_GREEN, C_WHITE, n);
    } else if (s_tab == TAB_ROOMS) {
        ui_text(12 + cw / 2, cy + 20, 13, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_THEMED_ROOMS));
        ui_text_wrap(24, cy + 44, cw - 24, 10, C_MUTED, ALIGN_CENTER, FONT_BODY, tr(S_THEMED_SUB), 2, 0);
    } else {
        ui_text(12 + cw / 2, cy + 20, 13, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_GLOBAL_ROOM));
        ui_text_wrap(24, cy + 44, cw - 24, 10, C_MUTED, ALIGN_CENTER, FONT_BODY, tr(S_GLOBAL_SUB), 2, 0);
    }
    // global room card
    float gy = cy + ch + 10, gh = 46;
    ui_rrect(12, gy, cw, gh, 12, C_ORANGE);
    ui_text(24, gy + 8, 13, C_WHITE, ALIGN_LEFT, FONT_HEAD, tr(S_GLOBAL_ROOM));
    ui_text(24, gy + 26, 9, C_ORANGE_TINT, ALIGN_LEFT, FONT_BODY, tr(S_GLOBAL_SUB));
    char here[24];
    snprintf(here, sizeof(here), "%d %s", g_session.players_online, tr(S_HERE));
    float hw = ui_text_width(11, FONT_HEAD, here) + 18;
    ui_chip(12 + cw - 12 - hw, gy + (gh - ui_line_height(11) - 10) / 2, 11, 9, 5, C_INK, C_WHITE, here);
}

// ---- Bottom ------------------------------------------------------------------------------------

static void tab(int i, const char *label, int badge) {
    float x = i * (BOT_W / 3.0f), w = BOT_W / 3.0f;
    bool on = s_tab == i;
    ui_rect(x, 0, w, 30, on ? C_INK : C_SAND);
    float lw = ui_text_width(12, FONT_HEAD, label);
    float bw = badge ? 18 : 0;
    float tx = x + (w - lw - bw - (badge ? 5 : 0)) / 2;
    ui_text_v(tx, 0, 30, 12, on ? C_WHITE : C_MUTED, ALIGN_LEFT, FONT_HEAD, label);
    if (badge) {
        char n[8];
        snprintf(n, sizeof(n), "%d", badge);
        ui_chip(tx + lw + 5, 8, 9, 5, 1, C_RED, C_WHITE, n);
    }
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    // list (clipped via scissor)
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_NORMAL, BOT_H - (LIST_Y + LIST_H), 0, BOT_H - LIST_Y, BOT_W);
    float y = LIST_Y + 8 - s_scroll;
    int first = s_tab == TAB_REQUESTS ? 0 : -1;
    int rows = row_count();
    for (int r = 0; r < rows; r++, y += ROW_H + ROW_GAP) {
        if (y + ROW_H < LIST_Y || y > LIST_Y + LIST_H) continue;
        int idx = r + first;
        bool sel = idx == s_sel;
        float x = 8, w = BOT_W - 16;
        if (idx < 0) {
            // pinned row: Global Room / + New room
            ui_card(x, y, w, ROW_H, 10, 2, s_tab == TAB_ROOMS ? C_BLUE : C_ORANGE, C_INK, 0, 3, s_tab == TAB_ROOMS ? C_BLUE_SHADOW : C_ORANGE_SHADOW);
            ui_icon(s_tab == TAB_ROOMS ? ICON_PLUS : ICON_GLOBE, x + 18, y + ROW_H / 2, 16, C_WHITE);
            ui_text_v(x + 33, y, ROW_H, 13, C_WHITE, ALIGN_LEFT, FONT_HEAD, s_tab == TAB_ROOMS ? tr(S_NEW_ROOM) : tr(S_GLOBAL_ROOM));
            if (s_tab != TAB_ROOMS) {
                char n[16];
                snprintf(n, sizeof(n), "%d \xE2\x80\xBA", g_session.players_online);
                ui_text_v(x + w - 10, y, ROW_H, 10, C_WHITE, ALIGN_RIGHT, FONT_HEAD, n);
            }
            if (sel) ui_rrect_border(x - 2, y - 2, w + 4, ROW_H + 4, 12, 2, RGBA(0, 0), C_INK);
            continue;
        }
        if (s_tab == TAB_FRIENDS) {
            const Friend *f = &g_friends[idx];
            bool online = f->status != FSTATUS_OFFLINE;
            ui_rrect_border(x, y, w, ROW_H, 10, 2, C_WHITE, C_INK);
            if (sel) ui_rrect_border(x + 2, y + 2, w - 4, ROW_H - 4, 8, 2, RGBA(0, 0), C_GREEN);
            u32 dim = online ? 255 : 153;
            ui_circle(x + 13, y + ROW_H / 2, 4, online ? C_GREEN : C_STONE);
            ui_avatar(x + 21, y + 6, 22, true, f->uid, f->username, 0, 0);
            ui_text_v(x + 51, y, ROW_H, 13, RGBA(0x241F1A, dim), ALIGN_LEFT, FONT_HEAD, f->username);
            if (f->unread) {
                char n[8];
                snprintf(n, sizeof(n), "%d", f->unread);
                float cw = ui_text_width(9, FONT_HEAD, n) + 12;
                ui_chip(x + w - 9 - cw, y + (ROW_H - ui_line_height(9) - 4) / 2, 9, 6, 2, C_RED, C_WHITE, n);
            } else {
                char st[40];
                if (f->status == FSTATUS_IN_ROOM) snprintf(st, sizeof(st), "%s", tr(S_IN_A_ROOM));
                else if (online) st[0] = 0;
                else ui_format_ago(st, sizeof(st), f->last_seen);
                ui_text_v(x + w - 9, y, ROW_H, 9, C_MUTED, ALIGN_RIGHT, FONT_BODY, st);
            }
        } else if (s_tab == TAB_REQUESTS) {
            const FriendRequest *rq = &g_requests[idx];
            ui_rrect_border(x, y, w, ROW_H, 10, 2, C_WHITE, C_INK);
            ui_avatar(x + 8, y + 6, 22, true, rq->uid, rq->username, 0, 0);
            char name[40];
            ui_ellipsize(name, sizeof(name), 12, FONT_HEAD, rq->username, w - 8 - 30 - 125);
            ui_text_v(x + 38, y, ROW_H, 12, C_INK, ALIGN_LEFT, FONT_HEAD, name);
            float ax = BOT_W - 8 - 60 - 6 - 52, dx = BOT_W - 8 - 60;
            ui_rrect_border(ax, y + 5, 52, 24, 7, 2, C_GREEN, C_INK);
            ui_text_v(ax + 26, y + 5, 24, 9, C_WHITE, ALIGN_CENTER, FONT_HEAD, tr(S_ACCEPT));
            ui_rrect_border(dx, y + 5, 52, 24, 7, 2, C_WHITE, C_INK);
            ui_text_v(dx + 26, y + 5, 24, 9, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_DENY));
        } else {
            const ThemedRoom *rm = &g_rooms[idx];
            ui_rrect_border(x, y, w, ROW_H, 10, 2, C_WHITE, C_INK);
            if (sel) ui_rrect_border(x + 2, y + 2, w - 4, ROW_H - 4, 8, 2, RGBA(0, 0), C_BLUE);
            ui_rrect(x + 8, y + 6, 22, 22, 7, C_BLUE);
            ui_icon(ICON_PEOPLE, x + 19, y + 17, 12, C_WHITE);
            ui_text(x + 38, y + 3, 12, C_INK, ALIGN_LEFT, FONT_HEAD, rm->name);
            char sub[64];
            ui_ellipsize(sub, sizeof(sub), 8, FONT_BODY, rm->topic[0] ? rm->topic : rm->host, w - 38 - 50);
            ui_text(x + 38, y + 19, 8, C_MUTED, ALIGN_LEFT, FONT_BODY, sub);
            char n[16];
            snprintf(n, sizeof(n), "%d", rm->count);
            float cw = ui_text_width(10, FONT_HEAD, n) + 14;
            ui_chip(x + w - 9 - cw, y + (ROW_H - ui_line_height(10) - 6) / 2, 10, 7, 3, rm->count ? C_GREEN : C_SAND, rm->count ? C_WHITE : C_INK, n);
        }
    }
    if (rows == (s_tab == TAB_REQUESTS ? 0 : 1)) {
        const char *empty = s_tab == TAB_FRIENDS ? tr(S_NO_FRIENDS) : s_tab == TAB_REQUESTS ? tr(S_NO_REQUESTS) : tr(S_NO_ROOMS);
        ui_text_wrap(20, LIST_Y + 8 + (s_tab == TAB_REQUESTS ? 20 : ROW_H + ROW_GAP + 14), BOT_W - 40, 10, C_MUTED2, ALIGN_CENTER, FONT_BODY, s_loading && !list_count() ? "…" : empty, 2, 0);
    }
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    // tabs (drawn after so the list scrolls under them)
    ui_rect(0, 0, BOT_W, 33, C_CREAM);
    tab(TAB_FRIENDS, tr(S_TAB_FRIENDS), 0);
    tab(TAB_REQUESTS, tr(S_TAB_REQUESTS), g_requests_count);
    tab(TAB_ROOMS, tr(S_TAB_ROOMS), 0);
    ui_rect(0, 30, BOT_W, 3, C_INK);
    // footer
    footer_bar(BOT_H - 30, 30, tr(S_BACK), NULL, C_SAND, C_INK, C_INK);
    float aw = ui_text_width(10, FONT_HEAD, tr(S_ADD_FRIEND));
    ui_icon(ICON_ADD_FRIEND, BOT_W - 10 - aw - 12, BOT_H - 15, 14, C_INK);
    ui_text_v(BOT_W - 10, BOT_H - 30, 30, 10, C_INK, ALIGN_RIGHT, FONT_HEAD, tr(S_ADD_FRIEND));
}

static void leave(void) {
    net_cancel_tag(TAG_FRIENDS);
    net_cancel_tag(TAG_ROOMS);
}

const ScreenVTable SCREEN_FRIENDS = {enter, leave, update, draw_top, draw_bottom};
