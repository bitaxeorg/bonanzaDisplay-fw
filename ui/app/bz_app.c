#include "bz_internal.h"

#include <string.h>

#define HOLD_MS 700u          // Push and hold: back / cancel
#define MENU_IDLE_MS 30000u   // Leave an untouched menu
#define SAVE_DELAY_MS 3000u   // Batch page flips into one flash write
#define STAT_PERIOD_MS 1000u
#define STAT_BUCKET_SAMPLES 10u // 60 buckets of 10 s: a ten minute peak window
#define AVERAGE_SAMPLES 300u    // About five minutes
#define TOAST_MS 1200u

bz_state_t bz;

static struct {
    const bz_platform_t *platform;
    bool base_mining;  // Underlying screen: false = loading, true = mining
    bool menu_open;
    // Input, accumulated between ticks.
    int steps;
    bool pressed, hold_fired;
    uint32_t press_start;
    uint8_t clicks, holds;
    uint32_t last_input;
    // Settings persistence.
    bool save_pending;
    uint32_t save_after;
    uint8_t applied_brightness;
    // Stats.
    uint32_t next_stat;
    uint8_t bucket_samples;
    bool toast_on;
    uint32_t toast_start;
    bool started;
    uint8_t shadow[BZ_W * BZ_H];
} app;

// ---- Statistics for the bar meters --------------------------------------------

static void stats_sample(void) {
    bool close_bucket = ++app.bucket_samples >= STAT_BUCKET_SAMPLES;
    if (close_bucket) app.bucket_samples = 0;
    for (int metric = 1; metric < BZ_M_COUNT; ++metric) {
        bz_reading_t reading;
        bz_metric_read((bz_metric_t)metric, &bz.data, &reading);
        bz_stat_t *stat = &bz.stats[metric];
        if (reading.gauge && reading.valid) {
            float v = reading.number;
            // A plain mean at first, settling into a running average.
            if (stat->samples < AVERAGE_SAMPLES) stat->samples++;
            stat->average += (v - stat->average) / (float)stat->samples;
            if (v > stat->session_max) stat->session_max = v;
            if (v > stat->bucket_max) stat->bucket_max = v;
        }
        if (close_bucket) {
            stat->history[stat->history_head] = stat->bucket_max;
            stat->history_head = (uint8_t)((stat->history_head + 1u) % 60u);
            if (stat->history_count < 60u) stat->history_count++;
            stat->bucket_max = 0;
        }
    }
}

float bz_stat_peak(bz_metric_t metric) {
    const bz_stat_t *stat = &bz.stats[metric];
    float peak = stat->bucket_max;
    for (unsigned i = 0; i < stat->history_count; ++i)
        if (stat->history[i] > peak) peak = stat->history[i];
    return peak;
}

// ---- Settings -------------------------------------------------------------------

static void apply_brightness(void) {
    if (app.applied_brightness == bz.settings.brightness) return;
    app.applied_brightness = bz.settings.brightness;
    if (app.platform && app.platform->set_brightness)
        app.platform->set_brightness(bz.settings.brightness);
}

void bz_settings_changed(void) {
    apply_brightness();
    app.save_pending = true;
    app.save_after = bz.now + SAVE_DELAY_MS;
}

static void save_now(void) {
    if (!app.save_pending) return;
    app.save_pending = false;
    if (!app.platform || !app.platform->save) return;
    uint8_t blob[BZ_SETTINGS_BLOB_SIZE];
    bz_settings_encode(&bz.settings, blob);
    app.platform->save(blob, sizeof(blob));
}

// ---- Public API -----------------------------------------------------------------

void bz_app_init(const bz_platform_t *platform, uint32_t now_ms) {
    memset(&bz, 0, sizeof(bz));
    memset(&app, 0, sizeof(app));
    app.platform = platform;
    bz.now = now_ms;
    bz_data_reset(&bz.data);
    bz_settings_defaults(&bz.settings);
    uint8_t blob[BZ_SETTINGS_BLOB_SIZE];
    if (platform && platform->load && platform->load(blob, sizeof(blob)))
        bz_settings_decode(blob, &bz.settings);
    app.applied_brightness = 0xff;
    apply_brightness();
    app.next_stat = now_ms + STAT_PERIOD_MS;
    app.last_input = now_ms;
    bz.version = platform ? platform->version : NULL;
    bz.build = platform ? platform->build : NULL;
    bz_boot_enter(true);
    app.started = true;
    memset(app.shadow, 0xff, sizeof(app.shadow)); // Force the first frame out
}

void bz_app_set_data(const bz_data_t *data) {
    bz.data = *data;
}

void bz_app_input(int steps, bool pressed, uint32_t now_ms) {
    app.steps += steps;
    if (pressed && !app.pressed) {
        app.press_start = now_ms;
        app.hold_fired = false;
    } else if (pressed && !app.hold_fired && now_ms - app.press_start >= HOLD_MS) {
        app.hold_fired = true;
        app.holds++;
    } else if (!pressed && app.pressed && !app.hold_fired) {
        app.clicks++;
    }
    app.pressed = pressed;
}

static void handle_input(void) {
    int steps = app.steps;
    bz_press_t press = app.holds ? BZ_PRESS_HOLD : app.clicks ? BZ_PRESS_CLICK : BZ_PRESS_NONE;
    app.steps = 0;
    app.clicks = app.holds = 0;
    if (!steps && press == BZ_PRESS_NONE) {
        if (app.menu_open && bz.now - app.last_input >= MENU_IDLE_MS) {
            app.menu_open = false;
            app.save_after = bz.now;
        }
        return;
    }
    app.last_input = bz.now;

    if (app.menu_open) {
        if (!bz_menu_input(steps, press)) {
            app.menu_open = false;
            app.save_after = bz.now; // Write any changes as the menu closes
        }
    } else if (press == BZ_PRESS_CLICK) {
        app.menu_open = true;
        app.toast_on = false;
        bz_menu_open();
    } else if (steps && app.base_mining) {
        // Turning the dial on the mining screen flips between pages.
        int page = ((int)bz.settings.page + steps) % BZ_PAGE_COUNT;
        if (page < 0) page += BZ_PAGE_COUNT;
        bz.settings.page = (uint8_t)page;
        app.toast_on = true;
        app.toast_start = bz.now;
        bz_settings_changed();
    }
}

static void draw_toast(void) {
    // Briefly name the page after a flip, as an inverse tag in the corner.
    char text[12];
    const char *name = bz_page_name((bz_page_t)bz.settings.page);
    size_t n = strlen(name);
    memcpy(text, name, n);
    text[n] = ' ';
    text[n + 1] = (char)('1' + bz.settings.page);
    text[n + 2] = '/';
    text[n + 3] = (char)('0' + BZ_PAGE_COUNT);
    text[n + 4] = '\0';
    int w = bz_text_w(text, BZ_FONT_SMALL, 1) + 5;
    bz_fill(BZ_W - w - 1, BZ_H - 12, w + 1, 12, BZ_BLACK);
    bz_fill(BZ_W - w, BZ_H - 11, w, 11, BZ_WHITE);
    bz_text(BZ_W - w + 3, BZ_H - 9, text, BZ_FONT_SMALL, BZ_BLACK);
}

bool bz_app_tick(uint32_t now_ms) {
    if (!app.started) return false;
    bz.now = now_ms;

    // Meter statistics only cover time spent mining, not the start-up ramp.
    if ((int32_t)(now_ms - app.next_stat) >= 0) {
        app.next_stat = now_ms + STAT_PERIOD_MS;
        if (app.base_mining) stats_sample();
    }

    // The loading screen owns the display until the host is up and hashing,
    // and takes it back whenever the host goes away.
    if (!app.base_mining) {
        bz_boot_tick();
        if (bz_boot_done()) app.base_mining = true;
    } else if (bz.data.link != BZ_LINK_CONNECTED) {
        app.base_mining = false;
        bz_boot_enter(false);
    }

    handle_input();
    if (app.toast_on && now_ms - app.toast_start >= TOAST_MS) app.toast_on = false;
    if (app.save_pending && !app.menu_open && (int32_t)(now_ms - app.save_after) >= 0) save_now();

    bz_clip_reset();
    bz_clear();
    if (app.menu_open) {
        bz_menu_draw();
    } else if (!app.base_mining) {
        bz_boot_draw();
    } else {
        bz_mining_draw();
        if (app.toast_on) draw_toast();
    }

    if (!memcmp(app.shadow, bz_fb, sizeof(bz_fb))) return false;
    memcpy(app.shadow, bz_fb, sizeof(bz_fb));
    return true;
}

bz_mode_t bz_app_mode(void) {
    return app.menu_open ? BZ_MODE_MENU : app.base_mining ? BZ_MODE_MINING : BZ_MODE_BOOT;
}

const bz_settings_t *bz_app_settings(void) {
    return &bz.settings;
}
