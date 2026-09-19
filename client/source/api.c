#include "api.h"
#include "adpcm.h"
#include "avatar.h"
#include "mii.h"
#include "net.h"
#include "store.h"
#include "dbg.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

MessageList g_chat;
Friend g_friends[MAX_FRIENDS];
int g_friends_count;
FriendRequest g_requests[MAX_REQUESTS];
int g_requests_count;
ThemedRoom g_rooms[MAX_THEMED_ROOMS];
int g_rooms_count;
bool g_relay_voice_transcode;

typedef struct {
    ApiDone done;
    void *user;
    char id[UID_LEN];   // media fetch: message id
    char room[ROOM_ID_LEN];
} Ctx;

static Ctx *ctx_new(ApiDone done, void *user) {
    Ctx *c = (Ctx *)calloc(1, sizeof(Ctx));
    if (c) {
        c->done = done;
        c->user = user;
    }
    return c;
}

static void copy_str(char *dst, size_t n, cJSON *j, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (cJSON_IsString(v) && v->valuestring) {
        strncpy(dst, v->valuestring, n - 1);
        dst[n - 1] = 0;
    } else {
        dst[0] = 0;
    }
}
static double num(cJSON *j, const char *key, double def) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static bool boolean(cJSON *j, const char *key) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsTrue(v);
}

// Generic completion: parse JSON (if any) and forward.
static void on_json(NetJob *job) {
    Ctx *c = (Ctx *)job->user;
    cJSON *json = NULL;
    if (job->resp && job->resp_len && job->resp[0] == '{') json = cJSON_ParseWithLength((const char *)job->resp, job->resp_len);
    if (c && c->done) c->done(job->status, json, c->user);
    if (json) cJSON_Delete(json);
    free(c);
}

static void get(const char *path, ApiDone done, void *user, int tag) {
    net_request("GET", path, NULL, 0, NULL, on_json, ctx_new(done, user), tag, NULL);
}

static void post_json(const char *path, cJSON *body, ApiDone done, void *user, int tag) {
    char *s = body ? cJSON_PrintUnformatted(body) : NULL;
    net_request("POST", path, s, s ? strlen(s) : 0, "application/json", on_json, ctx_new(done, user), tag, NULL);
    free(s);
}

static void lang_suffix(char *out, size_t n, const char *prefix) {
    snprintf(out, n, "%s%slang=%s", prefix, strchr(prefix, '?') ? "&" : "?", g_lang == LANG_FR ? "fr" : "en");
}

// ---- Init ---------------------------------------------------------------------------------

static void avatar_done(NetJob *job) {
    if (job->status == 200 && job->resp_len == 48 * 48 * 4) avatar_put(job->key, job->resp, 48);
    else avatar_mark_failed(job->key);
}

// Keys are a Dingplay uid (profile picture) or "mii:<hash>" (a Mii registered in
// mii.c, rendered by the relay). Neither needs a session, only Wi-Fi.
static void avatar_fetch(const char *key) {
    if (!g_wifi) {
        avatar_mark_failed(key);
        return;
    }
    const u8 *mii = mii_lookup(key);
    if (mii) {
        net_request("POST", "/mii/render?s=48", mii, MII_LEN, "application/octet-stream", avatar_done, NULL, TAG_AVATAR, key);
        return;
    }
    char path[96];
    snprintf(path, sizeof(path), "/avatar/%s?s=48", key);
    net_request("GET", path, NULL, 0, NULL, avatar_done, NULL, TAG_AVATAR, key);
}

void api_init(void) {
    memset(&g_chat, 0, sizeof(g_chat));
    avatar_set_fetcher(avatar_fetch);
    api_apply_settings();
}

void api_apply_settings(void) {
    net_set_base(g_settings.relay);
    net_set_token(g_settings.token);
}

// ---- Boot / misc ---------------------------------------------------------------------------------

static void health_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) g_relay_voice_transcode = boolean(json, "voiceTranscode");
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_health(ApiDone done, void *user) { get("/health", health_done, ctx_new(done, user), TAG_BOOT); }

static void stats_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) g_session.players_online = (int)num(json, "online", 0);
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_stats(ApiDone done, void *user) { get("/stats", stats_done, ctx_new(done, user), TAG_MISC); }

static void news_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) {
        copy_str(g_session.news_tag, sizeof(g_session.news_tag), json, "tag");
        copy_str(g_session.news_text, sizeof(g_session.news_text), json, "text");
    }
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_news(ApiDone done, void *user) {
    char path[48];
    lang_suffix(path, sizeof(path), "/news");
    get(path, news_done, ctx_new(done, user), TAG_MISC);
}

// ---- Auth ---------------------------------------------------------------------------------------------

static void apply_user(cJSON *user) {
    if (!user) return;
    copy_str(g_session.uid, sizeof(g_session.uid), user, "uid");
    copy_str(g_session.username, sizeof(g_session.username), user, "username");
}

static void login_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) {
        copy_str(g_settings.token, sizeof(g_settings.token), json, "token");
        apply_user(cJSON_GetObjectItemCaseSensitive(json, "user"));
        g_session.logged_in = g_settings.token[0] != 0;
        net_set_token(g_settings.token);
        if (g_settings.stay_signed_in) store_save_token(g_settings.token);
        else store_clear_token();
    }
    if (c->done) c->done(status, json, c->user);
    free(c);
}

void api_login(const char *login, const char *password, ApiDone done, void *user) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "login", login);
    cJSON_AddStringToObject(b, "password", password);
    post_json("/auth/login", b, login_done, ctx_new(done, user), TAG_AUTH);
    cJSON_Delete(b);
}

void api_logout(void) {
    if (g_settings.token[0]) post_json("/auth/logout", NULL, NULL, NULL, TAG_AUTH);
    g_settings.token[0] = 0;
    g_session.logged_in = false;
    g_session.uid[0] = 0;
    g_session.username[0] = 0;
    g_session.unread = g_session.friends_online = g_session.friends_total = g_session.requests = 0;
    net_set_token("");
    store_clear_token();
    avatar_clear();
    g_friends_count = g_requests_count = 0;
    msglist_clear(&g_chat);
}

static void me_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) {
        apply_user(cJSON_GetObjectItemCaseSensitive(json, "user"));
        g_session.logged_in = true;
        g_session.friends_online = (int)num(json, "friendsOnline", 0);
        g_session.friends_total = (int)num(json, "friendsTotal", 0);
        g_session.requests = (int)num(json, "requests", 0);
        g_session.unread = (int)num(json, "unread", 0);
    } else if (status == 401) {
        // Token expired or revoked: forget it so the boot flow lands on Login.
        g_settings.token[0] = 0;
        g_session.logged_in = false;
        net_set_token("");
        store_clear_token();
    }
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_me(ApiDone done, void *user) { get("/me", me_done, ctx_new(done, user), TAG_ME); }

// ---- Friends -------------------------------------------------------------------------------------------

static void friends_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) {
        g_friends_count = 0;
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(json, "friends");
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (g_friends_count >= MAX_FRIENDS) break;
            Friend *f = &g_friends[g_friends_count++];
            memset(f, 0, sizeof(*f));
            copy_str(f->uid, sizeof(f->uid), it, "uid");
            copy_str(f->username, sizeof(f->username), it, "username");
            copy_str(f->lobby_title, sizeof(f->lobby_title), it, "lobbyTitle");
            char st[16];
            copy_str(st, sizeof(st), it, "status");
            f->status = strcmp(st, "online") == 0 ? FSTATUS_ONLINE : strcmp(st, "in_room") == 0 ? FSTATUS_IN_ROOM : FSTATUS_OFFLINE;
            f->last_seen = (int64_t)num(it, "lastSeen", 0);
            f->unread = (int)num(it, "unread", 0);
        }
        g_requests_count = 0;
        arr = cJSON_GetObjectItemCaseSensitive(json, "requests");
        cJSON_ArrayForEach(it, arr) {
            if (g_requests_count >= MAX_REQUESTS) break;
            FriendRequest *r = &g_requests[g_requests_count++];
            copy_str(r->uid, sizeof(r->uid), it, "uid");
            copy_str(r->username, sizeof(r->username), it, "username");
        }
        int online = 0, unread = 0;
        for (int i = 0; i < g_friends_count; i++) {
            if (g_friends[i].status != FSTATUS_OFFLINE) online++;
            unread += g_friends[i].unread;
        }
        g_session.friends_online = online;
        g_session.friends_total = g_friends_count;
        g_session.requests = g_requests_count;
        g_session.unread = unread;
    }
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_friends(ApiDone done, void *user) { get("/friends", friends_done, ctx_new(done, user), TAG_FRIENDS); }

void api_friend_request(const char *username, ApiDone done, void *user) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "username", username);
    post_json("/friends/request", b, done, user, TAG_FRIENDS);
    cJSON_Delete(b);
}

void api_friend_respond(const char *uid, bool accept, ApiDone done, void *user) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "uid", uid);
    cJSON_AddBoolToObject(b, "accept", accept);
    post_json("/friends/respond", b, done, user, TAG_FRIENDS);
    cJSON_Delete(b);
}

// ---- Messages -----------------------------------------------------------------------------------------

void message_free(Message *m) {
    if (m->draw) {
        free(m->draw->strokes);
        free(m->draw->pts);
        free(m->draw);
        m->draw = NULL;
    }
    free(m->voice);
    m->voice = NULL;
    m->voice_len = 0;
}

void msglist_clear(MessageList *l) {
    for (int i = 0; i < l->count; i++) message_free(&l->items[i]);
    l->count = 0;
    l->newest_ts = 0;
    l->loaded = false;
}

static Message *msglist_find(MessageList *l, const char *id) {
    if (!id || !*id) return NULL;
    for (int i = l->count - 1; i >= 0; i--)
        if (strcmp(l->items[i].id, id) == 0) return &l->items[i];
    return NULL;
}

Message *msglist_push(MessageList *l, const Message *m) {
    Message *existing = msglist_find(l, m->id);
    if (existing) {
        // A confirmed copy of a pending send: keep the buffers, take the server timestamp.
        existing->pending = false;
        if (m->ts) existing->ts = m->ts;
        if (m->ts > l->newest_ts) l->newest_ts = m->ts;
        return existing;
    }
    if (l->count >= MSG_HISTORY) {
        message_free(&l->items[0]);
        memmove(&l->items[0], &l->items[1], sizeof(Message) * (MSG_HISTORY - 1));
        l->count = MSG_HISTORY - 1;
    }
    // keep chronological order: insert before any newer message (pending ones stay last)
    int pos = l->count;
    while (pos > 0 && !l->items[pos - 1].pending && l->items[pos - 1].ts > m->ts && m->ts) pos--;
    if (pos < l->count) memmove(&l->items[pos + 1], &l->items[pos], sizeof(Message) * (l->count - pos));
    l->items[pos] = *m;
    l->count++;
    if (m->ts > l->newest_ts) l->newest_ts = m->ts;
    return &l->items[pos];
}

DrawingRef *drawing_compact(const Drawing *d) {
    if (!d || d->nstrokes == 0) return NULL;
    DrawingRef *r = (DrawingRef *)calloc(1, sizeof(DrawingRef));
    if (!r) return NULL;
    r->w = d->w;
    r->h = d->h;
    r->nstrokes = d->nstrokes;
    r->npoints = d->npoints;
    r->strokes = (Stroke *)malloc(sizeof(Stroke) * d->nstrokes);
    r->pts = (uint16_t *)malloc(sizeof(uint16_t) * 2 * d->npoints);
    if (!r->strokes || !r->pts) {
        free(r->strokes);
        free(r->pts);
        free(r);
        return NULL;
    }
    memcpy(r->strokes, d->strokes, sizeof(Stroke) * d->nstrokes);
    memcpy(r->pts, d->pts, sizeof(uint16_t) * 2 * d->npoints);
    return r;
}

// {w,h,s:[[color,pen,x,y,x,y,...],...]}
DrawingRef *drawing_from_json(cJSON *j) {
    if (!cJSON_IsObject(j)) return NULL;
    cJSON *s = cJSON_GetObjectItemCaseSensitive(j, "s");
    if (!cJSON_IsArray(s)) return NULL;
    Drawing *d = (Drawing *)calloc(1, sizeof(Drawing));
    if (!d) return NULL;
    d->w = (uint16_t)num(j, "w", 230);
    d->h = (uint16_t)num(j, "h", 160);
    cJSON *st;
    cJSON_ArrayForEach(st, s) {
        if (!cJSON_IsArray(st) || d->nstrokes >= DRAW_MAX_STROKES) break;
        int n = cJSON_GetArraySize(st);
        if (n < 4) continue;
        Stroke *k = &d->strokes[d->nstrokes];
        k->color = (uint8_t)cJSON_GetArrayItem(st, 0)->valueint;
        k->pen = (uint8_t)cJSON_GetArrayItem(st, 1)->valueint;
        if (k->color >= DRAW_COLOR_COUNT) k->color = 0;
        if (k->pen > 2) k->pen = 1;
        k->start = d->npoints;
        k->count = 0;
        for (int i = 2; i + 1 < n && d->npoints < DRAW_MAX_POINTS; i += 2) {
            d->pts[d->npoints * 2] = (uint16_t)cJSON_GetArrayItem(st, i)->valuedouble;
            d->pts[d->npoints * 2 + 1] = (uint16_t)cJSON_GetArrayItem(st, i + 1)->valuedouble;
            d->npoints++;
            k->count++;
        }
        if (k->count) d->nstrokes++;
    }
    DrawingRef *r = drawing_compact(d);
    free(d);
    return r;
}

cJSON *drawing_to_json(const Drawing *d) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "w", d->w);
    cJSON_AddNumberToObject(j, "h", d->h);
    cJSON *s = cJSON_AddArrayToObject(j, "s");
    for (int i = 0; i < d->nstrokes; i++) {
        const Stroke *k = &d->strokes[i];
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(k->color));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(k->pen));
        for (int p = 0; p < k->count; p++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(d->pts[(k->start + p) * 2]));
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(d->pts[(k->start + p) * 2 + 1]));
        }
        cJSON_AddItemToArray(s, arr);
    }
    return j;
}

// Eight RMS buckets of the decoded note, scaled 3..15 for the bubble bars.
void waveform_from_dpv(const u8 *dpv, size_t len, u8 bars[8]) {
    for (int i = 0; i < 8; i++) bars[i] = 4 + ((i * 7 + 3) % 9);  // pseudo pattern fallback
    if (!dpv || len < 16 || memcmp(dpv, "DPV1", 4) != 0) return;
    u32 count = dpv[8] | (dpv[9] << 8) | (dpv[10] << 16) | ((u32)dpv[11] << 24);
    if (count == 0 || count > (len - 16) * 2) return;
    AdpcmState st;
    adpcm_decoder_init(&st, dpv + 12);
    const u8 *nib = dpv + 16;
    u32 per = count / 8;
    if (per == 0) return;
    double peak = 1;
    double acc[8] = {0};
    for (u32 i = 0; i < count; i++) {
        u8 byte = nib[i >> 1];
        s16 s = adpcm_decode_nibble(&st, (i & 1) ? (byte >> 4) : (byte & 0x0F));
        u32 b = i / per;
        if (b > 7) b = 7;
        acc[b] += (double)s * s;
    }
    for (int i = 0; i < 8; i++) {
        acc[i] = sqrt(acc[i] / per);
        if (acc[i] > peak) peak = acc[i];
    }
    for (int i = 0; i < 8; i++) bars[i] = (u8)(3 + 12 * (acc[i] / peak));
}

static bool parse_message(cJSON *it, Message *m) {
    memset(m, 0, sizeof(*m));
    copy_str(m->id, sizeof(m->id), it, "id");
    copy_str(m->uid, sizeof(m->uid), it, "uid");
    copy_str(m->name, sizeof(m->name), it, "name");
    char type[12];
    copy_str(type, sizeof(type), it, "type");
    m->type = strcmp(type, "voice") == 0 ? MSG_VOICE : strcmp(type, "draw") == 0 ? MSG_DRAW : strcmp(type, "image") == 0 ? MSG_IMAGE : MSG_TEXT;
    m->ts = (int64_t)num(it, "ts", 0);
    copy_str(m->text, sizeof(m->text), it, "text");
    m->has_media = boolean(it, "media");
    m->expired = boolean(it, "exp");
    m->dur_ms = (int16_t)num(it, "dur", 0);
    m->mine = g_session.uid[0] && strcmp(m->uid, g_session.uid) == 0;
    if (m->type == MSG_DRAW) {
        m->draw = drawing_from_json(cJSON_GetObjectItemCaseSensitive(it, "draw"));
        if (!m->draw) m->type = MSG_IMAGE;
    }
    if (m->type == MSG_VOICE) {
        // deterministic preview until the audio is fetched
        unsigned h = 2166136261u;
        for (const char *p = m->id; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
        for (int i = 0; i < 8; i++) m->bars[i] = 4 + ((h >> (i * 4)) & 0xF) * 11 / 15;
    }
    return m->id[0] != 0;
}

// ---- Chat ------------------------------------------------------------------------------------------------

const char *api_chat_room(void) { return g_chat.room; }
const char *api_global_room(void) { return g_settings.chat_lang == LANG_FR ? "global-fr" : "global-en"; }

void api_chat_open(const char *room) {
    net_cancel_tag(TAG_CHAT_POLL);
    net_cancel_tag(TAG_MEDIA);
    if (strcmp(g_chat.room, room) == 0 && g_chat.loaded) return;
    msglist_clear(&g_chat);
    strncpy(g_chat.room, room, sizeof(g_chat.room) - 1);
    store_load_cached(room, &g_chat);
    for (int i = 0; i < g_chat.count; i++) g_chat.items[i].mine = g_session.uid[0] && strcmp(g_chat.items[i].uid, g_session.uid) == 0;
    g_chat.loaded = true;
}

static void poll_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json && strcmp(c->room, g_chat.room) == 0) {
        int added = 0;
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(json, "messages");
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            Message m;
            if (!parse_message(it, &m)) continue;
            if (msglist_find(&g_chat, m.id)) {
                message_free(&m);
                msglist_push(&g_chat, &m);  // confirms a pending send
                continue;
            }
            msglist_push(&g_chat, &m);
            added++;
        }
        if (added) store_cache_messages(g_chat.room, &g_chat);
        if (c->done) c->done(added, json, c->user);
    } else if (c->done) {
        c->done(status < 0 ? status : -status, json, c->user);
    }
    free(c);
}

void api_chat_poll(ApiDone done, void *user) {
    dbg_log("api: poll room='%s' since=%lld", g_chat.room, (long long)g_chat.newest_ts);
    if (!g_chat.room[0]) return;
    char path[128];
    char base[96];
    snprintf(base, sizeof(base), "/chat/%s?since=%lld", g_chat.room, (long long)g_chat.newest_ts);
    lang_suffix(path, sizeof(path), base);
    Ctx *c = ctx_new(done, user);
    strncpy(c->room, g_chat.room, sizeof(c->room) - 1);
    get(path, poll_done, c, TAG_CHAT_POLL);
}

static void add_pending(const Message *m) {
    Message copy = *m;
    copy.pending = true;
    copy.mine = true;
    strncpy(copy.uid, g_session.uid, sizeof(copy.uid) - 1);
    strncpy(copy.name, g_session.username, sizeof(copy.name) - 1);
    copy.ts = 0;
    msglist_push(&g_chat, &copy);
}

typedef struct {
    ApiDone done;
    void *user;
    char temp_id[UID_LEN];
} SendCtx;

static void send_done(int status, cJSON *json, void *user) {
    SendCtx *s = (SendCtx *)user;
    Message *m = msglist_find(&g_chat, s->temp_id);
    if (status == 200 && json) {
        if (m) {
            copy_str(m->id, sizeof(m->id), json, "id");
            m->ts = (int64_t)time(NULL) * 1000;
            m->pending = false;
        }
    } else if (m) {
        // drop the optimistic bubble; the screen shows a toast
        int idx = (int)(m - g_chat.items);
        message_free(m);
        memmove(&g_chat.items[idx], &g_chat.items[idx + 1], sizeof(Message) * (g_chat.count - idx - 1));
        g_chat.count--;
    }
    if (s->done) s->done(status, json, s->user);
    free(s);
}

static SendCtx *send_ctx(ApiDone done, void *user, Message *pending) {
    SendCtx *s = (SendCtx *)calloc(1, sizeof(SendCtx));
    static unsigned counter;
    snprintf(pending->id, sizeof(pending->id), "tmp-%u-%u", (unsigned)osGetTime(), ++counter);
    if (s) {
        s->done = done;
        s->user = user;
        strncpy(s->temp_id, pending->id, sizeof(s->temp_id) - 1);
    }
    return s;
}

void api_chat_send_text(const char *text, ApiDone done, void *user) {
    Message m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_TEXT;
    strncpy(m.text, text, sizeof(m.text) - 1);
    SendCtx *s = send_ctx(done, user, &m);
    add_pending(&m);
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "type", "text");
    cJSON_AddStringToObject(b, "text", text);
    char path[96];
    snprintf(path, sizeof(path), "/chat/%s?lang=%s", g_chat.room, g_lang == LANG_FR ? "fr" : "en");
    post_json(path, b, send_done, s, TAG_CHAT_SEND);
    cJSON_Delete(b);
}

void api_chat_send_draw(const Drawing *d, ApiDone done, void *user) {
    Message m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_DRAW;
    m.draw = drawing_compact(d);
    SendCtx *s = send_ctx(done, user, &m);
    add_pending(&m);
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "type", "draw");
    cJSON_AddItemToObject(b, "draw", drawing_to_json(d));
    char path[96];
    snprintf(path, sizeof(path), "/chat/%s?lang=%s", g_chat.room, g_lang == LANG_FR ? "fr" : "en");
    post_json(path, b, send_done, s, TAG_CHAT_SEND);
    cJSON_Delete(b);
}

void api_chat_send_voice(const u8 *dpv, size_t len, ApiDone done, void *user) {
    Message m;
    memset(&m, 0, sizeof(m));
    m.type = MSG_VOICE;
    m.voice = (u8 *)malloc(len);
    if (m.voice) {
        memcpy(m.voice, dpv, len);
        m.voice_len = len;
    }
    m.has_media = true;
    m.played = true;
    u32 rate = dpv[4] | (dpv[5] << 8) | (dpv[6] << 16) | ((u32)dpv[7] << 24);
    u32 count = dpv[8] | (dpv[9] << 8) | (dpv[10] << 16) | ((u32)dpv[11] << 24);
    m.dur_ms = rate ? (int16_t)((u64)count * 1000 / rate) : 0;
    waveform_from_dpv(dpv, len, m.bars);
    SendCtx *s = send_ctx(done, user, &m);
    add_pending(&m);
    char path[96];
    snprintf(path, sizeof(path), "/chat/%s/voice?lang=%s", g_chat.room, g_lang == LANG_FR ? "fr" : "en");
    net_request("POST", path, dpv, len, "application/octet-stream", on_json, ctx_new(send_done, s), TAG_CHAT_SEND, NULL);
}

static void media_done(NetJob *job) {
    Ctx *c = (Ctx *)job->user;
    Message *m = msglist_find(&g_chat, c->id);
    if (m && job->status == 200 && job->resp_len > 16 && memcmp(job->resp, "DPV1", 4) == 0) {
        free(m->voice);
        m->voice = (u8 *)malloc(job->resp_len);
        if (m->voice) {
            memcpy(m->voice, job->resp, job->resp_len);
            m->voice_len = job->resp_len;
            waveform_from_dpv(m->voice, m->voice_len, m->bars);
        }
    } else if (m && job->status == 410) {
        m->expired = true;
    }
    if (c->done) c->done(job->status, NULL, c->user);
    free(c);
}

void api_media_fetch(Message *msg, ApiDone done, void *user) {
    Ctx *c = ctx_new(done, user);
    strncpy(c->id, msg->id, sizeof(c->id) - 1);
    char path[160];
    snprintf(path, sizeof(path), "/media/%s/%s?lang=%s", g_chat.room, msg->id, g_lang == LANG_FR ? "fr" : "en");
    net_request("GET", path, NULL, 0, NULL, media_done, c, TAG_MEDIA, NULL);
}

// ---- Themed rooms ----------------------------------------------------------------------------------------

static void rooms_done(int status, cJSON *json, void *user) {
    Ctx *c = (Ctx *)user;
    if (status == 200 && json) {
        g_rooms_count = 0;
        cJSON *arr = cJSON_GetObjectItemCaseSensitive(json, "rooms");
        cJSON *it;
        cJSON_ArrayForEach(it, arr) {
            if (g_rooms_count >= MAX_THEMED_ROOMS) break;
            ThemedRoom *r = &g_rooms[g_rooms_count++];
            copy_str(r->id, sizeof(r->id), it, "id");
            copy_str(r->name, sizeof(r->name), it, "name");
            copy_str(r->topic, sizeof(r->topic), it, "topic");
            copy_str(r->host, sizeof(r->host), it, "host");
            copy_str(r->host_uid, sizeof(r->host_uid), it, "hostUid");
            r->count = (int)num(it, "count", 0);
        }
    }
    if (c->done) c->done(status, json, c->user);
    free(c);
}
void api_rooms_list(ApiDone done, void *user) { get("/rooms", rooms_done, ctx_new(done, user), TAG_ROOMS); }

void api_room_create(const char *name, const char *topic, ApiDone done, void *user) {
    cJSON *b = cJSON_CreateObject();
    cJSON_AddStringToObject(b, "name", name);
    cJSON_AddStringToObject(b, "topic", topic ? topic : "");
    post_json("/rooms", b, done, user, TAG_ROOMS);
    cJSON_Delete(b);
}

void api_room_join(const char *id, ApiDone done, void *user) {
    char path[64];
    snprintf(path, sizeof(path), "/rooms/%s/join", id);
    post_json(path, NULL, done, user, TAG_ROOMS);
}

void api_room_close(const char *id, ApiDone done, void *user) {
    char path[64];
    snprintf(path, sizeof(path), "/rooms/%s/close", id);
    post_json(path, NULL, done, user, TAG_MISC);
}

void api_room_leave(const char *id) {
    char path[64];
    snprintf(path, sizeof(path), "/rooms/%s/leave", id);
    post_json(path, NULL, NULL, NULL, TAG_ROOMS);
}
