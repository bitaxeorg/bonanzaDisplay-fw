// Host test of the LVGL glue: what LVGL flushes to the panel must be exactly
// the retro UI's framebuffer, and the STUDIO page must hand the display to the
// LVGL Editor screen and back. Uses the firmware's lv_conf.h and an L8,
// full-refresh display like lv_port_disp.c.

#include "bonanza_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t buffer_a[BZ_W * BZ_H], buffer_b[BZ_W * BZ_H];
static uint8_t panel[BZ_W * BZ_H];
static unsigned flushes;
static uint32_t now = 1000;
static bool pressed;

static uint32_t tick_cb(void) {
    return now;
}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
    assert(lv_area_get_width(area) == BZ_W && lv_area_get_height(area) == BZ_H);
    memcpy(panel, px_map, sizeof(panel));
    flushes++;
    lv_display_flush_ready(display);
}

static void run(uint32_t ms, int steps) {
    for (uint32_t end = now + ms; now < end;) {
        now += 5;
        bonanza_ui_input(steps, pressed);
        steps = 0;
        lv_timer_handler();
    }
}

static void click(void) {
    pressed = true;
    run(100, 0);
    pressed = false;
    run(100, 0);
}

// The panel only has the top four bits of each pixel.
static bool panel_matches_framebuffer(void) {
    for (size_t i = 0; i < sizeof(panel); ++i)
        if ((panel[i] >> 4) != (bz_fb[i] >> 4)) return false;
    return true;
}

static unsigned lit(const uint8_t *pixels) {
    unsigned count = 0;
    for (size_t i = 0; i < sizeof(panel); ++i) count += (pixels[i] >> 4) != 0;
    return count;
}

int main(void) {
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *display = lv_display_create(BZ_W, BZ_H);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_L8);
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_buffers(display, buffer_a, buffer_b, sizeof(buffer_a),
                           LV_DISPLAY_RENDER_MODE_FULL);

    bonanza_ui_init(NULL);
    bonanza_ui_start(NULL); // No storage or brightness hooks: defaults apply

    // Loading screen: animated, and shown pixel for pixel.
    run(2000, 0);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    assert(flushes > 10 && lit(panel) > 500 && panel_matches_framebuffer());

    bz_data_t data;
    bz_data_reset(&data);
    data.link = BZ_LINK_CONNECTED;
    snprintf(data.family, sizeof(data.family), "Bonanza");
    snprintf(data.asic, sizeof(data.asic), "BM1370");
    snprintf(data.ip, sizeof(data.ip), "192.168.1.234");
    data.hashrate_ghs = 1210;
    data.chip_temp_c = 58;
    data.power_mw = 17600;
    data.fan_rpm = 4200;
    bonanza_ui_set_data(&data);
    bonanza_ui_set_metrics(data.family, data.asic, "BAP connected", data.ip, "4.29G",
                           1210, 58, 18, UINT32_MAX, 4200);
    run(6000, 0);
    assert(bz_app_mode() == BZ_MODE_MINING && panel_matches_framebuffer());

    // A static mining page does not keep re-sending identical frames.
    unsigned before = flushes;
    run(3000, 0);
    assert(flushes - before < 5);

    // Setup menu.
    click();
    assert(bz_app_mode() == BZ_MODE_MENU);
    run(200, 0);
    assert(panel_matches_framebuffer());
    pressed = true; // Hold: leave the menu
    run(1000, 0);
    pressed = false;
    run(200, 0);
    assert(bz_app_mode() == BZ_MODE_MINING);

    // Three clicks of the dial: HERO -> GRID -> DUO -> STUDIO.
    lv_obj_t *retro = lv_screen_active();
    uint8_t retro_panel[BZ_W * BZ_H];
    memcpy(retro_panel, panel, sizeof(panel));
    run(50, 1);
    run(50, 1);
    run(50, 1);
    assert(bz_app_settings()->page == BZ_PAGE_STUDIO);
    run(2500, 0);
    assert(bz_app_shows_studio() && lv_screen_active() != retro);
    assert(lit(panel) > 500 && !panel_matches_framebuffer());
    FILE *f = fopen("studio.pgm", "wb"); // For a visual check of the editor screen
    if (f) {
        fprintf(f, "P5\n%d %d\n255\n", BZ_W, BZ_H);
        fwrite(panel, 1, sizeof(panel), f);
        fclose(f);
    }

    // One more click wraps to HERO: the retro screen is back and correct.
    run(2500, 1);
    assert(!bz_app_shows_studio() && lv_screen_active() == retro);
    assert(panel_matches_framebuffer() && lit(panel) > 500);

    puts("UI LVGL glue test passed");
    return 0;
}
