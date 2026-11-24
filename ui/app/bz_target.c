// TARGET page: what the miner is actually trying to do. A block is found when
// a hash comes out at or below the network target, so the page writes the
// target, the best share so far and the latest share out as hex, one above the
// other. Each number's leading zeros are drawn as a solid bar, so the three
// can be compared at a glance: the longer the bar, the smaller the number.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

#define ROW_H 9
#define LABEL_X 12
#define HEX_X 42
#define HEX_DIGITS 27  // Shown of each number's 64
#define DIGIT_W 6
#define FLASH_MS 250u

static struct {
    bz_u256_t last;      // Latest share drawn, to notice a new one
    uint32_t flash_start;
    bool flashing;
} t;

// Exact value if the host sent one, else the nearest thing its difficulty
// text allows.
static bz_u256_t resolve(const bz_u256_t *exact, const char *difficulty) {
    bz_u256_t value = *exact;
    if (!value.digits) bz_u256_from_text(difficulty, &value);
    return value;
}

static void difficulty_text(const bz_u256_t *value, const char *host_text, char *out, size_t size) {
    if (*host_text) snprintf(out, size, "%s", host_text);
    else if (value->digits) bz_format_scaled(bz_u256_difficulty(value), out, size);
    else snprintf(out, size, "--");
}

// "30,172x", or "1.25M TIMES" once the digits stop meaning much.
static void times_text(double ratio, char *out, size_t size) {
    if (ratio < 9999999.5) {
        unsigned long n = (unsigned long)(ratio + 0.5);
        if (n >= 1000000) snprintf(out, size, "%lu,%03lu,%03lux", n / 1000000, n / 1000 % 1000, n % 1000);
        else if (n >= 1000) snprintf(out, size, "%lu,%03lux", n / 1000, n % 1000);
        else snprintf(out, size, "%lux", n);
    } else {
        char scaled[12];
        bz_format_scaled(ratio, scaled, sizeof(scaled));
        snprintf(out, size, "%s TIMES", scaled);
    }
}

// One number: icon, name, hex, and the difficulty it stands for.
static void draw_row(int y, bz_metric_t icon, const char *name, const bz_u256_t *value,
                     const char *difficulty, bool flash) {
    if (flash) {
        bz_fill(0, y, BZ_ICON_SIZE, BZ_ICON_SIZE, BZ_WHITE);
        bz_icon(0, y, icon, BZ_BLACK);
    } else {
        bz_icon(0, y, icon, BZ_WHITE);
    }
    bz_text(LABEL_X, y + 2, name, BZ_FONT_TINY, BZ_MID);
    bz_text_right(BZ_W - 1, y + 1, difficulty, BZ_FONT_SMALL, BZ_WHITE);
    if (!value->digits) {
        bz_text(HEX_X, y + 2, "WAITING FOR HOST", BZ_FONT_TINY, BZ_DIM);
        return;
    }

    int zeros = bz_u256_zeros(value);
    if (zeros > HEX_DIGITS) zeros = HEX_DIGITS;
    if (zeros) bz_fill(HEX_X - 1, y, zeros * DIGIT_W + 1, ROW_H, BZ_WHITE);
    for (int i = 0; i < HEX_DIGITS; ++i) {
        int x = HEX_X + i * DIGIT_W;
        if (i >= value->digits) {
            bz_px(x + 2, y + 4, BZ_MID); // A digit the host's rounding hides
            continue;
        }
        char digit[2] = {"0123456789ABCDEF"[bz_u256_digit(value, i)], '\0'};
        bz_text(x, y + 1, digit, BZ_FONT_SMALL, i < zeros ? BZ_BLACK : BZ_WHITE);
    }
}

void bz_target_draw(void) {
    bz_u256_t target = resolve(&bz.data.net_target, bz.data.net_diff);
    bz_u256_t best = resolve(&bz.data.best_hash, bz.data.best_diff);
    const bz_u256_t *last = &bz.data.last_hash;

    // A new share lights its icon for a moment.
    if (last->digits && memcmp(last, &t.last, sizeof(*last))) {
        t.flashing = t.last.digits != 0;
        t.flash_start = bz.now;
        t.last = *last;
    }
    if (t.flashing && bz.now - t.flash_start >= FLASH_MS) t.flashing = false;

    bz_text(0, 1, "A BLOCK NEEDS A HASH AT OR BELOW THE NETWORK TARGET", BZ_FONT_TINY, BZ_MID);

    char text[20];
    difficulty_text(&target, bz.data.net_diff, text, sizeof(text));
    draw_row(10, BZ_M_NET_DIFF, "TARGET", &target, text, false);
    difficulty_text(&best, bz.data.best_diff, text, sizeof(text));
    draw_row(21, BZ_M_BEST_DIFF, "BEST", &best, text, false);
    difficulty_text(last, "", text, sizeof(text));
    draw_row(32, BZ_M_SHARES, "LAST", last, text, t.flashing);

    // The line a share's zeros have to reach.
    int target_zeros = bz_u256_zeros(&target);
    if (target.digits && target_zeros <= HEX_DIGITS)
        bz_vdots(HEX_X + target_zeros * DIGIT_W - 1, 10 + ROW_H, 22, BZ_WHITE);

    bz_hdots(0, 45, BZ_W, BZ_MID);
    if (!target.digits || !best.digits) {
        bz_text(0, 52, target.digits ? "WAITING FOR A SHARE" : "WAITING FOR THE NETWORK TARGET",
                BZ_FONT_TINY, BZ_MID);
        return;
    }
    double ratio = bz_u256_ratio(&best, &target);
    char line[48];
    if (ratio <= 1.0) {
        snprintf(line, sizeof(line), "BLOCK FOUND!");
    } else {
        times_text(ratio, text, sizeof(text));
        snprintf(line, sizeof(line), "BEST HASH IS %s TOO HIGH", text);
    }
    bz_text(0, 51, line, BZ_FONT_SMALL, BZ_WHITE);
    snprintf(line, sizeof(line), "%d OF %d ZEROS", bz_u256_zeros(&best), target_zeros);
    bz_text_right(BZ_W - 1, 53, line, BZ_FONT_TINY, BZ_MID);
}

// Thumbnail: three bars of zeros against the line they have to reach.
void bz_target_draw_preview(int x, int y) {
    static const uint8_t bars[3] = {78, 60, 40};
    bz_rect(x, y, 121, 33, BZ_MID);
    for (int i = 0; i < 3; ++i) {
        bz_fill(x + 6, y + 5 + i * 9, bars[i], 5, BZ_WHITE);
        bz_hdots(x + 6 + bars[i] + 2, y + 7 + i * 9, 108 - bars[i], BZ_MID);
    }
    bz_vdots(x + 6 + bars[0], y + 3, 28, BZ_WHITE);
}
