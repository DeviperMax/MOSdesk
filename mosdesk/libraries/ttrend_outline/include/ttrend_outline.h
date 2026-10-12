#ifndef TTREND_OUTLINE_H
#define TTREND_OUTLINE_H

/* ttrend_outline - smooth (anti-aliased) OpenType/TrueType text renderer.
 *
 * Reads .otf (CFF outlines) and .ttf (glyf outlines, incl. composite glyphs)
 * straight from memory and rasterises them with exact-area coverage, so text
 * is smooth at any pixel size instead of being stamped from a bitmap font.
 *
 *  - integer / fixed-point only: no floating point, no libm, no malloc in the
 *    renderer itself, so it is safe in freestanding Minimal-OS programs
 *  - sub-pixel (1/4 px) glyph positioning with fractional advances
 *  - UTF-8 input, cmap formats 4 and 12
 *  - small glyph-coverage cache, so redrawing a desktop every frame is cheap
 *
 * Not implemented: hinting, kerning/GPOS/GSUB, CID-keyed CFF fonts, variable
 * fonts, colour glyphs.  ttrend_outline_font_load_*() returns -1 for anything
 * it cannot render so callers can fall back to the bitmap renderer.
 *
 * Consumers that cannot link another object (mosWM) may #define
 * TTREND_OUTLINE_STATIC and #include the .c file; every API symbol then gets
 * internal linkage.  #define TTREND_OUTLINE_NO_FILE to drop the file loader
 * (and its dependency on the mos_open/mos_read externs and malloc). */

#include <stddef.h>
#include <stdint.h>

#ifdef TTREND_OUTLINE_STATIC
#if defined(__GNUC__)
#define TTREND_OUTLINE_API static __attribute__((unused))
#else
#define TTREND_OUTLINE_API static
#endif
#else
#define TTREND_OUTLINE_API
#endif

typedef struct {
    uint32_t* pixels;   /* ARGB8888 */
    uint32_t width;
    uint32_t height;
    uint32_t stride;    /* in pixels */
} ttrend_outline_surface_t;

typedef struct {
    /* style: set after loading, may be changed freely between draw calls */
    uint32_t pixel_size;   /* em size in pixels (e.g. 12, 16); max 96 */
    uint32_t tracking;     /* extra pixels between glyphs */
    uint32_t italic;       /* non-zero: synthetic slant (~11 degrees) */

    /* parsed font (read-only for callers) */
    const uint8_t* data;
    size_t data_length;
    void* owned_data;      /* freed by _destroy when loaded via _load_file */
    size_t owned_size;
    uint32_t units_per_em;
    uint32_t glyph_count;
    uint32_t hmetric_count;
    int32_t ascent;        /* font units, from hhea */
    int32_t descent;       /* font units, positive */
    int32_t line_gap;
    int32_t cap_height;    /* font units, 0 if the font does not say */
    uint32_t hmtx_offset;
    uint32_t cmap_offset;  /* chosen cmap subtable */
    uint32_t cmap_format;
    uint32_t glyf_offset;  /* TrueType outlines (0 when CFF) */
    uint32_t loca_offset;
    uint32_t loca_long;
    uint32_t loca_length;
    uint32_t glyf_length;
    uint32_t cff_charstrings;  /* CFF outlines: offset of CharStrings INDEX */
    uint32_t cff_gsubrs;
    uint32_t cff_lsubrs;
    int32_t cff_is_cff;
} ttrend_outline_font_t;

/* Returns 0 on success.  `data` must stay alive while the font is used. */
TTREND_OUTLINE_API int ttrend_outline_font_load_memory(
    ttrend_outline_font_t* font, const void* data, size_t length,
    uint32_t pixel_size);
#ifndef TTREND_OUTLINE_NO_FILE
TTREND_OUTLINE_API int ttrend_outline_font_load_file(
    ttrend_outline_font_t* font, const char* path, uint32_t pixel_size);
TTREND_OUTLINE_API int ttrend_outline_font_load_path(
    ttrend_outline_font_t* font, const char* path, uint32_t pixel_size);
#endif
TTREND_OUTLINE_API int ttrend_outline_font_destroy(ttrend_outline_font_t* font);

/* Metrics at the font's current pixel_size.  Width is rounded up. */
TTREND_OUTLINE_API uint32_t ttrend_outline_text_width(
    const char* text, const ttrend_outline_font_t* font);
TTREND_OUTLINE_API uint32_t ttrend_outline_text_height(
    const char* text, const ttrend_outline_font_t* font);
TTREND_OUTLINE_API int32_t ttrend_outline_ascent(
    const ttrend_outline_font_t* font);
TTREND_OUTLINE_API int32_t ttrend_outline_descent(
    const ttrend_outline_font_t* font);
TTREND_OUTLINE_API int32_t ttrend_outline_cap_height(
    const ttrend_outline_font_t* font);
TTREND_OUTLINE_API int32_t ttrend_outline_line_height(
    const ttrend_outline_font_t* font);

/* y is the top of the line box (baseline = y + ascent).  `color` is
 * 0xAARRGGBB and is alpha-blended with the glyph coverage. */
TTREND_OUTLINE_API void ttrend_outline_draw_text(
    ttrend_outline_surface_t* surface, int32_t x, int32_t y,
    const char* text, uint32_t color, const ttrend_outline_font_t* font);
TTREND_OUTLINE_API void ttrend_outline_draw_text_baseline(
    ttrend_outline_surface_t* surface, int32_t x, int32_t baseline,
    const char* text, uint32_t color, const ttrend_outline_font_t* font);

/* Non-zero when `data` starts like an sfnt (OTF/TTF/TTC) font file. */
TTREND_OUTLINE_API int ttrend_outline_is_sfnt(const void* data, size_t length);

#endif
