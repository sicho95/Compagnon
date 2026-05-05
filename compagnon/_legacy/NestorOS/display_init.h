#pragma once
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "pin_config.h"

#define SCREEN_W  LCD_WIDTH
#define SCREEN_H  LCD_HEIGHT

extern Arduino_CO5300 *gfx;   // expose pour pwr_button (setBrightness/displayOff)

void display_init();
