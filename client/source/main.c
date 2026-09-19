// Dingplay Chat — standalone foreground homebrew for the 3DS / 2DS family.
// Top screen 400×240 is display only; all touch lives on the bottom 320×240.
#include "app.h"
#include "api.h"
#include "avatar.h"
#include "dbg.h"
#include "local/udsnet.h"
#include "net.h"
#include "store.h"
#include "ui.h"
#include "voice.h"
#include <stdio.h>
#include <string.h>

Settings g_settings;
Session g_session;
bool g_wifi;
int g_wifi_bars;
bool g_new3ds;
float g_time;
C3D_RenderTarget *g_top, *g_bottom;

extern const ScreenVTable SCREEN_BOOT, SCREEN_LOGIN, SCREEN_HOME, SCREEN_FRIENDS, SCREEN_CHAT, SCREEN_LOBBY, SCREEN_CREATE_ROOM,
    SCREEN_LOCAL_CHAT, SCREEN_MANAGE_ROOM, SCREEN_SETTINGS;

const ScreenVTable *const SCREENS[SCR_COUNT] = {
    [SCR_BOOT] = &SCREEN_BOOT,
    [SCR_LOGIN] = &SCREEN_LOGIN,
    [SCR_HOME] = &SCREEN_HOME,
    [SCR_FRIENDS] = &SCREEN_FRIENDS,
    [SCR_CHAT] = &SCREEN_CHAT,
    [SCR_LOBBY] = &SCREEN_LOBBY,
    [SCR_CREATE_ROOM] = &SCREEN_CREATE_ROOM,
    [SCR_LOCAL_CHAT] = &SCREEN_LOCAL_CHAT,
    [SCR_MANAGE_ROOM] = &SCREEN_MANAGE_ROOM,
    [SCR_SETTINGS] = &SCREEN_SETTINGS,
};

// ---- Navigation --------------------------------------------------------------------------------

#define NAV_DEPTH 8
static ScreenId s_stack[NAV_DEPTH];
static int s_depth;
static bool s_quit;
static bool s_transitioning;  // guards against nested enter/leave inside update

static const char *const SCREEN_NAMES[SCR_COUNT] = {"boot", "login", "home", "friends", "chat", "lobby", "create_room", "local_chat", "manage_room", "settings"};

static void enter_screen(ScreenId id, void *arg) {
    dbg_log("screen -> %s (depth %d)", SCREEN_NAMES[id], s_depth);
    s_transitioning = true;
    SCREENS[id]->enter(arg);
    s_transitioning = false;
}

void app_go(ScreenId id, void *arg) {
    if (s_depth > 0) SCREENS[s_stack[s_depth - 1]]->leave();
    if (s_depth >= NAV_DEPTH) {
        memmove(s_stack, s_stack + 1, sizeof(ScreenId) * (NAV_DEPTH - 1));
        s_depth = NAV_DEPTH - 1;
    }
    s_stack[s_depth++] = id;
    enter_screen(id, arg);
}

void app_replace(ScreenId id, void *arg) {
    if (s_depth > 0) {
        SCREENS[s_stack[s_depth - 1]]->leave();
        s_stack[s_depth - 1] = id;
    } else {
        s_stack[s_depth++] = id;
    }
    enter_screen(id, arg);
}

void app_back(void) {
    if (s_depth <= 1) {
        // Home is the root; B on Home does nothing (START quits).
        if (s_depth == 1 && s_stack[0] != SCR_HOME) app_reset_to(SCR_HOME, NULL);
        return;
    }
    SCREENS[s_stack[s_depth - 1]]->leave();
    s_depth--;
    enter_screen(s_stack[s_depth - 1], NULL);
}

void app_reset_to(ScreenId id, void *arg) {
    if (s_depth > 0) SCREENS[s_stack[s_depth - 1]]->leave();
    s_depth = 0;
    s_stack[s_depth++] = id;
    enter_screen(id, arg);
}

ScreenId app_current(void) { return s_depth ? s_stack[s_depth - 1] : SCR_BOOT; }
void app_quit(void) { s_quit = true; }

// ---- Toast ------------------------------------------------------------------------------------------

static char s_toast[128];
static u32 s_toast_color;
static float s_toast_ttl;

void app_toast(const char *text, u32 color) {
    strncpy(s_toast, text, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = 0;
    s_toast_color = color;
    s_toast_ttl = 2.6f;
}

static void draw_toast(void) {
    if (s_toast_ttl <= 0) return;
    float a = s_toast_ttl < 0.4f ? s_toast_ttl / 0.4f : 1.0f;
    float w = ui_text_width(11, FONT_HEAD, s_toast) + 24;
    if (w > BOT_W - 20) w = BOT_W - 20;
    float x = (BOT_W - w) / 2, y = BOT_H - 30 - 34 * a;
    ui_card(x, y, w, 30, 10, 2, s_toast_color, C_INK, 0, 3, C_INK);
    char t[128];
    ui_ellipsize(t, sizeof(t), 11, FONT_HEAD, s_toast, w - 20);
    ui_text_v(x + w / 2, y, 30, 11, C_WHITE, ALIGN_CENTER, FONT_HEAD, t);
}

// ---- Services ---------------------------------------------------------------------------------------

static bool s_ac_ok, s_ptmu_ok;

static void wifi_poll(void) {
    static float timer = 1.0f;
    timer += 1.0f / 60.0f;
    if (timer < 1.0f) return;
    timer = 0;
    u32 status = 0;
    if (s_ac_ok && R_SUCCEEDED(ACU_GetWifiStatus(&status))) g_wifi = status != 0;
    else g_wifi = false;
    g_wifi_bars = osGetWifiStrength();
}

static void services_init(void) {
    romfsInit();
    store_init();
    dbg_init();
    store_load_settings(&g_settings);
    dbg_log("relay=%s lang=%d token=%s", g_settings.relay, g_settings.lang, g_settings.token[0] ? "yes" : "no");
    i18n_set(g_settings.lang);
    if (store_load_token(g_settings.token, sizeof(g_settings.token))) {
        // validated by the boot screen's GET /me; cleared there if stale
    }
    s_ac_ok = R_SUCCEEDED(acInit());
    s_ptmu_ok = R_SUCCEEDED(ptmuInit());
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    g_new3ds = n3ds;
    if (g_new3ds) osSetSpeedupEnable(true);
    avatar_init();
    net_init();
    api_init();
    voice_init();
    memset(&g_session, 0, sizeof(g_session));
    g_session.logged_in = false;
}

static void services_exit(void) {
    local_exit();
    voice_exit();
    net_exit();
    avatar_exit();
    if (s_ptmu_ok) ptmuExit();
    if (s_ac_ok) acExit();
    romfsExit();
}

// ---- Main -----------------------------------------------------------------------------------------------

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    g_bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    services_init();
    ui_init();
    app_reset_to(SCR_BOOT, NULL);

    Input in;
    memset(&in, 0, sizeof(in));
    u64 last_tick = svcGetSystemTick();

    while (aptMainLoop() && !s_quit) {
        hidScanInput();
        u64 now = svcGetSystemTick();
        in.dt = (float)(now - last_tick) / (float)SYSCLOCK_ARM11;
        if (in.dt > 0.1f) in.dt = 0.1f;
        last_tick = now;
        g_time += in.dt;

        in.down = hidKeysDown();
        in.held = hidKeysHeld();
        in.up = hidKeysUp();
        in.touch_down = (in.down & KEY_TOUCH) != 0;
        in.touch_up = (in.up & KEY_TOUCH) != 0;
        in.touching = (in.held & KEY_TOUCH) != 0;
        if (in.touching) {
            hidTouchRead(&in.touch);  // keep the last position on release
            if (in.touch_down) in.touch_start = in.touch;
        }
        if (in.touch_up) dbg_log("tap up at %d,%d (down at %d,%d) on %s", in.touch.px, in.touch.py, in.touch_start.px, in.touch_start.py, SCREEN_NAMES[app_current()]);
        if (in.down & ~KEY_TOUCH) dbg_log("keys down 0x%08lx on %s", (unsigned long)(in.down & ~KEY_TOUCH), SCREEN_NAMES[app_current()]);
        if (in.down & KEY_START && app_current() != SCR_HOME && app_current() != SCR_BOOT) {
            // START anywhere but Home: go home first (Home's START quits).
            app_reset_to(SCR_HOME, NULL);
        }

        wifi_poll();
        net_pump();
        voice_update();
        local_update();
        if (s_toast_ttl > 0) s_toast_ttl -= in.dt;

        ScreenId cur = app_current();
        SCREENS[cur]->update(&in);
        cur = app_current();

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_frame_begin();
        C2D_TargetClear(g_top, C_CREAM);
        C2D_SceneBegin(g_top);
        SCREENS[cur]->draw_top();
        C2D_TargetClear(g_bottom, C_CREAM);
        C2D_SceneBegin(g_bottom);
        SCREENS[cur]->draw_bottom();
        draw_toast();
        C3D_FrameEnd(0);
    }

    if (s_depth > 0) SCREENS[s_stack[s_depth - 1]]->leave();
    store_save_settings(&g_settings);
    ui_exit();
    services_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
