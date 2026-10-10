#ifndef TTREND_OUTLINE_H
#define TTREND_OUTLINE_H

#include <stddef.h>
#include <stdint.h>

#include "../../ttrend_raster/include/ttrend_raster.h"

typedef ttrend_raster_font_t ttrend_outline_font_t;
typedef ttrend_raster_surface_t ttrend_outline_surface_t;

int ttrend_outline_font_destroy(ttrend_outline_font_t* font);
int ttrend_outline_font_load_memory(ttrend_outline_font_t* font,
                                  const void* data,
                                  size_t length,
                                  uint32_t pixel_size);
int ttrend_outline_font_load_file(ttrend_outline_font_t* font,
                                const char* path,
                                uint32_t pixel_size);
int ttrend_outline_font_load_path(ttrend_outline_font_t* font,
                                const char* path,
                                uint32_t pixel_size);
uint32_t ttrend_outline_text_width(const char* text,
                                  const ttrend_outline_font_t* font);
uint32_t ttrend_outline_text_height(const char* text,
                                  const ttrend_outline_font_t* font);
void ttrend_outline_draw_text(ttrend_outline_surface_t* surface,
                            int32_t x,
                            int32_t y,
                            const char* text,
                            uint32_t color,
                            const ttrend_outline_font_t* font);

#endif
