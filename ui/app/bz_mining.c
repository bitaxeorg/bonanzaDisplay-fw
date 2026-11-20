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

// ---- History graph ----------------------------------------------------------------

// Rounds a positive span up to 1, 2 or 5 times a power of ten, no finer than
// 0.01, and reports how many decimals a multiple of it needs.
static float nice_step(float raw, int *decimals) {
    float step = 1;
    while (step > raw && step > 0.01f) step /= 10;
    while (step * 10 <= raw) step *= 10;
    float mantissa = raw / step;
    step *= mantissa <= 1 ? 1 : mantissa <= 2 ? 2 : mantissa <= 5 ? 5 : 10;
    *decimals = step >= 1 ? 0 : step >= 0.1f ? 1 : 2;
    return step;
}

static void graph_label(char *out, size_t size, long multiple, float step, int decimals) {
    // Work in whole hundredths so labels never show float rounding noise.
    long hundredths = multiple * (long)(step * 100 + 0.5f);
    if (decimals == 0) snprintf(out, size, "%ld", hundredths / 100);
    else if (decimals == 1) snprintf(out, size, "%ld.%ld", hundredths / 100, hundredths % 100 / 10);
    else snprintf(out, size, "%ld.%02ld", hundredths / 100, hundredths % 100);
}

// Scrolling area graph of the last ten minutes: newest on the right, one
// column per history point, with the live value in the last column. The
// vertical scale fits the data in view, and its ends are labelled.
static void draw_graph(int x, int y, int w, int h, bz_metric_t metric, const bz_reading_t *r) {
    const int bottom = y + h - 1;
    bz_hdots(x, bottom + 1, w + 1, BZ_WHITE);
    bz_vline(x + w, y, h + 1, BZ_WHITE);
    bz_text(x + w + 3, y + h / 2 - 2, "10M", BZ_FONT_TINY, BZ_DIM);

    // Column c shows the point (w - 1 - c) steps back; the last is live.
    float lo = 0, hi = 0;
    bool any = false;
    for (int c = 0; c < w; ++c) {
        float v = c == w - 1 ? (r->valid ? r->number : BZ_GRAPH_GAP) : bz_stat_point(metric, w - 2 - c);
        if (v < 0) continue;
        if (!any || v < lo) lo = v;
        if (!any || v > hi) hi = v;
        any = true;
    }
    if (!any) return;

    // Keep a nearly flat trace from filling the plot with noise, then round
    // the scale outward to steps that label cleanly in the unit shown.
    float div = r->unit_div;
    float floor_span = hi * 0.02f > div * 0.02f ? hi * 0.02f : div * 0.02f;
    if (hi - lo < floor_span) {
        float mid = (hi + lo) / 2;
        lo = mid - floor_span / 2;
        hi = mid + floor_span / 2;
        if (lo < 0) lo = 0;
    }
    int decimals;
    float step = nice_step((hi - lo) / div / 3, &decimals);
    long lo_multiple = (long)(lo / div / step + 0.001f);
    long hi_multiple = (long)(hi / div / step - 0.001f) + 1;
    if (hi_multiple <= lo_multiple) hi_multiple = lo_multiple + 1;
    lo = (float)lo_multiple * step * div;
    hi = (float)hi_multiple * step * div;

    char text[12];
    graph_label(text, sizeof(text), hi_multiple, step, decimals);
    bz_text(x + w + 3, y, text, BZ_FONT_TINY, BZ_MID);
    graph_label(text, sizeof(text), lo_multiple, step, decimals);
    bz_text(x + w + 3, bottom - 4, text, BZ_FONT_TINY, BZ_MID);

    int previous = -1;
    for (int c = 0; c < w; ++c) {
        float v = c == w - 1 ? (r->valid ? r->number : BZ_GRAPH_GAP) : bz_stat_point(metric, w - 2 - c);
        if (v < 0) {
            previous = -1; // Break the trace across a gap
            continue;
        }
        int py = bottom - (int)((v - lo) / (hi - lo) * (float)(h - 1) + 0.5f);
        if (py < y) py = y;
        if (py > bottom) py = bottom;
        bz_dither(x + c, py + 1, 1, bottom - py, BZ_DIM + 1, 0);
        // Join to the previous column so steep changes stay one line.
        int from = previous < 0 ? py : previous < py ? previous + 1 : previous > py ? previous - 1 : py;
        if (from < py) bz_vline(x + c, from, py - from + 1, BZ_WHITE);
        else bz_vline(x + c, py, from - py + 1, BZ_WHITE);
        previous = py;
    }
}

// ---- HERO page --------------------------------------------------------------------

// The page is split by a dotted line: the hero metric and its graph on the
// left, and on the right four icon metrics over the stratum host and the
// network difficulty.
#define HERO_MAIN_W 142   // Hero zone is x 0..141
#define HERO_SPLIT_X 145
#define HERO_SIDE_X 149   // Side zone is x 149..255
#define HERO_CELL_W 52
#define HERO_CELL_PITCH 55

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

static int badge_w(const char *text) {
    return bz_text_w(text, BZ_FONT_TINY, 1) + 3;
}

static void draw_badge_right(int right, int y, const char *text) {
    int w = badge_w(text);
    bz_fill(right - w + 1, y, w, 7, BZ_WHITE);
    bz_text(right - w + 3, y + 1, text, BZ_FONT_TINY, BZ_BLACK);
}

static void draw_info_row(int y, const char *label, const char *value) {
    bz_text(0, y, label, BZ_FONT_SMALL, BZ_DIM);
    bz_clip(0, y, HERO_MAIN_W, 8);
    bz_text(30, y, *value ? value : "--", BZ_FONT_SMALL, BZ_WHITE);
    bz_clip_reset();
}

static void draw_hero_main(bz_metric_t metric) {
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);

    // Link status cluster in the top right corner of the zone: WiFi bars over
    // the ASIC badge. A wide hero value takes the badge's place.
    const int right = HERO_MAIN_W - 1;
    int badge_left = *bz.data.asic ? right - badge_w(bz.data.asic) + 1 : HERO_MAIN_W;
    draw_wifi_bars(right - 10, 0);

    if (metric == BZ_M_NONE) {
        bz_bitmap(0, 1, bz_logo_bits, bz_logo_w, bz_logo_h, BZ_WHITE);
    } else if (bz_big_text_w(r.value) <= 108) {
        int x = bz_big_text(0, 0, r.value, BZ_WHITE) + 4;
        const char *name = bz_metric_name(metric);
        if (x + bz_text_w(name, BZ_FONT_SMALL, 1) > right - 12) name = bz_metric_short(metric);
        if (x + bz_text_w(name, BZ_FONT_SMALL, 1) <= right - 12)
            bz_text(x, 2, name, BZ_FONT_SMALL, BZ_MID);
        int unit_end = x + bz_text_w(r.unit, BZ_FONT_LARGE, 1);
        if (unit_end <= HERO_MAIN_W) {
            bz_text(x, BIG_FOOT - 13, r.unit, BZ_FONT_LARGE, BZ_WHITE);
        } else {
            unit_end = x + bz_text_w(r.unit, BZ_FONT_SMALL, 1);
            bz_text(x, BIG_FOOT - 8, r.unit, BZ_FONT_SMALL, BZ_WHITE);
        }
        if (*r.unit && unit_end > badge_left - 2) badge_left = HERO_MAIN_W;
    } else {
        // Long text such as an address: one normal-height line.
        bz_text(0, 0, bz_metric_name(metric), BZ_FONT_SMALL, BZ_MID);
        bz_clip(0, 0, HERO_MAIN_W, BZ_H);
        bz_text(0, 18, r.value, BZ_FONT_LARGE, BZ_WHITE);
        bz_clip_reset();
    }
    if (*bz.data.asic && badge_left < HERO_MAIN_W) draw_badge_right(right, 11, bz.data.asic);

    if (!r.gauge) {
        // Nothing to graph: use the space for who and where this miner is.
        char device[40];
        snprintf(device, sizeof(device), "%s %s", bz.data.family, bz.data.asic);
        draw_info_row(42, "HOST", device);
        draw_info_row(54, "IP", bz.data.ip);
        return;
    }

    draw_graph(0, 35, BZ_GRAPH_POINTS + 1, 28, metric, &r);
}

// Side cell: the metric's icon with its value straight after it. The value
// steps down a font when it is too long, so nothing is cut short without
// first trying a smaller size.
static void draw_side_cell(int x, int y, bz_metric_t metric) {
    if (metric == BZ_M_NONE) return;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);
    const int w = HERO_CELL_W;
    const int text_x = x + BZ_ICON_SIZE + 3, room = w - BZ_ICON_SIZE - 3;
    int small_w = bz_text_w(r.value, BZ_FONT_SMALL, 1);
    // Too long even for the small font: the tiny font, on the full width
    // under a raised icon.
    bool tiny = small_w > room + 2;
    bz_icon(x, tiny ? y : y + 3, metric, BZ_WHITE);

    char joined[sizeof(r.value) + sizeof(r.unit)];
    snprintf(joined, sizeof(joined), "%s%s", r.value, r.unit);
    if (tiny) {
        bz_clip(x, y, w, 16);
        bz_text(x, y + 11, r.value, BZ_FONT_TINY, BZ_WHITE);
        bz_clip_reset();
    } else if (*r.unit && bz_text_w(joined, BZ_FONT_LARGE, 1) <= room) {
        // Short units ride on the value: "58C", "55.0W".
        bz_text(text_x, y + 1, joined, BZ_FONT_LARGE, BZ_WHITE);
    } else if (bz_text_w(r.value, BZ_FONT_LARGE, 1) <= room) {
        bz_text(text_x, y + 1, r.value, BZ_FONT_LARGE, BZ_WHITE);
    } else {
        bz_text(small_w > room ? text_x - 2 : text_x, y + 4, r.value, BZ_FONT_SMALL, BZ_WHITE);
    }
}

// Info line: icon, then text in the small font, or the tiny font when the
// small one would not fit.
static void draw_info_line(int y, bz_metric_t metric, const char *text) {
    const int x = HERO_SIDE_X + BZ_ICON_SIZE + 3, room = BZ_W - x;
    bz_icon(HERO_SIDE_X, y, metric, BZ_WHITE);
    if (!*text) text = "--";
    if (bz_text_w(text, BZ_FONT_SMALL, 1) <= room) {
        bz_text(x, y + 1, text, BZ_FONT_SMALL, BZ_WHITE);
    } else {
        bz_clip(x, y, room, BZ_ICON_SIZE);
        bz_text(x, y + 2, text, BZ_FONT_TINY, BZ_WHITE);
        bz_clip_reset();
    }
}

static void draw_hero(const uint8_t *slots) {
    draw_hero_main((bz_metric_t)slots[0]);
    bz_vdots(HERO_SPLIT_X, 0, BZ_H, BZ_MID);
    for (int i = 0; i < 4; ++i)
        draw_side_cell(HERO_SIDE_X + (i % 2) * HERO_CELL_PITCH, (i / 2) * 17, (bz_metric_t)slots[1 + i]);
    bz_hdots(HERO_SIDE_X, 36, BZ_W - HERO_SIDE_X, BZ_MID);
    draw_info_line(40, BZ_M_POOL, bz.data.pool);
    draw_info_line(53, BZ_M_NET_DIFF, bz.data.net_diff);
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
    static const uint8_t hero[5][4] = {{0, 0, 66, 32}, {66, 0, 27, 10}, {93, 0, 27, 10},
                                       {66, 10, 27, 10}, {93, 10, 27, 10}};
    static const uint8_t grid[6][4] = {{0, 0, 40, 16}, {40, 0, 40, 16}, {80, 0, 40, 16},
                                       {0, 16, 40, 16}, {40, 16, 40, 16}, {80, 16, 40, 16}};
    static const uint8_t duo[2][4] = {{0, 0, 60, 32}, {60, 0, 60, 32}};
    bz_page_t page = (bz_page_t)bz.settings.page;
    const uint8_t (*cells)[4] = page == BZ_PAGE_HERO ? hero : page == BZ_PAGE_GRID ? grid : duo;
    int count = bz_page_slot_count(page);
    if (page == BZ_PAGE_HERO) {
        // The fixed stratum host and network difficulty lines.
        bz_rect(x + 66, y + 20, 55, 13, BZ_MID);
        bz_text_center(x + 94, y + 24, "POOL+NET", BZ_FONT_TINY, BZ_DIM);
    }
    for (int i = 0; i < count; ++i) {
        int cx = x + cells[i][0], cy = y + cells[i][1], cw = cells[i][2], ch = cells[i][3];
        bz_rect(cx, cy, cw + 1, ch + 1, BZ_MID);
        const char *label = bz_metric_short((bz_metric_t)bz.settings.slots[page][i]);
        bz_text_center(cx + cw / 2 + 1, cy + (ch - 4) / 2, *label ? label : "-", BZ_FONT_TINY, BZ_WHITE);
        if (i == highlight && blink_on) bz_invert(cx + 1, cy + 1, cw - 1, ch - 1);
    }
}
