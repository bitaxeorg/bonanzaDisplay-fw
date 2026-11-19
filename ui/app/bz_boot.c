// Retro loading screen: the Bitaxe logo, a typed-out power-on self test log
// that follows the real host start-up, and a dithered progress meter.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

#define LOG_COLS 28
#define LOG_ROWS 5
#define LOG_MAX 12
#define LOG_X 88
#define TYPE_MS 14u          // Per character
#define CRT_BLANK_MS 120u    // Power-on: dark, then a line, then the picture
#define CRT_LINE_MS 320u
#define CRT_OPEN_MS 720u
#define MIN_COLD_MS 3500u    // Let the animation play even if the host is up
#define READY_HOLD_MS 900u
#define NO_HASH_MS 25000u    // Connected but idle: show the dashboard anyway

static struct {
    bool cold;
    uint32_t start;
    char lines[LOG_MAX][LOG_COLS + 1];
    int count;       // Lines queued
    int typed_line;  // First line not yet fully typed
    int typed_chars;
    uint32_t next_char;
    int wait_line;   // Line that ends in a spinner until the link is up, or -1
    bz_link_t last_link;
    bool said_link, said_host, said_wifi, said_hash;
    uint32_t link_since;
    int progress;    // 0..1000
    uint8_t creep;
    bool ready;
    uint32_t ready_since;
    bool done;
} b;

static void push(const char *text) {
    if (b.count == LOG_MAX) {
        memmove(b.lines[0], b.lines[1], sizeof(b.lines[0]) * (LOG_MAX - 1));
        b.count--;
        if (b.typed_line > 0) b.typed_line--;
        if (b.wait_line >= 0) b.wait_line--;
    }
    snprintf(b.lines[b.count++], sizeof(b.lines[0]), "%s", text);
}

static void push_link_line(void) {
    push("BAP LINK 115200-8N1 ... ");
    b.wait_line = b.count - 1;
}

void bz_boot_enter(bool cold) {
    memset(&b, 0, sizeof(b));
    b.cold = cold;
    b.start = b.next_char = bz.now;
    b.wait_line = -1;
    b.last_link = BZ_LINK_WAITING;
    if (cold) {
        push("BONANZA DISPLAY BIOS V3.1");
        push("RP2354A 256X64 16-GRAY  OK");
        push_link_line();
    } else if (bz.data.link == BZ_LINK_WAITING) {
        push("HOST LINK LOST");
        push_link_line();
    }
    // Other link states are announced by the first tick.
}

static bool typing_finished(void) {
    return b.typed_line >= b.count;
}

static int progress_target(void) {
    return 100 + 300 * b.said_link + 200 * b.said_host + 200 * b.said_wifi + 200 * b.said_hash;
}

void bz_boot_tick(void) {
    const bz_data_t *d = &bz.data;
    char line[64];

    // ---- Follow the host ------------------------------------------------------
    if (d->link != b.last_link) {
        if (d->link == BZ_LINK_AP_MODE) {
            push("HOST IN WIFI SETUP MODE");
            push("JOIN THE BITAXE AP TO SETUP");
        } else if (d->link == BZ_LINK_RESTARTING) {
            push("HOST RESTARTING");
        } else if (d->link == BZ_LINK_WAITING && b.last_link == BZ_LINK_CONNECTED) {
            push("HOST LINK LOST");
        }
        if (d->link != BZ_LINK_CONNECTED) {
            b.said_link = b.said_host = b.said_wifi = b.said_hash = false;
            b.ready = false;
            if (b.wait_line < 0) push_link_line();
        }
        b.last_link = d->link;
    }
    if (d->link == BZ_LINK_CONNECTED) {
        if (!b.said_link) {
            b.said_link = true;
            b.link_since = bz.now;
            if (b.wait_line >= 0) {
                strncat(b.lines[b.wait_line], "OK", LOG_COLS - strlen(b.lines[b.wait_line]));
                b.wait_line = -1;
            } else {
                push("BAP LINK OK");
            }
        }
        if (!b.said_host && *d->family && *d->asic) {
            b.said_host = true;
            snprintf(line, sizeof(line), "HOST %s %s", d->family, d->asic);
            push(line);
        }
        if (!b.said_wifi && *d->ip && strcmp(d->ip, "--")) {
            b.said_wifi = true;
            snprintf(line, sizeof(line), "WIFI %s", d->ip);
            push(line);
        }
        if (!b.said_hash) {
            bool hashing = d->hashrate_ghs != BZ_UNKNOWN && d->hashrate_ghs > 0;
            if (hashing) {
                bz_reading_t r;
                bz_metric_read(BZ_M_HASHRATE, d, &r);
                snprintf(line, sizeof(line), "ASIC HASHING %s %s", r.value, r.unit);
                push(line);
            } else if (bz.now - b.link_since >= NO_HASH_MS) {
                push("NO HASHRATE YET. STANDBY");
            }
            if (hashing || bz.now - b.link_since >= NO_HASH_MS) {
                // Skip any milestones the host never reported.
                b.said_hash = b.said_host = b.said_wifi = true;
            }
        }
    }

    // ---- Typewriter -----------------------------------------------------------
    while (!typing_finished() && (int32_t)(bz.now - b.next_char) >= 0) {
        b.next_char += TYPE_MS;
        // The spinner line stays open (cursor after it) until the link is up.
        int length = (int)strlen(b.lines[b.typed_line]);
        if (b.typed_chars < length) {
            b.typed_chars++;
        } else if (b.typed_line == b.wait_line && b.typed_line == b.count - 1) {
            b.next_char = bz.now;
            break;
        } else {
            b.typed_line++;
            b.typed_chars = 0;
            b.next_char += 8 * TYPE_MS; // Carriage return pause
        }
    }
    if (typing_finished()) b.next_char = bz.now;

    // ---- Progress meter -------------------------------------------------------
    int target = progress_target();
    if (b.progress < target) {
        int step = (target - b.progress) / 10;
        b.progress += step < 4 ? 4 : step;
        if (b.progress > target) b.progress = target;
    } else if (target < 1000 && b.progress < target + 180 && d->link != BZ_LINK_AP_MODE) {
        // Creep a little between milestones so a slow host still looks alive.
        if (++b.creep >= 12) {
            b.creep = 0;
            b.progress++;
        }
    } else if (b.progress > target + 180) {
        b.progress = target + 180;
    }

    // ---- Hand over to the mining screen ----------------------------------------
    bool ready = b.said_hash && d->link == BZ_LINK_CONNECTED && b.progress >= 1000 &&
                 typing_finished();
    if (ready && !b.ready) b.ready_since = bz.now;
    b.ready = ready;
    b.done = ready && bz.now - b.ready_since >= READY_HOLD_MS &&
             (!b.cold || bz.now - b.start >= MIN_COLD_MS);
}

bool bz_boot_done(void) {
    return b.done;
}

static void draw_log(void) {
    static const char spinner[] = "|/-\\";
    int last = b.typed_line < b.count ? b.typed_line : b.count - 1;
    int top = last >= LOG_ROWS ? last - LOG_ROWS + 1 : 0;
    for (int i = top; i <= last; ++i) {
        int y = (i - top) * 8;
        char text[LOG_COLS + 2];
        int shown = i < b.typed_line ? LOG_COLS : b.typed_chars;
        snprintf(text, sizeof(text), "%.*s", shown, b.lines[i]);
        int x = bz_text(LOG_X, y, text, BZ_FONT_SMALL, i == last ? BZ_WHITE : BZ_MID);
        if (i != last) continue;
        bool whole = shown >= (int)strlen(b.lines[i]);
        if (i == b.wait_line && whole) {
            char spin[2] = {spinner[(bz.now / 120u) % 4u], '\0'};
            bz_text(x, y, spin, BZ_FONT_SMALL, BZ_WHITE);
        } else if (!b.ready && x < BZ_W - 5 && (bz.now / 250u) % 2u == 0) {
            bz_fill(x, y, 5, 7, BZ_WHITE); // Block cursor
        }
    }
}

static void draw_meter(void) {
    const int x0 = 2, x1 = 216; // Fill travel
    char text[8];

    // Scale and ticks, 0..100 %.
    for (int i = 0; i <= 4; ++i) {
        int x = x0 + i * (x1 - x0) / 4;
        snprintf(text, sizeof(text), "%d", i * 25);
        if (i == 0) bz_text(0, 44, text, BZ_FONT_TINY, BZ_MID);
        else if (i == 4) bz_text_right(219, 44, text, BZ_FONT_TINY, BZ_MID);
        else bz_text_center(x + 1, 44, text, BZ_FONT_TINY, BZ_MID);
        bz_vline(x, 50, 2, BZ_WHITE);
    }
    bz_hdots(0, 53, 219, BZ_WHITE);
    bz_hdots(0, 63, 219, BZ_WHITE);
    bz_vline(0, 50, 14, BZ_WHITE);
    bz_vline(218, 50, 14, BZ_WHITE);

    int fill = (x1 - x0) * b.progress / 1000;
    if (fill > 0) {
        bz_dither(x0, 55, fill, 7, BZ_WHITE, (int)((bz.now / 200u) & 1u));
        bz_vline(x0 + fill, 55, 7, BZ_WHITE);
    }

    // Two-row status badge: percentage over an inverse state tag.
    const char *state = b.ready ? "READY"
                        : bz.data.link == BZ_LINK_AP_MODE ? "SETUP"
                        : bz.data.link == BZ_LINK_RESTARTING ? "RESET"
                        : bz.data.link == BZ_LINK_CONNECTED ? "LOAD" : "WAIT";
    snprintf(text, sizeof(text), "%d%%", b.progress / 10);
    bz_rect(223, 43, 33, 11, BZ_WHITE);
    bz_text_center(240, 45, text, BZ_FONT_SMALL, BZ_WHITE);
    bool flash = b.ready && (bz.now / 150u) % 2u;
    bz_fill(223, 54, 33, 10, flash ? BZ_BLACK : BZ_WHITE);
    bz_rect(223, 54, 33, 10, BZ_WHITE);
    bz_text_center(240, 55, state, BZ_FONT_SMALL, flash ? BZ_WHITE : BZ_BLACK);
}

void bz_boot_draw(void) {
    uint32_t t = bz.now - b.start;
    if (b.cold && t < CRT_BLANK_MS) return;
    if (b.cold && t < CRT_LINE_MS) {
        // A horizontal line sweeps out from the centre, like a CRT warming up.
        int half = (int)((t - CRT_BLANK_MS) * (BZ_W / 2) / (CRT_LINE_MS - CRT_BLANK_MS));
        bz_fill(BZ_W / 2 - half, BZ_H / 2 - 1, half * 2, 2, BZ_WHITE);
        return;
    }

    bz_bitmap(3, 1, bz_logo_bits, bz_logo_w, bz_logo_h, BZ_WHITE);
    bz_text_center(41, 34, "OPEN SOURCE MINING", BZ_FONT_TINY, BZ_MID);
    draw_log();
    draw_meter();

    if (b.cold && t < CRT_OPEN_MS) {
        // The picture opens vertically from that line.
        int half = 1 + (int)((t - CRT_LINE_MS) * (BZ_H / 2) / (CRT_OPEN_MS - CRT_LINE_MS));
        bz_fill(0, 0, BZ_W, BZ_H / 2 - half, BZ_BLACK);
        bz_fill(0, BZ_H / 2 + half, BZ_W, BZ_H / 2 - half, BZ_BLACK);
        int glow = 15 - 12 * half / (BZ_H / 2);
        bz_hline(0, BZ_H / 2 - half, BZ_W, glow);
        bz_hline(0, BZ_H / 2 + half - 1, BZ_W, glow);
    }
}
