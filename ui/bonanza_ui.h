#ifndef BONANZA_UI_H
#define BONANZA_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "app/bz_app.h"
#include "bonanza_ui_gen.h"

void bonanza_ui_init(const char *asset_path);

// Loading screen, mining pages and the dial-driven setup menu (ui/app). These
// draw into one L8 framebuffer that is shown through a full-screen LVGL image.

// Creates the screen and starts the UI. Call once, after bonanza_ui_init().
// platform supplies settings storage, panel brightness and the firmware
// version; it may be NULL.
void bonanza_ui_start(const bz_platform_t *platform);
// Latest host data. Call from the LVGL thread whenever it changes.
void bonanza_ui_set_data(const bz_data_t *data);
// Dial detents since the last call (clockwise positive) and the push switch.
void bonanza_ui_input(int32_t steps, bool pressed);

#endif /* BONANZA_UI_H */
