#pragma once
#include "syscall.h"
#define SYS_GRAPHICS_PIXEL_FORMAT_ARGB8888 1u
#define SYS_GRAPHICS_BLIT_OPAQUE 1u
typedef struct { uint32_t width, height, pixel_format, pitch; } syscall_graphics_info_t;
uint64_t mos_graphics_acquire(void);
uint64_t mos_graphics_release(void);
uint64_t mos_graphics_info(syscall_graphics_info_t* i);
uint64_t mos_graphics_blit_surface(const uint32_t* px, uint32_t w, uint32_t h, uint32_t pitch,
    int32_t sx, int32_t sy, int32_t sw, int32_t sh, int32_t dx, int32_t dy, uint32_t flags);
uint64_t mos_graphics_present(int32_t x, int32_t y, int32_t w, int32_t h);
