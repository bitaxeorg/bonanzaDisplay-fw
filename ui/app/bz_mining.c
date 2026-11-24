// The HERO overview page, whose slots the setup menu fills, and the entry
// points that hand the other pages to their own files.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

// ---- Big numbers ----------------------------------------------------------------

// Dot-matrix digits: 3x Portfolio, each font pixel a 2x2 dot.
int bz_big_text_w(const char *text) {
    return bz_text_w(text, BZ_FONT_SMALL, 3);
}

// A digit's first lit row is BIG_TOP below y and its last BIG_BASE below y,
// so the caller can line text up with either.
int bz_big_text(int x, int y, const char *text, int level) {
    return bz_text_ex(x, y + 3, text, BZ_FONT_SMALL, 3, true, level);
}

#define BIG_TOP 3
#define BIG_BASE 22
// Where small-font text goes to stand on the same baseline as the digits.
#define BIG_UNIT_Y (BIG_BASE - 6)

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
// left, and on the right a device line, four icon metrics, and the stratum
// host and network difficulty.
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

    if (metric == BZ_M_NONE) {
        bz_bitmap(0, 1, bz_logo_bits, bz_logo_w, bz_logo_h, BZ_WHITE);
    } else if (bz_big_text_w(r.value) <= 108) {
        // The value, then its name over its unit: the name level with the
        // top of the digits, the unit standing on their baseline.
        int x = bz_big_text(0, 0, r.value, BZ_WHITE) + 4;
        const char *name = bz_metric_name(metric);
        if (x + bz_text_w(name, BZ_FONT_SMALL, 1) > HERO_MAIN_W) name = bz_metric_short(metric);
        if (x + bz_text_w(name, BZ_FONT_SMALL, 1) <= HERO_MAIN_W)
            bz_text(x, BIG_TOP, name, BZ_FONT_SMALL, BZ_MID);
        bz_text(x, BIG_UNIT_Y, r.unit, BZ_FONT_SMALL, BZ_WHITE);
    } else {
        // Long text such as an address: one normal-height line.
        bz_icon(0, 1, metric, BZ_WHITE);
        bz_text(BZ_ICON_SIZE + 4, 2, bz_metric_name(metric), BZ_FONT_SMALL, BZ_MID);
        bz_clip(0, 0, HERO_MAIN_W, BZ_H);
        bz_text(0, 14, r.value, BZ_FONT_LARGE, BZ_WHITE);
        bz_clip_reset();
    }

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

// Side cell: the metric's icon, its value in the small font, and its unit in
// the tiny font, all on one line. A value too long for that loses its unit,
// then drops to the tiny font itself.
static void draw_side_cell(int x, int y, bz_metric_t metric) {
    if (metric == BZ_M_NONE) return;
    bz_reading_t r;
    bz_metric_read(metric, &bz.data, &r);
    const int text_x = x + BZ_ICON_SIZE + 3;
    const int room = HERO_CELL_PITCH - BZ_ICON_SIZE - 3 - 2; // Up to the next cell's icon
    int value_w = bz_text_w(r.value, BZ_FONT_SMALL, 1);
    int unit_w = *r.unit ? 1 + bz_text_w(r.unit, BZ_FONT_TINY, 1) : 0;
    bz_icon(x, y, metric, BZ_WHITE);
    bz_clip(text_x, y, room, BZ_ICON_SIZE);
    if (value_w > room) {
        bz_text(text_x, y + 3, r.value, BZ_FONT_TINY, BZ_WHITE);
    } else {
        int end = bz_text(text_x, y + 1, r.value, BZ_FONT_SMALL, BZ_WHITE);
        // Both fonts sit on the same baseline.
        if (unit_w && value_w + unit_w <= room) bz_text(end + 1, y + 3, r.unit, BZ_FONT_TINY, BZ_MID);
    }
    bz_clip_reset();
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

// Device line: the host's network name (its family name until it reports
// one), then the WiFi bars and the ASIC badge at the right end.
static void draw_device_line(int y) {
    int left = BZ_W;
    if (*bz.data.asic) {
        draw_badge_right(BZ_W - 1, y + 1, bz.data.asic);
        left -= badge_w(bz.data.asic) + 3;
    }
    left -= 11;
    draw_wifi_bars(left, y);
    bz_clip(HERO_SIDE_X, y, left - 3 - HERO_SIDE_X, 8);
    bz_text(HERO_SIDE_X, y + 2, *bz.data.hostname ? bz.data.hostname : bz.data.family, BZ_FONT_TINY, BZ_MID);
    bz_clip_reset();
}

// Three bands on the right: the device, four metrics, and the network.
static void draw_hero(const uint8_t *slots) {
    draw_hero_main((bz_metric_t)slots[0]);
    bz_vdots(HERO_SPLIT_X, 0, BZ_H, BZ_MID);
    draw_device_line(0);
    bz_hdots(HERO_SIDE_X, 11, BZ_W - HERO_SIDE_X, BZ_MID);
    for (int i = 0; i < 4; ++i)
        draw_side_cell(HERO_SIDE_X + (i % 2) * HERO_CELL_PITCH, 15 + (i / 2) * 11, (bz_metric_t)slots[1 + i]);
    bz_hdots(HERO_SIDE_X, 38, BZ_W - HERO_SIDE_X, BZ_MID);
    draw_info_line(42, BZ_M_POOL, bz.data.pool);
    draw_info_line(53, BZ_M_NET_DIFF, bz.data.net_diff);
}

// ---- Entry points -------------------------------------------------------------------

void bz_mining_draw(void) {
    switch (bz.settings.page) {
    case BZ_PAGE_TARGET: bz_target_draw(); break;
    case BZ_PAGE_ASIC: bz_asic_draw(); break;
    default: draw_hero(bz.settings.slots[BZ_PAGE_HERO]); break;
    }
}

// Thumbnail of a page in a 121x33 box. HERO shows one outlined cell per slot,
// labelled with the metric in it; neighbouring cells share a border.
void bz_mining_draw_preview(int x, int y, bz_page_t page, int highlight, bool blink_on) {
    static const uint8_t cells[5][4] = {{0, 0, 66, 32}, {66, 7, 27, 9}, {93, 7, 27, 9},
                                        {66, 16, 27, 9}, {93, 16, 27, 9}};
    if (page == BZ_PAGE_TARGET) {
        bz_target_draw_preview(x, y);
        return;
    }
    if (page == BZ_PAGE_ASIC) {
        bz_asic_draw_preview(x, y);
        return;
    }
    // The fixed bands: the device line above the metrics, and the stratum
    // host and network difficulty below.
    bz_rect(x + 66, y, 55, 8, BZ_MID);
    bz_text_center(x + 94, y + 2, "DEVICE", BZ_FONT_TINY, BZ_DIM);
    bz_rect(x + 66, y + 25, 55, 8, BZ_MID);
    bz_text_center(x + 94, y + 27, "POOL+NET", BZ_FONT_TINY, BZ_DIM);
    for (int i = 0; i < bz_page_slot_count(BZ_PAGE_HERO); ++i) {
        int cx = x + cells[i][0], cy = y + cells[i][1], cw = cells[i][2], ch = cells[i][3];
        bz_rect(cx, cy, cw + 1, ch + 1, BZ_MID);
        const char *label = bz_metric_short((bz_metric_t)bz.settings.slots[BZ_PAGE_HERO][i]);
        bz_text_center(cx + cw / 2 + 1, cy + (ch - 4) / 2, *label ? label : "-", BZ_FONT_TINY, BZ_WHITE);
        if (i == highlight && blink_on) bz_invert(cx + 1, cy + 1, cw - 1, ch - 1);
    }
}
