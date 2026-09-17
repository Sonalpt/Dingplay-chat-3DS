#include "kbd.h"
#include <3ds.h>
#include <string.h>

bool kbd_prompt(KbdKind kind, const char *hint, char *buf, size_t n, int max_chars) {
    SwkbdState kb;
    SwkbdType type = kind == KBD_NUMPAD ? SWKBD_TYPE_NUMPAD : kind == KBD_USERNAME ? SWKBD_TYPE_QWERTY : SWKBD_TYPE_NORMAL;
    swkbdInit(&kb, type, 2, max_chars);
    swkbdSetInitialText(&kb, buf);
    if (hint) swkbdSetHintText(&kb, hint);
    swkbdSetFeatures(&kb, SWKBD_PREDICTIVE_INPUT);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (kind == KBD_PASSWORD) swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
    if (kind == KBD_NUMPAD) swkbdSetNumpadKeys(&kb, 0, 0);
    char tmp[512];
    if (n > sizeof(tmp)) n = sizeof(tmp);
    memset(tmp, 0, sizeof(tmp));
    SwkbdButton btn = swkbdInputText(&kb, tmp, n);
    if (btn != SWKBD_BUTTON_CONFIRM) return false;
    strncpy(buf, tmp, n - 1);
    buf[n - 1] = 0;
    return true;
}
