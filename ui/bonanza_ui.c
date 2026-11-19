#include "bonanza_ui.h"

#include <stdio.h>

// UINT32_MAX means the host has not supplied this value (or is disconnected).
static void set_metric_text(lv_subject_t *subject, uint32_t value, const char *unit) {
    char text[32];
    if (value == UINT32_MAX) snprintf(text, sizeof(text), "--%s", unit);
    else snprintf(text, sizeof(text), "%lu%s", (unsigned long)value, unit);
    lv_subject_copy_string(subject, text);
}

void bonanza_ui_init(const char *asset_path) {
    bonanza_ui_init_gen(asset_path);
}

// ---- Retro UI -------------------------------------------------------------------

static lv_image_dsc_t framebuffer_image;
static lv_obj_t *retro_screen;
static lv_obj_t *retro_image;
static lv_obj_t *studio_screen;

static void ui_timer_cb(lv_timer_t *timer) {
    (void)timer;
    bool changed = bz_app_tick(lv_tick_get());
    lv_obj_t *wanted = retro_screen;
    if (bz_app_shows_studio()) {
        // The editor-designed screen is only built if the user pages to it.
        if (!studio_screen) studio_screen = main_screen_create();
        wanted = studio_screen;
    }
    if (lv_screen_active() != wanted) lv_screen_load(wanted);
    if (changed) lv_obj_invalidate(retro_image);
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

    retro_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(retro_screen);
    lv_obj_set_style_bg_color(retro_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(retro_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(retro_screen, false);
    retro_image = lv_image_create(retro_screen);
    lv_image_set_src(retro_image, &framebuffer_image);
    lv_obj_set_pos(retro_image, 0, 0);
    lv_screen_load(retro_screen);

    lv_timer_create(ui_timer_cb, 33, NULL);
}

void bonanza_ui_set_data(const bz_data_t *data) {
    bz_app_set_data(data);
}

void bonanza_ui_input(int32_t steps, bool pressed) {
    bz_app_input((int)steps, pressed, lv_tick_get());
}

// ---- LVGL Editor screen -----------------------------------------------------------

void bonanza_ui_set_dial_state(int32_t position, int32_t direction,
                               bool pressed, uint32_t cw, uint32_t ccw,
                               uint32_t presses, uint8_t phase) {
    lv_subject_set_int(&dial_position, position);
    lv_subject_set_int(&dial_direction, direction);
    lv_subject_set_int(&switch_pressed, pressed ? 1 : 0);
    lv_subject_set_int(&cw_count, (int32_t)cw);
    lv_subject_set_int(&ccw_count, (int32_t)ccw);
    lv_subject_set_int(&press_count, (int32_t)presses);
    lv_subject_set_int(&dial_phase, phase);
}

void bonanza_ui_set_metrics(const char *device_family, const char *device_model,
                            const char *device_name, const char *ip_address,
                            const char *best_share, uint32_t hashrate,
                            uint32_t temperature, uint32_t power,
                            uint32_t frequency, uint32_t fan) {
    char identity[UI_SUBJECT_STRING_LENGTH];
    snprintf(identity, sizeof(identity), "%s%s%s", device_family,
             *device_model ? " " : "", device_model);

    lv_subject_copy_string(&device_identity, identity);
    lv_subject_copy_string(&device_name_text, device_name);
    lv_subject_copy_string(&ip_address_text, ip_address);
    lv_subject_copy_string(&best_share_text, best_share);
    if (hashrate != UINT32_MAX && hashrate >= 10000u) {
        // Bonanza rates can exceed the space available for a GH/s integer.
        char text[32];
        snprintf(text, sizeof(text), "%lu.%luTH/s", (unsigned long)(hashrate / 1000u),
                 (unsigned long)((hashrate % 1000u) / 100u));
        lv_subject_copy_string(&hashrate_text, text);
    } else {
        set_metric_text(&hashrate_text, hashrate, "GH/s");
    }
    set_metric_text(&temperature_text, temperature, "C");
    set_metric_text(&power_text, power, "W");
    set_metric_text(&frequency_text, frequency, "MHz");
    set_metric_text(&fan_text, fan, "RPM");
}
