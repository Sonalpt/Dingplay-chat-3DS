#pragma once
// System software keyboard (swkbd). Every tap-to-type field goes through here;
// the applet briefly suspends the app, which is expected.
#include <stdbool.h>
#include <stddef.h>

typedef enum { KBD_TEXT, KBD_PASSWORD, KBD_NUMPAD, KBD_USERNAME } KbdKind;

// Returns true when the user confirmed. `buf` holds the initial text and receives the result.
bool kbd_prompt(KbdKind kind, const char *hint, char *buf, size_t n, int max_chars);
