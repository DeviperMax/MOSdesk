#include "../include/ttrend_outline.h"

#define ttrend_raster_font_destroy ttrend_outline_font_destroy
#define ttrend_raster_font_load_memory ttrend_outline_font_load_memory
#define ttrend_raster_font_load_file ttrend_outline_font_load_file
#define ttrend_raster_font_load_path ttrend_outline_font_load_path
#define ttrend_raster_text_width ttrend_outline_text_width
#define ttrend_raster_text_height ttrend_outline_text_height
#define ttrend_raster_draw_text ttrend_outline_draw_text

#include "../../ttrend_raster/src/ttrend_raster.c"
