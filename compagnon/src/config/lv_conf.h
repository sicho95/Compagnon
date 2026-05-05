#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP       1   // Big-Endian RGB565 pour CO5300

#define LV_MEM_SIZE           (192 * 1024U)
#define LV_MEM_CUSTOM          1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

#define LV_USE_LOG             1
#define LV_LOG_LEVEL           LV_LOG_LEVEL_WARN

// Polices
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_48  1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

// Widgets
#define LV_USE_LABEL           1
#define LV_USE_BTN             1
#define LV_USE_IMG             1
#define LV_USE_ARC             1
#define LV_USE_SPINNER         1
#define LV_USE_MSGBOX          1
#define LV_USE_TEXTAREA        1
#define LV_USE_KEYBOARD        1
#define LV_USE_TABLE           1
#define LV_USE_CHART           1
#define LV_USE_SYMBOL_DEF      1

#endif
