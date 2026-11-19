#include "bonanza_ui.h"

// LVGL side of the UI. The screens themselves are drawn by ui/app into one
// L8 framebuffer; this file shows that framebuffer through a full-screen
// image and keeps it refreshed.

static lv_image_dsc_t framebuffer_image;
static lv_obj_t *framebuffer_view;

void bonanza_ui_init(const char *asset_path) {
    bonanza_ui_init_gen(asset_path);
}

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (bz_app_tick(lv_tick_get())) lv_obj_invalidate(framebuffer_view);
}

void bonanza_ui_start(const bz_platform_t *platform) {
    bz_app_init(platform, lv_tick_get());

    framebuffer_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    framebuffer_image.header.cf = LV_COLOR_FORMAT_L8;
    framebuffer_image.header.w = BZ_W;
    framebuffer_image.header.h = BZ_H;
    framebuffer_image.header.stride = BZ_W;
    framebuffer_image.data = bz_fb;
    framebuffer_image.data_size = sizeof(bz_fb);

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);
    framebuffer_view = lv_image_create(screen);
    lv_image_set_src(framebuffer_view, &framebuffer_image);
    lv_obj_set_pos(framebuffer_view, 0, 0);
    lv_screen_load(screen);

    lv_timer_create(ui_timer_cb, 33, NULL);
}

void bonanza_ui_set_data(const bz_data_t *data) {
    bz_app_set_data(data);
}

void bonanza_ui_input(int32_t steps, bool pressed) {
    bz_app_input((int)steps, pressed, lv_tick_get());
}
