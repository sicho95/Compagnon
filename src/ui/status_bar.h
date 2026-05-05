#pragma once
#include <lvgl.h>
void ui_status_bar_init();
void ui_status_bar_tick();
void ui_status_bar_set_wifi(bool ok);
void ui_status_bar_set_ble(bool ok);
