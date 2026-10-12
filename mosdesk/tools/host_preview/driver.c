#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "window.h"
extern uint32_t* settings_draw(uint32_t w, uint32_t h, uint32_t cat, uint32_t accent);
extern uint32_t* wm_frame(int menu_open, int cx, int cy, const char* status);
extern syscall_window_info_t harness_windows[8];
extern int harness_nwin;
extern uint32_t* harness_frames[8];
extern uint32_t harness_w, harness_h;
static void save(const char* path, const uint32_t* px, uint32_t w, uint32_t h) {
    FILE* o = fopen(path, "wb"); fprintf(o, "P6 %u %u 255\n", w, h);
    for (uint32_t i = 0; i < w * h; i++) { fputc((px[i] >> 16) & 255, o); fputc((px[i] >> 8) & 255, o); fputc(px[i] & 255, o); }
    fclose(o);
}
int main(int argc, char** argv) {
    int menu = argc > 2 && atoi(argv[2]);
    const char* out = argc > 1 ? argv[1] : "desktop.ppm";
    harness_w = 1280; harness_h = 720;
    syscall_window_style_t st = { .title_bar = 0xFF263945u, .title_text = 0xFFF4F7F8u, .close_button = 0xFF263945u,
        .minimize_button = 0xFF263945u, .maximize_button = 0xFF263945u, .close_icon = 0xFFF4F7F8u,
        .minimize_icon = 0xFFF4F7F8u, .maximize_icon = 0xFFF4F7F8u, .border = 0xFF536773u, .content_background = 0xFFF3F5F6u };
    uint32_t* a = settings_draw(520, 300, 0, 0xFF59A6D8u);
    static uint32_t fa[520 * 300], fb[520 * 300];
    memcpy(fa, a, sizeof fa);
    uint32_t* b = settings_draw(520, 300, 1, 0xFF59A6D8u);
    memcpy(fb, b, sizeof fb);
    harness_nwin = 2;
    syscall_window_info_t w0 = { .id = 1, .frame_version = 1, .flags = SYSCALL_WINDOW_VISIBLE, .x = 470, .y = 190, .width = 520, .height = 300, .style = st };
    strcpy(w0.title, "MOSdesk Settings - Mouse");
    syscall_window_info_t w1 = { .id = 2, .frame_version = 1, .flags = SYSCALL_WINDOW_VISIBLE | SYSCALL_WINDOW_FOCUSED, .x = 140, .y = 80, .width = 520, .height = 300, .style = st };
    strcpy(w1.title, "MOSdesk Settings");
    harness_windows[0] = w0; harness_frames[0] = fb;
    harness_windows[1] = w1; harness_frames[1] = fa;
    uint32_t* fbp = wm_frame(menu, 700, 330, NULL);
    save(out, fbp, harness_w, harness_h);
    printf("wrote %s\n", out);
    return 0;
}
