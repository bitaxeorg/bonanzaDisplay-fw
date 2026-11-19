#ifndef BZ_APP_H
#define BZ_APP_H

// The bonanzaDisplay retro UI: a loading screen while the bitaxe starts up,
// mining pages once it is hashing, and a dial-driven setup menu that chooses
// which metrics those pages show.
//
// The app draws into bz_fb (see bz_gfx.h) and knows nothing about LVGL, the
// Pico SDK or BAP. Call everything from one thread.

#include "bz_gfx.h"
#include "bz_model.h"

typedef struct {
    // Persistent storage for one BZ_SETTINGS_BLOB_SIZE blob. May be NULL.
    bool (*load)(uint8_t *blob, size_t size);
    bool (*save)(const uint8_t *blob, size_t size);
    // level is 0 (dimmest) .. BZ_BRIGHTNESS_LEVELS-1. May be NULL.
    void (*set_brightness)(uint8_t level);
    // Firmware version ("0.1.0") and build id (git revision) for the loading
    // screen. Either may be NULL.
    const char *version;
    const char *build;
} bz_platform_t;

typedef enum { BZ_MODE_BOOT, BZ_MODE_MINING, BZ_MODE_MENU } bz_mode_t;

void bz_app_init(const bz_platform_t *platform, uint32_t now_ms);
// Latest host snapshot; call whenever it changes.
void bz_app_set_data(const bz_data_t *data);
// Dial movement since the last call (+1 per clockwise detent) and whether the
// push switch is currently held. Call as often as the dial is polled.
void bz_app_input(int steps, bool pressed, uint32_t now_ms);
// Advances animations and redraws. Returns true when bz_fb changed.
bool bz_app_tick(uint32_t now_ms);

bz_mode_t bz_app_mode(void);
const bz_settings_t *bz_app_settings(void);

#endif
