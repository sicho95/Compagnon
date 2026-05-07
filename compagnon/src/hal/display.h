#pragma once
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

extern Arduino_DataBus *gfx_bus;
extern Arduino_CO5300  *gfx;

void          hal_display_init();
lv_display_t *hal_display_get();
