#ifndef BZ_INTERNAL_H
#define BZ_INTERNAL_H

// Shared between the app state machine and its screens. Not a public API.

#include "bz_app.h"

#define BZ_TICK_MS 33u

typedef struct {
    uint16_t samples;  // Readings averaged so far, saturating
    float average;     // Slow running average, base units
    float session_max; // Highest value since power-up; sets the meter range
    float bucket_max;  // Highest value in the bucket being filled
    float history[60]; // Bucket maxima, newest at history_head - 1
    uint8_t history_head;
    uint8_t history_count;
} bz_stat_t;

typedef struct {
    bz_data_t data;
    bz_settings_t settings;
    uint32_t now;          // Time of the current tick
    const char *version;   // Firmware version and build id, or NULL
    const char *build;
    bz_stat_t stats[BZ_M_COUNT];
} bz_state_t;

extern bz_state_t bz;

// Highest recent value for the peak marker, in base units.
float bz_stat_peak(bz_metric_t metric);
// Marks settings changed: applies brightness and schedules a save.
void bz_settings_changed(void);

// ---- Loading screen ---------------------------------------------------------
// cold is true at power-up; false when returning because the host went away.
void bz_boot_enter(bool cold);
void bz_boot_tick(void);
bool bz_boot_done(void);
void bz_boot_draw(void);

// ---- Mining pages -----------------------------------------------------------
void bz_mining_draw(void);
// Large value text in the user's digit style. Returns the x after the text.
int bz_big_text(int x, int y, const char *text, int level);
int bz_big_text_w(const char *text);
// Page thumbnail for the setup menu; highlight is a slot index or -1.
void bz_mining_draw_preview(int x, int y, int highlight, bool blink_on);

// ---- Setup menu -------------------------------------------------------------
typedef enum { BZ_PRESS_NONE, BZ_PRESS_CLICK, BZ_PRESS_HOLD } bz_press_t;

void bz_menu_open(void);
// Returns false once the user has left the menu.
bool bz_menu_input(int steps, bz_press_t press);
void bz_menu_draw(void);

#endif
