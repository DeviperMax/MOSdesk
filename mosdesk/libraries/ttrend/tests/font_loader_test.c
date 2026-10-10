#include "../include/ttrend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_u32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

int main(void) {
    uint8_t font_bytes[32 + 128u * 7u];
    memset(font_bytes, 0, sizeof(font_bytes));
    memcpy(font_bytes, "TTRENDF", 7);
    write_u32(font_bytes + 8, 1u);
    write_u32(font_bytes + 12, 128u);
    write_u32(font_bytes + 16, 7u);
    write_u32(font_bytes + 20, 5u);
    write_u32(font_bytes + 24, 7u);
    write_u32(font_bytes + 28, 6u);

    /* Custom 'A' glyph: a single vertical bar and a cap to make it distinct. */
    uint8_t* glyphs = font_bytes + 32u;
    glyphs[(size_t)'A' * 7u + 0] = 0x00u;
    glyphs[(size_t)'A' * 7u + 1] = 0x10u;
    glyphs[(size_t)'A' * 7u + 2] = 0x10u;
    glyphs[(size_t)'A' * 7u + 3] = 0x10u;
    glyphs[(size_t)'A' * 7u + 4] = 0x10u;
    glyphs[(size_t)'A' * 7u + 5] = 0x10u;
    glyphs[(size_t)'A' * 7u + 6] = 0x00u;

    ttrend_font_t font = {0};
    if (ttrend_font_load_memory(&font, font_bytes, sizeof(font_bytes), 1u) != 0) {
        fprintf(stderr, "font load failed\n");
        return 1;
    }
    if (font.glyph_count != 128u) {
        fprintf(stderr, "glyph_count mismatch: %u\n", font.glyph_count);
        return 2;
    }
    if (ttrend_text_width("A", &font) != 6u) {
        fprintf(stderr, "width mismatch\n");
        return 3;
    }

    uint32_t buffer[32 * 16];
    memset(buffer, 0, sizeof(buffer));
    ttrend_surface_t surface = {buffer, 32u, 16u, 32u};
    ttrend_draw_text(&surface, 4, 4, "A", 0xFFFFFFFFu, &font);
    if (buffer[5u * 32u + 4u] != 0xFFFFFFFFu) {
        fprintf(stderr, "glyph not rendered at expected location\n");
        return 4;
    }
    printf("font-loader-test passed\n");
    return 0;
}
