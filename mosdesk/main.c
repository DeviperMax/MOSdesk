#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ttrend.h"
#include "minimalos.h"
#include "stdlib.h"
#include "string.h"

#define TASKBAR_HEIGHT 48u
#define CURSOR_SIZE 16u
#define DIRTY_CAPACITY 32u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t* pixels;
} mosdesk_surface_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} mosdesk_rect_t;

static syscall_graphics_info_t display_info;
static mosdesk_surface_t wallpaper;
static mosdesk_surface_t scene;
static mosdesk_surface_t taskbar;
static mosdesk_surface_t cursor;
static mosdesk_rect_t dirty[DIRTY_CAPACITY];
static uint32_t dirty_count;
static int32_t cursor_x;
static int32_t cursor_y;
static bool screen_owned;

static const ttrend_font_t font_small = {1, 1, 0, 0};
static const ttrend_font_t font_medium = {2, 1, 1, 0};
static ttrend_font_t runtime_font_small = {0};
static ttrend_font_t runtime_font_medium = {0};
static bool runtime_fonts_ready = false;

static void init_runtime_fonts(void) {
    static const char* font_paths[] = {
        "0:/programs/mosdesk/fonts/arial.font",
        "0:/programs/mosdesk/fonts/roboto.font",
        "0:/programs/mosdesk/fonts/default.font",
        NULL
    };
    if (runtime_fonts_ready) return;
    runtime_fonts_ready = true;
    mos_mkdir("0:/programs/mosdesk/fonts");
    for (uint32_t i = 0; font_paths[i]; i++) {
        if (ttrend_font_load_path(&runtime_font_small, font_paths[i], 1u) == 0) {
            runtime_font_medium = runtime_font_small;
            runtime_font_medium.scale = 2u;
            runtime_font_medium.pixel_size = 2u;
            runtime_font_medium.tracking = 1u;
            runtime_font_medium.bold = 1u;
            return;
        }
    }
}

static const ttrend_font_t* choose_text_font(uint32_t size) {
    if (size > 1u && runtime_font_medium.data) return &runtime_font_medium;
    if (size == 1u && runtime_font_small.data) return &runtime_font_small;
    return size > 1u ? &font_medium : &font_small;
}

static ttrend_surface_t text_surface(mosdesk_surface_t* surface) {
    return (ttrend_surface_t){surface->pixels, surface->width,
                              surface->height, surface->width};
}

static bool surface_create(mosdesk_surface_t* surface,
                           uint32_t width, uint32_t height) {
    if (!width || !height || (size_t)width > SIZE_MAX / height / 4u)
        return false;
    size_t bytes = (size_t)width * height * 4u;
    if (bytes > SYSCALL_GFX_MAX_BUFFER_BYTES) return false;
    surface->pixels = (uint32_t*)malloc(bytes);
    if (!surface->pixels) return false;
    surface->width = width;
    surface->height = height;
    return true;
}

static void surface_destroy(mosdesk_surface_t* surface) {
    if (surface->pixels) free(surface->pixels);
    surface->pixels = NULL;
    surface->width = 0;
    surface->height = 0;
}

static void fill_rect(mosdesk_surface_t* surface, int32_t x, int32_t y,
                      int32_t width, int32_t height, uint32_t color) {
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    if (right <= x || bottom <= y) return;
    for (int32_t py = y; py < bottom; py++) {
        uint32_t* row = surface->pixels + (size_t)py * surface->width;
        for (int32_t px = x; px < right; px++) row[px] = color;
    }
}

static bool rect_intersection(mosdesk_rect_t a, mosdesk_rect_t b,
                              mosdesk_rect_t* out) {
    int32_t left = a.x > b.x ? a.x : b.x;
    int32_t top = a.y > b.y ? a.y : b.y;
    int64_t ar = (int64_t)a.x + a.w;
    int64_t br = (int64_t)b.x + b.w;
    int64_t ab = (int64_t)a.y + a.h;
    int64_t bb = (int64_t)b.y + b.h;
    int64_t right = ar < br ? ar : br;
    int64_t bottom = ab < bb ? ab : bb;
    if (right <= left || bottom <= top) return false;
    *out = (mosdesk_rect_t){left, top, (int32_t)(right - left),
                            (int32_t)(bottom - top)};
    return true;
}

static mosdesk_rect_t rect_union(mosdesk_rect_t a, mosdesk_rect_t b) {
    int32_t left = a.x < b.x ? a.x : b.x;
    int32_t top = a.y < b.y ? a.y : b.y;
    int64_t ar = (int64_t)a.x + a.w;
    int64_t br = (int64_t)b.x + b.w;
    int64_t ab = (int64_t)a.y + a.h;
    int64_t bb = (int64_t)b.y + b.h;
    int64_t right = ar > br ? ar : br;
    int64_t bottom = ab > bb ? ab : bb;
    return (mosdesk_rect_t){left, top, (int32_t)(right - left),
                            (int32_t)(bottom - top)};
}

static bool rects_touch(mosdesk_rect_t a, mosdesk_rect_t b) {
    return (int64_t)a.x <= (int64_t)b.x + b.w &&
           (int64_t)b.x <= (int64_t)a.x + a.w &&
           (int64_t)a.y <= (int64_t)b.y + b.h &&
           (int64_t)b.y <= (int64_t)a.y + a.h;
}

static void dirty_add(mosdesk_rect_t rect) {
    mosdesk_rect_t bounds = {0, 0, (int32_t)display_info.width,
                             (int32_t)display_info.height};
    if (!rect_intersection(rect, bounds, &rect)) return;
    for (uint32_t i = 0; i < dirty_count;) {
        if (rects_touch(dirty[i], rect)) {
            rect = rect_union(dirty[i], rect);
            dirty[i] = dirty[--dirty_count];
            i = 0;
        } else {
            i++;
        }
    }
    if (dirty_count == DIRTY_CAPACITY) {
        for (uint32_t i = 1; i < dirty_count; i++)
            rect = rect_union(rect, dirty[i]);
        dirty[0] = rect;
        dirty_count = 1;
    } else {
        dirty[dirty_count++] = rect;
    }
}

static void paint_wallpaper(void) {
    uint32_t width = wallpaper.width;
    uint32_t height = wallpaper.height;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t red = 20u + (x * 22u) / (width ? width : 1u);
            uint32_t green = 48u + (y * 74u) / (height ? height : 1u);
            uint32_t blue = 76u + ((x + y) * 54u) / (width + height);
            wallpaper.pixels[(size_t)y * width + x] =
                0xFF000000u | (red << 16) | (green << 8) | blue;
        }
    }
}

static void draw_icon(int32_t x, int32_t y, uint32_t color,
                      const char* label) {
    fill_rect(&scene, x + 3, y + 3, 42, 38, 0x40000000u);
    fill_rect(&scene, x, y, 42, 38, color);
    fill_rect(&scene, x + 5, y + 5, 32, 24, 0xFFE9F0F2u);
    fill_rect(&scene, x + 5, y + 31, 32, 3, 0xFF263944u);
    ttrend_surface_t surface = text_surface(&scene);
    ttrend_draw_text(&surface, x - 2, y + 45, label,
                     0xFFFFFFFFu, choose_text_font(1u));
}

static void draw_icons(void) {
    draw_icon(30, 38, 0xFF46A1A0u, "FILES");
    draw_icon(30, 116, 0xFFE1A957u, "SYSTEM");
    draw_icon(30, 194, 0xFF6690C8u, "NETWORK");
}

static void uptime_label(char* output, uint64_t seconds) {
    char digits[20];
    uint32_t count = 0;
    do {
        digits[count++] = (char)('0' + seconds % 10u);
        seconds /= 10u;
    } while (seconds && count < sizeof(digits));
    uint32_t at = 0;
    const char prefix[] = "UPTIME ";
    for (uint32_t i = 0; i < sizeof(prefix) - 1; i++) output[at++] = prefix[i];
    while (count) output[at++] = digits[--count];
    output[at++] = 'S';
    output[at] = '\0';
}

static void draw_taskbar(uint64_t seconds) {
    fill_rect(&taskbar, 0, 0, (int32_t)taskbar.width,
              (int32_t)taskbar.height, 0xF018252Du);
    fill_rect(&taskbar, 0, 0, (int32_t)taskbar.width, 2, 0xFF77D2C5u);
    fill_rect(&taskbar, 12, 9, 31, 30, 0xFF77D2C5u);
    fill_rect(&taskbar, 17, 14, 21, 3, 0xFF18333Au);
    fill_rect(&taskbar, 17, 21, 21, 3, 0xFF18333Au);
    fill_rect(&taskbar, 17, 28, 21, 3, 0xFF18333Au);
    ttrend_surface_t surface = text_surface(&taskbar);
    ttrend_draw_text(&surface, 54, 14, "MOSDESK",
                     0xFFFFFFFFu, choose_text_font(2u));
    char label[32];
    uptime_label(label, seconds);
    uint32_t text_width = ttrend_text_width(label, choose_text_font(1u));
    int32_t text_x = (int32_t)taskbar.width - (int32_t)text_width - 16;
    ttrend_draw_text(&surface, text_x, 20, label,
                     0xFFB8D2D0u, choose_text_font(1u));
}

static void paint_cursor(void) {
    static const char* shape[CURSOR_SIZE] = {
        "#...............", "##..............", "#x#.............",
        "#xx#............", "#xxx#...........", "#xxxx#..........",
        "#xxxxx#.........", "#xxxxxx#........", "#xxxxxxx#.......",
        "#xxxxxxxx#......", "#xxxxx#####.....", "#xx#xx#..........",
        "#x#..#xx#........", "##....#xx#.......",
        "......#xx#.......", ".......##........"
    };
    memset(cursor.pixels, 0, CURSOR_SIZE * CURSOR_SIZE * sizeof(uint32_t));
    for (uint32_t y = 0; y < CURSOR_SIZE; y++) {
        for (uint32_t x = 0; x < CURSOR_SIZE; x++) {
            char pixel = shape[y][x];
            if (pixel == '#') cursor.pixels[y * CURSOR_SIZE + x] = 0xFF10191Du;
            else if (pixel == 'x') cursor.pixels[y * CURSOR_SIZE + x] = 0xFFFFFFFFu;
        }
    }
}

static bool rebuild_surfaces(void) {
    surface_destroy(&wallpaper);
    surface_destroy(&scene);
    surface_destroy(&taskbar);
    if (mos_graphics_info(&display_info) != SYS_SUCCESS ||
        display_info.pixel_format != SYS_GRAPHICS_PIXEL_FORMAT_ARGB8888 ||
        display_info.width < 320 || display_info.height < 300 ||
        display_info.width > INT32_MAX || display_info.height > INT32_MAX)
        return false;
    if (!surface_create(&wallpaper, display_info.width, display_info.height) ||
        !surface_create(&scene, display_info.width, display_info.height) ||
        !surface_create(&taskbar, display_info.width, TASKBAR_HEIGHT))
        return false;
    paint_wallpaper();
    draw_taskbar(mos_uptime() / 1000u);
    cursor_x = (int32_t)(display_info.width / 2u);
    cursor_y = (int32_t)(display_info.height / 2u);
    dirty_count = 0;
    dirty_add((mosdesk_rect_t){0, 0, (int32_t)display_info.width,
                               (int32_t)display_info.height});
    return true;
}

static bool compose_rect(mosdesk_rect_t rect) {
    for (int32_t y = rect.y; y < rect.y + rect.h; y++) {
        size_t offset = (size_t)y * scene.width + (uint32_t)rect.x;
        memcpy(scene.pixels + offset, wallpaper.pixels + offset,
               (size_t)rect.w * sizeof(uint32_t));
    }
    draw_icons();

    mosdesk_rect_t bar_bounds = {0, (int32_t)(display_info.height - TASKBAR_HEIGHT),
                                 (int32_t)display_info.width, TASKBAR_HEIGHT};
    mosdesk_rect_t overlap;
    if (rect_intersection(rect, bar_bounds, &overlap)) {
        for (int32_t y = 0; y < overlap.h; y++) {
            size_t dst = (size_t)(overlap.y + y) * scene.width + overlap.x;
            size_t src = (size_t)(overlap.y - bar_bounds.y + y) * taskbar.width + overlap.x;
            memcpy(scene.pixels + dst, taskbar.pixels + src,
                   (size_t)overlap.w * sizeof(uint32_t));
        }
    }

    if (mos_graphics_blit_surface(scene.pixels, scene.width, scene.height,
            scene.width * 4u, rect.x, rect.y, rect.w, rect.h,
            rect.x, rect.y, SYS_GRAPHICS_BLIT_OPAQUE) != SYS_SUCCESS)
        return false;

    mosdesk_rect_t cursor_bounds = {cursor_x, cursor_y, CURSOR_SIZE, CURSOR_SIZE};
    if (rect_intersection(rect, cursor_bounds, &overlap)) {
        if (mos_graphics_blit_surface(cursor.pixels, CURSOR_SIZE, CURSOR_SIZE,
                CURSOR_SIZE * 4u, overlap.x - cursor_x, overlap.y - cursor_y,
                overlap.w, overlap.h, overlap.x, overlap.y,
                SYS_GRAPHICS_BLIT_ALPHA) != SYS_SUCCESS)
            return false;
    }
    return mos_graphics_present(rect.x, rect.y, rect.w, rect.h) == SYS_SUCCESS;
}

static void flush_dirty(void) {
    uint32_t count = dirty_count;
    dirty_count = 0;
    for (uint32_t i = 0; i < count; i++) compose_rect(dirty[i]);
}

static void cleanup(void) {
    if (screen_owned) {
        mos_graphics_release();
        screen_owned = false;
    }
    surface_destroy(&wallpaper);
    surface_destroy(&scene);
    surface_destroy(&taskbar);
    surface_destroy(&cursor);
}

int main(void) {
    init_runtime_fonts();
    if (mos_graphics_acquire() != SYS_SUCCESS) {
        mos_puts("MOSdesk: display is already owned\n");
        return 1;
    }
    screen_owned = true;
    mos_register_cleanup(cleanup);
    if (!surface_create(&cursor, CURSOR_SIZE, CURSOR_SIZE) ||
        !rebuild_surfaces()) {
        mos_puts("MOSdesk: unsupported display size or low memory\n");
        cleanup();
        return 1;
    }
    paint_cursor();
    mos_mouse_call(SYS_MOUSE_INIT, 0, 0);

    uint64_t last_second = UINT64_MAX;
    for (;;) {
        syscall_graphics_info_t current = {
            .size = sizeof(current),
            .version = SYS_GRAPHICS_ABI_VERSION
        };
        if (mos_graphics_info(&current) == SYS_SUCCESS &&
            (current.width != display_info.width ||
             current.height != display_info.height)) {
            display_info = current;
            if (!rebuild_surfaces()) {
                cleanup();
                return 1;
            }
            dirty_add((mosdesk_rect_t){cursor_x, cursor_y,
                                       CURSOR_SIZE, CURSOR_SIZE});
        }

        mos_mouse_call(SYS_MOUSE_POLL, 0, 0);
        syscall_mouse_state_t mouse = {0};
        if (mos_mouse_call(SYS_MOUSE_GET_STATE, (uint64_t)(uintptr_t)&mouse, 0) == SYS_SUCCESS &&
            (mouse.dx || mouse.dy)) {
            dirty_add((mosdesk_rect_t){cursor_x, cursor_y,
                                       CURSOR_SIZE, CURSOR_SIZE});
            cursor_x += mouse.dx;
            cursor_y += mouse.dy;
            if (cursor_x < 0) cursor_x = 0;
            if (cursor_y < 0) cursor_y = 0;
            if ((uint32_t)cursor_x >= display_info.width)
                cursor_x = (int32_t)display_info.width - 1;
            if ((uint32_t)cursor_y >= display_info.height)
                cursor_y = (int32_t)display_info.height - 1;
            dirty_add((mosdesk_rect_t){cursor_x, cursor_y,
                                       CURSOR_SIZE, CURSOR_SIZE});
        }

        uint64_t seconds = mos_uptime() / 1000u;
        if (seconds != last_second) {
            draw_taskbar(seconds);
            dirty_add((mosdesk_rect_t){0,
                (int32_t)(display_info.height - TASKBAR_HEIGHT),
                (int32_t)display_info.width, TASKBAR_HEIGHT});
            last_second = seconds;
        }
        flush_dirty();
        mos_sleep(16);
    }
}