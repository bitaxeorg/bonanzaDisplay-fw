// Mining pages. Each page is a fixed arrangement of slots; the setup menu
// decides which metric each slot shows.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

// ---- Big numbers ----------------------------------------------------------------

static bool dot_matrix(void) {
    return bz.settings.digits == BZ_DIGITS_DOT_MATRIX;
}

int bz_big_text_w(const char *text) {
    return dot_matrix() ? bz_text_w(text, BZ_FONT_SMALL, 3) : bz_text_w(text, BZ_FONT_LARGE, 2);
}

// Both styles put the top of a digit at y and its foot 26 rows below, so the
// caller can align units to them: solid is 2x FM Towns (26 rows of caps), dot
// matrix is 3x Portfolio (7 rows of 2x2 dots, centred in the same band).
int bz_big_text(int x, int y, const char *text, int level) {
    if (dot_matrix()) return bz_text_ex(x, y + 3, text, BZ_FONT_SMALL, 3, true, level);
    return bz_text_ex(x, y, text, BZ_FONT_LARGE, 2, false, level);
}

#define BIG_FOOT 26

// ---- Bar meter ------------------------------------------------------------------

typedef struct {
    float max;    // Full scale, base units
    float div;    // Base units per labelled unit
    int decimals; // For the scale labels
} scale_t;

static scale_t meter_scale(bz_metric_t metric, const bz_reading_t *r) {
    scale_t s = {r->fixed_max, r->unit_div, 0};
    if (s.max <= 0) {
        // Auto-range: the smallest round full scale with headroom over the
        // highest value seen, in the unit currently shown.
        static const float mantissas[] = {1, 2, 4, 6, 8, 10};
        float top = bz.stats[metric].session_max;
        if (r->valid && r->number > top) top = r->number;
        top = top * 1.15f / s.div;
        float decade = 1;
        while (decade * 10 < top) decade *= 10;
        while (decade > top && decade > 0.001f) decade /= 10;
        s.max = decade * 10;
        for (unsigned i = 0; i < sizeof(mantissas) / sizeof(*mantissas); ++i) {
            if (decade * mantissas[i] >= top) {
                s.max = decade * mantissas[i];
                break;
            }
        }
        if (top <= 0) s.max = 1;
        s.max *= s.div;
    }
    float step = s.max / s.div / 4;
    float tenths = step * 10;
    if (step != (float)(int)step) s.decimals = tenths == (float)(int)tenths ? 1 : 2;
    return s;
}

static void scale_label(char *out, size_t size, const scale_t *s, int quarter) {
    float v = s->max / s->div * (float)quarter / 4;
    long scaled = (long)(v * (s->decimals == 2 ? 100 : s->decimals == 1 ? 10 : 1) + 0.5f);
    if (quarter == 0) snprintf(out, size, "0");
    else if (s->decimals == 2) snprintf(out, size, "%ld.%02ld", scaled / 100, scaled % 100);
    else if (s->decimals == 1) snprintf(out, size, "%ld.%ld", scaled / 10, scaled % 10);
    else snprintf(out, size, "%ld", scaled);
}

static int bar_px(float value, const scale_t *s, int width) {
    if (value <= 0 || s->max <= 0) return 0;
    if (value >= s->max) return width;
    return (int)(value / s->max * (float)width + 0.5f);
}

// One dithered bar with a solid leading edge and a peak-hold marker.
static void draw_bar(int x, int y, int w, int h, float value, float peak, const scale_t *s) {
    int fill = bar_px(value, s, w);
    if (fill > 0) {
        bz_dither(x, y, fill, h, BZ_WHITE, 0);
        bz_vline(x + fill - 1, y, h, BZ_WHITE);
    }
    int mark = bar_px(peak, s, w);
    if (mark > fill + 1) bz_vline(x + mark - 1, y + 1, h - 2, BZ_WHITE);
}

// Scale numbers with tick marks under them, spanning x .. x+w-1.
static void draw_scale(int x, int y, int w, const scale_t *s, int label_level) {
    char text[12];
    for (int i = 0; i <= 4; ++i) {
        int tx = x + i * (w - 1) / 4;
        scale_label(text, sizeof(text), s, i);
        if (i == 0) bz_text(tx, y, text, BZ_FONT_TINY, label_level);
        else if (i == 4) bz_text_right(tx + 1, y, text, BZ_FONT_TINY, label_level);
        else bz_text_center(tx + 1, y, text, BZ_FONT_TINY, label_level);
        bz_vline(tx, y + 6, 3, BZ_WHITE);
    }
}

// ---- HERO page --------------------------------------------------------------------

static void draw_wifi_bars(int x, int y) {
    int lit = 0;
    if (bz.data.rssi_dbm != BZ_RSSI_UNKNOWN) {
        int rssi = (int)bz.data.rssi_dbm;
        lit = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
        int h = 2 + i * 2;
        if (i < lit) bz_fill(x + i * 3, y + 8 - h, 2, h, BZ_WHITE);
        else bz_fill(x + i * 3, y + 7, 2, 1, BZ_DIM);
    }
}

static void draw_badge_right(int right, int y, const char *text) {
    int w = bz_text_w(text, BZ_FONT_TINY, 1) + 3;
    bz_fill(right - w + 1, y, w, 7, BZ_WHITE);
    bz_text(right - w + 3, y + 1, text, BZ_FONT_TINY, BZ_BLACK);
}

static void draw_info_row(int y, const char *label, const char *value) {
    char text[25];
    bz_text(0, y, label, BZ_FONT_SMALL, BZ_DIM);
    snprintf(text, sizeof(text), "%s", *value ? value : "--");
    bz_text(30, y, text, BZ_FONT_SMALL, BZ_WHITE);
}

static void draw_hero_main(bz_metric_t metric) {
    const int zone_w = 174;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);

    // Link status cluster, top right of the zone.
    draw_wifi_bars(163, 0);
    if (*bz.data.asic) draw_badge_right(173, 11, bz.data.asic);

    if (metric == BZ_M_NONE) {
        bz_bitmap(0, 1, bz_logo_bits, bz_logo_w, bz_logo_h, BZ_WHITE);
    } else if (bz_big_text_w(r.value) <= 108) {
        int x = bz_big_text(0, 0, r.value, BZ_WHITE) + 4;
        const char *name = bz_metric_name(metric);
        if (x + bz_text_w(name, BZ_FONT_SMALL, 1) > 158) name = bz_metric_short(metric);
        bz_text(x, 2, name, BZ_FONT_SMALL, BZ_MID);
        bz_text(x, BIG_FOOT - 13, r.unit, BZ_FONT_LARGE, BZ_WHITE);
    } else {
        // Long text such as an address: one normal-height line.
        bz_text(0, 0, bz_metric_name(metric), BZ_FONT_SMALL, BZ_MID);
        bz_clip(0, 0, 144, BZ_H);
        bz_text(0, 12, r.value, BZ_FONT_LARGE, BZ_WHITE);
        bz_clip_reset();
    }

    if (!r.gauge) {
        // Nothing to meter: use the space for who and where this miner is.
        char device[40];
        snprintf(device, sizeof(device), "%s %s", bz.data.family, bz.data.asic);
        draw_info_row(36, "HOST", device);
        draw_info_row(46, "POOL", bz.data.pool);
        draw_info_row(56, "IP", bz.data.ip);
        return;
    }

    // Twin bar meter: the value now and its running average on one scale,
    // with a marker at the recent peak.
    const int bar_x = 17, bar_w = 155;
    scale_t s = meter_scale(metric, &r);
    draw_scale(bar_x, 33, bar_w, &s, BZ_MID);
    bz_vline(15, 39, 25, BZ_WHITE);
    bz_vline(zone_w - 1, 39, 25, BZ_WHITE);
    bz_hdots(16, 42, bar_w + 2, BZ_WHITE);
    bz_hdots(16, 63, bar_w + 2, BZ_WHITE);
    bz_text(1, 45, "NOW", BZ_FONT_TINY, BZ_WHITE);
    bz_text(1, 55, "AVG", BZ_FONT_TINY, BZ_WHITE);
    const bz_stat_t *stat = &bz.stats[metric];
    float peak = bz_stat_peak(metric);
    draw_bar(bar_x, 44, bar_w, 8, r.valid ? r.number : 0, peak, &s);
    draw_bar(bar_x, 54, bar_w, 8, stat->samples && r.valid ? stat->average : 0, 0, &s);
}

// Side slot: name and unit stacked on the left, value on the right.
static void draw_side_slot(int x, int y, int w, bz_metric_t metric) {
    if (metric == BZ_M_NONE) return;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);
    int right = x + w - 1;
    size_t n = strlen(r.value);
    bz_text(x, y + 1, bz_metric_short(metric), BZ_FONT_SMALL, BZ_MID);
    if (n + strlen(r.unit) <= 5) {
        // Short units ride on the value: "58C", "17.6W".
        char joined[12];
        snprintf(joined, sizeof(joined), "%s%s", r.value, r.unit);
        bz_text_right(right, y + 1, joined, BZ_FONT_LARGE, BZ_WHITE);
    } else if (n <= 6) {
        bz_text(x, y + 10, r.unit, BZ_FONT_TINY, BZ_MID);
        bz_text_right(right, y + 1, r.value, BZ_FONT_LARGE, BZ_WHITE);
    } else if (n <= (size_t)(w / 6)) {
        bz_text_right(right, y + 8, r.value, BZ_FONT_SMALL, BZ_WHITE);
    } else {
        bz_clip(x, y, w, 16);
        bz_text_right(right, y + 10, r.value, BZ_FONT_TINY, BZ_WHITE);
        bz_clip_reset();
    }
}

static void draw_hero(const uint8_t *slots) {
    draw_hero_main((bz_metric_t)slots[0]);
    bz_vdots(177, 0, BZ_H, BZ_MID);
    for (int i = 0; i < 4; ++i) draw_side_slot(181, i * 16, 75, (bz_metric_t)slots[1 + i]);
}

// ---- GRID page --------------------------------------------------------------------

static void draw_grid_cell(int x, int y, int w, bz_metric_t metric) {
    if (metric == BZ_M_NONE) return;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);
    bz_text(x, y + 2, bz_metric_name(metric), BZ_FONT_SMALL, BZ_MID);
    int value_w = bz_text_w(r.value, BZ_FONT_LARGE, 1);
    int unit_w = *r.unit ? 2 + bz_text_w(r.unit, BZ_FONT_SMALL, 1) : 0;
    bz_clip(x, y, w, 31);
    if (value_w + unit_w <= w) {
        int end = bz_text(x, y + 13, r.value, BZ_FONT_LARGE, BZ_WHITE);
        bz_text(end + 2, y + 19, r.unit, BZ_FONT_SMALL, BZ_MID);
    } else if (bz_text_w(r.value, BZ_FONT_SMALL, 1) <= w) {
        bz_text(x, y + 17, r.value, BZ_FONT_SMALL, BZ_WHITE);
    } else {
        bz_text(x, y + 19, r.value, BZ_FONT_TINY, BZ_WHITE);
    }
    bz_clip_reset();
}

static void draw_grid(const uint8_t *slots) {
    bz_vdots(85, 0, BZ_H, BZ_MID);
    bz_vdots(171, 0, BZ_H, BZ_MID);
    bz_hdots(0, 31, BZ_W, BZ_MID);
    for (int i = 0; i < 6; ++i)
        draw_grid_cell(2 + (i % 3) * 86 + (i % 3 ? 2 : 0), (i / 3) * 32, 80, (bz_metric_t)slots[i]);
}

// ---- DUO page ---------------------------------------------------------------------

static void draw_duo_half(int x, int w, bz_metric_t metric) {
    if (metric == BZ_M_NONE) return;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);
    bz_text(x, 0, bz_metric_name(metric), BZ_FONT_SMALL, BZ_MID);
    int big_w = bz_big_text_w(r.value);
    int unit_w = *r.unit ? 3 + bz_text_w(r.unit, BZ_FONT_LARGE, 1) : 0;
    bz_clip(x, 0, w, BZ_H);
    if (big_w + unit_w <= w) {
        int end = bz_big_text(x, 10, r.value, BZ_WHITE);
        bz_text(end + 3, 10 + BIG_FOOT - 13, r.unit, BZ_FONT_LARGE, BZ_WHITE);
    } else if (big_w <= w) {
        bz_big_text(x, 10, r.value, BZ_WHITE);
        bz_text_right(x + w - 1, 0, r.unit, BZ_FONT_SMALL, BZ_WHITE);
    } else if (bz_text_w(r.value, BZ_FONT_LARGE, 1) <= w) {
        bz_text(x, 16, r.value, BZ_FONT_LARGE, BZ_WHITE);
    } else {
        bz_text(x, 20, r.value, BZ_FONT_SMALL, BZ_WHITE);
    }
    bz_clip_reset();
    if (!r.gauge) return;

    scale_t s = meter_scale(metric, &r);
    bz_vline(x, 42, 11, BZ_WHITE);
    bz_vline(x + w - 1, 42, 11, BZ_WHITE);
    for (int i = 1; i < 4; ++i) bz_vline(x + i * (w - 1) / 4, 42, 2, BZ_WHITE);
    bz_hdots(x, 44, w, BZ_WHITE);
    bz_hdots(x, 55, w, BZ_WHITE);
    draw_bar(x + 2, 46, w - 4, 8, r.valid ? r.number : 0, bz_stat_peak(metric), &s);
    char text[12];
    bz_text(x, 58, "0", BZ_FONT_TINY, BZ_MID);
    scale_label(text, sizeof(text), &s, 2);
    bz_text_center(x + w / 2, 58, text, BZ_FONT_TINY, BZ_MID);
    scale_label(text, sizeof(text), &s, 4);
    bz_text_right(x + w - 1, 58, text, BZ_FONT_TINY, BZ_MID);
}

static void draw_duo(const uint8_t *slots) {
    bz_vdots(127, 0, BZ_H, BZ_MID);
    draw_duo_half(0, 122, (bz_metric_t)slots[0]);
    draw_duo_half(133, 122, (bz_metric_t)slots[1]);
}

// ---- Entry points -------------------------------------------------------------------

void bz_mining_draw(void) {
    const uint8_t *slots = bz.settings.slots[bz.settings.page];
    switch (bz.settings.page) {
    case BZ_PAGE_HERO: draw_hero(slots); break;
    case BZ_PAGE_GRID: draw_grid(slots); break;
    default: draw_duo(slots); break;
    }
}

// Thumbnail in a 121x33 box: one outlined cell per slot, labelled with the
// metric it shows. Neighbouring cells share a border.
void bz_mining_draw_preview(int x, int y, int highlight, bool blink_on) {
    static const uint8_t hero[5][4] = {{0, 0, 80, 32}, {80, 0, 40, 8}, {80, 8, 40, 8},
                                       {80, 16, 40, 8}, {80, 24, 40, 8}};
    static const uint8_t grid[6][4] = {{0, 0, 40, 16}, {40, 0, 40, 16}, {80, 0, 40, 16},
                                       {0, 16, 40, 16}, {40, 16, 40, 16}, {80, 16, 40, 16}};
    static const uint8_t duo[2][4] = {{0, 0, 60, 32}, {60, 0, 60, 32}};
    bz_page_t page = (bz_page_t)bz.settings.page;
    const uint8_t (*cells)[4] = page == BZ_PAGE_HERO ? hero : page == BZ_PAGE_GRID ? grid : duo;
    int count = bz_page_slot_count(page);
    for (int i = 0; i < count; ++i) {
        int cx = x + cells[i][0], cy = y + cells[i][1], cw = cells[i][2], ch = cells[i][3];
        bz_rect(cx, cy, cw + 1, ch + 1, BZ_MID);
        const char *label = bz_metric_short((bz_metric_t)bz.settings.slots[page][i]);
        bz_text_center(cx + cw / 2 + 1, cy + (ch - 4) / 2, *label ? label : "-", BZ_FONT_TINY, BZ_WHITE);
        if (i == highlight && blink_on) bz_invert(cx + 1, cy + 1, cw - 1, ch - 1);
    }
}
