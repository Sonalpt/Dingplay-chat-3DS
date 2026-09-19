#pragma once
// Local Wireless rooms over the uds (NWM) service. One console hosts the
// network and is the authority on membership (approve / deny / mute / kick);
// nothing persists past the host closing the room.
//
// Wire format: compact binary frames (see udsnet.c), never JSON.
#include "app.h"

typedef enum {
    LOCAL_IDLE,        // not in a room
    LOCAL_HOSTING,
    LOCAL_CONNECTING,  // udsConnectNetwork in progress / just succeeded
    LOCAL_WAITING,     // HELLO sent, host has not answered yet
    LOCAL_JOINED,
    LOCAL_DENIED,
    LOCAL_FULL,
    LOCAL_LOST,        // host heartbeat gone
    LOCAL_KICKED,
    LOCAL_CLOSED,      // host closed the room
    LOCAL_ERROR
} LocalState;

typedef struct {
    u8 mac[6];
    char name[ROOM_NAME_LEN];
    char host[NAME_LEN];
    u8 slots, members, flags;   // flags: bit0 locked, bit1 inviting
    int misses;                 // scans in a row where the beacon was absent (signal proxy)
    void *net;                  // udsNetworkStruct copy for joining
} LocalRoom;

typedef struct {
    u16 node;
    char name[NAME_LEN];
    char avatar_key[UID_LEN];   // Dingplay uid, "mii:<hash>", or "" (placeholder)
    bool muted, is_host, is_me;
    u64 last_seen;
} LocalMember;

typedef struct {
    u16 node;
    char name[NAME_LEN];
    char avatar_key[UID_LEN];
    int dropped;     // times this name fell out of range before
    u64 since;
} LocalPending;

#define LOCAL_MAX_ROOMS 12
#define LOCAL_MAX_NODES 16

extern LocalRoom g_local_rooms[LOCAL_MAX_ROOMS];
extern int g_local_room_count;
extern MessageList g_local_chat;

bool local_init(void);          // udsInit; false when local wireless is unavailable
void local_exit(void);
bool local_available(void);
const char *local_my_name(void);
// Avatar key for this console: Dingplay uid when signed in, else the owner's Mii.
const char *local_my_avatar_key(void);

// Lobby
int local_scan(void);           // blocking (~0.3 s); refreshes g_local_rooms; returns count
bool local_join(const LocalRoom *room, const char *passcode);

// Host
bool local_host(const char *name, int slots, const char *passcode);  // passcode NULL/"" = open
void local_set_inviting(bool on);
void local_accept(u16 node);
void local_deny(u16 node);
void local_kick(u16 node);
void local_mute(u16 node, bool muted);

// Either
void local_leave(void);         // host: close the room; guest: disconnect
void local_update(void);        // once per frame
LocalState local_state(void);
bool local_is_host(void);
bool local_in_room(void);       // HOSTING or JOINED
const char *local_room_name(void);
int local_members(LocalMember *out, int max);
int local_pending(LocalPending *out, int max);
int local_signal(void);         // 0 lost · 1 weak · 2 good
int local_uptime_s(void);
int local_msg_count(void);
bool local_muted_me(void);
bool local_send_text(const char *text);
bool local_send_draw(const Drawing *d);
bool local_send_voice(const u8 *dpv, size_t len);
// Set when a new message arrived since the last call (for the notification blip).
bool local_take_new_message_flag(void);
