// 10 · Camera. Live preview on the top screen; A/L/R takes the photo and sends it to
// the open chat, X flips front/back, B cancels. The frame is RGB565; the relay encodes
// it to JPEG so phones see it too.
#include "common.h"
#include "../api.h"
#include "../camera.h"
#include "../avatar.h"   // tex_upload_rgba
#include "../dbg.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define TEX_SZ 256

static bool s_inner;
static bool s_busy;
static C3D_Tex s_tex;
static bool s_tex_ok;
static u8 *s_rgba;          // CAM_W*CAM_H*4 scratch
static u16 *s_shot;         // copied frame to send
static bool s_have_frame;

static void enter(void *arg) {
    (void)arg;
    s_inner = false;
    s_busy = false;
    s_have_frame = false;
    if (!s_rgba) s_rgba = (u8 *)malloc(CAM_W * CAM_H * 4);
    if (!s_shot) s_shot = (u16 *)malloc(CAM_W * CAM_H * 2);
    if (!s_tex_ok) s_tex_ok = C3D_TexInit(&s_tex, TEX_SZ, TEX_SZ, GPU_RGBA8);
    camera_start(s_inner);
}

static void leave(void) {
    camera_stop();
}

static void sent(int status, cJSON *json, void *user) {
    (void)json;
    (void)user;
    s_busy = false;
    if (status != 200) app_toast(tr(S_SEND_FAILED), C_RED);
    app_back();
}

static void update(const Input *in) {
    if (s_busy) return;
    if (in->down & KEY_B) {
        app_back();
        return;
    }
    if (in->down & KEY_X) {
        s_inner = !s_inner;
        camera_flip(s_inner);
        return;
    }
    // grab a frame for the preview / shot
    const u16 *f = camera_frame();
    if (f) {
        memcpy(s_shot, f, CAM_W * CAM_H * 2);
        s_have_frame = true;
    }
    if ((in->down & (KEY_A | KEY_L | KEY_R)) && s_have_frame) {
        s_busy = true;
        camera_stop();
        api_chat_send_photo(s_shot, CAM_W, CAM_H, sent, NULL);
    }
}

static void draw_top(void) {
    ui_rect(0, 0, TOP_W, TOP_H, C_INK);
    if (s_have_frame && s_tex_ok) {
        // RGB565 -> RGBA8
        for (int i = 0; i < CAM_W * CAM_H; i++) {
            u16 p = s_shot[i];
            int r = (p >> 11) & 0x1f, g = (p >> 5) & 0x3f, b = p & 0x1f;
            s_rgba[i * 4] = (u8)((r * 255 + 15) / 31);
            s_rgba[i * 4 + 1] = (u8)((g * 255 + 31) / 63);
            s_rgba[i * 4 + 2] = (u8)((b * 255 + 15) / 31);
            s_rgba[i * 4 + 3] = 255;
        }
        tex_upload_rgba(&s_tex, s_rgba, CAM_W, CAM_H, CAM_W);
        C3D_TexSetFilter(&s_tex, GPU_LINEAR, GPU_LINEAR);
        static Tex3DS_SubTexture sub;
        sub.width = CAM_W;
        sub.height = CAM_H;
        sub.left = 0.0f;
        sub.right = (float)CAM_W / TEX_SZ;
        sub.top = 1.0f;
        sub.bottom = 1.0f - (float)CAM_H / TEX_SZ;
        C2D_Image img = {&s_tex, &sub};
        float sc = (float)TOP_H / CAM_H;  // fit height, centre horizontally (4:3 in a 5:3 screen)
        float iw = CAM_W * sc;
        C2D_DrawImageAt(img, (TOP_W - iw) / 2, 0, 0.5f, NULL, sc, sc);
    } else {
        ui_text_v(TOP_W / 2, 0, TOP_H, 12, C_STONE, ALIGN_CENTER, FONT_HEAD, "...");
    }
    ui_rrect(8, 6, 150, 16, 6, RGBA(0x000000, 150));
    ui_text_v(14, 6, 16, 9, C_WHITE, ALIGN_LEFT, FONT_HEAD, s_inner ? tr(S_CAM_INNER) : tr(S_CAM_OUTER));
}

static void draw_bottom(void) {
    ui_rect(0, 0, BOT_W, BOT_H, C_CREAM);
    ui_text(BOT_W / 2, 20, 14, C_INK, ALIGN_CENTER, FONT_HEAD, tr(S_CAM_TITLE));
    ui_text_wrap(20, 60, BOT_W - 40, 11, C_MUTED, ALIGN_CENTER, FONT_BODY, tr(S_CAM_HINT), 3, 0);
    if (s_busy) ui_text(BOT_W / 2, BOT_H - 30, 12, C_GREEN, ALIGN_CENTER, FONT_HEAD, tr(S_CAM_SENDING));
}

const ScreenVTable SCREEN_CAMERA = {enter, leave, update, draw_top, draw_bottom};
