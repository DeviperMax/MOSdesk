#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics.h"
#include "string.h"
#include "window.h"

#define SYS_HEAP_ALLOC 1u
#define SYS_HEAP_FREE 2u
#define MAX_WINDOWS SYSCALL_WINDOW_MAX_WINDOWS
#define TASKBAR_HEIGHT 36u
#define TITLE_HEIGHT 28u
#define CORNER_RADIUS 9u
#define SNAP_DISTANCE 24
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
static uint32_t focused_id;
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
            uint32_t red = 15u + x * 24u / display.width;
            uint32_t green = 37u + y * 58u / display.height;
            uint32_t blue = 66u + (x + y) * 48u /
                                      (display.width + display.height);
            canvas[(size_t)y * display.width + x] =
                0xFF000000u | red << 16 | green << 8 | blue;
        }
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
}

static void draw_chrome_text(const syscall_window_info_t* info) {
    char title[SYSCALL_WINDOW_MAX_TITLE];
    uint32_t i = 0;
    while (i + 1u < sizeof(title) && info->title[i]) {
        title[i] = info->title[i];
        i++;
    }
    title[i] = '\0';
    if (i > 24u) {
        title[21] = '.';
        title[22] = '.';
        title[23] = '.';
        title[24] = '\0';
    }
    mos_graphics_text(title, (int32_t)info->x + 12,
                     (int32_t)info->y + 10,
                     (uint8_t)(info->style.title_text >> 16),
                     (uint8_t)(info->style.title_text >> 8),
                     (uint8_t)info->style.title_text);

    int32_t right = (int32_t)info->x + (int32_t)info->width + 1;
    int32_t by = (int32_t)info->y + 5;
    int32_t close_x = right - 24;
    int32_t max_x = right - 48;
    int32_t min_x = right - 72;
    mos_graphics_fill_rectangle(close_x, by, 18, 18,
        (uint8_t)(info->style.close_button >> 16),
        (uint8_t)(info->style.close_button >> 8),
        (uint8_t)info->style.close_button);
    mos_graphics_line(close_x + 6, by + 6, close_x + 12, by + 12,
        (uint8_t)(info->style.close_icon >> 16),
        (uint8_t)(info->style.close_icon >> 8),
        (uint8_t)info->style.close_icon);
    mos_graphics_line(close_x + 12, by + 6, close_x + 6, by + 12,
        (uint8_t)(info->style.close_icon >> 16),
        (uint8_t)(info->style.close_icon >> 8),
        (uint8_t)info->style.close_icon);
    mos_graphics_fill_rectangle(max_x, by, 18, 18,
        (uint8_t)(info->style.maximize_button >> 16),
        (uint8_t)(info->style.maximize_button >> 8),
        (uint8_t)info->style.maximize_button);
    mos_graphics_rectangle(max_x + 5, by + 5, 8, 8,
        (uint8_t)(info->style.maximize_icon >> 16),
        (uint8_t)(info->style.maximize_icon >> 8),
        (uint8_t)info->style.maximize_icon);
    mos_graphics_fill_rectangle(min_x, by, 18, 18,
        (uint8_t)(info->style.minimize_button >> 16),
        (uint8_t)(info->style.minimize_button >> 8),
        (uint8_t)info->style.minimize_button);
    mos_graphics_line(min_x + 5, by + 12, min_x + 13, by + 12,
        (uint8_t)(info->style.minimize_icon >> 16),
        (uint8_t)(info->style.minimize_icon >> 8),
        (uint8_t)info->style.minimize_icon);
}

static void draw_cursor(void) {
    static const char* shape[] = {
        "#...........", "##..........", "#x#.........",
        "#xx#........", "#xxx#.......", "#xxxx#......",
        "#xxxxx#.....", "#xxxxxx#....", "#xxxxxxx#...",
        "#xxxxxxxx#..", "#xxxxx###...", "#xx#xx#......"
    };
    for (uint32_t y = 0; y < 12; y++) {
        for (uint32_t x = 0; x < 12; x++) {
            if (shape[y][x] == '#')
                put_pixel(cursor_x + (int32_t)x, cursor_y + (int32_t)y,
                          0xFF10191Du);
            else if (shape[y][x] == 'x')
                put_pixel(cursor_x + (int32_t)x, cursor_y + (int32_t)y,
                          0xFFFFFFFFu);
        }
    }
}

static void draw_taskbar(const syscall_window_info_t* windows,
                         uint64_t count) {
    uint32_t top = display.height - TASKBAR_HEIGHT;
    fill_rect(0, (int32_t)top, (int32_t)display.width,
              (int32_t)TASKBAR_HEIGHT, 0xF018252Du);
    fill_rect(0, (int32_t)top, (int32_t)display.width, 2, 0xFF77D2C5u);
    fill_rounded_rect(9, (int32_t)top + 6, 26, 24, 5, 0xFF77D2C5u);
    uint32_t x = 48;
    for (uint64_t i = 0; i < count && x + 100 < display.width - 150; i++) {
        const syscall_window_info_t* info = &windows[i];
        uint32_t color = info->id == focused_id ? 0xFF354B54u : 0xFF23363Eu;
        fill_rounded_rect((int32_t)x, (int32_t)top + 5, 96, 26, 5, color);
        x += 101;
    }
    if (start_menu_open) {
        fill_rounded_rect(10, (int32_t)top - 148, 210, 138, 8,
                          0xF025363Eu);
        fill_rect(20, (int32_t)top - 128, 190, 1, 0xFF526A72u);
    }
    draw_cursor();
}

static void render(const syscall_window_info_t* windows, uint64_t count) {
    draw_wallpaper();
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
    if (mos_graphics_blit_surface(canvas, display.width, display.height,
            display.width * sizeof(uint32_t), 0, 0,
            (int32_t)display.width, (int32_t)display.height, 0, 0,
            SYS_GRAPHICS_BLIT_OPAQUE) != SYS_SUCCESS) {
        strcpy(status_message, "Framebuffer update failed");
        return;
    }
    for (uint64_t i = 0; i < count; i++) {
        const syscall_window_info_t* info = &windows[i];
        if (!(info->flags & SYSCALL_WINDOW_VISIBLE) ||
            (info->flags & SYSCALL_WINDOW_MINIMIZED))
            continue;
        draw_chrome_text(info);
    }
    mos_graphics_text("MOS", 14, (int32_t)display.height - 28,
                     24, 48, 55);
    uint32_t x = 52;
    for (uint64_t i = 0; i < count && x + 94 < display.width - 150; i++) {
        char title[SYSCALL_WINDOW_MAX_TITLE];
        uint32_t j = 0;
        while (j < 10 && windows[i].title[j]) {
            title[j] = windows[i].title[j];
            j++;
        }
        title[j] = '\0';
        mos_graphics_text(title, (int32_t)x + 6,
            (int32_t)display.height - 25, 238, 245, 246);
        x += 101;
    }
    if (start_menu_open) {
        mos_graphics_text("Open windows", 22,
            (int32_t)display.height - (int32_t)TASKBAR_HEIGHT - 136,
            255, 255, 255);
        mos_graphics_text("Click an app to focus", 22,
            (int32_t)display.height - (int32_t)TASKBAR_HEIGHT - 116,
            185, 208, 210);
    }
    if (status_message[0])
        mos_graphics_text(status_message, 8, 8, 255, 230, 150);
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
    if ((uint32_t)cursor_y >= taskbar_y) {
        if (cursor_x < 44) {
            start_menu_open = !start_menu_open;
            return;
        }
        uint32_t index = ((uint32_t)cursor_x - 48u) / 101u;
        if (cursor_x >= 48 && index < count) {
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
        cursor_x += input->dx;
        cursor_y += input->dy;
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
    syscall_window_info_t windows[MAX_WINDOWS];
    syscall_input_event_t input[32];

    for (;;) {
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
