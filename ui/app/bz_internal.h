#ifndef BZ_INTERNAL_H
#define BZ_INTERNAL_H

// Shared between the app state machine and its screens. Not a public API.

#include "bz_app.h"

#define BZ_TICK_MS 33u

// One graph column per BZ_GRAPH_STEP_S seconds: about ten minutes in all.
#define BZ_GRAPH_POINTS 119
#define BZ_GRAPH_STEP_S 5u
#define BZ_GRAPH_GAP (-1.0f) // A point with no data (host away, value unknown)

typedef struct {
    float bucket_sum;  // Readings in the point being filled
    uint8_t bucket_count;
    // Mean of each completed point in base units, as a ring; the newest is
    // at graph_head - 1.
    float graph[BZ_GRAPH_POINTS];
    uint8_t graph_head;
    uint8_t graph_count;
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

// Graph point `age` steps back (0 is the newest completed point). Returns
// BZ_GRAPH_GAP for a point with no data or one older than the history.
float bz_stat_point(bz_metric_t metric, int age);
// Marks settings changed: applies brightness and schedules a save.
void bz_settings_changed(void);

// ---- Loading screen ---------------------------------------------------------
// cold is true at power-up; false when returning because the host went away.
void bz_boot_enter(bool cold);
void bz_boot_tick(void);
bool bz_boot_done(void);
void bz_boot_draw(void);

// ---- Metric icons -----------------------------------------------------------
#define BZ_ICON_SIZE 9
// Draws the metric's icon with its top-left corner at (x, y); BZ_M_NONE has none.
void bz_icon(int x, int y, bz_metric_t metric, int level);

// ---- Mining pages -----------------------------------------------------------
void bz_mining_draw(void);
// Large value text in dot-matrix digits. Returns the x after the text.
int bz_big_text(int x, int y, const char *text, int level);
int bz_big_text_w(const char *text);
// The TARGET page, in bz_target.c, and its thumbnail in a 121x33 box.
void bz_target_draw(void);
void bz_target_draw_preview(int x, int y);
// The ASIC page, in bz_asic.c, and its thumbnail.
void bz_asic_draw(void);
void bz_asic_draw_preview(int x, int y);
// Page thumbnail for the setup menu; highlight is a HERO slot index or -1.
void bz_mining_draw_preview(int x, int y, bz_page_t page, int highlight, bool blink_on);

// ---- Setup menu -------------------------------------------------------------
typedef enum { BZ_PRESS_NONE, BZ_PRESS_CLICK, BZ_PRESS_HOLD } bz_press_t;

void bz_menu_open(void);
// Returns false once the user has left the menu.
bool bz_menu_input(int steps, bz_press_t press);
void bz_menu_draw(void);

#endif
