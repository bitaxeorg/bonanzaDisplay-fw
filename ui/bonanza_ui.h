#ifndef BONANZA_UI_H
#define BONANZA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "app/bz_app.h"
#include "bonanza_ui_gen.h"

void bonanza_ui_init(const char *asset_path);

// ---- Retro UI (ui/app) --------------------------------------------------------
// Loading screen, mining pages and the dial-driven setup menu. These draw into
// one L8 framebuffer that is shown through a full-screen LVGL image.

// Creates the screens and starts the UI. Call once, after bonanza_ui_init().
// platform supplies settings storage and panel brightness; it may be NULL.
void bonanza_ui_start(const bz_platform_t *platform);
// Latest host data. Call from the LVGL thread whenever it changes.
void bonanza_ui_set_data(const bz_data_t *data);
// Dial detents since the last call (clockwise positive) and the push switch.
void bonanza_ui_input(int32_t steps, bool pressed);

// ---- LVGL Editor screen (main_screen.xml), shown as the STUDIO page -------------
void bonanza_ui_set_dial_state(int32_t position, int32_t direction,
                               bool pressed, uint32_t cw, uint32_t ccw,
                               uint32_t presses, uint8_t phase);
// Fan speed is in RPM. UINT32_MAX marks an unknown numeric metric.
void bonanza_ui_set_metrics(const char *device_family, const char *device_model,
                            const char *device_name, const char *ip_address,
                            const char *best_share, uint32_t hashrate,
                            uint32_t temperature, uint32_t power,
                            uint32_t frequency, uint32_t fan);

#endif /* BONANZA_UI_H */
