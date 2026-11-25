// Retro loading screen: a twinkling Bitaxe logo, a typed-out start-up log
// that follows the real host start-up, and a dithered progress bar. If the
// host stays without WiFi, the WiFi setup screen (bz_wifi.c) takes its place.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

#define LOG_COLS 28
#define LOG_ROWS 7
#define LOG_MAX 12
#define LOG_X 88
#define BAR_Y 58             // Progress bar: this row to the bottom edge
#define TYPE_MS 14u          // Per character
#define CRT_BLANK_MS 120u    // Power-on: dark, then a line, then the picture
#define CRT_LINE_MS 320u
#define CRT_OPEN_MS 720u
#define MIN_COLD_MS 3500u    // Let the animation play even if the host is up
#define READY_HOLD_MS 900u
#define NO_HASH_MS 25000u    // Connected but idle: show the dashboard anyway
// The host has no WiFi for the first few seconds of every start. Only after
// this long without it is that a problem worth a screen of its own.
#define WIFI_GRACE_MS 10000u

static struct {
    bool cold;
    uint32_t start;
    char lines[LOG_MAX][LOG_COLS + 1];
    int count;       // Lines queued
    int typed_line;  // First line not yet fully typed
    int typed_chars;
    uint32_t next_char;
    int wait_line;   // Line that ends in a spinner until its step is done, or -1
    bz_link_t last_link;
    bool said_link, said_host, said_wifi, said_asic, said_hash;
    uint32_t link_since;
    uint32_t ap_since;   // When the host said it had no WiFi
    bool wifi_setup;     // Showing the WiFi setup screen instead of the log
    bool wifi_failed;    // The log's WiFi line ended in FAILED
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

// Queues a line for a step that is still in progress. It shows a spinner
// until finish_wait() marks it done.
static void push_wait(const char *text) {
    push(text);
    b.wait_line = b.count - 1;
}

static void finish_wait(const char *result) {
    if (b.wait_line < 0) return;
    char *line = b.lines[b.wait_line];
    strncat(line, result, LOG_COLS - strlen(line));
    b.wait_line = -1;
}

static void push_link_line(void) {
    push_wait("BAP LINK 115200-8N1 ... ");
}

void bz_boot_enter(bool cold) {
    memset(&b, 0, sizeof(b));
    b.cold = cold;
    b.start = b.next_char = bz.now;
    b.wait_line = -1;
    b.last_link = BZ_LINK_WAITING;
    if (cold) {
        char line[64];
        if (bz.version && *bz.version) snprintf(line, sizeof(line), "BONANZA DISPLAY V%s", bz.version);
        else snprintf(line, sizeof(line), "BONANZA DISPLAY");
        push(line);
        if (bz.build && *bz.build) {
            snprintf(line, sizeof(line), "BUILD %s", bz.build);
            push(line);
        }
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
    bool link = b.said_link || bz.data.link == BZ_LINK_AP_MODE; // A host without WiFi still talks
    return 100 + 250 * link + 150 * b.said_host + 150 * b.said_wifi + 100 * b.said_asic +
           250 * b.said_hash;
}

void bz_boot_tick(void) {
    const bz_data_t *d = &bz.data;
    char line[64];

    // ---- Follow the host ------------------------------------------------------
    if (d->link != b.last_link) {
        if (d->link == BZ_LINK_AP_MODE) {
            // The host is talking but is not on WiFi.
            if (b.last_link == BZ_LINK_CONNECTED || !b.count) {
                b.wait_line = -1; // Whatever was in progress will not finish
                push("WIFI CONNECTION LOST");
            } else {
                finish_wait("OK"); // The link line
            }
            push_wait("WIFI CONNECTING ... ");
            b.ap_since = bz.now;
        } else if (d->link != BZ_LINK_CONNECTED) {
            b.wait_line = -1;
            push(d->link == BZ_LINK_RESTARTING ? "HOST RESTARTING" : "HOST LINK LOST");
            push_link_line();
        }
        if (d->link != BZ_LINK_CONNECTED) {
            b.said_link = b.said_host = b.said_wifi = b.said_asic = b.said_hash = false;
            b.ready = false;
        }
        if (d->link != BZ_LINK_AP_MODE && d->link != BZ_LINK_CONNECTED) b.wifi_failed = false;
        b.last_link = d->link;
    }
    b.wifi_setup = d->link == BZ_LINK_AP_MODE && bz.now - b.ap_since >= WIFI_GRACE_MS;
    if (b.wifi_setup && !b.wifi_failed) {
        b.wifi_failed = true;
        finish_wait("FAILED");
    }
    if (d->link == BZ_LINK_CONNECTED) {
        bool hashing = d->hashrate_ghs != BZ_UNKNOWN && d->hashrate_ghs > 0;
        if (!b.said_link) {
            b.said_link = true;
            b.link_since = bz.now;
            if (b.wait_line >= 0) finish_wait("OK");
            else push(b.wifi_failed ? "WIFI CONNECTED" : "BAP LINK OK");
            b.wifi_failed = false;
        }
        if (!b.said_host && *d->family && *d->asic) {
            b.said_host = true;
            snprintf(line, sizeof(line), "HOST %s %s", d->family, d->asic);
            push(line);
        }
        if (!b.said_wifi && *d->ip) {
            b.said_wifi = true;
            snprintf(line, sizeof(line), "WIFI %s", d->ip);
            push(line);
        }
        // ESP-Miner only brings up the ASIC once WiFi is connected, and
        // reports no hashrate until the chip is initialised and mining. Say
        // so, rather than sitting silently on the WiFi line.
        if (!b.said_asic && b.said_wifi && !hashing && !b.said_hash) {
            b.said_asic = true;
            snprintf(line, sizeof(line), "%.8s INIT ... ", *d->asic ? d->asic : "ASIC");
            push_wait(line);
        }
        if (!b.said_hash) {
            bool give_up = !hashing && bz.now - b.link_since >= NO_HASH_MS;
            if (hashing) {
                bz_reading_t r;
                bz_metric_read(BZ_M_HASHRATE, d, &r);
                finish_wait("OK");
                snprintf(line, sizeof(line), "HASHING %s %s", r.value, r.unit);
                push(line);
            } else if (give_up) {
                b.wait_line = -1;
                push("NO HASHRATE YET. STANDBY");
            }
            if (hashing || give_up) {
                // Skip any milestones the host never reported.
                b.said_hash = b.said_host = b.said_wifi = b.said_asic = true;
            }
        }
    }

    // ---- Typewriter -----------------------------------------------------------
    while (!typing_finished() && (int32_t)(bz.now - b.next_char) >= 0) {
        b.next_char += TYPE_MS;
        // A waiting line stays open (spinner after it) until its step is done.
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

    // ---- Progress bar ---------------------------------------------------------
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

// The logo shimmers. It rests dim; a band of brightness drifts through it,
// and individual pixels flare to full white and fade back, each on its own
// schedule, lighting their neighbours as they go. Everything derives from the
// clock and the pixel position, so no per-pixel state is kept.
//
// The panel's gray levels are linear in light output, so to the eye level 8
// is already three quarters of the way to white. The resting level has to be
// very low for the sparkles to stand out.
#define LOGO_REST 2          // Resting brightness
#define LOGO_RIPPLE 5        // The drifting band adds up to this much
#define TWINKLE_RISE_MS 100u
#define TWINKLE_FALL_MS 600u
#define TWINKLE_MIN_MS 1500u // Each pixel flares once in this long...
#define TWINKLE_SPREAD_MS 3000u // ...plus up to this much more

static uint32_t pixel_hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static bool logo_pixel(int x, int y) {
    if (x < 0 || y < 0 || x >= bz_logo_w || y >= bz_logo_h) return false;
    return bz_logo_bits[y * ((bz_logo_w + 7) / 8) + x / 8] & (0x80 >> (x % 8));
}

// How far through its flare a logo pixel is: 0 (resting) to 15 (peak).
static int flare(int x, int y) {
    if (!logo_pixel(x, y)) return 0;
    uint32_t h = pixel_hash(x, y);
    uint32_t period = TWINKLE_MIN_MS + h % TWINKLE_SPREAD_MS;
    uint32_t t = (bz.now + (h >> 12)) % period;
    if (t < TWINKLE_RISE_MS) return (int)(t * 15u / TWINKLE_RISE_MS);
    if (t < TWINKLE_RISE_MS + TWINKLE_FALL_MS)
        return (int)((TWINKLE_RISE_MS + TWINKLE_FALL_MS - t) * 15u / TWINKLE_FALL_MS);
    return 0;
}

static void draw_logo(int x0, int y0) {
    for (int y = 0; y < bz_logo_h; ++y) {
        for (int x = 0; x < bz_logo_w; ++x) {
            if (!logo_pixel(x, y)) continue;

            // Ripple: a triangle wave about 43 pixels long, drifting sideways
            // once every three seconds or so.
            uint32_t wave = ((uint32_t)(x * 3 + y * 5) * 8u + bz.now / 3u) & 1023u;
            int level = LOGO_REST +
                        (int)((wave < 512u ? wave : 1023u - wave) * (LOGO_RIPPLE + 1u) / 512u);

            // Twinkle: the pixel's own flare, or half of its brightest
            // neighbour's, so each sparkle is a small glint rather than a dot.
            int glint = flare(x, y);
            static const int8_t around[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int n = 0; n < 4; ++n) {
                int near = flare(x + around[n][0], y + around[n][1]) / 2;
                if (near > glint) glint = near;
            }
            if (glint > level) level = glint;
            bz_px(x0 + x, y0 + y, level);
        }
    }
}

// A plain checkered bar across the bottom edge.
static void draw_progress(void) {
    int fill = BZ_W * b.progress / 1000;
    if (fill <= 0) return;
    bz_dither(0, BAR_Y, fill, BZ_H - BAR_Y, BZ_WHITE, (int)((bz.now / 200u) & 1u));
    if (fill < BZ_W) bz_vline(fill - 1, BAR_Y, BZ_H - BAR_Y, BZ_WHITE);
}

void bz_boot_draw(void) {
    if (b.wifi_setup) {
        bz_wifi_draw();
        return;
    }
    uint32_t t = bz.now - b.start;
    if (b.cold && t < CRT_BLANK_MS) return;
    if (b.cold && t < CRT_LINE_MS) {
        // A horizontal line sweeps out from the centre, like a CRT warming up.
        int half = (int)((t - CRT_BLANK_MS) * (BZ_W / 2) / (CRT_LINE_MS - CRT_BLANK_MS));
        bz_fill(BZ_W / 2 - half, BZ_H / 2 - 1, half * 2, 2, BZ_WHITE);
        return;
    }

    draw_logo(3, 8);
    bz_text_center(41, 42, "OPEN SOURCE MINING", BZ_FONT_TINY, BZ_MID);
    draw_log();
    draw_progress();

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
