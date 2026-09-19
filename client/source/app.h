#pragma once
// App-wide state, data model and screen plumbing.
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdint.h>
#include "i18n.h"

// Public relay (Vultr box shared with the 3LWO relay); config.ini `relay=` overrides it.
#define RELAY_DEFAULT "https://199.247.12.221:8000"

// ---- Data model ----------------------------------------------------------------
// Small structs sized for what local wireless frames and a polled relay can carry,
// not a port of the mobile schema.

#define UID_LEN 40
#define NAME_LEN 32
#define MSG_TEXT_LEN 208      // relay caps text at 200 chars
#define ROOM_ID_LEN 48
#define ROOM_NAME_LEN 26
#define MSG_HISTORY 60        // messages kept in RAM per open chat (Old 3DS budget)

typedef enum { MSG_TEXT = 0, MSG_DRAW = 1, MSG_VOICE = 2, MSG_IMAGE = 3 } MsgType;

#define DRAW_MAX_STROKES 120
#define DRAW_MAX_POINTS 2000

typedef struct {
    uint8_t color;   // index into DRAW_COLORS
    uint8_t pen;     // 0..2
    uint16_t start;  // index of first point (x,y pairs) in pts
    uint16_t count;  // number of points
} Stroke;

// The live canvas. Received drawings are stored compacted (drawing_compact).
typedef struct {
    uint16_t w, h;
    uint16_t nstrokes, npoints;
    Stroke strokes[DRAW_MAX_STROKES];
    uint16_t pts[DRAW_MAX_POINTS * 2];
} Drawing;

typedef struct {
    uint16_t w, h;
    uint16_t nstrokes, npoints;
    Stroke *strokes;
    uint16_t *pts;
} DrawingRef;  // heap-owned compact copy

typedef struct {
    char id[UID_LEN];
    char uid[UID_LEN];
    char name[NAME_LEN];
    uint8_t type;        // MsgType
    bool mine;
    bool pending;        // sent, not yet confirmed by the relay
    bool expired;        // media past its 1 h lifetime
    bool has_media;
    bool played;         // voice: tapped at least once
    int16_t dur_ms;      // voice duration (clamped to 10 s)
    int64_t ts;          // unix ms
    char text[MSG_TEXT_LEN];
    DrawingRef *draw;    // MSG_DRAW only
    uint8_t *voice;      // MSG_VOICE: DPV1 bytes once fetched (local mode: always set)
    uint32_t voice_len;
    uint8_t bars[8];     // 0..15 waveform preview
    uint16_t local_node; // local wireless: sender node id
} Message;

typedef struct {
    Message items[MSG_HISTORY];
    int count;
    int64_t newest_ts;   // for ?since=
    char room[ROOM_ID_LEN];
    bool loaded;
} MessageList;

typedef enum { FSTATUS_OFFLINE = 0, FSTATUS_ONLINE = 1, FSTATUS_IN_ROOM = 2 } FriendStatus;

typedef struct {
    char uid[UID_LEN];
    char username[NAME_LEN];
    uint8_t status;      // FriendStatus
    char lobby_title[NAME_LEN];
    int64_t last_seen;
    int unread;
} Friend;

typedef struct {
    char uid[UID_LEN];
    char username[NAME_LEN];
} FriendRequest;

#define MAX_FRIENDS 64
#define MAX_REQUESTS 16
#define MAX_THEMED_ROOMS 30

typedef struct {
    char id[24];
    char name[ROOM_NAME_LEN];
    char topic[36];
    char host[NAME_LEN];
    char host_uid[UID_LEN];
    int count;
} ThemedRoom;

// ---- Session / settings -------------------------------------------------------------

typedef struct {
    char relay[128];        // http://host:port
    char token[200];
    bool stay_signed_in;
    bool notif_sound;
    bool discoverable;
    int sync_seconds;       // 3 / 5 / 15
    Lang lang;
    Lang chat_lang;         // which world-chat room (FR / EN); independent of the UI language
} Settings;

typedef struct {
    bool logged_in;
    char uid[UID_LEN];
    char username[NAME_LEN];
    int friends_online;
    int friends_total;
    int requests;
    int unread;
    int players_online;
    char news_tag[16];
    char news_text[96];
} Session;

// ---- Screens -----------------------------------------------------------------------------

typedef enum {
    SCR_BOOT, SCR_LOGIN, SCR_HOME, SCR_FRIENDS, SCR_CHAT, SCR_LOBBY, SCR_CREATE_ROOM,
    SCR_LOCAL_CHAT, SCR_MANAGE_ROOM, SCR_SETTINGS, SCR_COUNT
} ScreenId;

typedef struct {
    uint32_t down, held, up;     // hid keys
    bool touching, touch_down, touch_up;
    touchPosition touch;         // current (or last) stylus position
    touchPosition touch_start;
    float dt;                    // seconds since last frame
} Input;

typedef struct {
    void (*enter)(void *arg);
    void (*leave)(void);
    void (*update)(const Input *in);
    void (*draw_top)(void);
    void (*draw_bottom)(void);
} ScreenVTable;

extern const ScreenVTable *const SCREENS[SCR_COUNT];

// Navigation: B always steps back one level in this tree.
void app_go(ScreenId id, void *arg);        // push
void app_replace(ScreenId id, void *arg);   // replace top of stack
void app_back(void);                        // pop
void app_reset_to(ScreenId id, void *arg);  // clear stack, push
ScreenId app_current(void);
void app_quit(void);

// Transient toast on the bottom screen (errors, confirmations).
void app_toast(const char *text, u32 color);

// ---- Globals -----------------------------------------------------------------------------
extern Settings g_settings;
extern Session g_session;
extern bool g_wifi;             // AC says we have a connection
extern int g_wifi_bars;         // 0..3
extern bool g_new3ds;
extern float g_time;            // seconds since boot
extern C3D_RenderTarget *g_top, *g_bottom;
