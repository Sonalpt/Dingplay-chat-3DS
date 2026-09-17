#pragma once
// Asynchronous HTTP over the console's httpc service. Requests run on a worker
// thread and complete on the main thread through net_pump(), so a slow poll
// never stalls rendering or input.
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct NetJob NetJob;
typedef void (*NetCallback)(NetJob *job);

struct NetJob {
    char method[8];
    char url[320];
    char content_type[48];
    u8 *body;
    size_t body_len;
    // Result (valid inside the callback)
    int status;          // HTTP status, or < 0 for transport failures
    u8 *resp;            // NUL-terminated for convenience
    size_t resp_len;
    char hdr_xsize[16];
    // Bookkeeping
    NetCallback cb;
    void *user;
    int tag;             // used to cancel a family of requests (e.g. when leaving a screen)
    char key[48];        // free-form id (uid for avatar fetches)
    bool cancelled;
    NetJob *next;
};

bool net_init(void);
void net_exit(void);
void net_set_base(const char *base_url);    // "http://host:port"
void net_set_token(const char *token);      // "" clears
// `path` is relative to the base URL. Body is copied.
NetJob *net_request(const char *method, const char *path, const void *body, size_t len, const char *content_type,
                    NetCallback cb, void *user, int tag, const char *key);
void net_pump(void);           // main thread: dispatch finished jobs
void net_cancel_tag(int tag);  // callbacks of matching jobs will not fire
int net_pending(void);         // queued + in-flight
bool net_inflight_tag(int tag);
