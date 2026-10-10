#ifndef TTREND_H
#define TTREND_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t scale;
    uint32_t tracking;
    uint32_t bold;
    uint32_t italic;
    uint32_t pixel_size;
    const uint8_t* data;
    size_t data_length;
    uint32_t cmap_offset;
    uint32_t cmap_length;
    uint32_t glyf_offset;
    uint32_t glyf_length;
    uint32_t loca_offset;
    uint32_t loca_length;
    uint32_t hmtx_offset;
    uint32_t hmtx_length;
    uint32_t units_per_em;
    uint32_t glyph_count;
    uint32_t horizontal_metrics;
    int32_t ascent;
    int32_t descent;
    int32_t line_gap;
    int32_t loca_format;
    void* owned_data;
    size_t owned_size;
} ttrend_font_t;

typedef struct {
    uint32_t* pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} ttrend_surface_t;

/* The font data remains owned by the caller and must stay alive while used,
 * unless it was loaded from a path via ttrend_font_load_file(). */
int ttrend_font_destroy(ttrend_font_t* font);
int ttrend_font_load_memory(ttrend_font_t* font, const void* data,
                            size_t length, uint32_t pixel_size);
int ttrend_font_load_file(ttrend_font_t* font, const char* path,
                          uint32_t pixel_size);
int ttrend_font_load_path(ttrend_font_t* font, const char* path,
                          uint32_t pixel_size);
uint32_t ttrend_text_width(const char* text, const ttrend_font_t* font);
uint32_t ttrend_text_height(const char* text, const ttrend_font_t* font);
void ttrend_draw_text(ttrend_surface_t* surface, int32_t x, int32_t y,
                      const char* text, uint32_t color,
                      const ttrend_font_t* font);

#endif