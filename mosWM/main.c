#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics.h"
#include "string.h"
#include "window.h"
#include "file_icons.h"
#include "wm_text.h"
#define MOSDESK_SYSCALL mos_syscall3
#include "../mosdesk/settings.h"

#ifndef SYS_EXEC
#define SYS_EXEC 33u
#endif

#define SYS_HEAP_ALLOC 1u
#define SYS_HEAP_FREE 2u
#define MAX_WINDOWS SYSCALL_WINDOW_MAX_WINDOWS
#define TASKBAR_HEIGHT 48u
#define TITLE_HEIGHT 28u
#define CORNER_RADIUS 9u
#define SNAP_DISTANCE 24
#define CURSOR_MAX_SIZE 32u
#define DRAG_NONE 0u
#define DRAG_MOVE 1u
#define DRAG_RESIZE 2u
#define MODE_NORMAL 0u
#define MODE_MAXIMIZED 1u
#define MODE_FULLSCREEN 2u
#define MODE_MINIMIZED 3u

typedef struct {
    uint32_t id;
    uint32_t frame_version;
    uint32_t width;
    uint32_t height;
    uint32_t* pixels;
    uint32_t last_mode;
    uint32_t saved_x;
    uint32_t saved_y;
    uint32_t saved_width;
    uint32_t saved_height;
} cached_window_t;

static syscall_graphics_info_t display;
static uint32_t* canvas;
static cached_window_t cache[MAX_WINDOWS];
static int32_t cursor_x;
static int32_t cursor_y;
static int32_t mouse_remainder_x;
static int32_t mouse_remainder_y;
static uint32_t focused_id;
static uint32_t cursor_pixels[CURSOR_MAX_SIZE * CURSOR_MAX_SIZE];
static uint32_t cursor_width;
static uint32_t cursor_height;
static uint32_t cursor_hotspot_x;
static uint32_t cursor_hotspot_y;
static uint32_t cursor_fill_color = 0xFFFFFFFFu;
static uint32_t cursor_scale = 1u;
static uint32_t mouse_speed = 100u;
static uint32_t accent_color = 0xFF59A6D8u;
static uint32_t dragging_id;
static uint32_t drag_mode;
static uint32_t drag_start_x;
static uint32_t drag_start_y;
static uint32_t drag_start_width;
static uint32_t drag_start_height;
static uint32_t drag_start_window_x;
static uint32_t drag_start_window_y;
static uint8_t previous_buttons;
static bool start_menu_open;
static char status_message[64];
static bool screen_owned;
static uint32_t settings_poll_frames;

static void* wm_malloc(size_t size) {
    uint64_t result = mos_syscall3(SYS_HEAP, SYS_HEAP_ALLOC, 0, size);
    int64_t signed_result = (int64_t)result;
    if (signed_result < 0 && signed_result >= -16) return NULL;
    return (void*)(uintptr_t)result;
}

static void wm_free(void* pointer) {
    if (pointer)
        mos_syscall3(SYS_HEAP, SYS_HEAP_FREE,
                     (uint64_t)(uintptr_t)pointer, 0);
}

static void load_desktop_settings(void) {
    mosdesk_settings_t settings = mosdesk_settings_load();
    mouse_speed = settings.mouse_speed;
    accent_color = settings.accent_color;
    cursor_fill_color = settings.cursor_fill;
    cursor_scale = settings.cursor_scale;
}

static uint16_t read_u16le(const uint8_t* bytes) {
    return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8u);
}

static uint32_t read_u32le(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static bool load_cursor(void) {
    const char* cursor_path = "0:/programs/mosWM/assets/posy.cur";
    long fd = (long)mos_syscall3(SYS_OPEN,
        (uint64_t)(uintptr_t)cursor_path, SYS_O_RDONLY, 0);
    if (fd < 0) return false;
    long file_size = (long)mos_syscall3(SYS_SIZE, (uint64_t)fd, 0, 0);
    if (file_size < 22 || file_size > 1024 * 1024) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return false;
    }
    uint8_t* data = (uint8_t*)wm_malloc((size_t)file_size);
    if (!data) {
        mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return false;
    }
    size_t total = 0;
    while (total < (size_t)file_size) {
        long got = (long)mos_syscall3(SYS_READ, (uint64_t)fd,
            (uint64_t)(uintptr_t)(data + total),
            (uint64_t)((size_t)file_size - total));
        if (got <= 0) break;
        total += (size_t)got;
    }
    mos_syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (total != (size_t)file_size || read_u16le(data) != 0u ||
        read_u16le(data + 2) != 2u) {
        wm_free(data);
        return false;
    }

    uint16_t count = read_u16le(data + 4);
    uint32_t best_offset = 0;
    uint32_t best_size = 0;
    uint32_t best_distance = UINT32_MAX;
    for (uint32_t i = 0; i < count; i++) {
        size_t entry_offset = 6u + (size_t)i * 16u;
        if (entry_offset + 16u > total) break;
        uint32_t width = data[entry_offset] ? data[entry_offset] : 256u;
        uint32_t height = data[entry_offset + 1] ? data[entry_offset + 1] : 256u;
        uint32_t resource_size = read_u32le(data + entry_offset + 8u);
        uint32_t image_offset = read_u32le(data + entry_offset + 12u);
        if (image_offset > total || resource_size > total - image_offset ||
            resource_size < 40u)
            continue;
        uint32_t distance = width > CURSOR_MAX_SIZE
            ? width - CURSOR_MAX_SIZE : CURSOR_MAX_SIZE - width;
        if (height > CURSOR_MAX_SIZE) distance += height - CURSOR_MAX_SIZE;
        else distance += CURSOR_MAX_SIZE - height;
        if (distance < best_distance) {
            best_distance = distance;
            best_offset = image_offset;
            best_size = resource_size;
            cursor_width = width;
            cursor_height = height;
            cursor_hotspot_x = read_u16le(data + entry_offset + 4u);
            cursor_hotspot_y = read_u16le(data + entry_offset + 6u);
        }
    }

    bool loaded = false;
    if (best_offset && cursor_width <= CURSOR_MAX_SIZE &&
        cursor_height <= CURSOR_MAX_SIZE && best_size >= 40u) {
        const uint8_t* dib = data + best_offset;
        uint32_t header_size = read_u32le(dib);
        int32_t dib_width = (int32_t)read_u32le(dib + 4u);
        int32_t dib_height = (int32_t)read_u32le(dib + 8u);
        uint16_t bits_per_pixel = read_u16le(dib + 14u);
        uint32_t compression = read_u32le(dib + 16u);
        size_t pixel_offset = header_size;
        size_t pixel_bytes = (size_t)cursor_width * cursor_height * 4u;
        if (header_size >= 40u && dib_width == (int32_t)cursor_width &&
            dib_height == (int32_t)(cursor_height * 2u) &&
            bits_per_pixel == 32u && compression == 0u &&
            pixel_offset <= best_size && pixel_bytes <= best_size - pixel_offset) {
            const uint8_t* source = dib + pixel_offset;
            for (uint32_t y = 0; y < cursor_height; y++) {
                uint32_t source_y = cursor_height - 1u - y;
                for (uint32_t x = 0; x < cursor_width; x++) {
                    size_t source_index =
                        ((size_t)source_y * cursor_width + x) * 4u;
                    uint8_t blue = source[source_index];
                    uint8_t green = source[source_index + 1u];
                    uint8_t red = source[source_index + 2u];
                    uint8_t alpha = source[source_index + 3u];
                    cursor_pixels[(size_t)y * CURSOR_MAX_SIZE + x] =
                        ((uint32_t)alpha << 24u) | ((uint32_t)red << 16u) |
                        ((uint32_t)green << 8u) | blue;
                }
            }
            loaded = true;
        }
    }
    wm_free(data);
    return loaded;
}

static void log_message(const char* message) {
    uint32_t length = 0;
    while (message[length]) length++;
    mos_write(message, length);
}

static uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static uint32_t alpha_blend(uint32_t source, uint32_t destination) {
    uint32_t alpha = source >> 24;
    if (alpha == 255u) return source;
    if (!alpha) return destination;
    uint32_t inverse = 255u - alpha;
    uint32_t red = (((source >> 16) & 255u) * alpha +
                    ((destination >> 16) & 255u) * inverse) / 255u;
    uint32_t green = (((source >> 8) & 255u) * alpha +
                      ((destination >> 8) & 255u) * inverse) / 255u;
    uint32_t blue = ((source & 255u) * alpha +
                     (destination & 255u) * inverse) / 255u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

static void put_pixel(int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= display.width ||
        (uint32_t)y >= display.height)
        return;
    uint32_t* pixel = canvas + (size_t)y * display.width + (uint32_t)x;
    *pixel = alpha_blend(color, *pixel);
}

static void fill_rect(int32_t x, int32_t y, int32_t width, int32_t height,
                      uint32_t color) {
    int64_t right = (int64_t)x + width;
    int64_t bottom = (int64_t)y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > display.width) right = display.width;
    if (bottom > display.height) bottom = display.height;
    for (int32_t py = y; py < bottom; py++) {
        for (int32_t px = x; px < right; px++) put_pixel(px, py, color);
    }
}

static void draw_wallpaper_line(int32_t x0, int32_t y0, int32_t x1,
                                int32_t y1, uint32_t color) {
    int32_t dx = x1 >= x0 ? x1 - x0 : x0 - x1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 >= y0 ? y0 - y1 : y1 - y0;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx + dy;
    for (;;) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t twice_error = error * 2;
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void wm_text(int32_t x, int32_t y, const char* text, uint32_t color,
                    uint32_t shadow, uint32_t scale) {
    moswm_draw_text(canvas, display.width, display.height, x, y, text, color,
                    shadow, scale);
}

static void stroke_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                        uint32_t color) {
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

/* Copies up to `max_chars` of `src` into `dst`, ending in ".." if cut. */
static void fit_text(char* dst, size_t dst_size, const char* src,
                     uint32_t max_chars) {
    uint32_t len = 0;
    while (src[len]) len++;
    if (max_chars + 1u > dst_size) max_chars = (uint32_t)dst_size - 1u;
    if (len <= max_chars) {
        for (uint32_t i = 0; i <= len; i++) dst[i] = src[i];
        return;
    }
    if (max_chars < 3u) {
        dst[0] = '\0';
        return;
    }
    for (uint32_t i = 0; i < max_chars - 2u; i++) dst[i] = src[i];
    dst[max_chars - 2u] = '.';
    dst[max_chars - 1u] = '.';
    dst[max_chars] = '\0';
}

static void fill_rounded_rect(int32_t x, int32_t y, int32_t width,
                              int32_t height, uint32_t radius,
                              uint32_t color) {
    if (width <= 0 || height <= 0) return;
    if (radius * 2u > (uint32_t)width) radius = (uint32_t)width / 2u;
    if (radius * 2u > (uint32_t)height) radius = (uint32_t)height / 2u;
    if (!radius) {
        fill_rect(x, y, width, height, color);
        return;
    }
    int32_t r = (int32_t)radius;
    int32_t rr = r * r;
    fill_rect(x + r, y, width - r * 2, height, color);
    fill_rect(x, y + r, width, height - r * 2, color);
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > rr) continue;
            put_pixel(x + r + dx, y + r + dy, color);
            put_pixel(x + width - r - 1 + dx, y + r + dy, color);
            put_pixel(x + r + dx, y + height - r - 1 + dy, color);
            put_pixel(x + width - r - 1 + dx,
                      y + height - r - 1 + dy, color);
        }
    }
}

static void draw_wallpaper(void) {
    for (uint32_t y = 0; y < display.height; y++) {
        for (uint32_t x = 0; x < display.width; x++) {
            uint32_t red = 14u + x * 17u / display.width;
            uint32_t green = 27u + y * 33u / display.height;
            uint32_t blue = 48u + (x + y) * 45u /
                                      (display.width + display.height);
            canvas[(size_t)y * display.width + x] =
                0xFF000000u | red << 16 | green << 8 | blue;
        }
    }

    int32_t logo_left = (int32_t)(display.width * 58u / 100u);
    int32_t logo_right = (int32_t)(display.width * 88u / 100u);
    int32_t logo_top = (int32_t)(display.height * 24u / 100u);
    int32_t logo_middle_x = (int32_t)(display.width * 72u / 100u);
    int32_t logo_middle_y = (int32_t)(display.height * 51u / 100u);
    int32_t logo_bottom = (int32_t)(display.height * 80u / 100u);
    uint32_t beam = 0x1859A6D8u;
    draw_wallpaper_line(logo_left, logo_top, logo_middle_x, logo_middle_y, beam);
    draw_wallpaper_line(logo_right, logo_top, logo_middle_x, logo_middle_y, beam);
    draw_wallpaper_line(logo_right, logo_top, logo_left, logo_bottom, beam);
    draw_wallpaper_line(logo_left, logo_bottom, logo_right, logo_bottom, beam);
    draw_wallpaper_line((logo_middle_x + logo_right) / 2,
                        (logo_middle_y + logo_bottom) / 2,
                        logo_right, logo_bottom, beam);
}

static void draw_desktop_icons(void) {
    static const struct {
        moswm_file_icon_t icon;
        const char* label;
    } items[] = {
        {MOSWM_ICON_FOLDER, "Home"},
        {MOSWM_ICON_DRIVE, "System"},
        {MOSWM_ICON_DOCUMENT, "Readme"},
        {MOSWM_ICON_CODE, "Source"}
    };
    for (uint32_t i = 0; i < sizeof(items) / sizeof(items[0]); i++)
    {
        int32_t icon_y = 30 + (int32_t)i * 66;
        moswm_draw_file_icon(canvas, display.width, display.height,
                             40, icon_y, items[i].icon);
        int32_t label_x = 56 - (int32_t)moswm_text_width(items[i].label, 2) / 2;
        if (label_x < 4) label_x = 4;
        wm_text(label_x, icon_y + 38, items[i].label, 0xFFF2F6F8u,
                0x99000000u, 2);
    }
}

static cached_window_t* cache_for(uint32_t id) {
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (cache[i].id == id) return &cache[i];
    }
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (!cache[i].id) {
            cache[i].id = id;
            return &cache[i];
        }
    }
    return NULL;
}

static void remove_missing_cache(const syscall_window_info_t* windows,
                                 uint64_t count) {
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        cached_window_t* entry = &cache[i];
        if (!entry->id) continue;
        bool found = false;
        for (uint64_t j = 0; j < count; j++) {
            if (windows[j].id == entry->id) {
                found = true;
                break;
            }
        }
        if (!found) {
            wm_free(entry->pixels);
            memset(entry, 0, sizeof(*entry));
        }
    }
}

static bool refresh_frame(cached_window_t* entry,
                          const syscall_window_info_t* info) {
    if (entry->width != info->width || entry->height != info->height) {
        if (info->width > SIZE_MAX / info->height / sizeof(uint32_t))
            return false;
        size_t bytes = (size_t)info->width * info->height * sizeof(uint32_t);
        uint32_t* pixels = wm_malloc(bytes);
        if (!pixels) return false;
        memset(pixels, 0, bytes);
        wm_free(entry->pixels);
        entry->pixels = pixels;
        entry->width = info->width;
        entry->height = info->height;
        entry->frame_version = UINT32_MAX;
    }
    if (entry->frame_version == info->frame_version) return true;
    syscall_window_frame_t frame = {
        .width = info->width,
        .height = info->height,
        .pitch = info->width * sizeof(uint32_t),
        .pixels = entry->pixels
    };
    if (mos_window_get_frame(info->id, &frame) != SYS_SUCCESS) return false;
    entry->frame_version = info->frame_version;
    return true;
}

static uint32_t window_mode(const syscall_window_info_t* info) {
    if (info->flags & SYSCALL_WINDOW_MINIMIZED) return MODE_MINIMIZED;
    if (info->flags & SYSCALL_WINDOW_FULLSCREEN) return MODE_FULLSCREEN;
    if (info->flags & SYSCALL_WINDOW_MAXIMIZED) return MODE_MAXIMIZED;
    return MODE_NORMAL;
}

static void save_bounds(cached_window_t* entry,
                        const syscall_window_info_t* info) {
    entry->saved_x = info->x;
    entry->saved_y = info->y;
    entry->saved_width = info->width;
    entry->saved_height = info->height;
}

static uint32_t mode_event(uint32_t mode) {
    if (mode == MODE_MINIMIZED) return SYSCALL_WINDOW_EVENT_MINIMIZED;
    if (mode == MODE_MAXIMIZED) return SYSCALL_WINDOW_EVENT_MAXIMIZED;
    if (mode == MODE_NORMAL) return SYSCALL_WINDOW_EVENT_RESTORED;
    return 0;
}

static void post_simple_event(uint32_t id, uint32_t type) {
    syscall_window_event_t event = {.type = type};
    uint64_t result = mos_window_post_event(id, &event);
    if (result == SYS_ERR_BUSY)
        strcpy(status_message, "Window event queue is full");
}

static bool set_window_mode(const syscall_window_info_t* info,
                            cached_window_t* entry, uint32_t mode) {
    if (!info || !entry) return false;
    uint32_t previous_mode = entry->last_mode;
    uint32_t flags = info->flags & ~(SYSCALL_WINDOW_MINIMIZED |
                                     SYSCALL_WINDOW_MAXIMIZED |
                                     SYSCALL_WINDOW_FULLSCREEN);
    if (mode == MODE_MINIMIZED) flags |= SYSCALL_WINDOW_MINIMIZED;
    else if (mode == MODE_MAXIMIZED) flags |= SYSCALL_WINDOW_MAXIMIZED;
    else if (mode == MODE_FULLSCREEN) flags |= SYSCALL_WINDOW_FULLSCREEN;

    if (mode != MODE_NORMAL && previous_mode == MODE_NORMAL)
        save_bounds(entry, info);
    if (mos_window_set_flags(info->id, flags) != SYS_SUCCESS) return false;
    entry->last_mode = mode;

    if (mode == MODE_MAXIMIZED || mode == MODE_FULLSCREEN) {
        uint32_t work_height = mode == MODE_FULLSCREEN
            ? display.height : display.height - TASKBAR_HEIGHT;
        if (display.width <= 2u || work_height <= TITLE_HEIGHT + 2u)
            return false;
        return mos_window_set_bounds(info->id, 0, 0, display.width - 2u,
                                     work_height - TITLE_HEIGHT - 2u) ==
               SYS_SUCCESS;
    }
    if (mode == MODE_NORMAL && previous_mode != MODE_NORMAL &&
        entry->saved_width && entry->saved_height) {
        bool restored = mos_window_set_bounds(info->id, entry->saved_x,
            entry->saved_y, entry->saved_width, entry->saved_height) ==
            SYS_SUCCESS;
        entry->saved_width = 0;
        entry->saved_height = 0;
        return restored;
    }
    return true;
}

static void sync_window_modes(syscall_window_info_t* windows, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        cached_window_t* entry = cache_for(info->id);
        if (!entry) continue;
        uint32_t requested = window_mode(info);
        if (requested == entry->last_mode) continue;
        uint32_t event = mode_event(requested);
        if (!set_window_mode(info, entry, requested)) {
            strcpy(status_message, "Window state update failed");
            continue;
        }
        if (event) post_simple_event(info->id, event);
    }
}

static void draw_chrome_text(const syscall_window_info_t* info);

static void composite_window(const syscall_window_info_t* info,
                             cached_window_t* entry) {
    int32_t x = (int32_t)info->x;
    int32_t y = (int32_t)info->y;
    int32_t outer_width = (int32_t)info->width + 2;
    int32_t outer_height = (int32_t)info->height + (int32_t)TITLE_HEIGHT + 2;
    if (x >= (int32_t)display.width || y >= (int32_t)display.height ||
        x + outer_width <= 0 || y + outer_height <= 0)
        return;

    fill_rounded_rect(x + 3, y + 5, outer_width, outer_height,
                      CORNER_RADIUS, 0x66000000u);
    fill_rounded_rect(x, y, outer_width, outer_height,
                      CORNER_RADIUS, info->style.border);
    fill_rounded_rect(x + 1, y + 1, outer_width - 2, outer_height - 2,
                      CORNER_RADIUS - 1u, info->style.content_background);
    fill_rect(x + 1, y + (int32_t)TITLE_HEIGHT, outer_width - 2, 2,
              info->style.title_bar);
    fill_rounded_rect(x + 1, y + 1, outer_width - 2,
                      (int32_t)TITLE_HEIGHT, CORNER_RADIUS - 1u,
                      info->style.title_bar);

    if (entry && entry->pixels) {
        int32_t content_x = x + 1;
        int32_t content_y = y + (int32_t)TITLE_HEIGHT + 1;
        for (uint32_t py = 0; py < info->height; py++) {
            int32_t dy = content_y + (int32_t)py;
            if (dy < 0 || (uint32_t)dy >= display.height) continue;
            for (uint32_t px = 0; px < info->width; px++) {
                int32_t dx = content_x + (int32_t)px;
                if (dx < 0 || (uint32_t)dx >= display.width) continue;
                if (py >= info->height - CORNER_RADIUS &&
                    (px < CORNER_RADIUS ||
                     px + CORNER_RADIUS >= info->width)) {
                    int32_t corner_x = px < CORNER_RADIUS
                        ? (int32_t)CORNER_RADIUS - 1
                        : (int32_t)info->width - (int32_t)CORNER_RADIUS;
                    int32_t corner_y =
                        (int32_t)info->height - (int32_t)CORNER_RADIUS;
                    int32_t dxr = (int32_t)px - corner_x;
                    int32_t dyr = (int32_t)py - corner_y;
                    if (dxr * dxr + dyr * dyr >
                        (int32_t)(CORNER_RADIUS * CORNER_RADIUS))
                        continue;
                }
                put_pixel(dx, dy,
                    entry->pixels[(size_t)py * info->width + px]);
            }
        }
    }
    draw_chrome_text(info);
}

static uint32_t opaque(uint32_t color) {
    return 0xFF000000u | (color & 0x00FFFFFFu);
}

static void draw_chrome_text(const syscall_window_info_t* info) {
    int32_t x = (int32_t)info->x;
    int32_t y = (int32_t)info->y;
    int32_t right = x + (int32_t)info->width + 1;
    int32_t by = y + 5;
    int32_t close_x = right - 24;
    int32_t max_x = right - 48;
    int32_t min_x = right - 72;

    /* Title: 2x text, truncated to the room left of the buttons. */
    char title[SYSCALL_WINDOW_MAX_TITLE];
    int32_t room = (min_x - 8) - (x + 12);
    uint32_t max_chars = room > 0 ? (uint32_t)room / 18u : 0u;
    fit_text(title, sizeof(title), info->title, max_chars);
    wm_text(x + 12, y + ((int32_t)TITLE_HEIGHT - 16) / 2, title,
            opaque(info->style.title_text), 0, 2);

    fill_rect(close_x, by, 18, 18, opaque(info->style.close_button));
    draw_wallpaper_line(close_x + 6, by + 6, close_x + 12, by + 12,
                        opaque(info->style.close_icon));
    draw_wallpaper_line(close_x + 12, by + 6, close_x + 6, by + 12,
                        opaque(info->style.close_icon));
    fill_rect(max_x, by, 18, 18, opaque(info->style.maximize_button));
    stroke_rect(max_x + 5, by + 5, 8, 8, opaque(info->style.maximize_icon));
    fill_rect(min_x, by, 18, 18, opaque(info->style.minimize_button));
    fill_rect(min_x + 5, by + 12, 9, 1, opaque(info->style.minimize_icon));
}

static void draw_cursor(void) {
    if (!cursor_width || !cursor_height) {
        static const char* fallback[] = {
            "#...........", "##..........", "#x#.........",
            "#xx#........", "#xxx#.......", "#xxxx#......",
            "#xxxxx#.....", "#xxxxxx#....", "#xxxxxxx#...",
            "#xxxxxxxx#..", "#xxxxx###...", "#xx#xx#......"
        };
        cursor_width = 12;
        cursor_height = 12;
        for (uint32_t y = 0; y < cursor_height; y++) {
            for (uint32_t x = 0; x < cursor_width; x++) {
                char pixel = fallback[y][x];
                if (pixel == '#')
                    cursor_pixels[y * CURSOR_MAX_SIZE + x] = 0xFF10191Du;
                else if (pixel == 'x')
                    cursor_pixels[y * CURSOR_MAX_SIZE + x] = 0xFFFFFFFFu;
            }
        }
    }
    int32_t origin_x = cursor_x - (int32_t)(cursor_hotspot_x * cursor_scale);
    int32_t origin_y = cursor_y - (int32_t)(cursor_hotspot_y * cursor_scale);
    for (uint32_t y = 0; y < cursor_height; y++) {
        for (uint32_t x = 0; x < cursor_width; x++) {
            uint32_t color = cursor_pixels[y * CURSOR_MAX_SIZE + x];
            if (!(color >> 24u)) continue;
            if (((color >> 16u) & 255u) > 210u &&
                ((color >> 8u) & 255u) > 210u && (color & 255u) > 210u)
                color = (color & 0xFF000000u) | (cursor_fill_color & 0x00FFFFFFu);
            for (uint32_t sy = 0; sy < cursor_scale; sy++) {
                for (uint32_t sx = 0; sx < cursor_scale; sx++) {
                    put_pixel(origin_x + (int32_t)(x * cursor_scale + sx),
                              origin_y + (int32_t)(y * cursor_scale + sy), color);
                }
            }
        }
    }
}

static void draw_taskbar(const syscall_window_info_t* windows,
                         uint64_t count) {
    uint32_t top = display.height - TASKBAR_HEIGHT;
    fill_rect(0, (int32_t)top, (int32_t)display.width,
              (int32_t)TASKBAR_HEIGHT, 0xF0131A24u);
    fill_rect(0, (int32_t)top, (int32_t)display.width, 1, 0xFF647786u);
    fill_rect(49, (int32_t)top + 8, 1, 32, 0xFF33434Fu);
    uint32_t x = 60;
    for (uint64_t i = 0; i < count && x + 184 < display.width - 126; i++) {
        const syscall_window_info_t* info = &windows[i];
        uint32_t color = info->id == focused_id ? 0xFF263945u : 0x00131A24u;
        fill_rect((int32_t)x, (int32_t)top + 5, 176, 38, color);
        if (info->id == focused_id)
            fill_rect((int32_t)x, (int32_t)top + 45, 176, 2, accent_color);
        char label[SYSCALL_WINDOW_MAX_TITLE];
        fit_text(label, sizeof(label), info->title, 9);
        wm_text((int32_t)x + 8, (int32_t)top + 16, label, 0xFFE0E9EFu, 0, 2);
        x += 180;
    }
    if (!count)
        wm_text(64, (int32_t)top + 16, "MOSDESK", 0xFFE0E9EFu, 0, 2);

    int32_t logo_x = 12;
    int32_t logo_y = (int32_t)top + 11;
    uint32_t accent = 0xFF000000u | (accent_color & 0x00FFFFFFu);
    draw_wallpaper_line(logo_x, logo_y, logo_x + 27, logo_y, accent);
    draw_wallpaper_line(logo_x, logo_y, logo_x + 10, logo_y + 10, accent);
    draw_wallpaper_line(logo_x + 27, logo_y, logo_x + 1, logo_y + 25, accent);
    draw_wallpaper_line(logo_x + 1, logo_y + 25, logo_x + 27, logo_y + 25,
                        accent);
    draw_wallpaper_line(logo_x + 18, logo_y + 15, logo_x + 27, logo_y + 25,
                        accent);
    if (start_menu_open) {
        fill_rect(12, (int32_t)top - 268, 286, 258, 0xF018222Du);
        fill_rect(12, (int32_t)top - 268, 3, 258, accent_color);
        fill_rect(30, (int32_t)top - 223, 250, 1, 0xFF3B4A56u);
        fill_rect(30, (int32_t)top - 166, 250, 36, 0xFF263945u);
        fill_rect(30, (int32_t)top - 46, 250, 1, 0xFF3B4A56u);
        moswm_draw_file_icon(canvas, display.width, display.height,
            35, (int32_t)top - 163, MOSWM_ICON_SETTINGS);
        wm_text(30, (int32_t)top - 254, "MOSdesk", 0xFFF2F6F9u, 0, 2);
        wm_text(30, (int32_t)top - 220, "SYSTEM", 0xFF9BAFBCu, 0, 1);
        wm_text(80, (int32_t)top - 156, "Settings", 0xFFF2F6F9u, 0, 2);
        wm_text(30, (int32_t)top - 33, "Open windows from the panel", 0xFF9BAFBCu, 0, 1);
    }
    if (status_message[0])
        wm_text(8, 8, status_message, 0xFFFFE696u, 0xAA000000u, 2);
}

static void render(const syscall_window_info_t* windows, uint64_t count) {
    draw_wallpaper();
    draw_desktop_icons();
    for (uint32_t focus_pass = 0; focus_pass < 2; focus_pass++) {
        for (uint64_t i = 0; i < count; i++) {
            const syscall_window_info_t* info = &windows[i];
            bool focused = info->id == focused_id;
            if ((focus_pass == 0 && focused) || (focus_pass == 1 && !focused))
                continue;
            if (!(info->flags & SYSCALL_WINDOW_VISIBLE) ||
                (info->flags & SYSCALL_WINDOW_MINIMIZED))
                continue;
            cached_window_t* entry = cache_for(info->id);
            if (entry) composite_window(info, entry);
        }
    }
    draw_taskbar(windows, count);
    draw_cursor();
    if (mos_graphics_blit_surface(canvas, display.width, display.height,
            display.width * sizeof(uint32_t), 0, 0,
            (int32_t)display.width, (int32_t)display.height, 0, 0,
            SYS_GRAPHICS_BLIT_OPAQUE) != SYS_SUCCESS) {
        strcpy(status_message, "Framebuffer update failed");
        return;
    }
    if (mos_graphics_present(0, 0, (int32_t)display.width,
                             (int32_t)display.height) != SYS_SUCCESS)
        strcpy(status_message, "Display present failed");
}

static syscall_window_info_t* window_at(syscall_window_info_t* windows,
                                        uint64_t count, int32_t px,
                                        int32_t py) {
    for (uint32_t focus_pass = 0; focus_pass < 2; focus_pass++) {
        for (uint64_t i = count; i > 0; i--) {
            syscall_window_info_t* info = &windows[i - 1u];
            if (!(info->flags & SYSCALL_WINDOW_VISIBLE) ||
                (info->flags & SYSCALL_WINDOW_MINIMIZED) ||
                ((info->id == focused_id) != (focus_pass == 0)))
                continue;
            int32_t right = (int32_t)info->x + (int32_t)info->width + 2;
            int32_t bottom = (int32_t)info->y + (int32_t)info->height +
                             (int32_t)TITLE_HEIGHT + 2;
            if (px >= (int32_t)info->x && px < right &&
                py >= (int32_t)info->y && py < bottom)
                return info;
        }
    }
    return NULL;
}

static void focus_window(syscall_window_info_t* info) {
    if (!info || focused_id == info->id) return;
    if (mos_window_set_flags(info->id,
            info->flags | SYSCALL_WINDOW_FOCUSED) != SYS_SUCCESS) {
        strcpy(status_message, "Unable to focus window");
        return;
    }
    focused_id = info->id;
}

static void request_close(syscall_window_info_t* info) {
    if (!info) return;
    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_CLOSE_REQUEST);
}

static void maximize_or_restore(syscall_window_info_t* info,
                                cached_window_t* entry) {
    if (!info || !entry) return;
    uint32_t mode = window_mode(info) == MODE_MAXIMIZED
        ? MODE_NORMAL : MODE_MAXIMIZED;
    if (!set_window_mode(info, entry, mode))
        strcpy(status_message, "Unable to change window size");
    else if (mode == MODE_MAXIMIZED)
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_MAXIMIZED);
    else
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
}

static void minimize_window(syscall_window_info_t* info) {
    if (!info) return;
    cached_window_t* entry = cache_for(info->id);
    if (!entry || !set_window_mode(info, entry, MODE_MINIMIZED)) {
        strcpy(status_message, "Unable to minimize window");
        return;
    }
    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_MINIMIZED);
    if (focused_id == info->id) focused_id = 0;
}

static void start_move(syscall_window_info_t* info, bool resize) {
    if (!info || (!(info->flags & SYSCALL_WINDOW_RESIZABLE) && resize)) return;
    cached_window_t* entry = cache_for(info->id);
    syscall_window_info_t current = *info;
    if (entry && window_mode(info) != MODE_NORMAL) {
        if (!set_window_mode(info, entry, MODE_NORMAL) ||
            mos_window_get_info(info->id, &current) != SYS_SUCCESS)
            return;
        post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
    }
    dragging_id = info->id;
    drag_mode = resize ? DRAG_RESIZE : DRAG_MOVE;
    drag_start_x = (uint32_t)cursor_x;
    drag_start_y = (uint32_t)cursor_y;
    drag_start_width = current.width;
    drag_start_height = current.height;
    drag_start_window_x = current.x;
    drag_start_window_y = current.y;
    if (entry) save_bounds(entry, &current);
}

static void move_dragged_window(syscall_window_info_t* windows,
                                uint64_t count) {
    if (!dragging_id || drag_mode == DRAG_NONE) return;
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        if (info->id != dragging_id) continue;
        int32_t dx = cursor_x - (int32_t)drag_start_x;
        int32_t dy = cursor_y - (int32_t)drag_start_y;
        uint32_t x = info->x;
        uint32_t y = info->y;
        uint32_t width = drag_start_width;
        uint32_t height = drag_start_height;
        if (drag_mode == DRAG_MOVE) {
            int32_t nx = (int32_t)drag_start_window_x + dx;
            int32_t ny = (int32_t)drag_start_window_y + dy;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            x = (uint32_t)nx;
            y = (uint32_t)ny;
        } else {
            int32_t nw = (int32_t)drag_start_width + dx;
            int32_t nh = (int32_t)drag_start_height + dy;
            if (nw < 64) nw = 64;
            if (nh < 48) nh = 48;
            width = (uint32_t)nw;
            height = (uint32_t)nh;
        }
        if (mos_window_set_bounds(info->id, x, y, width, height) != SYS_SUCCESS)
            strcpy(status_message, "Window resize or move failed");
        break;
    }
}

static void on_mouse_down(syscall_window_info_t* windows, uint64_t count) {
    uint32_t taskbar_y = display.height - TASKBAR_HEIGHT;
    if (start_menu_open && cursor_x >= 12 && cursor_x < 298 &&
        cursor_y >= (int32_t)taskbar_y - 268 &&
        cursor_y < (int32_t)taskbar_y - 10) {
        if (cursor_y >= (int32_t)taskbar_y - 166 &&
            cursor_y < (int32_t)taskbar_y - 130) {
            const char* settings_path = "0:/programs/mosdesk/mosdesk.run";
            int64_t process_id = (int64_t)mos_syscall3(SYS_EXEC,
                (uint64_t)(uintptr_t)settings_path, 0, 0);
            if (process_id < 0)
                strcpy(status_message, "Unable to open MOSdesk Settings");
            start_menu_open = false;
        }
        return;
    }
    if ((uint32_t)cursor_y >= taskbar_y) {
        if (cursor_x < 44) {
            start_menu_open = !start_menu_open;
            return;
        }
        uint32_t index = cursor_x >= 60
            ? ((uint32_t)cursor_x - 60u) / 180u : UINT32_MAX;
        if (cursor_x >= 60 && index < count) {
            syscall_window_info_t* info = &windows[index];
            if (info->flags & SYSCALL_WINDOW_MINIMIZED) {
                cached_window_t* entry = cache_for(info->id);
                if (entry && set_window_mode(info, entry, MODE_NORMAL))
                    post_simple_event(info->id, SYSCALL_WINDOW_EVENT_RESTORED);
                else
                    strcpy(status_message, "Unable to restore window");
            }
            syscall_window_info_t current;
            if (mos_window_get_info(info->id, &current) == SYS_SUCCESS)
                focus_window(&current);
        }
        return;
    }

    syscall_window_info_t* info =
        window_at(windows, count, cursor_x, cursor_y);
    if (!info) {
        start_menu_open = false;
        return;
    }
    focus_window(info);
    int32_t x = cursor_x - (int32_t)info->x;
    int32_t y = cursor_y - (int32_t)info->y;
    int32_t right = (int32_t)info->width + 1;
    if (y < (int32_t)TITLE_HEIGHT) {
        if (x >= right - 24) {
            request_close(info);
        } else if (x >= right - 48) {
            maximize_or_restore(info, cache_for(info->id));
        } else if (x >= right - 72) {
            minimize_window(info);
        } else {
            start_move(info, false);
        }
    } else if (x >= right - 14 &&
               y >= (int32_t)info->height + (int32_t)TITLE_HEIGHT - 14) {
        start_move(info, true);
    }
}

static void on_mouse_up(syscall_window_info_t* windows, uint64_t count) {
    if (!dragging_id) return;
    for (uint64_t i = 0; i < count; i++) {
        syscall_window_info_t* info = &windows[i];
        if (info->id != dragging_id) continue;
        if (drag_mode == DRAG_MOVE) {
            uint32_t work_height = display.height - TASKBAR_HEIGHT;
            cached_window_t* entry = cache_for(info->id);
            if (cursor_x < SNAP_DISTANCE && cursor_y < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, 0, 0,
                    display.width / 2u - 2u,
                    work_height / 2u - TITLE_HEIGHT - 2u);
            } else if (cursor_x >= (int32_t)display.width - SNAP_DISTANCE &&
                       cursor_y < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, display.width / 2u, 0,
                    display.width / 2u - 2u,
                    work_height / 2u - TITLE_HEIGHT - 2u);
            } else if (cursor_x < SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, 0, 0,
                    display.width / 2u - 2u,
                    work_height - TITLE_HEIGHT - 2u);
            } else if (cursor_x >= (int32_t)display.width - SNAP_DISTANCE) {
                if (entry) save_bounds(entry, info);
                mos_window_set_flags(info->id,
                    info->flags & ~(SYSCALL_WINDOW_MAXIMIZED |
                                    SYSCALL_WINDOW_FULLSCREEN |
                                    SYSCALL_WINDOW_MINIMIZED));
                mos_window_set_bounds(info->id, display.width / 2u, 0,
                    display.width / 2u - 2u,
                    work_height - TITLE_HEIGHT - 2u);
            } else if (cursor_y < SNAP_DISTANCE) {
                set_window_mode(info, entry, MODE_MAXIMIZED);
            } else if (window_mode(info) != MODE_NORMAL) {
                set_window_mode(info, entry, MODE_NORMAL);
            }
        }
        break;
    }
    dragging_id = 0;
    drag_mode = DRAG_NONE;
}

static void route_input(const syscall_input_event_t* input,
                        syscall_window_info_t* windows, uint64_t count) {
    if (input->type == SYSCALL_INPUT_MOUSE_MOVE) {
        int32_t scaled_x = input->dx * (int32_t)mouse_speed + mouse_remainder_x;
        int32_t scaled_y = input->dy * (int32_t)mouse_speed + mouse_remainder_y;
        cursor_x += scaled_x / 100;
        cursor_y += scaled_y / 100;
        mouse_remainder_x = scaled_x % 100;
        mouse_remainder_y = scaled_y % 100;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if ((uint32_t)cursor_x >= display.width)
            cursor_x = (int32_t)display.width - 1;
        if ((uint32_t)cursor_y >= display.height)
            cursor_y = (int32_t)display.height - 1;
        move_dragged_window(windows, count);
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0 &&
                mos_window_post_event(under->id, &event) != SYS_SUCCESS)
                strcpy(status_message, "Unable to deliver mouse movement");
        }
    } else if (input->type == SYSCALL_INPUT_MOUSE_BUTTON) {
        bool was_down =
            (previous_buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
        bool is_down = (input->buttons & SYSCALL_INPUT_BTN_LEFT) != 0;
        previous_buttons = input->buttons;
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            focus_window(under);
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0) mos_window_post_event(under->id, &event);
        }
        if (!was_down && is_down) on_mouse_down(windows, count);
        else if (was_down && !is_down) on_mouse_up(windows, count);
    } else if (input->type == SYSCALL_INPUT_MOUSE_WHEEL) {
        syscall_window_info_t* under =
            window_at(windows, count, cursor_x, cursor_y);
        if (under) {
            syscall_window_event_t event = {
                .type = SYSCALL_WINDOW_EVENT_MOUSE_WHEEL,
                .x = cursor_x - (int32_t)under->x - 1,
                .y = cursor_y - (int32_t)under->y -
                     (int32_t)TITLE_HEIGHT - 1,
                .wheel = input->wheel,
                .buttons = input->buttons,
                .mods = input->mods,
                .time_ms = input->time_ms
            };
            if (event.y >= 0) mos_window_post_event(under->id, &event);
        }
    } else if (input->type == SYSCALL_INPUT_KEY_DOWN ||
               input->type == SYSCALL_INPUT_KEY_UP) {
        if (!focused_id) return;
        syscall_window_event_t event = {
            .type = SYSCALL_WINDOW_EVENT_KEY,
            .key = input->key,
            .buttons = input->type == SYSCALL_INPUT_KEY_DOWN,
            .mods = input->mods,
            .time_ms = input->time_ms
        };
        if (mos_window_post_event(focused_id, &event) == SYS_ERR_BUSY)
            strcpy(status_message, "Window event queue is full");
    }
}

static void cleanup(void) {
    if (screen_owned) {
        mos_graphics_release();
        screen_owned = false;
    }
    wm_free(canvas);
    canvas = NULL;
    for (uint32_t i = 0; i < MAX_WINDOWS; i++) {
        wm_free(cache[i].pixels);
        memset(&cache[i], 0, sizeof(cache[i]));
    }
}

static void cleanup_and_exit(void) {
    cleanup();
    mos_exit();
}

int main(void) {
    if (mos_graphics_acquire() != SYS_SUCCESS) {
        log_message("mosWM: another process owns the display\n");
        return 1;
    }
    screen_owned = true;
    if (mos_window_register_manager() != SYS_SUCCESS) {
        mos_graphics_release();
        screen_owned = false;
        log_message("mosWM: this executable is not the registered window server\n");
        return 1;
    }
    mos_register_cleanup(cleanup_and_exit);
    if (mos_graphics_info(&display) != SYS_SUCCESS ||
        display.pixel_format != SYS_GRAPHICS_PIXEL_FORMAT_ARGB8888 ||
        display.width < 320 || display.height < TASKBAR_HEIGHT + 120u ||
        (size_t)display.width > SIZE_MAX / display.height / sizeof(uint32_t)) {
        log_message("mosWM: unsupported display size\n");
        cleanup();
        return 1;
    }
    canvas = wm_malloc((size_t)display.width * display.height *
                       sizeof(uint32_t));
    if (!canvas) {
        log_message("mosWM: unable to allocate compositor surface\n");
        cleanup();
        return 1;
    }
    cursor_x = (int32_t)(display.width / 2u);
    cursor_y = (int32_t)(display.height / 2u);
    load_desktop_settings();
    load_cursor();
    syscall_window_info_t windows[MAX_WINDOWS];
    syscall_input_event_t input[32];

    for (;;) {
        if (++settings_poll_frames >= 30u) {
            settings_poll_frames = 0;
            load_desktop_settings();
        }
        uint64_t count = mos_window_list(windows, MAX_WINDOWS);
        if (count > MAX_WINDOWS) {
            strcpy(status_message, "Unable to enumerate windows");
            count = 0;
        }
        remove_missing_cache(windows, count);
        sync_window_modes(windows, count);
        focused_id = 0;
        for (uint64_t i = 0; i < count; i++) {
            cached_window_t* entry = cache_for(windows[i].id);
            if (windows[i].flags & SYSCALL_WINDOW_FOCUSED)
                focused_id = windows[i].id;
            if (entry && !refresh_frame(entry, &windows[i]))
                strcpy(status_message, "Unable to read a window surface");
        }

        uint64_t event_count = mos_input_read(input, 32, 0, NULL);
        if (event_count <= 32) {
            for (uint64_t i = 0; i < event_count; i++)
                route_input(&input[i], windows, count);
        } else if (event_count != SYS_ERR_AGAIN) {
            strcpy(status_message, "Input read failed");
        }
        render(windows, count);
        mos_sleep(16);
    }
}
