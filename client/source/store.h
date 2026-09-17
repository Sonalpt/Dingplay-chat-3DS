#pragma once
// SD card persistence: settings, cached session token, recent messages per room.
// Everything lives under sdmc:/3ds/dingplay-chat/. Local wireless rooms never persist.
#include "app.h"

void store_init(void);
void store_load_settings(Settings *s);
void store_save_settings(const Settings *s);
void store_save_token(const char *token);
void store_clear_token(void);
bool store_load_token(char *out, size_t n);
// Recently synced messages so a chat doesn't open blank while the first poll runs.
void store_cache_messages(const char *room, const MessageList *l);
bool store_load_cached(const char *room, MessageList *l);
