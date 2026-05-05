#pragma once
typedef enum {
    ORIENT_PORTRAIT = 0, ORIENT_LANDSCAPE_L, ORIENT_PORTRAIT_INV, ORIENT_LANDSCAPE_R
} ScreenOrientation;
void hal_imu_init();
void hal_imu_tick();
ScreenOrientation hal_imu_orientation();
bool hal_imu_changed();
