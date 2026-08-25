#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)
#include <lvgl.h>
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_MOWER_220X99X1
#define LV_ATTRIBUTE_MOWER_220X99X1
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_MOWER_220X99X1
uint8_t mower_220x99x1_map[] = {
    // Palette: 2 Farben à 4 Bytes
    0x00, 0x00, 0x00, 0xFF,   // Farbe 0 (schwarz)
    0xFF, 0xFF, 0xFF, 0xFF,   // Farbe 1 (weiß)

    // Pixeldaten: 2 Zeilen * 4 Bytes = 8 Bytes
    0x00,0x00,0x00,0xff,
    0xff,0x80,0x00,0x55
};

const lv_image_dsc_t mower_220x99x1 = {
  .header = {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I1,
    .flags = 0,
    .w = 32,
    .h = 2,
    .stride = 4,
    .reserved_2 = 0,
  },
  .data_size = sizeof(mower_220x99x1_map),
  .data = mower_220x99x1_map,
  .reserved = NULL,
};

