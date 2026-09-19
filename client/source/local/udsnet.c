#include "udsnet.h"
#include "../api.h"
#include "../theme.h"
#include "../mii.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---- Constants ----------------------------------------------------------------------------------

#define WLANCOMM_ID 0x00D1A600u   // APP_UNIQUE_ID << 8
#define ID8 0
#define DATA_CHANNEL 1
#define SHAREDMEM_SIZE 0x20000
#define RECV_BUF_SIZE 0x10000
#define SCAN_BUF_SIZE 0x4000
#define CHUNK_PAYLOAD 1400        // < UDS_DATAFRAME_MAXSIZE (0x5C6)
#define HEARTBEAT_MS 1000
#define WEAK_MS 2500
#define LOST_MS 6000
#define ASSEMBLY_TIMEOUT_MS 12000
#define MAX_ASSEMBLIES 3
#define MAX_PAYLOAD (12 + 4 + (16360 * 10 + 1) / 2)  // a 10 s voice note

enum {
    PKT_HELLO = 1,   // guest→host  name[NAME_LEN]
    PKT_WELCOME = 2, // host→guest  (roster follows separately)
    PKT_DENY = 3,    // host→guest  u8 reason: 0 denied, 1 full, 2 closed
    PKT_ROSTER = 4,  // host→all    u8 count; {u16 node; u8 flags; char name[NAME_LEN]}[count]; char room[ROOM_NAME_LEN]
    PKT_TEXT = 5,    // any→all     u32 msgid; char text[]
    PKT_CHUNK = 6,   // any→node    u32 msgid; u8 kind; u16 idx; u16 total; u32 total_len; u8 data[]
    PKT_HEART = 7,   // host→all    u32 uptime_s; u32 msg_count
    PKT_KICK = 8,    // host→node
    PKT_CLOSE = 9,   // host→all
    PKT_LEAVE = 10,  // guest→host
    PKT_PROFILE = 11,// host→all    u16 node; char uid[UID_LEN]; u8 mii[MII_LEN]  (avatar identity of one member)
};
#define HELLO_LEN (NAME_LEN + UID_LEN + MII_LEN)  // name, Dingplay uid ("" if guest), Mii
enum { KIND_DRAW = 1, KIND_VOICE = 2 };
enum { ROSTER_MUTED = 1, ROSTER_HOST = 2 };

typedef struct __attribute__((packed)) {
    u8 type;
    u8 flags;
    u16 len;
} PktHdr;

typedef struct __attribute__((packed)) {
    char magic[4];  // "DPC1"
    u8 version;
    u8 slots;
    u8 members;
    u8 flags;       // bit0 locked, bit1 inviting
    char name[ROOM_NAME_LEN];
    char host[NAME_LEN];
} Beacon;

// ---- State -----------------------------------------------------------------------------------------

LocalRoom g_local_rooms[LOCAL_MAX_ROOMS];
int g_local_room_count;
MessageList g_local_chat;

static bool s_uds_ok;
static LocalState s_state = LOCAL_IDLE;
static bool s_host;
static udsBindContext s_bind;
static udsNetworkStruct s_network;
static char s_my_name[NAME_LEN];
static u16 s_my_node;
static char s_room_name[ROOM_NAME_LEN];
static u8 s_slots;
static bool s_locked, s_inviting;
static u64 s_started_tick;
static u64 s_last_heartbeat_sent, s_last_heartbeat_seen;
static u32 s_msg_counter;
static int s_msg_count;
static bool s_muted_me;
static bool s_new_message;
static u32 s_hello_msgid;
static u8 s_scanbuf[SCAN_BUF_SIZE];
static udsNetworkStruct s_room_nets[LOCAL_MAX_ROOMS];

typedef struct {
    bool used, accepted, muted;
    u16 node;
    char name[NAME_LEN];
    char uid[UID_LEN];          // Dingplay uid or ""
    u8 mii[MII_LEN];            // owner's Mii (all zero if unknown)
    char avatar_key[UID_LEN];   // derived: uid, else mii key, else ""
    u64 last_seen;
    u64 since;
    int dropped;
} Node;
static Node s_nodes[LOCAL_MAX_NODES];   // host: everyone connected; guest: roster

static bool send_raw(u16 dst, u8 type, const void *payload, u16 len);
static void broadcast_members(u8 type, const void *payload, u16 len);

// Names that recently dropped out, to count "out of range N times".
typedef struct {
    char name[NAME_LEN];
    int count;
} Dropped;
static Dropped s_dropped[LOCAL_MAX_NODES];

typedef struct {
    bool used;
    u16 src;
    u32 msgid;
    u8 kind;
    u16 total, received;
    u32 total_len;
    u8 *buf;
    u8 *have;   // bitmap by chunk
    u64 started;
} Assembly;
static Assembly s_asm[MAX_ASSEMBLIES];

static inline u64 now_ms(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000); }
static inline u16 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static inline u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static inline void wr16(u8 *p, u16 v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static inline void wr32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// ---- Init -----------------------------------------------------------------------------------------------

static void load_my_name(void) {
    // Prefer the Dingplay username; otherwise the console's Mii name.
    if (g_session.logged_in && g_session.username[0]) {
        strncpy(s_my_name, g_session.username, sizeof(s_my_name) - 1);
        return;
    }
    strncpy(s_my_name, mii_own_name(), sizeof(s_my_name) - 1);
    if (!s_my_name[0]) strcpy(s_my_name, "3DS");
}

bool local_init(void) {
    load_my_name();
    Result rc = udsInit(SHAREDMEM_SIZE, s_my_name);
    s_uds_ok = R_SUCCEEDED(rc);
    memset(&g_local_chat, 0, sizeof(g_local_chat));
    strcpy(g_local_chat.room, "local");
    return s_uds_ok;
}

void local_exit(void) {
    local_leave();
    msglist_clear(&g_local_chat);
    if (s_uds_ok) udsExit();
    s_uds_ok = false;
}

bool local_available(void) { return s_uds_ok; }
const char *local_my_name(void) {
    load_my_name();
    return s_my_name;
}

const char *local_my_avatar_key(void) {
    if (g_session.logged_in && g_session.uid[0]) return g_session.uid;
    return mii_own_key();
}

// Fills a node's identity from a HELLO / PROFILE payload and derives its avatar key.
static void node_set_profile(Node *n, const char *uid, const u8 *mii) {
    if (!n) return;
    if (uid) {
        strncpy(n->uid, uid, UID_LEN - 1);
        n->uid[UID_LEN - 1] = 0;
    }
    if (mii) memcpy(n->mii, mii, MII_LEN);
    if (n->uid[0]) strncpy(n->avatar_key, n->uid, UID_LEN - 1);
    else mii_register(n->mii, n->avatar_key);
}

static void my_profile(char *uid, u8 *mii) {
    memset(uid, 0, UID_LEN);
    if (g_session.logged_in) strncpy(uid, g_session.uid, UID_LEN - 1);
    if (!mii_own(mii)) memset(mii, 0, MII_LEN);
}

static void send_hello(void) {
    u8 hello[HELLO_LEN];
    memset(hello, 0, sizeof(hello));
    strncpy((char *)hello, s_my_name, NAME_LEN - 1);
    my_profile((char *)hello + NAME_LEN, hello + NAME_LEN + UID_LEN);
    send_raw(UDS_HOST_NETWORKNODEID, PKT_HELLO, hello, HELLO_LEN);
}

// Host → everyone: who each accepted member is (one frame per member).
static void send_profiles(u16 dst) {
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        Node *n = &s_nodes[i];
        if (!n->used || !n->accepted) continue;
        u8 buf[2 + UID_LEN + MII_LEN];
        wr16(buf, n->node);
        memcpy(buf + 2, n->uid, UID_LEN);
        memcpy(buf + 2 + UID_LEN, n->mii, MII_LEN);
        if (dst == UDS_BROADCAST_NETWORKNODEID) broadcast_members(PKT_PROFILE, buf, sizeof(buf));
        else send_raw(dst, PKT_PROFILE, buf, sizeof(buf));
    }
}

// ---- Sending helpers -----------------------------------------------------------------------------------

static bool send_raw(u16 dst, u8 type, const void *payload, u16 len) {
    u8 frame[sizeof(PktHdr) + CHUNK_PAYLOAD + 32];
    if (len > sizeof(frame) - sizeof(PktHdr)) return false;
    PktHdr *h = (PktHdr *)frame;
    h->type = type;
    h->flags = 0;
    h->len = len;
    if (len) memcpy(frame + sizeof(PktHdr), payload, len);
    Result rc = udsSendTo(dst, DATA_CHANNEL, UDS_SENDFLAG_Default, frame, sizeof(PktHdr) + len);
    return R_SUCCEEDED(rc);
}

static void broadcast_members(u8 type, const void *payload, u16 len) {
    // Unicast to each accepted member (802.11 unicast is ACKed; broadcast is not).
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        Node *n = &s_nodes[i];
        if (!n->used || !n->accepted || n->node == s_my_node) continue;
        send_raw(n->node, type, payload, len);
    }
}

static void send_roster(u16 dst) {
    u8 buf[1 + LOCAL_MAX_NODES * (3 + NAME_LEN) + ROOM_NAME_LEN];
    int count = 0;
    u8 *p = buf + 1;
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        Node *n = &s_nodes[i];
        if (!n->used || !n->accepted) continue;
        wr16(p, n->node);
        p[2] = (n->muted ? ROSTER_MUTED : 0) | (n->node == UDS_HOST_NETWORKNODEID ? ROSTER_HOST : 0);
        memcpy(p + 3, n->name, NAME_LEN);
        p += 3 + NAME_LEN;
        count++;
    }
    buf[0] = (u8)count;
    memcpy(p, s_room_name, ROOM_NAME_LEN);
    p += ROOM_NAME_LEN;
    u16 len = (u16)(p - buf);
    if (dst == UDS_BROADCAST_NETWORKNODEID) broadcast_members(PKT_ROSTER, buf, len);
    else send_raw(dst, PKT_ROSTER, buf, len);
}

static void update_beacon(void) {
    if (!s_host) return;
    Beacon b;
    memset(&b, 0, sizeof(b));
    memcpy(b.magic, "DPC1", 4);
    b.version = 1;
    b.slots = s_slots;
    int members = 0;
    for (int i = 0; i < LOCAL_MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].accepted) members++;
    b.members = (u8)members;
    b.flags = (s_locked ? 1 : 0) | (s_inviting ? 2 : 0);
    strncpy(b.name, s_room_name, ROOM_NAME_LEN - 1);
    strncpy(b.host, s_my_name, NAME_LEN - 1);
    udsSetApplicationData(&b, sizeof(b));
}

static void passphrase_for(char *out, size_t n, const char *code) {
    snprintf(out, n, "dingplay-chat:%s", (code && *code) ? code : "open");
}

// ---- Nodes ------------------------------------------------------------------------------------------------

static Node *node_find(u16 id) {
    for (int i = 0; i < LOCAL_MAX_NODES; i++)
        if (s_nodes[i].used && s_nodes[i].node == id) return &s_nodes[i];
    return NULL;
}

static Node *node_add(u16 id, const char *name, bool accepted) {
    Node *n = node_find(id);
    if (!n) {
        for (int i = 0; i < LOCAL_MAX_NODES; i++)
            if (!s_nodes[i].used) {
                n = &s_nodes[i];
                break;
            }
        if (!n) return NULL;
        memset(n, 0, sizeof(*n));
        n->used = true;
        n->node = id;
        n->since = now_ms();
    }
    if (name) strncpy(n->name, name, NAME_LEN - 1);
    n->accepted = accepted;
    n->last_seen = now_ms();
    if (!accepted) {
        n->dropped = 0;
        for (int i = 0; i < LOCAL_MAX_NODES; i++)
            if (s_dropped[i].name[0] && strcmp(s_dropped[i].name, n->name) == 0) n->dropped = s_dropped[i].count;
    }
    return n;
}

static void note_dropped(const char *name) {
    if (!name || !*name) return;
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        if (s_dropped[i].name[0] && strcmp(s_dropped[i].name, name) == 0) {
            s_dropped[i].count++;
            return;
        }
    }
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        if (!s_dropped[i].name[0]) {
            strncpy(s_dropped[i].name, name, NAME_LEN - 1);
            s_dropped[i].count = 1;
            return;
        }
    }
}

static void node_remove(u16 id, bool count_drop) {
    Node *n = node_find(id);
    if (!n) return;
    if (count_drop) note_dropped(n->name);
    memset(n, 0, sizeof(*n));
}

static void reset_room_state(void) {
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_dropped, 0, sizeof(s_dropped));
    for (int i = 0; i < MAX_ASSEMBLIES; i++) {
        free(s_asm[i].buf);
        free(s_asm[i].have);
    }
    memset(s_asm, 0, sizeof(s_asm));
    msglist_clear(&g_local_chat);
    s_msg_count = 0;
    s_msg_counter = 0;
    s_muted_me = false;
    s_inviting = false;
    s_last_heartbeat_seen = 0;
    s_last_heartbeat_sent = 0;
}

// ---- Scan -------------------------------------------------------------------------------------------------

int local_scan(void) {
    if (!s_uds_ok) return 0;
    udsNetworkScanInfo *networks = NULL;
    size_t total = 0;
    bool connected = s_state == LOCAL_HOSTING || s_state == LOCAL_JOINED || s_state == LOCAL_WAITING;
    memset(s_scanbuf, 0, sizeof(s_scanbuf));
    Result rc = udsScanBeacons(s_scanbuf, sizeof(s_scanbuf), &networks, &total, WLANCOMM_ID, ID8, NULL, connected);
    if (R_FAILED(rc)) return g_local_room_count;

    bool seen[LOCAL_MAX_ROOMS] = {false};
    for (size_t i = 0; i < total; i++) {
        udsNetworkScanInfo *info = &networks[i];
        Beacon b;
        size_t actual = 0;
        if (R_FAILED(udsGetNetworkStructApplicationData(&info->network, &b, sizeof(b), &actual)) || actual < sizeof(b)) continue;
        if (memcmp(b.magic, "DPC1", 4) != 0) continue;
        // don't list our own room
        if (s_host && strncmp(b.name, s_room_name, ROOM_NAME_LEN) == 0 && strncmp(b.host, s_my_name, NAME_LEN) == 0) continue;
        int slot = -1;
        for (int k = 0; k < g_local_room_count; k++)
            if (memcmp(g_local_rooms[k].mac, info->network.host_macaddress, 6) == 0) slot = k;
        if (slot < 0) {
            if (g_local_room_count >= LOCAL_MAX_ROOMS) continue;
            slot = g_local_room_count++;
            memset(&g_local_rooms[slot], 0, sizeof(LocalRoom));
            memcpy(g_local_rooms[slot].mac, info->network.host_macaddress, 6);
        }
        LocalRoom *r = &g_local_rooms[slot];
        b.name[ROOM_NAME_LEN - 1] = 0;
        b.host[NAME_LEN - 1] = 0;
        strncpy(r->name, b.name, ROOM_NAME_LEN - 1);
        strncpy(r->host, b.host, NAME_LEN - 1);
        r->slots = b.slots;
        r->members = b.members;
        r->flags = b.flags;
        r->misses = 0;
        s_room_nets[slot] = info->network;
        r->net = &s_room_nets[slot];
        seen[slot] = true;
    }
    // Beacons absent from this scan: count a miss; drop after three (signal proxy).
    for (int k = 0; k < g_local_room_count;) {
        if (!seen[k]) g_local_rooms[k].misses++;
        if (g_local_rooms[k].misses >= 3) {
            for (int j = k; j < g_local_room_count - 1; j++) {
                g_local_rooms[j] = g_local_rooms[j + 1];
                s_room_nets[j] = s_room_nets[j + 1];
                g_local_rooms[j].net = &s_room_nets[j];
                seen[j] = seen[j + 1];
            }
            g_local_room_count--;
        } else {
            k++;
        }
    }
    if (networks) free(networks);
    return g_local_room_count;
}

// ---- Host ---------------------------------------------------------------------------------------------------

bool local_host(const char *name, int slots, const char *passcode) {
    if (!s_uds_ok) return false;
    local_leave();
    reset_room_state();
    strncpy(s_room_name, name, ROOM_NAME_LEN - 1);
    s_room_name[ROOM_NAME_LEN - 1] = 0;
    s_slots = (u8)(slots == 4 || slots == 8 || slots == 16 ? slots : 8);
    s_locked = passcode && *passcode;
    char pass[40];
    passphrase_for(pass, sizeof(pass), passcode);
    udsGenerateDefaultNetworkStruct(&s_network, WLANCOMM_ID, ID8, s_slots);
    Result rc = udsCreateNetwork(&s_network, pass, strlen(pass), &s_bind, DATA_CHANNEL, RECV_BUF_SIZE);
    if (R_FAILED(rc)) {
        s_state = LOCAL_ERROR;
        return false;
    }
    s_host = true;
    s_my_node = UDS_HOST_NETWORKNODEID;
    udsConnectionStatus st;
    if (R_SUCCEEDED(udsGetConnectionStatus(&st))) s_my_node = st.cur_NetworkNodeID;
    {
        Node *me = node_add(s_my_node, s_my_name, true);
        char uid[UID_LEN];
        u8 mii[MII_LEN];
        my_profile(uid, mii);
        node_set_profile(me, uid, mii);
    }
    s_state = LOCAL_HOSTING;
    s_started_tick = now_ms();
    update_beacon();
    return true;
}

void local_set_inviting(bool on) {
    s_inviting = on;
    update_beacon();
}

void local_accept(u16 node) {
    Node *n = node_find(node);
    if (!s_host || !n || n->accepted) return;
    n->accepted = true;
    send_raw(node, PKT_WELCOME, NULL, 0);
    send_roster(UDS_BROADCAST_NETWORKNODEID);
    send_profiles(UDS_BROADCAST_NETWORKNODEID);
    update_beacon();
}

void local_deny(u16 node) {
    if (!s_host) return;
    u8 reason = 0;
    send_raw(node, PKT_DENY, &reason, 1);
    node_remove(node, false);
    udsEjectClient(node);
}

void local_kick(u16 node) {
    if (!s_host) return;
    send_raw(node, PKT_KICK, NULL, 0);
    node_remove(node, false);
    udsEjectClient(node);
    send_roster(UDS_BROADCAST_NETWORKNODEID);
    update_beacon();
}

void local_mute(u16 node, bool muted) {
    Node *n = node_find(node);
    if (!s_host || !n) return;
    n->muted = muted;
    send_roster(UDS_BROADCAST_NETWORKNODEID);
}

// ---- Guest ----------------------------------------------------------------------------------------------------

bool local_join(const LocalRoom *room, const char *passcode) {
    if (!s_uds_ok || !room || !room->net) return false;
    local_leave();
    reset_room_state();
    s_network = *(udsNetworkStruct *)room->net;
    strncpy(s_room_name, room->name, ROOM_NAME_LEN - 1);
    s_slots = room->slots;
    s_locked = room->flags & 1;
    char pass[40];
    passphrase_for(pass, sizeof(pass), passcode);
    s_state = LOCAL_CONNECTING;
    Result rc = udsConnectNetwork(&s_network, pass, strlen(pass), &s_bind, UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, DATA_CHANNEL, RECV_BUF_SIZE);
    if (R_FAILED(rc)) {
        s_state = LOCAL_ERROR;
        return false;
    }
    s_host = false;
    udsConnectionStatus st;
    s_my_node = 0;
    if (R_SUCCEEDED(udsGetConnectionStatus(&st))) s_my_node = st.cur_NetworkNodeID;
    send_hello();
    s_state = LOCAL_WAITING;
    s_started_tick = now_ms();
    s_last_heartbeat_seen = now_ms();
    return true;
}

// ---- Leave -------------------------------------------------------------------------------------------------------

void local_leave(void) {
    if (s_state == LOCAL_IDLE) return;
    if (s_host) {
        broadcast_members(PKT_CLOSE, NULL, 0);
        udsDestroyNetwork();
    } else if (s_state == LOCAL_JOINED || s_state == LOCAL_WAITING) {
        send_raw(UDS_HOST_NETWORKNODEID, PKT_LEAVE, NULL, 0);
        udsDisconnectNetwork();
    } else if (s_state == LOCAL_CONNECTING) {
        udsDisconnectNetwork();
    }
    if (s_state != LOCAL_ERROR) udsUnbind(&s_bind);
    s_host = false;
    s_state = LOCAL_IDLE;
    reset_room_state();
}

// ---- Receiving -----------------------------------------------------------------------------------------------------

static const char *name_for(u16 node) {
    Node *n = node_find(node);
    return n ? n->name : "?";
}

static void push_message(u16 src, u32 msgid, u8 type, const char *text, DrawingRef *draw, u8 *voice, size_t voice_len) {
    Message m;
    memset(&m, 0, sizeof(m));
    snprintf(m.id, sizeof(m.id), "l-%u-%u", src, (unsigned)msgid);
    {
        Node *n = node_find(src);
        if (n && n->avatar_key[0]) strncpy(m.uid, n->avatar_key, sizeof(m.uid) - 1);
        else if (src == s_my_node) strncpy(m.uid, local_my_avatar_key(), sizeof(m.uid) - 1);
        else snprintf(m.uid, sizeof(m.uid), "node-%u", src);
    }
    strncpy(m.name, src == s_my_node ? s_my_name : name_for(src), NAME_LEN - 1);
    m.type = type;
    m.mine = src == s_my_node;
    m.ts = (int64_t)time(NULL) * 1000;
    m.local_node = src;
    if (text) strncpy(m.text, text, MSG_TEXT_LEN - 1);
    m.draw = draw;
    if (voice) {
        m.voice = voice;
        m.voice_len = voice_len;
        m.has_media = true;
        u32 rate = rd32(voice + 4), count = rd32(voice + 8);
        m.dur_ms = rate ? (int16_t)((u64)count * 1000 / rate) : 0;
        waveform_from_dpv(voice, voice_len, m.bars);
        m.played = m.mine;
    }
    msglist_push(&g_local_chat, &m);
    s_msg_count++;
    if (!m.mine) s_new_message = true;
}

static DrawingRef *drawing_unpack(const u8 *p, size_t len) {
    if (len < 8) return NULL;
    Drawing *d = (Drawing *)calloc(1, sizeof(Drawing));
    if (!d) return NULL;
    d->w = rd16(p);
    d->h = rd16(p + 2);
    d->nstrokes = rd16(p + 4);
    d->npoints = rd16(p + 6);
    size_t need = 8 + (size_t)d->nstrokes * 6 + (size_t)d->npoints * 4;
    if (d->nstrokes > DRAW_MAX_STROKES || d->npoints > DRAW_MAX_POINTS || len < need) {
        free(d);
        return NULL;
    }
    const u8 *q = p + 8;
    for (int i = 0; i < d->nstrokes; i++, q += 6) {
        d->strokes[i].color = q[0] < DRAW_COLOR_COUNT ? q[0] : 0;
        d->strokes[i].pen = q[1] > 2 ? 1 : q[1];
        d->strokes[i].start = rd16(q + 2);
        d->strokes[i].count = rd16(q + 4);
        if (d->strokes[i].start + d->strokes[i].count > d->npoints) {
            free(d);
            return NULL;
        }
    }
    for (int i = 0; i < d->npoints * 2; i++, q += 2) d->pts[i] = rd16(q);
    DrawingRef *r = drawing_compact(d);
    free(d);
    return r;
}

static size_t drawing_pack(const Drawing *d, u8 *out, size_t cap) {
    size_t need = 8 + (size_t)d->nstrokes * 6 + (size_t)d->npoints * 4;
    if (need > cap) return 0;
    wr16(out, d->w);
    wr16(out + 2, d->h);
    wr16(out + 4, d->nstrokes);
    wr16(out + 6, d->npoints);
    u8 *q = out + 8;
    for (int i = 0; i < d->nstrokes; i++, q += 6) {
        q[0] = d->strokes[i].color;
        q[1] = d->strokes[i].pen;
        wr16(q + 2, d->strokes[i].start);
        wr16(q + 4, d->strokes[i].count);
    }
    for (int i = 0; i < d->npoints * 2; i++, q += 2) wr16(q, d->pts[i]);
    return need;
}

static void assembly_complete(Assembly *a) {
    if (a->kind == KIND_DRAW) {
        DrawingRef *d = drawing_unpack(a->buf, a->total_len);
        if (d) push_message(a->src, a->msgid, MSG_DRAW, NULL, d, NULL, 0);
    } else if (a->kind == KIND_VOICE) {
        if (a->total_len > 16 && memcmp(a->buf, "DPV1", 4) == 0) {
            push_message(a->src, a->msgid, MSG_VOICE, NULL, NULL, a->buf, a->total_len);
            a->buf = NULL;  // ownership moved to the message
        }
    }
    free(a->buf);
    free(a->have);
    memset(a, 0, sizeof(*a));
}

static void handle_chunk(u16 src, const u8 *p, u16 len) {
    if (len < 13) return;
    u32 msgid = rd32(p);
    u8 kind = p[4];
    u16 idx = rd16(p + 5), total = rd16(p + 7);
    u32 total_len = rd32(p + 9);
    const u8 *data = p + 13;
    u16 dlen = len - 13;
    if (total == 0 || idx >= total || total_len == 0 || total_len > MAX_PAYLOAD) return;
    if ((u32)idx * CHUNK_PAYLOAD + dlen > total_len) return;
    Assembly *a = NULL;
    for (int i = 0; i < MAX_ASSEMBLIES; i++)
        if (s_asm[i].used && s_asm[i].src == src && s_asm[i].msgid == msgid) a = &s_asm[i];
    if (!a) {
        for (int i = 0; i < MAX_ASSEMBLIES; i++)
            if (!s_asm[i].used) {
                a = &s_asm[i];
                break;
            }
        if (!a) return;  // too many in flight; the sender's note is lost
        memset(a, 0, sizeof(*a));
        a->buf = (u8 *)malloc(total_len);
        a->have = (u8 *)calloc((total + 7) / 8, 1);
        if (!a->buf || !a->have) {
            free(a->buf);
            free(a->have);
            memset(a, 0, sizeof(*a));
            return;
        }
        a->used = true;
        a->src = src;
        a->msgid = msgid;
        a->kind = kind;
        a->total = total;
        a->total_len = total_len;
        a->started = now_ms();
    }
    if (a->have[idx / 8] & (1 << (idx % 8))) return;
    memcpy(a->buf + (size_t)idx * CHUNK_PAYLOAD, data, dlen);
    a->have[idx / 8] |= (1 << (idx % 8));
    a->received++;
    if (a->received >= a->total) assembly_complete(a);
}

static void handle_roster(const u8 *p, u16 len) {
    if (len < 1) return;
    int count = p[0];
    if (len < 1 + count * (3 + NAME_LEN) + ROOM_NAME_LEN) return;
    bool was_muted = s_muted_me;
    // Keep identities we already know; drop nodes no longer listed.
    Node old[LOCAL_MAX_NODES];
    memcpy(old, s_nodes, sizeof(old));
    memset(s_nodes, 0, sizeof(s_nodes));
    const u8 *q = p + 1;
    for (int i = 0; i < count; i++, q += 3 + NAME_LEN) {
        u16 node = rd16(q);
        char name[NAME_LEN];
        memcpy(name, q + 3, NAME_LEN);
        name[NAME_LEN - 1] = 0;
        Node *n = node_add(node, name, true);
        if (n) {
            n->muted = (q[2] & ROSTER_MUTED) != 0;
            for (int k = 0; k < LOCAL_MAX_NODES; k++)
                if (old[k].used && old[k].node == node) node_set_profile(n, old[k].uid, old[k].mii);
            if (node == s_my_node && !n->avatar_key[0]) {
                char uid[UID_LEN];
                u8 mii[MII_LEN];
                my_profile(uid, mii);
                node_set_profile(n, uid, mii);
            }
        }
        if (node == s_my_node) s_muted_me = (q[2] & ROSTER_MUTED) != 0;
    }
    memcpy(s_room_name, q, ROOM_NAME_LEN);
    s_room_name[ROOM_NAME_LEN - 1] = 0;
    (void)was_muted;
}

static void handle_packet(u16 src, const u8 *frame, size_t size) {
    if (size < sizeof(PktHdr)) return;
    const PktHdr *h = (const PktHdr *)frame;
    if (h->len + sizeof(PktHdr) > size) return;
    const u8 *p = frame + sizeof(PktHdr);
    u16 len = h->len;
    Node *sender = node_find(src);
    if (sender) sender->last_seen = now_ms();

    switch (h->type) {
        case PKT_HELLO:
            if (!s_host || len < 1) break;
            {
                char name[NAME_LEN];
                memcpy(name, p, len < NAME_LEN ? len : NAME_LEN);
                name[NAME_LEN - 1] = 0;
                int accepted = 0;
                for (int i = 0; i < LOCAL_MAX_NODES; i++)
                    if (s_nodes[i].used && s_nodes[i].accepted) accepted++;
                if (accepted >= s_slots) {
                    u8 reason = 1;
                    send_raw(src, PKT_DENY, &reason, 1);
                    udsEjectClient(src);
                    break;
                }
                Node *n = node_add(src, name, false);  // lands in "waiting to join"
                if (len >= HELLO_LEN) {
                    char uid[UID_LEN];
                    memcpy(uid, p + NAME_LEN, UID_LEN);
                    uid[UID_LEN - 1] = 0;
                    node_set_profile(n, uid, p + NAME_LEN + UID_LEN);
                }
            }
            break;
        case PKT_WELCOME:
            if (s_host) break;
            s_state = LOCAL_JOINED;
            s_last_heartbeat_seen = now_ms();
            break;
        case PKT_DENY:
            if (s_host) break;
            s_state = (len >= 1 && p[0] == 1) ? LOCAL_FULL : LOCAL_DENIED;
            udsDisconnectNetwork();
            udsUnbind(&s_bind);
            break;
        case PKT_ROSTER:
            if (s_host) break;
            handle_roster(p, len);
            break;
        case PKT_TEXT: {
            if (len < 5) break;
            Node *n = node_find(src);
            if (n && n->muted) break;
            if (s_host && (!n || !n->accepted)) break;
            char text[MSG_TEXT_LEN];
            u16 tl = len - 4;
            if (tl >= MSG_TEXT_LEN) tl = MSG_TEXT_LEN - 1;
            memcpy(text, p + 4, tl);
            text[tl] = 0;
            push_message(src, rd32(p), MSG_TEXT, text, NULL, NULL, 0);
            break;
        }
        case PKT_CHUNK: {
            Node *n = node_find(src);
            if (n && n->muted) break;
            if (s_host && (!n || !n->accepted)) break;
            handle_chunk(src, p, len);
            break;
        }
        case PKT_HEART:
            if (s_host) break;
            s_last_heartbeat_seen = now_ms();
            if (len >= 8) s_msg_count = (int)rd32(p + 4);
            break;
        case PKT_KICK:
            if (s_host) break;
            s_state = LOCAL_KICKED;
            udsDisconnectNetwork();
            udsUnbind(&s_bind);
            break;
        case PKT_CLOSE:
            if (s_host) break;
            s_state = LOCAL_CLOSED;
            udsDisconnectNetwork();
            udsUnbind(&s_bind);
            break;
        case PKT_PROFILE: {
            if (s_host || len < 2 + UID_LEN + MII_LEN) break;
            Node *n = node_find(rd16(p));
            if (!n) break;
            char uid[UID_LEN];
            memcpy(uid, p + 2, UID_LEN);
            uid[UID_LEN - 1] = 0;
            node_set_profile(n, uid, p + 2 + UID_LEN);
            break;
        }
        case PKT_LEAVE:
            if (!s_host) break;
            node_remove(src, false);
            send_roster(UDS_BROADCAST_NETWORKNODEID);
            update_beacon();
            break;
    }
}

// ---- Per-frame ----------------------------------------------------------------------------------------------

static void host_reconcile_nodes(void) {
    udsConnectionStatus st;
    if (R_FAILED(udsGetConnectionStatus(&st))) return;
    bool changed = false;
    for (int i = 0; i < LOCAL_MAX_NODES; i++) {
        Node *n = &s_nodes[i];
        if (!n->used || n->node == s_my_node) continue;
        bool present = n->node < 16 && (st.node_bitmask & (1 << (n->node - 1)));
        if (!present) {
            // Vanished at the link layer: out of range or powered off.
            note_dropped(n->name);
            memset(n, 0, sizeof(*n));
            changed = true;
        }
    }
    if (changed) {
        send_roster(UDS_BROADCAST_NETWORKNODEID);
        update_beacon();
    }
}

void local_update(void) {
    if (!s_uds_ok || s_state == LOCAL_IDLE) return;
    if (s_state == LOCAL_HOSTING || s_state == LOCAL_JOINED || s_state == LOCAL_WAITING) {
        // Drain the receive buffer.
        static u8 frame[UDS_DATAFRAME_MAXSIZE];
        for (int guard = 0; guard < 64; guard++) {
            size_t actual = 0;
            u16 src = 0;
            Result rc = udsPullPacket(&s_bind, frame, sizeof(frame), &actual, &src);
            if (R_FAILED(rc) || actual == 0) break;
            handle_packet(src, frame, actual);
            if (s_state == LOCAL_DENIED || s_state == LOCAL_FULL || s_state == LOCAL_KICKED || s_state == LOCAL_CLOSED) break;
        }
    }
    u64 now = now_ms();
    if (s_state == LOCAL_HOSTING) {
        if (udsWaitConnectionStatusEvent(false, false)) host_reconcile_nodes();
        if (now - s_last_heartbeat_sent >= HEARTBEAT_MS) {
            u8 hb[8];
            wr32(hb, (u32)((now - s_started_tick) / 1000));
            wr32(hb + 4, (u32)s_msg_count);
            broadcast_members(PKT_HEART, hb, sizeof(hb));
            s_last_heartbeat_sent = now;
        }
    } else if (s_state == LOCAL_JOINED || s_state == LOCAL_WAITING) {
        if (now - s_last_heartbeat_seen > LOST_MS) {
            s_state = LOCAL_LOST;
            udsDisconnectNetwork();
            udsUnbind(&s_bind);
        } else if (s_state == LOCAL_WAITING && now - s_started_tick > 3000 && ((now - s_started_tick) / 1000) % 3 == 0 && s_hello_msgid != (u32)((now - s_started_tick) / 3000)) {
            // Re-send HELLO every 3 s until the host answers (first frame may race the bind).
            s_hello_msgid = (u32)((now - s_started_tick) / 3000);
            send_hello();
        }
    }
    // Expire stale reassemblies.
    for (int i = 0; i < MAX_ASSEMBLIES; i++) {
        if (s_asm[i].used && now - s_asm[i].started > ASSEMBLY_TIMEOUT_MS) {
            free(s_asm[i].buf);
            free(s_asm[i].have);
            memset(&s_asm[i], 0, sizeof(Assembly));
        }
    }
}

// ---- Queries ---------------------------------------------------------------------------------------------------

LocalState local_state(void) { return s_state; }
bool local_is_host(void) { return s_host && s_state == LOCAL_HOSTING; }
bool local_in_room(void) { return s_state == LOCAL_HOSTING || s_state == LOCAL_JOINED; }
const char *local_room_name(void) { return s_room_name; }
bool local_muted_me(void) { return s_muted_me; }
int local_msg_count(void) { return s_msg_count; }
int local_uptime_s(void) { return s_state == LOCAL_IDLE ? 0 : (int)((now_ms() - s_started_tick) / 1000); }

int local_members(LocalMember *out, int max) {
    int c = 0;
    // host first
    for (int pass = 0; pass < 2 && c < max; pass++) {
        for (int i = 0; i < LOCAL_MAX_NODES && c < max; i++) {
            Node *n = &s_nodes[i];
            if (!n->used || !n->accepted) continue;
            bool is_host = n->node == UDS_HOST_NETWORKNODEID;
            if ((pass == 0) != is_host) continue;
            out[c].node = n->node;
            strncpy(out[c].name, n->name, NAME_LEN - 1);
            out[c].name[NAME_LEN - 1] = 0;
            strncpy(out[c].avatar_key, n->avatar_key, UID_LEN - 1);
            out[c].avatar_key[UID_LEN - 1] = 0;
            out[c].muted = n->muted;
            out[c].is_host = is_host;
            out[c].is_me = n->node == s_my_node;
            out[c].last_seen = n->last_seen;
            c++;
        }
    }
    return c;
}

int local_pending(LocalPending *out, int max) {
    int c = 0;
    if (!s_host) return 0;
    for (int i = 0; i < LOCAL_MAX_NODES && c < max; i++) {
        Node *n = &s_nodes[i];
        if (!n->used || n->accepted) continue;
        out[c].node = n->node;
        strncpy(out[c].name, n->name, NAME_LEN - 1);
        out[c].name[NAME_LEN - 1] = 0;
        strncpy(out[c].avatar_key, n->avatar_key, UID_LEN - 1);
        out[c].avatar_key[UID_LEN - 1] = 0;
        out[c].dropped = n->dropped;
        out[c].since = n->since;
        c++;
    }
    return c;
}

int local_signal(void) {
    if (s_state == LOCAL_HOSTING) return 2;
    if (s_state != LOCAL_JOINED && s_state != LOCAL_WAITING) return 0;
    u64 age = now_ms() - s_last_heartbeat_seen;
    return age < WEAK_MS ? 2 : age < LOST_MS ? 1 : 0;
}

bool local_take_new_message_flag(void) {
    bool v = s_new_message;
    s_new_message = false;
    return v;
}

// ---- Sending ------------------------------------------------------------------------------------------------------

bool local_send_text(const char *text) {
    if (!local_in_room() || s_muted_me || !text || !*text) return false;
    u8 buf[4 + MSG_TEXT_LEN];
    u32 id = ++s_msg_counter;
    wr32(buf, id);
    size_t tl = strlen(text);
    if (tl > MSG_TEXT_LEN - 1) tl = MSG_TEXT_LEN - 1;
    memcpy(buf + 4, text, tl);
    if (s_host) broadcast_members(PKT_TEXT, buf, (u16)(4 + tl));
    else send_raw(UDS_BROADCAST_NETWORKNODEID, PKT_TEXT, buf, (u16)(4 + tl));
    push_message(s_my_node, id, MSG_TEXT, text, NULL, NULL, 0);
    return true;
}

static bool send_chunked(u8 kind, const u8 *data, size_t len) {
    if (!local_in_room() || s_muted_me || len == 0 || len > MAX_PAYLOAD) return false;
    u32 id = ++s_msg_counter;
    u16 total = (u16)((len + CHUNK_PAYLOAD - 1) / CHUNK_PAYLOAD);
    u8 frame[13 + CHUNK_PAYLOAD];
    for (u16 idx = 0; idx < total; idx++) {
        size_t off = (size_t)idx * CHUNK_PAYLOAD;
        size_t n = len - off < CHUNK_PAYLOAD ? len - off : CHUNK_PAYLOAD;
        wr32(frame, id);
        frame[4] = kind;
        wr16(frame + 5, idx);
        wr16(frame + 7, total);
        wr32(frame + 9, (u32)len);
        memcpy(frame + 13, data + off, n);
        // Unicast to every member we know of (host: all accepted; guest: the roster).
        for (int i = 0; i < LOCAL_MAX_NODES; i++) {
            Node *m = &s_nodes[i];
            if (!m->used || !m->accepted || m->node == s_my_node) continue;
            send_raw(m->node, PKT_CHUNK, frame, (u16)(13 + n));
        }
        // Give the radio room between bursts on larger payloads.
        if ((idx & 7) == 7) svcSleepThread(2000000ULL);
    }
    return true;
}

bool local_send_draw(const Drawing *d) {
    if (!d || d->nstrokes == 0) return false;
    size_t cap = 8 + (size_t)d->nstrokes * 6 + (size_t)d->npoints * 4;
    u8 *buf = (u8 *)malloc(cap);
    if (!buf) return false;
    size_t n = drawing_pack(d, buf, cap);
    bool ok = n && send_chunked(KIND_DRAW, buf, n);
    free(buf);
    if (ok) push_message(s_my_node, s_msg_counter, MSG_DRAW, NULL, drawing_compact(d), NULL, 0);
    return ok;
}

bool local_send_voice(const u8 *dpv, size_t len) {
    if (!send_chunked(KIND_VOICE, dpv, len)) return false;
    u8 *copy = (u8 *)malloc(len);
    if (!copy) return true;
    memcpy(copy, dpv, len);
    push_message(s_my_node, s_msg_counter, MSG_VOICE, NULL, NULL, copy, len);
    return true;
}
