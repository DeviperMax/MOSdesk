/* Host test for ttrend_outline (and the ttrend vector path).
 *
 *   cc -I../include outline_render_test.c ../src/ttrend_outline.c -o t
 *   ./t path/to/font.otf|ttf [out.ppm]
 *
 * Checks metrics, that text actually paints anti-aliased (partial-coverage)
 * pixels, that rendering is deterministic and clips safely, and optionally
 * writes a PPM for eyeballing. */
#include "../include/ttrend_outline.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s font [out.ppm]\n", argv[0]); return 2; }
    ttrend_outline_font_t f;
    CHECK(ttrend_outline_font_load_file(&f, argv[1], 16u) == 0, "font loads");
    CHECK(f.units_per_em >= 16u && f.glyph_count > 0u, "font header parsed");
    CHECK(ttrend_outline_ascent(&f) > 0 && ttrend_outline_descent(&f) > 0, "metrics");
    CHECK(ttrend_outline_cap_height(&f) > 0, "cap height");

    uint32_t w1 = ttrend_outline_text_width("Settings", &f);
    uint32_t w2 = ttrend_outline_text_width("Settings Settings", &f);
    CHECK(w1 > 20u && w2 > w1 * 2u - 8u, "text width grows with text");
    f.pixel_size = 32u;
    CHECK(ttrend_outline_text_width("Settings", &f) > w1 * 3u / 2u, "width scales with size");
    f.pixel_size = 16u;
    CHECK(ttrend_outline_text_width("", &f) == 0u, "empty string has no width");

    enum { W = 200, H = 40 };
    static uint32_t a[W * H], b[W * H];
    for (int i = 0; i < W * H; i++) a[i] = b[i] = 0xFFFFFFFFu;
    ttrend_outline_surface_t sa = {a, W, H, W}, sb = {b, W, H, W};
    ttrend_outline_draw_text(&sa, 4, 4, "Hamburgefonstiv", 0xFF000000u, &f);
    ttrend_outline_draw_text(&sb, 4, 4, "Hamburgefonstiv", 0xFF000000u, &f);
    CHECK(memcmp(a, b, sizeof(a)) == 0, "rendering is deterministic (cache hit == miss)");
    int ink = 0, partial = 0;
    for (int i = 0; i < W * H; i++) {
        uint32_t g = a[i] & 255u;
        if (g != 255u) ink++;
        if (g != 255u && g != 0u) partial++;
    }
    CHECK(ink > 100, "text paints pixels");
    CHECK(partial * 4 > ink, "edges are anti-aliased (many partial-coverage pixels)");

    /* sub-pixel positioning: a quarter-pixel nudge must change the pixels */
    for (int i = 0; i < W * H; i++) b[i] = 0xFFFFFFFFu;
    f.tracking = 0u;
    ttrend_outline_draw_text(&sb, 4, 4, "iiiiiiiiiiii", 0xFF000000u, &f);
    for (int i = 0; i < W * H; i++) a[i] = 0xFFFFFFFFu;
    ttrend_outline_draw_text(&sa, 5, 4, "iiiiiiiiiiii", 0xFF000000u, &f);
    CHECK(memcmp(a, b, sizeof(a)) != 0, "different pen positions render differently");

    /* clipping: drawing far outside / partly outside must not touch memory */
    ttrend_outline_draw_text(&sa, -30, -8, "Clip me", 0xFF102030u, &f);
    ttrend_outline_draw_text(&sa, W - 6, H - 6, "Clip me", 0xFF102030u, &f);
    ttrend_outline_draw_text(&sa, 100000, 100000, "gone", 0xFF102030u, &f);
    ttrend_outline_draw_text(&sa, 4, 4, "\xC3\xA5\xE2\x80\x94\xFF\xFE", 0xFF000000u, &f);

    /* alpha: half-transparent text must be lighter than opaque text */
    for (int i = 0; i < W * H; i++) a[i] = b[i] = 0xFFFFFFFFu;
    ttrend_outline_draw_text(&sa, 4, 4, "MMMM", 0xFF000000u, &f);
    ttrend_outline_draw_text(&sb, 4, 4, "MMMM", 0x80000000u, &f);
    uint64_t da = 0, db = 0;
    for (int i = 0; i < W * H; i++) { da += a[i] & 255u; db += b[i] & 255u; }
    CHECK(db > da, "alpha < 255 blends lighter");

    if (argc > 2) {
        enum { PW = 480, PH = 120 };
        static uint32_t px[PW * PH];
        for (int i = 0; i < PW * PH; i++) px[i] = 0xFFF3F5F6u;
        ttrend_outline_surface_t sp = {px, PW, PH, PW};
        const unsigned sizes[] = {11, 12, 14, 16, 20, 28};
        int y = 4;
        for (int i = 0; i < 6; i++) {
            f.pixel_size = sizes[i];
            ttrend_outline_draw_text(&sp, 8, y, "Smooth text: Settings, Desktop 12:45", 0xFF1B1E20u, &f);
            y += ttrend_outline_line_height(&f) + 2;
        }
        FILE* o = fopen(argv[2], "wb");
        if (o) {
            fprintf(o, "P6 %d %d 255\n", PW, PH);
            for (int i = 0; i < PW * PH; i++) {
                fputc((px[i] >> 16) & 255, o); fputc((px[i] >> 8) & 255, o); fputc(px[i] & 255, o);
            }
            fclose(o);
        }
    }
    ttrend_outline_font_destroy(&f);
    printf("ttrend_outline test passed\n");
    return 0;
}
