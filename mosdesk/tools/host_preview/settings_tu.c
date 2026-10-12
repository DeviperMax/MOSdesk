/* Compiles the real MOSdesk Settings client and exposes its drawing. */
#define main settings_main
#include "mosdesk/main.c"
#undef main
uint32_t* settings_draw(uint32_t w, uint32_t h, uint32_t cat, uint32_t accent) {
    preferences = mosdesk_settings_load();
    preferences.accent_color = accent;
    load_fonts();
    category = cat;
    resize_surface(w, h);
    draw_window();
    return surface.pixels;
}
