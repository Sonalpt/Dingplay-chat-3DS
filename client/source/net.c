#include "net.h"
#include "dbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HTTPC_POST_BUF 0x20000  // 128 KB shared memory: enough for a 10 s voice note
#define WORKER_STACK 0x8000
#define CHUNK 0x2000

static char s_base[160];
static char s_auth[240];
static bool s_httpc_ok;
static Thread s_thread;
static volatile bool s_quit;
static LightLock s_lock;
static LightEvent s_wake;
static NetJob *s_queue, *s_queue_tail;  // waiting
static NetJob *s_done;                  // finished, awaiting pump
static NetJob *s_current;               // in flight (worker owns)
static int s_pending;

static void free_job(NetJob *j) {
    free(j->body);
    free(j->resp);
    free(j);
}

// ---- Worker -------------------------------------------------------------------------------

static void perform(NetJob *j) {
    httpcContext ctx;
    HTTPC_RequestMethod m = strcmp(j->method, "POST") == 0 ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    Result rc = httpcOpenContext(&ctx, m, j->url, 1);
    if (R_FAILED(rc)) {
        j->status = -1;
        return;
    }
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "DingplayChat3DS/" APP_VERSION);
    httpcAddRequestHeaderField(&ctx, "Accept", "application/json, application/octet-stream");
    if (s_auth[0]) httpcAddRequestHeaderField(&ctx, "Authorization", s_auth);
    if (j->body && j->body_len) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", j->content_type[0] ? j->content_type : "application/json");
        rc = httpcAddPostDataRaw(&ctx, (const u32 *)j->body, (u32)j->body_len);
        if (R_FAILED(rc)) {
            httpcCloseContext(&ctx);
            j->status = -2;
            return;
        }
    } else if (m == HTTPC_METHOD_POST) {
        httpcAddRequestHeaderField(&ctx, "Content-Type", "application/json");
        httpcAddPostDataRaw(&ctx, (const u32 *)"{}", 2);
    }
    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        httpcCloseContext(&ctx);
        j->status = -3;
        return;
    }
    u32 status = 0;
    rc = httpcGetResponseStatusCode(&ctx, &status);
    if (R_FAILED(rc)) {
        httpcCloseContext(&ctx);
        j->status = -4;
        return;
    }
    j->status = (int)status;
    j->hdr_xsize[0] = 0;
    httpcGetResponseHeader(&ctx, "x-size", j->hdr_xsize, sizeof(j->hdr_xsize));

    size_t cap = CHUNK, len = 0;
    u8 *buf = (u8 *)malloc(cap + 1);
    if (!buf) {
        httpcCloseContext(&ctx);
        j->status = -5;
        return;
    }
    for (;;) {
        u32 got = 0;
        rc = httpcDownloadData(&ctx, buf + len, (u32)(cap - len), &got);
        len += got;
        if (rc == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
            if (len >= cap) {
                size_t ncap = cap * 2;
                if (ncap > 512 * 1024) {  // never let a runaway body eat the heap
                    break;
                }
                u8 *nb = (u8 *)realloc(buf, ncap + 1);
                if (!nb) break;
                buf = nb;
                cap = ncap;
            }
            continue;
        }
        break;
    }
    buf[len] = 0;
    j->resp = buf;
    j->resp_len = len;
    httpcCloseContext(&ctx);
}

static void worker(void *arg) {
    (void)arg;
    while (!s_quit) {
        LightLock_Lock(&s_lock);
        NetJob *j = s_queue;
        if (j) {
            s_queue = j->next;
            if (!s_queue) s_queue_tail = NULL;
            j->next = NULL;
            s_current = j;
        }
        LightLock_Unlock(&s_lock);
        if (!j) {
            LightEvent_Wait(&s_wake);
            continue;
        }
        if (!j->cancelled) perform(j);
        LightLock_Lock(&s_lock);
        s_current = NULL;
        j->next = s_done;
        s_done = j;
        LightLock_Unlock(&s_lock);
    }
}

// ---- Public -------------------------------------------------------------------------------

bool net_init(void) {
    Result rc = httpcInit(HTTPC_POST_BUF);
    s_httpc_ok = R_SUCCEEDED(rc);
    LightLock_Init(&s_lock);
    LightEvent_Init(&s_wake, RESET_ONESHOT);
    s_quit = false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    // Slightly lower priority than the main thread; any core (Old 3DS has only core 0 for apps).
    s_thread = threadCreate(worker, NULL, WORKER_STACK, prio + 1, -2, false);
    return s_httpc_ok && s_thread != NULL;
}

void net_exit(void) {
    s_quit = true;
    LightEvent_Signal(&s_wake);
    if (s_thread) {
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    NetJob *j = s_queue;
    while (j) {
        NetJob *n = j->next;
        free_job(j);
        j = n;
    }
    j = s_done;
    while (j) {
        NetJob *n = j->next;
        free_job(j);
        j = n;
    }
    s_queue = s_queue_tail = s_done = NULL;
    if (s_httpc_ok) httpcExit();
}

void net_set_base(const char *base_url) {
    strncpy(s_base, base_url ? base_url : "", sizeof(s_base) - 1);
    size_t n = strlen(s_base);
    while (n > 0 && s_base[n - 1] == '/') s_base[--n] = 0;
}

void net_set_token(const char *token) {
    if (token && *token) snprintf(s_auth, sizeof(s_auth), "Bearer %s", token);
    else s_auth[0] = 0;
}

NetJob *net_request(const char *method, const char *path, const void *body, size_t len, const char *content_type,
                    NetCallback cb, void *user, int tag, const char *key) {
    if (!s_httpc_ok) return NULL;
    NetJob *j = (NetJob *)calloc(1, sizeof(NetJob));
    if (!j) return NULL;
    strncpy(j->method, method, sizeof(j->method) - 1);
    if (path[0] == 'h') snprintf(j->url, sizeof(j->url), "%s", path);
    else snprintf(j->url, sizeof(j->url), "%s%s", s_base, path);
    if (content_type) strncpy(j->content_type, content_type, sizeof(j->content_type) - 1);
    if (body && len) {
        j->body = (u8 *)malloc(len);
        if (!j->body) {
            free(j);
            return NULL;
        }
        memcpy(j->body, body, len);
        j->body_len = len;
    }
    j->cb = cb;
    j->user = user;
    j->tag = tag;
    if (key) strncpy(j->key, key, sizeof(j->key) - 1);
    j->status = -100;
    dbg_log("net queue %s %s (%u bytes) tag %d", j->method, j->url, (unsigned)j->body_len, j->tag);
    LightLock_Lock(&s_lock);
    if (s_queue_tail) s_queue_tail->next = j;
    else s_queue = j;
    s_queue_tail = j;
    s_pending++;
    LightLock_Unlock(&s_lock);
    LightEvent_Signal(&s_wake);
    return j;
}

void net_pump(void) {
    for (;;) {
        LightLock_Lock(&s_lock);
        NetJob *j = s_done;
        if (j) s_done = j->next;
        if (j) s_pending--;
        LightLock_Unlock(&s_lock);
        if (!j) break;
        dbg_log("net done %s %s -> %d (%u bytes)%s", j->method, j->url, j->status, (unsigned)j->resp_len, j->cancelled ? " [cancelled]" : "");
        if (!j->cancelled && j->cb) j->cb(j);
        free_job(j);
    }
}

void net_cancel_tag(int tag) {
    LightLock_Lock(&s_lock);
    for (NetJob *j = s_queue; j; j = j->next)
        if (j->tag == tag) j->cancelled = true;
    for (NetJob *j = s_done; j; j = j->next)
        if (j->tag == tag) j->cancelled = true;
    if (s_current && s_current->tag == tag) s_current->cancelled = true;
    LightLock_Unlock(&s_lock);
}

int net_pending(void) { return s_pending; }

bool net_inflight_tag(int tag) {
    bool found = false;
    LightLock_Lock(&s_lock);
    if (s_current && s_current->tag == tag && !s_current->cancelled) found = true;
    for (NetJob *j = s_queue; j && !found; j = j->next)
        if (j->tag == tag && !j->cancelled) found = true;
    LightLock_Unlock(&s_lock);
    return found;
}
