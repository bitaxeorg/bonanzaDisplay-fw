// ASIC page: one row per chip. On the left, the chip as a whole: temperature,
// hashrate, clock frequency and voltage. On the right, the same chip broken
// down by its internal domains, one metric at a time; the metric changes every
// few seconds, and the three icons above show which one is up.

#include "bz_internal.h"

#include <stdio.h>

#define HEADER_H 9
#define ROW_Y 14
#define ROW_PITCH 12
#define SPLIT_X 127
#define DOMAIN_X 131      // Domain block is x 131..255
#define VIEW_MS 4000u

typedef enum { VIEW_HASHRATE, VIEW_VOLTAGE, VIEW_FREQUENCY, VIEW_COUNT } view_t;

// Whole-chip columns, each a right edge with its heading: an icon and a unit.
static const struct {
    uint8_t right;
    uint8_t icon;
    const char *unit;
} columns[4] = {
    {33, BZ_M_CHIP_TEMP, "C"},
    {65, BZ_M_HASHRATE, "GH/S"},
    {93, BZ_M_FREQUENCY, "MHZ"},
    {123, BZ_M_VOLTAGE, "V"},
};

static const struct {
    uint8_t icon;
    const char *unit;
} views[VIEW_COUNT] = {
    [VIEW_HASHRATE] = {BZ_M_HASHRATE, "GH/S"},
    [VIEW_VOLTAGE] = {BZ_M_VOLTAGE, "MV"},
    [VIEW_FREQUENCY] = {BZ_M_FREQUENCY, "MHZ"},
};

static void number_right(int right, int y, uint32_t value) {
    char text[12];
    if (value == BZ_UNKNOWN) {
        bz_text_right(right, y, "--", BZ_FONT_SMALL, BZ_MID);
        return;
    }
    snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    bz_text_right(right, y, text, BZ_FONT_SMALL, BZ_WHITE);
}

// Millivolts as volts with two decimals: 2801 -> "2.80".
static void volts_right(int right, int y, uint32_t millivolts) {
    char text[12];
    if (millivolts == BZ_UNKNOWN || millivolts > 99999u) {
        number_right(right, y, BZ_UNKNOWN);
        return;
    }
    uint32_t hundredths = (millivolts + 5u) / 10u;
    snprintf(text, sizeof(text), "%lu.%02lu", (unsigned long)(hundredths / 100u),
             (unsigned long)(hundredths % 100u));
    bz_text_right(right, y, text, BZ_FONT_SMALL, BZ_WHITE);
}

static void draw_header(view_t view) {
    for (int i = 0; i < 4; ++i) {
        int unit_w = bz_text_w(columns[i].unit, BZ_FONT_TINY, 1);
        int x = columns[i].right - unit_w - 1 - BZ_ICON_SIZE;
        bz_icon(x, 0, (bz_metric_t)columns[i].icon, BZ_WHITE);
        bz_text(x + BZ_ICON_SIZE + 2, 3, columns[i].unit, BZ_FONT_TINY, BZ_MID);
    }
    bz_hdots(0, HEADER_H + 2, BZ_W, BZ_MID);
    bz_vdots(SPLIT_X, 0, BZ_H, BZ_MID);

    // The three domain views; the one showing is in inverse video.
    for (int i = 0; i < VIEW_COUNT; ++i) {
        int x = DOMAIN_X + i * (BZ_ICON_SIZE + 3);
        if (i == (int)view) bz_fill(x, 0, BZ_ICON_SIZE + 2, HEADER_H, BZ_WHITE);
        bz_icon(x + 1, 0, (bz_metric_t)views[i].icon, i == (int)view ? BZ_BLACK : BZ_MID);
    }
    char title[24];
    if (bz.data.domain_count > 1)
        snprintf(title, sizeof(title), "DOMAIN 1-%d %s", bz.data.domain_count, views[view].unit);
    else snprintf(title, sizeof(title), "DOMAIN %s", views[view].unit);
    bz_text_right(BZ_W - 1, 3, title, BZ_FONT_TINY, BZ_MID);
}

static void draw_chip(int index, int y, view_t view) {
    const bz_asic_t *chip = &bz.data.asics[index];
    char digit[2] = {(char)('1' + index), '\0'};
    bz_fill(0, y - 1, 9, 10, BZ_WHITE);
    bz_text(2, y, digit, BZ_FONT_SMALL, BZ_BLACK);

    number_right(columns[0].right, y, chip->temp_c);
    number_right(columns[1].right, y, chip->hashrate_ghs);
    number_right(columns[2].right, y, chip->frequency_mhz);
    volts_right(columns[3].right, y, chip->voltage_mv);

    int domains = bz.data.domain_count;
    if (domains < 1) return;
    const uint32_t *values = view == VIEW_HASHRATE ? chip->domain_hashrate_ghs
                             : view == VIEW_VOLTAGE ? chip->domain_voltage_mv
                             : chip->domain_frequency_mhz;
    int width = (BZ_W - DOMAIN_X) / domains;
    for (int d = 0; d < domains; ++d) number_right(DOMAIN_X + (d + 1) * width - 2, y, values[d]);
}

void bz_asic_draw(void) {
    view_t view = (view_t)(bz.now / VIEW_MS % VIEW_COUNT);
    if (bz.data.asic_count == 0) {
        // Today's ESP-Miner reports the chips only as one total.
        bz_icon(BZ_W / 2 - 4, 14, BZ_M_DEVICE, BZ_WHITE);
        bz_text_center(BZ_W / 2, 30, "WAITING FOR HOST", BZ_FONT_SMALL, BZ_WHITE);
        bz_text_center(BZ_W / 2, 44, "THIS PAGE NEEDS PER-CHIP DATA FROM THE HOST", BZ_FONT_TINY, BZ_MID);
        return;
    }
    draw_header(view);
    for (int i = 0; i < bz.data.asic_count && i < BZ_MAX_ASICS; ++i)
        draw_chip(i, ROW_Y + i * ROW_PITCH, view);
    if (bz.data.domain_count < 1)
        bz_text_center((DOMAIN_X + BZ_W) / 2, 32, "NO DOMAIN DATA", BZ_FONT_TINY, BZ_MID);
}

// Thumbnail in a 121x33 box: four chips, each a tag, a line and its domains.
void bz_asic_draw_preview(int x, int y) {
    bz_rect(x, y, 121, 33, BZ_MID);
    bz_vdots(x + 60, y + 3, 27, BZ_MID);
    for (int row = 0; row < 4; ++row) {
        int ry = y + 4 + row * 7;
        bz_fill(x + 5, ry, 4, 4, BZ_WHITE);
        bz_hline(x + 12, ry + 2, 42, BZ_WHITE);
        for (int d = 0; d < 4; ++d) bz_fill(x + 66 + d * 13, ry, 9, 4, BZ_WHITE);
    }
}
