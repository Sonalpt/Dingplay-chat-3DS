#include "store.h"
#include "api.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DIR "sdmc:/3ds/dingplay-chat"
#define CACHE_DIR DIR "/cache"
#define CONFIG_FILE DIR "/config.ini"
#define TOKEN_FILE DIR "/session.tok"
#define CACHE_MAX 30

void store_init(void) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(DIR, 0777);
    mkdir(CACHE_DIR, 0777);
}

// ---- config.ini ---------------------------------------------------------------------------

void store_load_settings(Settings *s) {
    // Defaults (mockup 09: 5 s sync, sound on, discoverable on)
    strcpy(s->relay, "http://192.168.144.18:8080");  // Rémy's Mac on the LAN; override in config.ini
    s->token[0] = 0;
    s->stay_signed_in = true;
    s->notif_sound = true;
    s->discoverable = true;
    s->sync_seconds = 5;
    s->lang = LANG_EN;

    // Language default from the console's system language.
    u8 sys_lang = CFG_LANGUAGE_EN;
    if (R_SUCCEEDED(cfguInit())) {
        if (R_SUCCEEDED(CFGU_GetSystemLanguage(&sys_lang)) && sys_lang == CFG_LANGUAGE_FR) s->lang = LANG_FR;
        cfguExit();
    }

    FILE *f = fopen(CONFIG_FILE, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        size_t n = strlen(val);
        while (n && (val[n - 1] == '\n' || val[n - 1] == '\r' || val[n - 1] == ' ')) val[--n] = 0;
        if (strcmp(key, "relay") == 0) strncpy(s->relay, val, sizeof(s->relay) - 1);
        else if (strcmp(key, "stay_signed_in") == 0) s->stay_signed_in = atoi(val) != 0;
        else if (strcmp(key, "notif_sound") == 0) s->notif_sound = atoi(val) != 0;
        else if (strcmp(key, "discoverable") == 0) s->discoverable = atoi(val) != 0;
        else if (strcmp(key, "sync_seconds") == 0) {
            int v = atoi(val);
            s->sync_seconds = (v == 3 || v == 5 || v == 15) ? v : 5;
        } else if (strcmp(key, "lang") == 0) s->lang = strcmp(val, "fr") == 0 ? LANG_FR : LANG_EN;
    }
    fclose(f);
}

void store_save_settings(const Settings *s) {
    FILE *f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "relay=%s\nstay_signed_in=%d\nnotif_sound=%d\ndiscoverable=%d\nsync_seconds=%d\nlang=%s\n", s->relay,
            s->stay_signed_in ? 1 : 0, s->notif_sound ? 1 : 0, s->discoverable ? 1 : 0, s->sync_seconds,
            s->lang == LANG_FR ? "fr" : "en");
    fclose(f);
}

// ---- session token ----------------------------------------------------------------------------

void store_save_token(const char *token) {
    FILE *f = fopen(TOKEN_FILE, "w");
    if (!f) return;
    fputs(token, f);
    fclose(f);
}

void store_clear_token(void) { remove(TOKEN_FILE); }

bool store_load_token(char *out, size_t n) {
    FILE *f = fopen(TOKEN_FILE, "r");
    if (!f) return false;
    size_t got = fread(out, 1, n - 1, f);
    fclose(f);
    out[got] = 0;
    while (got && (out[got - 1] == '\n' || out[got - 1] == '\r')) out[--got] = 0;
    return got > 0;
}

// ---- message cache ----------------------------------------------------------------------------------

static void cache_path(char *out, size_t n, const char *room) {
    char safe[ROOM_ID_LEN];
    size_t i = 0;
    for (; room[i] && i < sizeof(safe) - 1; i++) {
        char c = room[i];
        safe[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c : '_';
    }
    safe[i] = 0;
    snprintf(out, n, CACHE_DIR "/%s.json", safe);
}

void store_cache_messages(const char *room, const MessageList *l) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "m");
    int start = l->count > CACHE_MAX ? l->count - CACHE_MAX : 0;
    for (int i = start; i < l->count; i++) {
        const Message *m = &l->items[i];
        if (m->pending) continue;
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", m->id);
        cJSON_AddStringToObject(j, "uid", m->uid);
        cJSON_AddStringToObject(j, "name", m->name);
        cJSON_AddNumberToObject(j, "type", m->type);
        cJSON_AddNumberToObject(j, "ts", (double)m->ts);
        if (m->type == MSG_TEXT) cJSON_AddStringToObject(j, "text", m->text);
        if (m->type == MSG_VOICE) {
            cJSON_AddNumberToObject(j, "dur", m->dur_ms);
            cJSON_AddBoolToObject(j, "media", m->has_media);
        }
        if (m->type == MSG_DRAW && m->draw) {
            // Store the compact stroke list so drawings reopen without a refetch.
            Drawing *d = (Drawing *)calloc(1, sizeof(Drawing));
            if (d) {
                d->w = m->draw->w;
                d->h = m->draw->h;
                d->nstrokes = m->draw->nstrokes;
                d->npoints = m->draw->npoints;
                memcpy(d->strokes, m->draw->strokes, sizeof(Stroke) * d->nstrokes);
                memcpy(d->pts, m->draw->pts, sizeof(uint16_t) * 2 * d->npoints);
                cJSON_AddItemToObject(j, "draw", drawing_to_json(d));
                free(d);
            }
        }
        cJSON_AddItemToArray(arr, j);
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s) return;
    char path[128];
    cache_path(path, sizeof(path), room);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(s, f);
        fclose(f);
    }
    free(s);
}

bool store_load_cached(const char *room, MessageList *l) {
    char path[128];
    cache_path(path, sizeof(path), room);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 256 * 1024) {
        fclose(f);
        return false;
    }
    char *buf = (char *)malloc(n + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    buf[got] = 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "m");
    cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        Message m;
        memset(&m, 0, sizeof(m));
        cJSON *v;
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "id")) && cJSON_IsString(v)) strncpy(m.id, v->valuestring, sizeof(m.id) - 1);
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "uid")) && cJSON_IsString(v)) strncpy(m.uid, v->valuestring, sizeof(m.uid) - 1);
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "name")) && cJSON_IsString(v)) strncpy(m.name, v->valuestring, sizeof(m.name) - 1);
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "text")) && cJSON_IsString(v)) strncpy(m.text, v->valuestring, sizeof(m.text) - 1);
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "type")) && cJSON_IsNumber(v)) m.type = (uint8_t)v->valueint;
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "ts")) && cJSON_IsNumber(v)) m.ts = (int64_t)v->valuedouble;
        if ((v = cJSON_GetObjectItemCaseSensitive(it, "dur")) && cJSON_IsNumber(v)) m.dur_ms = (int16_t)v->valueint;
        m.has_media = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(it, "media"));
        if (m.type == MSG_DRAW) {
            m.draw = drawing_from_json(cJSON_GetObjectItemCaseSensitive(it, "draw"));
            if (!m.draw) m.type = MSG_IMAGE;
        }
        if (m.type == MSG_VOICE) {
            // media older than an hour is gone server-side too
            int64_t now = (int64_t)time(NULL) * 1000;
            if (m.ts && now - m.ts > 3600 * 1000) m.expired = true;
            for (int i = 0; i < 8; i++) m.bars[i] = 4 + ((i * 5 + 2) % 11);
        }
        if (m.id[0]) msglist_push(l, &m);
    }
    cJSON_Delete(root);
    return l->count > 0;
}
