#pragma once
// Pieces shared by several screens: the top-screen chat log, voice bubbles,
// the status cluster, and the argument structs screens are entered with.
#include "../app.h"
#include "../ui.h"

typedef struct {
    char room[ROOM_ID_LEN];   // "global" | "dm-<uid>" | "room-<id>"
    char title[NAME_LEN];
    char peer_uid[UID_LEN];   // dm only (avatar)
    int kind;                 // 0 global · 1 dm · 2 themed
    int count;                // people here (global / themed)
} ChatArg;

typedef enum { LOG_NAVY = 0, LOG_CREAM = 1 } LogStyle;

// Draws messages bottom-aligned inside (x, y, w, h). `selected` is the index of
// the message the D-pad cursor is on (-1 = none). Returns the number of messages
// that fit, so callers can size the cursor.
int chatlog_draw(const MessageList *l, float x, float y, float w, float h, LogStyle style, int selected);
// Index of the newest voice message, or -1.
int chatlog_last_voice(const MessageList *l);
// Move a voice-message cursor: dir -1 (older) / +1 (newer). Returns new index or -1.
int chatlog_step_voice(const MessageList *l, int current, int dir);

// Voice element (screen 08): 17 px tall on hardware. Returns its width.
float voice_bubble(float x, float y, const Message *m, bool mine, LogStyle style, float scale);

// Wi-Fi bars · clock · battery, right-aligned at (right, y).
void status_cluster(float right, float y, u32 fg);
// "DINGPLAY CHAT" 22 px ink bar with the status cluster.
void top_brand_bar(void);

// Bottom footer bar: 28/30 px sand strip with left/right hints.
void footer_bar(float y, float h, const char *left, const char *right, u32 bg, u32 fg_left, u32 fg_right);

// Hold-to-record panel (screen 07b): bars, "0:04 / 0:10", big red button, CANCEL / SEND.
typedef enum { VP_NONE, VP_SEND, VP_CANCEL } VoicePanelAction;
VoicePanelAction voice_panel_update(const Input *in, float x, float y, float w, float h);
void voice_panel_draw(float x, float y, float w, float h);
void voice_panel_reset(void);
