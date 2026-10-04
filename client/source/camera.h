#pragma once
// 3DS camera capture at DS-LCD size (256x192), RGB565, one frame at a time.
#include <3ds.h>
#include <stdbool.h>

#define CAM_W 256
#define CAM_H 192

bool camera_start(bool inner);        // outer (false) or inner (true) camera
void camera_stop(void);
void camera_flip(bool inner);
bool camera_active(void);
// Grabs the next frame into an internal RGB565 buffer; returns it (CAM_W*CAM_H u16) or NULL.
const u16 *camera_frame(void);
