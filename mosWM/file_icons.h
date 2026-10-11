#ifndef MOSWM_FILE_ICONS_H
#define MOSWM_FILE_ICONS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    MOSWM_ICON_FOLDER,
    MOSWM_ICON_DRIVE,
    MOSWM_ICON_DOCUMENT,
    MOSWM_ICON_PDF,
    MOSWM_ICON_IMAGE,
    MOSWM_ICON_AUDIO,
    MOSWM_ICON_VIDEO,
    MOSWM_ICON_CODE,
    MOSWM_ICON_ARCHIVE,
    MOSWM_ICON_SETTINGS,
    MOSWM_ICON_NETWORK,
    MOSWM_ICON_POINTER
} moswm_file_icon_t;

static void moswm_icon_pixel(uint32_t* pixels, uint32_t surface_width,
                             uint32_t surface_height, int32_t x, int32_t y,
                             uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= surface_width ||
        (uint32_t)y >= surface_height)
        return;
    uint32_t* destination = pixels + (size_t)y * surface_width + (uint32_t)x;
    uint32_t alpha = color >> 24u;
    if (alpha == 255u) {
        *destination = color;
        return;
    }
    if (!alpha) return;
    uint32_t inverse = 255u - alpha;
    uint32_t red = (((color >> 16u) & 255u) * alpha +
                    ((*destination >> 16u) & 255u) * inverse) / 255u;
    uint32_t green = (((color >> 8u) & 255u) * alpha +
                      ((*destination >> 8u) & 255u) * inverse) / 255u;
    uint32_t blue = ((color & 255u) * alpha +
                     (*destination & 255u) * inverse) / 255u;
    *destination = 0xFF000000u | (red << 16u) | (green << 8u) | blue;
}

static void moswm_icon_rect(uint32_t* pixels, uint32_t surface_width,
                            uint32_t surface_height, int32_t x, int32_t y,
                            int32_t width, int32_t height, uint32_t color) {
    for (int32_t row = 0; row < height; row++)
        for (int32_t column = 0; column < width; column++)
            moswm_icon_pixel(pixels, surface_width, surface_height,
                             x + column, y + row, color);
}

static void moswm_draw_file_icon(uint32_t* pixels, uint32_t surface_width,
                                 uint32_t surface_height, int32_t x, int32_t y,
                                 moswm_file_icon_t type) {
    const uint32_t paper = 0xFFF2F5F6u;
    const uint32_t ink = 0xFF61727Du;
    switch (type) {
    case MOSWM_ICON_FOLDER:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 7,
                        17, 6, 0xFFE7B65Fu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 11,
                        28, 17, 0xFFE5AE4Fu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 4, y + 14,
                        24, 11, 0xFFF3C971);
        break;
    case MOSWM_ICON_DRIVE:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 3, y + 6,
                        27, 18, 0xFF9AAAB3u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 5, y + 8,
                        23, 11, 0xFFDCE4E8u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 24,
                        29, 5, 0xFF70828Cu);
        moswm_icon_pixel(pixels, surface_width, surface_height,
                         x + 25, y + 26, 0xFF63C49Bu);
        break;
    case MOSWM_ICON_IMAGE:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 3, y + 4,
                        26, 24, paper);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 6, y + 20,
                        20, 5, 0xFF69A995u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 9, y + 16,
                        6, 6, 0xFF69A995u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 17, y + 14,
                        8, 8, 0xFF69A995u);
        moswm_icon_pixel(pixels, surface_width, surface_height,
                         x + 10, y + 10, 0xFFE6B75Fu);
        break;
    case MOSWM_ICON_AUDIO:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 17, y + 4,
                        3, 18, 0xFF558EC0u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 19, y + 5,
                        10, 3, 0xFF558EC0u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 14, y + 20,
                        6, 4, 0xFF558EC0u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 23, y + 17,
                        6, 4, 0xFF558EC0u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 12, y + 22,
                        6, 5, 0xFF558EC0u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 21, y + 19,
                        6, 5, 0xFF558EC0u);
        break;
    case MOSWM_ICON_VIDEO:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 7,
                        28, 20, 0xFF586F8Fu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 13, y + 11,
                        3, 12, paper);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 16, y + 13,
                        3, 8, paper);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 19, y + 15,
                        3, 4, paper);
        break;
    case MOSWM_ICON_CODE:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 5,
                        28, 22, 0xFF293B4Du);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 2, y + 5,
                        28, 4, 0xFF526879u);
        moswm_icon_pixel(pixels, surface_width, surface_height,
                         x + 7, y + 15, 0xFF79B9D5u);
        moswm_icon_pixel(pixels, surface_width, surface_height,
                         x + 6, y + 16, 0xFF79B9D5u);
        moswm_icon_pixel(pixels, surface_width, surface_height,
                         x + 7, y + 17, 0xFF79B9D5u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 14, y + 14,
                        2, 3, 0xFFE3B96Cu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 19, y + 14,
                        2, 3, 0xFF79B9D5u);
        break;
    case MOSWM_ICON_ARCHIVE:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 5, y + 5,
                        22, 24, 0xFFB98552u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 3, y + 3,
                        26, 6, 0xFFD7A46Au);
        for (int32_t row = 10; row < 24; row += 5)
            moswm_icon_rect(pixels, surface_width, surface_height, x + 14,
                            y + row, 5, 3, 0xFF715338u);
        break;
    case MOSWM_ICON_SETTINGS:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 6, y + 6,
                        20, 20, 0xFF81939Eu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 12, y + 12,
                        8, 8, 0xFFF2F5F6u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 15, y + 15,
                        2, 2, 0xFF81939Eu);
        break;
    case MOSWM_ICON_NETWORK:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 15, y + 6,
                        2, 17, 0xFF6E9CAFu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 6, y + 23,
                        20, 2, 0xFF6E9CAFu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 9, y + 14,
                        2, 10, 0xFF6E9CAFu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 22, y + 14,
                        2, 10, 0xFF6E9CAFu);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 12, y + 4,
                        8, 7, 0xFFDCE5E9u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 3, y + 23,
                        8, 7, 0xFFDCE5E9u);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 20, y + 23,
                        8, 7, 0xFFDCE5E9u);
        break;
    case MOSWM_ICON_POINTER:
        for (int32_t row = 0; row < 24; row++) {
            int32_t end = row / 2 + 2;
            if (end > 13) end = 13;
            moswm_icon_rect(pixels, surface_width, surface_height,
                x + 7, y + 3 + row, end, 1, 0xFF243640u);
            if (row > 0 && row < 22)
                moswm_icon_rect(pixels, surface_width, surface_height,
                    x + 8, y + 4 + row, end - 2, 1, 0xFFF5F7F8u);
        }
        moswm_icon_rect(pixels, surface_width, surface_height,
            x + 18, y + 21, 4, 2, 0xFF243640u);
        moswm_icon_rect(pixels, surface_width, surface_height,
            x + 19, y + 23, 5, 2, 0xFF243640u);
        break;
    case MOSWM_ICON_PDF:
    case MOSWM_ICON_DOCUMENT:
    default:
        moswm_icon_rect(pixels, surface_width, surface_height, x + 6, y + 3,
                        18, 26, paper);
        moswm_icon_rect(pixels, surface_width, surface_height, x + 18, y + 3,
                        6, 6, 0xFFDCE5E9u);
        if (type == MOSWM_ICON_PDF) {
            moswm_icon_rect(pixels, surface_width, surface_height, x + 6,
                            y + 17, 18, 8, 0xFFC34F52u);
            moswm_icon_rect(pixels, surface_width, surface_height, x + 9,
                            y + 19, 2, 4, paper);
            moswm_icon_rect(pixels, surface_width, surface_height, x + 14,
                            y + 19, 2, 4, paper);
            moswm_icon_rect(pixels, surface_width, surface_height, x + 19,
                            y + 19, 2, 4, paper);
        } else {
            moswm_icon_rect(pixels, surface_width, surface_height, x + 9, y + 13,
                            12, 1, ink);
            moswm_icon_rect(pixels, surface_width, surface_height, x + 9, y + 17,
                            12, 1, ink);
            moswm_icon_rect(pixels, surface_width, surface_height, x + 9, y + 21,
                            9, 1, ink);
        }
        break;
    }
}

#endif
