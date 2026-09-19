#pragma once
// Append-only debug log on the SD card (sdmc:/3ds/dingplay-chat/debug.log) so a
// session on the emulator or console can be reconstructed afterwards.
void dbg_init(void);
void dbg_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
