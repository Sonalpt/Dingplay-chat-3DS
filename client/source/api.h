#pragma once
// Relay API client. Screens call these and read the model below; results land on
// the main thread through net_pump().
#include "app.h"
#include "cJSON.h"

// Request families, for cancelling when a screen goes away.
enum { TAG_NONE = 0, TAG_BOOT, TAG_AUTH, TAG_ME, TAG_FRIENDS, TAG_CHAT_POLL, TAG_CHAT_SEND, TAG_MEDIA, TAG_ROOMS, TAG_AVATAR, TAG_MISC };

// status < 0: transport error. json may be NULL (non-JSON or empty body).
typedef void (*ApiDone)(int status, cJSON *json, void *user);

// ---- Model -------------------------------------------------------------------------------
extern MessageList g_chat;                 // the online chat currently open
extern Friend g_friends[MAX_FRIENDS];
extern int g_friends_count;
extern FriendRequest g_requests[MAX_REQUESTS];
extern int g_requests_count;
extern ThemedRoom g_rooms[MAX_THEMED_ROOMS];
extern int g_rooms_count;
extern bool g_relay_voice_transcode;       // relay has ffmpeg: phone voice notes playable

void api_init(void);
void api_apply_settings(void);             // pushes relay URL + token into net

// Boot / misc
void api_health(ApiDone done, void *user);
void api_stats(ApiDone done, void *user);
void api_news(ApiDone done, void *user);

// Auth
void api_login(const char *login, const char *password, ApiDone done, void *user);
void api_logout(void);
void api_me(ApiDone done, void *user);     // also refreshes g_session counters

// Friends
void api_friends(ApiDone done, void *user);   // fills g_friends / g_requests
void api_friend_request(const char *username, ApiDone done, void *user);
void api_friend_respond(const char *uid, bool accept, ApiDone done, void *user);

// Chat
void api_chat_open(const char *room);         // resets g_chat, loads SD cache
void api_chat_poll(ApiDone done, void *user); // GET /chat/<room>?since=
void api_chat_send_text(const char *text, ApiDone done, void *user);
void api_chat_send_draw(const Drawing *d, ApiDone done, void *user);
void api_chat_send_voice(const u8 *dpv, size_t len, ApiDone done, void *user);
// Fetches a voice note's DPV bytes into the message (msg->voice). done gets status only.
void api_media_fetch(Message *msg, ApiDone done, void *user);
const char *api_chat_room(void);

// Themed rooms
void api_rooms_list(ApiDone done, void *user);
void api_room_create(const char *name, const char *topic, ApiDone done, void *user);
void api_room_join(const char *id, ApiDone done, void *user);
void api_room_leave(const char *id);

// Message helpers shared with local mode
Message *msglist_push(MessageList *l, const Message *m);   // dedupes by id; evicts oldest
void msglist_clear(MessageList *l);
void message_free(Message *m);
DrawingRef *drawing_compact(const Drawing *d);
DrawingRef *drawing_from_json(cJSON *j);
cJSON *drawing_to_json(const Drawing *d);
void waveform_from_dpv(const u8 *dpv, size_t len, u8 bars[8]);
