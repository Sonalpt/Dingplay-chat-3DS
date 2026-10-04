#include "camera.h"
#include "dbg.h"
#include <malloc.h>
#include <string.h>

#define BUFSZ (CAM_W * CAM_H * 2)

static bool s_active, s_inner, s_cam_inited;
static u16 *s_buf;
static Handle s_recv, s_err;
static u32 s_unit;

bool camera_active(void) { return s_active; }

static u32 sel(void) { return s_inner ? SELECT_IN1 : SELECT_OUT1; }

bool camera_start(bool inner) {
    camera_stop();
    s_inner = inner;
    if (!s_buf) s_buf = (u16 *)linearAlloc(BUFSZ);
    if (!s_buf) return false;
    if (!s_cam_inited) {
        if (R_FAILED(camInit())) return false;
        s_cam_inited = true;
    }
    u32 cam = sel();
    CAMU_SetSize(cam, SIZE_DS_LCD, CONTEXT_A);
    CAMU_SetOutputFormat(cam, OUTPUT_RGB_565, CONTEXT_A);
    CAMU_SetNoiseFilter(cam, true);
    CAMU_SetAutoExposure(cam, true);
    CAMU_SetAutoWhiteBalance(cam, true);
    CAMU_SetTrimming(PORT_CAM1, false);
    if (R_FAILED(CAMU_GetMaxBytes(&s_unit, CAM_W, CAM_H))) s_unit = CAM_W * 2;
    CAMU_SetTransferBytes(PORT_CAM1, s_unit, CAM_W, CAM_H);
    CAMU_Activate(cam);
    CAMU_ClearBuffer(PORT_CAM1);
    CAMU_GetBufferErrorInterruptEvent(&s_err, PORT_CAM1);
    CAMU_StartCapture(PORT_CAM1);
    s_active = true;
    dbg_log("camera: start %s unit=%lu", inner ? "inner" : "outer", (unsigned long)s_unit);
    return true;
}

const u16 *camera_frame(void) {
    if (!s_active) return NULL;
    if (R_FAILED(CAMU_SetReceiving(&s_recv, s_buf, PORT_CAM1, BUFSZ, (s16)s_unit))) return NULL;
    Result rc = svcWaitSynchronization(s_recv, 300 * 1000 * 1000);  // 300 ms
    svcCloseHandle(s_recv);
    s_recv = 0;
    if (R_FAILED(rc)) {
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_StartCapture(PORT_CAM1);
        return NULL;
    }
    return s_buf;
}

void camera_flip(bool inner) { camera_start(inner); }

void camera_stop(void) {
    if (!s_active) return;
    CAMU_StopCapture(PORT_CAM1);
    CAMU_Activate(SELECT_NONE);
    if (s_recv) { svcCloseHandle(s_recv); s_recv = 0; }
    if (s_err) { svcCloseHandle(s_err); s_err = 0; }
    s_active = false;
}
