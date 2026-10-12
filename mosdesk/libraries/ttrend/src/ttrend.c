#include "ttrend.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static long mos_open(const char* path, long flags) {
    return (long)open(path, flags == 0 ? O_RDONLY : O_RDWR);
}
static long mos_read(long fd, void* buf, size_t len) {
    return (long)read((int)fd, buf, len);
}
static long mos_close(long fd) {
    return (long)close((int)fd);
}
static long mos_size(long fd) {
    struct stat st;
    if (fstat((int)fd, &st) == 0) return (long)st.st_size;
    return -1;
}
#else
extern long mos_open(const char* path, long flags);
extern long mos_read(long fd, void* buf, size_t len);
extern long mos_close(long fd);
extern long mos_size(long fd);
#endif

/* Smooth OpenType/TrueType rendering lives in ttrend_outline. It is compiled
 * into this unit with internal linkage so ttrend needs no extra object. */
#define TTREND_OUTLINE_STATIC
#define TTREND_OUTLINE_NO_FILE
#include "../../ttrend_outline/src/ttrend_outline.c"

#define TTREND_MAGIC "TTRENDF"
#define TTREND_HEADER_SIZE 32u

static const uint8_t glyphs[128][7] = {
    ['!'] = {4, 4, 4, 4, 4, 0, 4}, ['"'] = {10, 10, 10, 0, 0, 0, 0},
    ['#'] = {10, 31, 10, 10, 31, 10, 0}, ['$'] = {4, 15, 20, 14, 5, 30, 4},
    ['%'] = {24, 25, 2, 4, 8, 19, 3}, ['&'] = {12, 18, 20, 8, 21, 18, 13},
    ['\''] = {4, 4, 8, 0, 0, 0, 0}, ['('] = {2, 4, 8, 8, 8, 4, 2},
    [')'] = {8, 4, 2, 2, 2, 4, 8}, ['*'] = {0, 21, 14, 31, 14, 21, 0},
    ['+'] = {0, 4, 4, 31, 4, 4, 0}, [','] = {0, 0, 0, 0, 4, 4, 8},
    ['-'] = {0, 0, 0, 31, 0, 0, 0}, ['.'] = {0, 0, 0, 0, 0, 4, 4},
    ['/'] = {1, 2, 2, 4, 8, 8, 16}, ['0'] = {14, 17, 19, 21, 25, 17, 14},
    ['1'] = {4, 12, 4, 4, 4, 4, 14}, ['2'] = {14, 17, 1, 2, 4, 8, 31},
    ['3'] = {30, 1, 1, 14, 1, 1, 30}, ['4'] = {2, 6, 10, 18, 31, 2, 2},
    ['5'] = {31, 16, 16, 30, 1, 1, 30}, ['6'] = {14, 16, 16, 30, 17, 17, 14},
    ['7'] = {31, 1, 2, 4, 8, 8, 8}, ['8'] = {14, 17, 17, 14, 17, 17, 14},
    ['9'] = {14, 17, 17, 15, 1, 1, 14}, [':'] = {0, 4, 4, 0, 4, 4, 0},
    [';'] = {0, 4, 4, 0, 4, 4, 8}, ['<'] = {2, 4, 8, 16, 8, 4, 2},
    ['='] = {0, 31, 0, 31, 0, 0, 0}, ['>'] = {8, 4, 2, 1, 2, 4, 8},
    ['?'] = {14, 17, 1, 2, 4, 0, 4}, ['@'] = {14, 17, 23, 21, 23, 16, 14},
    ['A'] = {14, 17, 17, 31, 17, 17, 17}, ['B'] = {30, 17, 17, 30, 17, 17, 30},
    ['C'] = {14, 17, 16, 16, 16, 17, 14}, ['D'] = {30, 17, 17, 17, 17, 17, 30},
    ['E'] = {31, 16, 16, 30, 16, 16, 31}, ['F'] = {31, 16, 16, 30, 16, 16, 16},
    ['G'] = {14, 17, 16, 23, 17, 17, 15}, ['H'] = {17, 17, 17, 31, 17, 17, 17},
    ['I'] = {14, 4, 4, 4, 4, 4, 14}, ['J'] = {7, 2, 2, 2, 2, 18, 12},
    ['K'] = {17, 18, 20, 24, 20, 18, 17}, ['L'] = {16, 16, 16, 16, 16, 16, 31},
    ['M'] = {17, 27, 21, 21, 17, 17, 17}, ['N'] = {17, 25, 21, 19, 17, 17, 17},
    ['O'] = {14, 17, 17, 17, 17, 17, 14}, ['P'] = {30, 17, 17, 30, 16, 16, 16},
    ['Q'] = {14, 17, 17, 17, 21, 18, 13}, ['R'] = {30, 17, 17, 30, 20, 18, 17},
    ['S'] = {15, 16, 16, 14, 1, 1, 30}, ['T'] = {31, 4, 4, 4, 4, 4, 4},
    ['U'] = {17, 17, 17, 17, 17, 17, 14}, ['V'] = {17, 17, 17, 17, 17, 10, 4},
    ['W'] = {17, 17, 17, 21, 21, 21, 10}, ['X'] = {17, 17, 10, 4, 10, 17, 17},
    ['Y'] = {17, 17, 10, 4, 4, 4, 4}, ['Z'] = {31, 1, 2, 4, 8, 16, 31},
    ['['] = {14, 8, 8, 8, 8, 8, 14}, ['\\'] = {16, 8, 8, 4, 2, 2, 1},
    [']'] = {14, 2, 2, 2, 2, 2, 14}, ['^'] = {4, 10, 17, 0, 0, 0, 0},
    ['_'] = {0, 0, 0, 0, 0, 0, 31}, ['`'] = {8, 4, 2, 0, 0, 0, 0},
    ['{'] = {2, 4, 4, 8, 4, 4, 2}, ['|'] = {4, 4, 4, 4, 4, 4, 4},
    ['}'] = {8, 4, 4, 2, 4, 4, 8}, ['~'] = {0, 0, 9, 22, 0, 0, 0}
};

static uint32_t read_u32le(const uint8_t* ptr) {
    if (!ptr) return 0u;
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8u) |
           ((uint32_t)ptr[2] << 16u) | ((uint32_t)ptr[3] << 24u);
}

int ttrend_font_is_vector(const ttrend_font_t* font) {
    return font && font->vector != NULL;
}

/* Outline-font view of `font` at its current style (cheap struct copy, so
 * copies of a ttrend_font_t can each use their own pixel_size). */
static ttrend_outline_font_t vector_view(const ttrend_font_t* font) {
    ttrend_outline_font_t v = *(const ttrend_outline_font_t*)font->vector;
    v.pixel_size = font->pixel_size ? font->pixel_size : 12u;
    v.tracking = font->tracking;
    v.italic = font->italic;
    return v;
}

int ttrend_font_destroy(ttrend_font_t* font) {
    if (!font) return 0;
    if (font->vector) {
        free(font->vector);
        font->vector = NULL;
    }
    if (font->owned_data) {
        free(font->owned_data);
        font->owned_data = NULL;
    }
    font->owned_size = 0u;
    memset(font, 0, sizeof(*font));
    return 0;
}

static uint32_t font_scale(const ttrend_font_t* font) {
    return font && font->scale ? font->scale : 1u;
}

static int font_has_custom_data(const ttrend_font_t* font) {
    return font && font->data && font->data_length >= TTREND_HEADER_SIZE &&
           memcmp(font->data, TTREND_MAGIC, 7u) == 0;
}

static uint32_t font_custom_cell_width(const ttrend_font_t* font) {
    if (!font_has_custom_data(font)) return 5u;
    return read_u32le((const uint8_t*)font->data + 20u);
}

static uint32_t font_custom_cell_height(const ttrend_font_t* font) {
    if (!font_has_custom_data(font)) return 7u;
    return read_u32le((const uint8_t*)font->data + 24u);
}

static uint32_t font_custom_advance(const ttrend_font_t* font) {
    if (!font_has_custom_data(font)) return 6u;
    return read_u32le((const uint8_t*)font->data + 28u);
}

static const uint8_t* font_custom_glyph_ptr(const ttrend_font_t* font,
                                            unsigned char ch,
                                            uint32_t glyph_count,
                                            uint32_t bytes_per_glyph) {
    if (!font_has_custom_data(font) || ch >= glyph_count) return NULL;
    const uint8_t* data = (const uint8_t*)font->data;
    size_t offset = TTREND_HEADER_SIZE + (size_t)ch * (size_t)bytes_per_glyph;
    if (offset + bytes_per_glyph > font->data_length) return NULL;
    return data + offset;
}

int ttrend_font_load_memory(ttrend_font_t* font, const void* data,
                            size_t length, uint32_t pixel_size) {
    if (!font || !data || length < 8u) return -1;
    if (font->owned_data) {
        free(font->owned_data);
        font->owned_data = NULL;
        font->owned_size = 0u;
    }
    if (font->vector) {
        free(font->vector);
        font->vector = NULL;
    }
    memset(font, 0, sizeof(*font));

    if (ttrend_outline_is_sfnt(data, length)) {
        ttrend_outline_font_t* vec =
            (ttrend_outline_font_t*)malloc(sizeof(*vec));
        if (!vec) return -1;
        if (ttrend_outline_font_load_memory(vec, data, length,
                                            pixel_size ? pixel_size : 12u)
            != 0) {
            free(vec);
            return -1;
        }
        font->vector = vec;
        font->scale = 1u;
        font->pixel_size = vec->pixel_size;
        font->data = (const uint8_t*)data;
        font->data_length = length;
        font->units_per_em = vec->units_per_em;
        font->glyph_count = vec->glyph_count;
        font->ascent = vec->ascent;
        font->descent = vec->descent;
        font->line_gap = vec->line_gap;
        return 0;
    }
    font->scale = pixel_size ? pixel_size : 1u;
    font->tracking = 0u;
    font->bold = 0u;
    font->italic = 0u;
    font->pixel_size = pixel_size ? pixel_size : 1u;
    font->data = (const uint8_t*)data;
    font->data_length = length;

    if (memcmp(data, TTREND_MAGIC, 7u) == 0) {
        if (length < TTREND_HEADER_SIZE) return -1;
        const uint8_t* bytes = (const uint8_t*)data;
        uint32_t version = read_u32le(bytes + 8u);
        uint32_t glyph_count = read_u32le(bytes + 12u);
        uint32_t bytes_per_glyph = read_u32le(bytes + 16u);
        uint32_t cell_width = read_u32le(bytes + 20u);
        uint32_t cell_height = read_u32le(bytes + 24u);
        uint32_t advance = read_u32le(bytes + 28u);
        if (version != 1u || glyph_count == 0u || bytes_per_glyph == 0u ||
            cell_width == 0u || cell_height == 0u || advance == 0u)
            return -1;
        size_t required = TTREND_HEADER_SIZE + (size_t)glyph_count *
                                              (size_t)bytes_per_glyph;
        if (length < required) return -1;
        font->glyph_count = glyph_count;
        font->cmap_offset = TTREND_HEADER_SIZE;
        font->cmap_length = (uint32_t)(required - TTREND_HEADER_SIZE);
        font->glyf_offset = TTREND_HEADER_SIZE;
        font->glyf_length = (uint32_t)(required - TTREND_HEADER_SIZE);
        font->units_per_em = cell_height;
        font->horizontal_metrics = advance;
        font->ascent = (int32_t)cell_height;
        font->descent = 0;
        font->line_gap = 1;
        return 0;
    }

    /* Generic fallback for raw 128x7 glyph tables stored in memory. */
    if (length >= 128u * 7u) {
        font->glyph_count = 128u;
        font->units_per_em = 7u;
        font->horizontal_metrics = 6u;
        font->ascent = 7;
        font->descent = 0;
        font->line_gap = 1;
        return 0;
    }

    return -1;
}

int ttrend_font_load_file(ttrend_font_t* font, const char* path,
                          uint32_t pixel_size) {
    if (!font || !path) return -1;
    long fd = mos_open(path, 0);
    if (fd < 0) return -1;
    long file_size = mos_size(fd);
    if (file_size <= 0) {
        mos_close(fd);
        return -1;
    }
    uint8_t* buffer = (uint8_t*)malloc((size_t)file_size);
    if (!buffer) {
        mos_close(fd);
        return -1;
    }
    size_t total = 0u;
    while (total < (size_t)file_size) {
        long got = mos_read(fd, buffer + total, (size_t)file_size - total);
        if (got <= 0) break;
        total += (size_t)got;
    }
    mos_close(fd);
    if (total != (size_t)file_size) {
        free(buffer);
        return -1;
    }
    if (ttrend_font_load_memory(font, buffer, total, pixel_size) != 0) {
        free(buffer);
        return -1;
    }
    font->owned_data = buffer;
    font->owned_size = total;
    return 0;
}

int ttrend_font_load_path(ttrend_font_t* font, const char* path,
                          uint32_t pixel_size) {
    return ttrend_font_load_file(font, path, pixel_size);
}

uint32_t ttrend_text_width(const char* text, const ttrend_font_t* font) {
    if (!text) return 0u;
    if (font && font->vector) {
        ttrend_outline_font_t v = vector_view(font);
        return ttrend_outline_text_width(text, &v);
    }
    uint32_t scale = font_scale(font);
    uint32_t tracking = font ? font->tracking : 0u;
    uint32_t max_width = 0u;
    uint32_t line_width = 0u;

    if (font_has_custom_data(font)) {
        uint32_t advance = font_custom_advance(font);
        for (; *text; text++) {
            if (*text == '\n') {
                if (line_width > max_width) max_width = line_width;
                line_width = 0u;
            } else {
                line_width += advance * scale + tracking;
            }
        }
        return line_width > max_width ? line_width : max_width;
    }

    for (; *text; text++) {
        if (*text == '\n') {
            if (line_width > max_width) max_width = line_width;
            line_width = 0u;
        } else {
            line_width += 6u * scale + tracking;
        }
    }
    return line_width > max_width ? line_width : max_width;
}

uint32_t ttrend_text_height(const char* text, const ttrend_font_t* font) {
    if (!text || !*text) return 0u;
    if (font && font->vector) {
        ttrend_outline_font_t v = vector_view(font);
        return ttrend_outline_text_height(text, &v);
    }
    uint32_t scale = font_scale(font);
    uint32_t lines = 1u;
    uint32_t height = font_has_custom_data(font) ?
        font_custom_cell_height(font) : 9u;
    for (; *text; text++) if (*text == '\n') lines++;
    return lines * height * scale;
}

void ttrend_draw_text(ttrend_surface_t* surface, int32_t x, int32_t y,
                      const char* text, uint32_t color,
                      const ttrend_font_t* font) {
    if (!surface || !surface->pixels || !text || !surface->stride) return;

    if (font && font->vector) {
        ttrend_outline_font_t v = vector_view(font);
        ttrend_outline_surface_t target = {surface->pixels, surface->width,
                                           surface->height, surface->stride};
        /* y is the top of capital letters, matching the bitmap fonts. */
        ttrend_outline_draw_text_baseline(&target, x,
            y + ttrend_outline_cap_height(&v), text, color, &v);
        return;
    }

    if (font_has_custom_data(font)) {
        uint32_t scale = font_scale(font);
        uint32_t tracking = font ? font->tracking : 0u;
        uint32_t bold = font ? font->bold : 0u;
        uint32_t italic = font ? font->italic : 0u;
        uint32_t glyph_count = font->glyph_count ? font->glyph_count : 128u;
        uint32_t bytes_per_glyph = 7u;
        int32_t origin_x = x;
        for (; *text; text++) {
            unsigned char ch = (unsigned char)*text;
            if (ch == '\n') {
                x = origin_x;
                y += (int32_t)(font_custom_cell_height(font) * scale);
                continue;
            }
            if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
            if (ch >= glyph_count) ch = '?';
            const uint8_t* glyph = font_custom_glyph_ptr(font, ch, glyph_count,
                                                        bytes_per_glyph);
            if (!glyph) glyph = font_custom_glyph_ptr(font, '?', glyph_count,
                                                     bytes_per_glyph);
            if (!glyph) continue;
            for (uint32_t row = 0u; row < font_custom_cell_height(font); row++) {
                uint8_t bits = glyph[row];
                int32_t slant = italic ? (int32_t)((int32_t)font_custom_cell_height(font) -
                                                 (int32_t)row) * (int32_t)scale / 3 : 0;
                for (uint32_t col = 0u; col < font_custom_cell_width(font); col++) {
                    if ((bits & (1u << (font_custom_cell_width(font) - 1u - col))) == 0u)
                        continue;
                    int32_t px = x + slant + (int32_t)col * (int32_t)scale;
                    int32_t py = y + (int32_t)row * (int32_t)scale;
                    for (uint32_t sy = 0u; sy < scale; sy++) {
                        for (uint32_t sx = 0u; sx < scale + bold; sx++) {
                            int32_t draw_x = px + (int32_t)sx;
                            int32_t draw_y = py + (int32_t)sy;
                            if (draw_x >= 0 && draw_y >= 0 &&
                                (uint32_t)draw_x < surface->width &&
                                (uint32_t)draw_y < surface->height)
                                surface->pixels[(size_t)draw_y * surface->stride +
                                                (uint32_t)draw_x] = color;
                        }
                    }
                }
            }
            x += (int32_t)(font_custom_advance(font) * scale + tracking);
        }
        return;
    }

    uint32_t scale = font_scale(font);
    uint32_t tracking = font ? font->tracking : 0u;
    uint32_t bold = font ? font->bold : 0u;
    uint32_t italic = font ? font->italic : 0u;
    int32_t origin_x = x;
    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if (ch == '\n') {
            x = origin_x;
            y += (int32_t)(9 * scale);
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
        if (ch < 32 || ch >= 128) ch = '?';
        for (int32_t row = 0; row < 7; row++) {
            int32_t slant = italic ? (int32_t)((6 - row) * scale / 3) : 0;
            uint8_t bits = glyphs[ch][row];
            for (int32_t col = 0; col < 5; col++) {
                if ((bits & (1u << (4 - col))) == 0) continue;
                int32_t px = x + slant + col * (int32_t)scale;
                int32_t py = y + row * (int32_t)scale;
                for (uint32_t sy = 0; sy < scale; sy++) {
                    for (uint32_t sx = 0; sx < scale + bold; sx++) {
                        int32_t draw_x = px + (int32_t)sx;
                        int32_t draw_y = py + (int32_t)sy;
                        if (draw_x >= 0 && draw_y >= 0 &&
                            (uint32_t)draw_x < surface->width &&
                            (uint32_t)draw_y < surface->height)
                            surface->pixels[(size_t)draw_y * surface->stride +
                                            (uint32_t)draw_x] = color;
                    }
                }
            }
        }
        x += (int32_t)(6 * scale + tracking);
    }
}