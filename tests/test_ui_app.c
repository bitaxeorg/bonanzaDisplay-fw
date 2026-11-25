// Host tests for the retro UI state machine (ui/app). No LVGL or Pico SDK.

#include "app/bz_app.h"
#include "app/bz_internal.h"
#include "app/bz_qr.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t flash[BZ_SETTINGS_BLOB_SIZE];
static bool flash_valid;
static unsigned saves;
static int brightness_calls, last_brightness;
static uint32_t now;
static bz_data_t data;

static bool load(uint8_t *blob, size_t size) {
    assert(size == sizeof(flash));
    if (flash_valid) memcpy(blob, flash, size);
    return flash_valid;
}

static bool save(const uint8_t *blob, size_t size) {
    assert(size == sizeof(flash));
    memcpy(flash, blob, size);
    flash_valid = true;
    saves++;
    return true;
}

static void set_brightness(uint8_t level) {
    brightness_calls++;
    last_brightness = level;
}

static const bz_platform_t platform = {load, save, set_brightness, "9.8.7", "abc1234"};

static void run(uint32_t ms) {
    for (uint32_t end = now + ms; (int32_t)(end - now) > 0;) {
        now += 33;
        bz_app_input(0, false, now);
        bz_app_tick(now);
    }
}

static void turn(int steps) {
    for (int i = 0; i < (steps < 0 ? -steps : steps); ++i) {
        now += 33;
        bz_app_input(steps < 0 ? -1 : 1, false, now);
        bz_app_tick(now);
    }
}

static void push(uint32_t held_ms) {
    for (uint32_t end = now + held_ms; (int32_t)(end - now) > 0;) {
        now += 33;
        bz_app_input(0, true, now);
        bz_app_tick(now);
    }
    run(66);
}

static void click(void) { push(100); }
static void hold(void) { push(900); }

static void host_up(void) {
    bz_data_reset(&data);
    data.link = BZ_LINK_CONNECTED;
    snprintf(data.family, sizeof(data.family), "Bonanza");
    snprintf(data.asic, sizeof(data.asic), "BM1370");
    snprintf(data.ip, sizeof(data.ip), "192.168.1.234");
    snprintf(data.best_diff, sizeof(data.best_diff), "4.29G");
    snprintf(data.pool, sizeof(data.pool), "public-pool.io");
    snprintf(data.net_diff, sizeof(data.net_diff), "129.44T");
    data.hashrate_ghs = 1210;
    data.chip_temp_c = 58;
    data.vr_temp_c = 47;
    data.power_mw = 17600;
    data.voltage_mv = 5020;
    data.current_ma = 3506;
    data.fan_rpm = 4200;
    data.shares_accepted = 1532;
    data.shares_rejected = 4;
    data.block_height = 921034;
    data.rssi_dbm = -58;
    bz_app_set_data(&data);
}

static void boot_to_mining(uint32_t start) {
    now = start;
    bz_app_init(&platform, now);
    host_up();
    run(6000);
    assert(bz_app_mode() == BZ_MODE_MINING);
}

static unsigned lit_pixels(void) {
    unsigned lit = 0;
    for (size_t i = 0; i < sizeof(bz_fb); ++i) lit += bz_fb[i] != 0;
    return lit;
}

static void test_metric_formatting(void) {
    bz_reading_t r;
    host_up();
    bz_metric_read(BZ_M_HASHRATE, &data, &r);
    assert(!strcmp(r.value, "1.21") && !strcmp(r.unit, "TH/s") && r.gauge && r.number == 1210.0f);
    data.hashrate_ghs = 512;
    bz_metric_read(BZ_M_HASHRATE, &data, &r);
    assert(!strcmp(r.value, "512") && !strcmp(r.unit, "GH/s") && r.unit_div == 1.0f);
    data.hashrate_ghs = 123456;
    bz_metric_read(BZ_M_HASHRATE, &data, &r);
    assert(!strcmp(r.value, "123.5") && !strcmp(r.unit, "TH/s"));
    data.hashrate_ghs = 1210;
    bz_metric_read(BZ_M_POWER, &data, &r);
    assert(!strcmp(r.value, "17.6") && !strcmp(r.unit, "W"));
    bz_metric_read(BZ_M_EFFICIENCY, &data, &r); // 17.6 W / 1.21 TH/s
    assert(!strcmp(r.value, "14.5") && !strcmp(r.unit, "J/TH") && r.valid);
    bz_metric_read(BZ_M_VOLTAGE, &data, &r);
    assert(!strcmp(r.value, "5.02") && !strcmp(r.unit, "V"));
    bz_metric_read(BZ_M_CURRENT, &data, &r);
    assert(!strcmp(r.value, "3.51") && !strcmp(r.unit, "A"));
    bz_metric_read(BZ_M_WIFI_RSSI, &data, &r);
    assert(!strcmp(r.value, "-58") && !r.gauge);
    bz_metric_read(BZ_M_CHIP_TEMP, &data, &r);
    assert(!strcmp(r.value, "58") && r.fixed_max == 100.0f);
    bz_metric_read(BZ_M_DEVICE, &data, &r);
    assert(!strcmp(r.value, "BM1370"));
    bz_metric_read(BZ_M_NET_DIFF, &data, &r);
    assert(!strcmp(r.value, "129.44T") && !*r.unit && !r.gauge);

    // Unknown values never show stale or garbage numbers.
    bz_data_reset(&data);
    for (int metric = 1; metric < BZ_M_COUNT; ++metric) {
        bz_metric_read((bz_metric_t)metric, &data, &r);
        assert(!r.valid && !strcmp(r.value, "--"));
        assert(strlen(bz_metric_name((bz_metric_t)metric)) <= 12);
        assert(strlen(bz_metric_short((bz_metric_t)metric)) <= 4);
    }
    data.power_mw = 17600;
    data.hashrate_ghs = 0; // Not hashing: no division by zero.
    bz_metric_read(BZ_M_EFFICIENCY, &data, &r);
    assert(!r.valid);
}

// Standard CRC-32, written independently of the one in bz_model.c.
static uint32_t crc32_of(const uint8_t *data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = crc & 1u ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
    }
    return ~crc;
}

static void set_crc(uint8_t blob[BZ_SETTINGS_BLOB_SIZE]) {
    uint32_t crc = crc32_of(blob, BZ_SETTINGS_BLOB_SIZE - 4);
    for (int i = 0; i < 4; ++i) blob[BZ_SETTINGS_BLOB_SIZE - 4 + i] = (uint8_t)(crc >> (8 * i));
}

static void test_settings_blob(void) {
    bz_settings_t a, b;
    uint8_t blob[BZ_SETTINGS_BLOB_SIZE];
    bz_settings_defaults(&a);
    a.page = BZ_PAGE_ASIC;
    a.brightness = 1;
    a.slots[BZ_PAGE_HERO][1] = BZ_M_POOL;
    bz_settings_encode(&a, blob);
    memset(&b, 0xee, sizeof(b));
    assert(bz_settings_decode(blob, &b) && !memcmp(&a, &b, sizeof(a)));

    // Erased flash, bit rot, and out-of-range values are all rejected.
    bz_settings_t untouched = b;
    uint8_t bad[BZ_SETTINGS_BLOB_SIZE];
    memset(bad, 0xff, sizeof(bad));
    assert(!bz_settings_decode(bad, &b));
    for (size_t i = 0; i < sizeof(bad); ++i) {
        memcpy(bad, blob, sizeof(bad));
        bad[i] ^= 0x40;
        assert(!bz_settings_decode(bad, &b));
    }
    a.slots[BZ_PAGE_HERO][0] = BZ_M_COUNT; // Valid CRC, invalid metric.
    bz_settings_encode(&a, bad);
    assert(!bz_settings_decode(bad, &b));
    assert(!memcmp(&untouched, &b, sizeof(b)));

    // Settings saved by firmware with an earlier set of pages still load.
    // HERO keeps its slots; a page that no longer exists lands on HERO.
    // Version 1 was HERO, GRID, DUO, STUDIO; version 2 HERO, GRID, DUO, TARGET.
    a.slots[BZ_PAGE_HERO][0] = BZ_M_POOL;
    bz_settings_encode(&a, blob);
    assert(blob[4] == 3);
    static const struct {
        uint8_t version, page, expect;
    } old[] = {{1, 0, BZ_PAGE_HERO}, {1, 1, BZ_PAGE_HERO}, {1, 2, BZ_PAGE_HERO}, {1, 3, BZ_PAGE_HERO},
               {2, 0, BZ_PAGE_HERO}, {2, 1, BZ_PAGE_HERO}, {2, 2, BZ_PAGE_HERO}, {2, 3, BZ_PAGE_TARGET}};
    for (size_t i = 0; i < sizeof(old) / sizeof(*old); ++i) {
        blob[4] = old[i].version;
        blob[6] = old[i].page;
        blob[7] = 1;                    // The big-digit style those versions stored
        memset(&blob[9 + 6], 7, 3 * 6); // The other pages' slots, as written then
        set_crc(blob);
        assert(bz_settings_decode(blob, &b) && b.page == old[i].expect && b.brightness == 1);
        assert(b.slots[BZ_PAGE_HERO][0] == BZ_M_POOL && b.slots[BZ_PAGE_HERO][1] == BZ_M_POOL);
        assert(b.slots[BZ_PAGE_TARGET][0] == BZ_M_NONE && b.slots[BZ_PAGE_ASIC][0] == BZ_M_NONE);
    }
    blob[6] = 4; // Never a valid page
    set_crc(blob);
    assert(!bz_settings_decode(blob, &b));
    bz_settings_encode(&a, blob);
    blob[6] = BZ_PAGE_COUNT; // Nor in today's blobs
    set_crc(blob);
    assert(!bz_settings_decode(blob, &b));
    blob[6] = 0;
    blob[4] = 4; // A version from the future
    set_crc(blob);
    assert(!bz_settings_decode(blob, &b));
}

static void test_boot_then_mining(void) {
    flash_valid = false;
    now = 5000;
    bz_app_init(&platform, now);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    assert(brightness_calls > 0 && last_brightness == BZ_BRIGHTNESS_LEVELS - 1);

    // No host: the loading screen stays up, and is actually drawing.
    run(60000);
    assert(bz_app_mode() == BZ_MODE_BOOT && lit_pixels() > 500);

    // Host answers but is not hashing yet: keep loading...
    host_up();
    data.hashrate_ghs = 0;
    bz_app_set_data(&data);
    run(10000);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    // ...until hashrate appears.
    data.hashrate_ghs = 1210;
    bz_app_set_data(&data);
    run(3000);
    assert(bz_app_mode() == BZ_MODE_MINING && lit_pixels() > 500);

    // Host restarts: back to loading, and to mining again when it returns.
    data.link = BZ_LINK_RESTARTING;
    bz_app_set_data(&data);
    run(100);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    run(5000);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    host_up();
    run(4000);
    assert(bz_app_mode() == BZ_MODE_MINING);

    // WiFi setup mode never reaches the mining screen on its own.
    data.link = BZ_LINK_AP_MODE;
    bz_app_set_data(&data);
    run(120000);
    assert(bz_app_mode() == BZ_MODE_BOOT);

    // A host that is connected but never hashes still gets a dashboard.
    host_up();
    data.hashrate_ghs = 0;
    bz_app_set_data(&data);
    run(20000);
    assert(bz_app_mode() == BZ_MODE_BOOT);
    run(10000);
    assert(bz_app_mode() == BZ_MODE_MINING);
    assert(saves == 0); // Nothing above changed a setting.
}

static void test_menu_changes_metrics(uint32_t start) {
    flash_valid = false;
    saves = 0;
    boot_to_mining(start);
    const bz_settings_t *s = bz_app_settings();
    assert(s->page == BZ_PAGE_HERO && s->slots[BZ_PAGE_HERO][1] == BZ_M_CHIP_TEMP);

    // SETUP > METRICS > TOP LEFT > turn to VOLTAGE > push.
    click();
    assert(bz_app_mode() == BZ_MODE_MENU);
    turn(1);
    click();  // METRICS
    turn(1);  // MAIN -> TOP LEFT
    click();  // Picker, cursor on CHIP TEMP
    turn(4);  // VR TEMP, POWER, EFFICIENCY, VOLTAGE
    assert(s->slots[BZ_PAGE_HERO][1] == BZ_M_VOLTAGE); // Live preview
    click();
    assert(s->slots[BZ_PAGE_HERO][1] == BZ_M_VOLTAGE);

    // Change MAIN, then cancel with a long push: the old metric returns.
    turn(-1);
    click();
    turn(3);
    assert(s->slots[BZ_PAGE_HERO][0] != BZ_M_HASHRATE);
    hold();
    assert(s->slots[BZ_PAGE_HERO][0] == BZ_M_HASHRATE);
    assert(bz_app_mode() == BZ_MODE_MENU && saves == 0); // Nothing written mid-edit

    // The picker stops at both ends; the last entry empties the slot.
    click();
    turn(100);
    assert(s->slots[BZ_PAGE_HERO][0] == BZ_M_NONE);
    turn(-100);
    assert(s->slots[BZ_PAGE_HERO][0] == BZ_M_HASHRATE);
    click();

    hold(); // METRICS -> SETUP
    hold(); // SETUP -> mining
    assert(bz_app_mode() == BZ_MODE_MINING);
    run(200);
    assert(saves == 1);

    // Power-cycle: the choice was stored.
    boot_to_mining(now + 1000);
    assert(bz_app_settings()->slots[BZ_PAGE_HERO][1] == BZ_M_VOLTAGE);
    assert(saves == 1);
}

static void test_menu_other_settings(void) {
    flash_valid = false;
    saves = 0;
    boot_to_mining(1000);
    const bz_settings_t *s = bz_app_settings();

    click();
    click();  // PAGE picker
    turn(1);  // HERO -> TARGET
    click();
    assert(s->page == BZ_PAGE_TARGET);
    turn(2);  // BRIGHTNESS
    click();
    turn(-3);
    assert(s->brightness == 0 && last_brightness == 0); // Applied while turning
    hold();   // Cancel
    assert(s->brightness == BZ_BRIGHTNESS_LEVELS - 1 && last_brightness == BZ_BRIGHTNESS_LEVELS - 1);
    click();
    turn(-2);
    click();
    assert(s->brightness == 1 && last_brightness == 1);

    // The metric menu edits HERO whichever page is showing.
    turn(-3);
    click();
    turn(5);   // Stops on the last page
    click();
    assert(s->page == BZ_PAGE_ASIC);
    turn(1);
    click();   // HERO METRICS
    turn(10);  // Stops on LOW RIGHT
    click();
    turn(-100);
    click();
    assert(s->slots[BZ_PAGE_HERO][4] == BZ_M_HASHRATE && s->page == BZ_PAGE_ASIC);
    assert(s->slots[BZ_PAGE_HERO][1] == BZ_M_CHIP_TEMP); // Other slots untouched
    hold();    // Back to SETUP

    // RESET ALL asks first.
    turn(2);
    click();
    click(); // NO, KEEP
    assert(s->page == BZ_PAGE_ASIC);
    click();
    turn(1);
    click(); // YES, RESET
    bz_settings_t defaults;
    bz_settings_defaults(&defaults);
    assert(!memcmp(s, &defaults, sizeof(defaults)) && last_brightness == defaults.brightness);

    turn(1);
    click(); // EXIT
    assert(bz_app_mode() == BZ_MODE_MINING);
    run(200);
    assert(saves == 1);

    // An untouched menu closes itself.
    click();
    assert(bz_app_mode() == BZ_MODE_MENU);
    run(29000);
    assert(bz_app_mode() == BZ_MODE_MENU);
    run(2000);
    assert(bz_app_mode() == BZ_MODE_MINING);
}

static void test_page_flip(void) {
    flash_valid = false;
    saves = 0;
    boot_to_mining(1000);
    const bz_settings_t *s = bz_app_settings();
    turn(1);
    assert(s->page == BZ_PAGE_TARGET);
    turn(1);
    assert(s->page == BZ_PAGE_ASIC);
    turn(1);
    assert(s->page == BZ_PAGE_HERO); // Wraps around
    turn(-1);
    assert(s->page == BZ_PAGE_ASIC);
    assert(saves == 0); // Several quick flips...
    run(4000);
    assert(saves == 1); // ...cost one flash write.
    run(60000);
    assert(saves == 1);

    // The dial does not flip pages behind the loading screen.
    data.link = BZ_LINK_WAITING;
    bz_app_set_data(&data);
    run(100);
    turn(1);
    assert(bz_app_mode() == BZ_MODE_BOOT && s->page == BZ_PAGE_ASIC);
    // The menu is still reachable there.
    click();
    assert(bz_app_mode() == BZ_MODE_MENU);
    hold();
    assert(bz_app_mode() == BZ_MODE_BOOT);
}

static void test_u256(void) {
    bz_u256_t v;
    // Difficulty 1 is 0xFFFF followed by 208 zero bits.
    assert(bz_u256_from_text("1", &v) && bz_u256_zeros(&v) == 8);
    assert(v.be[4] == 0xff && v.be[5] == 0xff && v.be[3] == 0 && v.be[6] == 0);
    assert(bz_u256_digit(&v, 8) == 15 && bz_u256_digit(&v, 12) == 0);
    double one = bz_u256_difficulty(&v);
    assert(one > 0.999 && one < 1.001);

    // A rounded difficulty only vouches for the digits it carries.
    assert(bz_u256_from_text("129.44T", &v) && bz_u256_zeros(&v) == 19 && v.digits == 23);
    double network = bz_u256_difficulty(&v);
    assert(network > 129.43e12 && network < 129.45e12);
    bz_u256_t best;
    assert(bz_u256_from_text("4.29G", &best) && best.digits == bz_u256_zeros(&best) + 2);
    double ratio = bz_u256_ratio(&best, &v);
    assert(ratio > 30100 && ratio < 30250);
    assert(bz_u256_from_text("512", &best) && bz_u256_from_text("2k", &best) && bz_u256_from_text("3K", &best));
    const char *bad[] = {"", "--", "-1", "abc", "1.2X", "1.2GG", "0", "nan", "inf"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        memset(&best, 0xee, sizeof(best));
        assert(!bz_u256_from_text(bad[i], &best) && best.digits == 0);
    }
    memset(&best, 0, sizeof(best)); // No value: nothing to compare.
    assert(bz_u256_zeros(&best) == 64 && bz_u256_difficulty(&best) == 0 && bz_u256_ratio(&best, &v) == 0);
    bz_u256_from_difficulty(1e-12, 4, &best); // Easier than any hash: clamps.
    assert(bz_u256_zeros(&best) == 0 && best.be[31] == 0xff);

    char text[16];
    bz_format_scaled(4.29e9, text, sizeof(text));
    assert(!strcmp(text, "4.29G"));
    bz_format_scaled(129.44e12, text, sizeof(text));
    assert(!strcmp(text, "129.44T"));
    bz_format_scaled(586.4, text, sizeof(text));
    assert(!strcmp(text, "586"));
    bz_format_scaled(999.996e3, text, sizeof(text));
    assert(strlen(text) < 12);
    bz_format_scaled(1e40, text, sizeof(text));
    assert(strlen(text) < 12);
}

static unsigned lit_in(int x0, int y0, int x1, int y1) {
    unsigned lit = 0;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) lit += bz_fb[y * BZ_W + x] != 0;
    return lit;
}

// Every metric has its own icon, and the HERO page always carries the stratum
// host and the network difficulty.
static void test_icons_and_hero_info(void) {
    static uint8_t seen[BZ_M_COUNT][BZ_ICON_SIZE * BZ_ICON_SIZE];
    bz_clip_reset();
    for (int metric = 0; metric < BZ_M_COUNT; ++metric) {
        bz_clear();
        bz_icon(3, 3, (bz_metric_t)metric, BZ_WHITE);
        unsigned lit = lit_pixels();
        assert(lit == lit_in(3, 3, 2 + BZ_ICON_SIZE, 2 + BZ_ICON_SIZE)); // Stays in its box
        assert(metric == BZ_M_NONE ? lit == 0 : lit >= 12);
        for (int y = 0; y < BZ_ICON_SIZE; ++y)
            memcpy(&seen[metric][y * BZ_ICON_SIZE], &bz_fb[(3 + y) * BZ_W + 3], BZ_ICON_SIZE);
        for (int other = 1; other < metric; ++other) assert(memcmp(seen[other], seen[metric], sizeof(seen[0])));
    }

    flash_valid = false;
    boot_to_mining(1000);
    assert(bz_app_settings()->page == BZ_PAGE_HERO);
    // Text of the two info lines, right of their icons.
    unsigned pool_line = lit_in(161, 42, 255, 50), net_line = lit_in(161, 53, 255, 61);
    assert(pool_line > 40 && net_line > 40);
    // A host that does not report network difficulty leaves a placeholder.
    data.net_diff[0] = '\0';
    bz_app_set_data(&data);
    run(100);
    unsigned placeholder = lit_in(161, 53, 255, 61);
    assert(lit_in(161, 42, 255, 50) == pool_line && placeholder > 0 && placeholder < net_line);
    // WiFi bars and the ASIC badge end the device line at the top; the hero
    // zone has nothing above its graph but the value, its name and its unit.
    unsigned status = lit_in(210, 0, 255, 8);
    assert(status > 60 && lit_in(133, 0, 141, 33) == 0);
    // The device line shows the family name until the host reports its own.
    static uint8_t family_frame[BZ_W * BZ_H];
    memcpy(family_frame, bz_fb, sizeof(family_frame));
    unsigned name = lit_in(149, 0, 205, 8);
    assert(name > 20);
    snprintf(data.hostname, sizeof(data.hostname), "bitaxe-shop");
    bz_app_set_data(&data);
    run(100);
    assert(lit_in(149, 0, 205, 8) > name && lit_in(210, 0, 255, 8) == status);
    data.hostname[0] = '\0';
    bz_app_set_data(&data);
    run(100);
    assert(!memcmp(&family_frame[0], &bz_fb[0], BZ_W * 9));
    data.asic[0] = '\0';
    data.rssi_dbm = BZ_RSSI_UNKNOWN;
    bz_app_set_data(&data);
    run(100);
    assert(lit_in(210, 0, 255, 8) < status);
}

// Width of the solid bar that starts at x on row y.
static int bar_width(int x, int y) {
    int w = 0;
    while (x + w < BZ_W && bz_fb[y * BZ_W + x + w]) ++w;
    return w;
}

// The TARGET page draws each number's leading zeros as a bar, six pixels a
// digit, and a line down from where the target's bar ends.
static void test_target_page(void) {
    bz_settings_t s;
    bz_settings_defaults(&s);
    s.page = BZ_PAGE_TARGET;
    bz_settings_encode(&s, flash);
    flash_valid = true;
    boot_to_mining(1000);
    assert(bz_app_settings()->page == BZ_PAGE_TARGET);
    // Only difficulties, as today's host sends them: 19 and 15 zeros, and no
    // latest share.
    assert(bar_width(41, 10) == 19 * 6 + 1 && bar_width(41, 21) == 15 * 6 + 1);
    assert(bar_width(41, 32) == 0);
    unsigned footer = lit_in(0, 48, 255, 60);
    assert(footer > 100);

    // Exact values from a host that sends them.
    bz_u256_from_difficulty(129.44e12, 6, &data.net_target);
    bz_u256_from_difficulty(64e9, 20, &data.best_hash);
    bz_u256_from_difficulty(600, 20, &data.last_hash);
    data.net_target.digits = data.best_hash.digits = data.last_hash.digits = 64;
    bz_app_set_data(&data);
    run(100);
    assert(bar_width(41, 10) == 19 * 6 + 1);
    assert(bar_width(41, 21) == bz_u256_zeros(&data.best_hash) * 6 + 1);
    assert(bar_width(41, 32) == bz_u256_zeros(&data.last_hash) * 6 + 1);
    assert(bz_u256_zeros(&data.best_hash) == 16 && bz_u256_zeros(&data.last_hash) == 10);
    assert(bz_fb[23 * BZ_W + 41 + 19 * 6] != 0 || bz_fb[24 * BZ_W + 41 + 19 * 6] != 0); // The line

    // A share at or below the target is a block.
    data.best_hash = data.net_target;
    bz_app_set_data(&data);
    run(100);
    assert(lit_in(0, 48, 255, 60) != footer);

    // Nothing known yet: no bars, and no line.
    memset(&data.net_target, 0, sizeof(data.net_target));
    memset(&data.best_hash, 0, sizeof(data.best_hash));
    memset(&data.last_hash, 0, sizeof(data.last_hash));
    data.net_diff[0] = data.best_diff[0] = '\0';
    bz_app_set_data(&data);
    run(100);
    assert(bar_width(41, 10) == 0 && bar_width(41, 21) == 0 && lit_in(41, 19, 255, 31) < 200);
}

// The ASIC page: one row per chip, and the domain block steps through
// hashrate, voltage and frequency.
static void test_asic_page(void) {
    bz_settings_t s;
    bz_settings_defaults(&s);
    s.page = BZ_PAGE_ASIC;
    bz_settings_encode(&s, flash);
    flash_valid = true;
    boot_to_mining(1000);
    assert(bz_app_settings()->page == BZ_PAGE_ASIC);
    // A host that reports no per-chip detail: a notice, not an empty table.
    unsigned waiting = lit_pixels();
    assert(waiting > 300 && lit_in(0, 0, 255, 12) == 0);

    data.asic_count = 4;
    data.domain_count = 4;
    for (int chip = 0; chip < 4; ++chip) {
        bz_asic_t *a = &data.asics[chip];
        a->temp_c = 56u + (uint32_t)chip;
        a->voltage_mv = 2800;
        a->frequency_mhz = 800;
        a->hashrate_ghs = 740;
        for (int d = 0; d < 4; ++d) {
            a->domain_voltage_mv[d] = 700;
            a->domain_frequency_mhz[d] = 800;
            a->domain_hashrate_ghs[d] = 185;
        }
    }
    bz_app_set_data(&data);
    run(100);
    // Four numbered rows, with numbers on both sides of the divider.
    for (int chip = 0; chip < 4; ++chip) {
        int y = 14 + chip * 12;
        assert(bz_fb[y * BZ_W] != 0 && lit_in(12, y, 123, y + 7) > 60 && lit_in(131, y, 255, y + 7) > 60);
    }
    // The domain block changes view on its own; the chip block does not.
    static uint8_t first[BZ_W * BZ_H];
    memcpy(first, bz_fb, sizeof(first));
    run(4000);
    bool left_same = true, right_same = true;
    for (int y = 0; y < BZ_H; ++y) {
        left_same = left_same && !memcmp(&first[y * BZ_W], &bz_fb[y * BZ_W], 126);
        right_same = right_same && !memcmp(&first[y * BZ_W + 131], &bz_fb[y * BZ_W + 131], 125);
    }
    assert(left_same && !right_same);
    run(8000); // Three views in all: back to the first.
    assert(!memcmp(first, bz_fb, sizeof(first)));

    // Fewer chips and domains, and values the host left out.
    for (int chips = 1; chips <= 4; ++chips) {
        for (int domains = 0; domains <= 4; ++domains) {
            data.asic_count = (uint8_t)chips;
            data.domain_count = (uint8_t)domains;
            data.asics[0].voltage_mv = chips % 2 ? BZ_UNKNOWN : 4000000000u;
            data.asics[chips - 1].domain_hashrate_ghs[0] = BZ_UNKNOWN;
            bz_app_set_data(&data);
            run(4100);
            assert(lit_in(0, 14 + chips * 12, 126, 63) == 0); // No row past the last chip
        }
    }
}

// ---- QR codes -------------------------------------------------------------------

static uint8_t qr_gf_mul(uint8_t a, uint8_t b) {
    unsigned product = 0;
    for (; b; b >>= 1) {
        if (b & 1) product ^= a;
        a = (uint8_t)((a << 1) ^ (a & 0x80 ? 0x11d : 0));
    }
    return (uint8_t)product;
}

static bool qr_is_fixed(int size, int x, int y) {
    if (x == 6 || y == 6) return true;                                  // Timing lines
    if ((x < 9 && y < 9) || (x >= size - 8 && y < 9) || (x < 9 && y >= size - 8)) return true;
    return size > 21 && x >= size - 9 && x <= size - 5 && y >= size - 9 && y <= size - 5;
}

// Reads a code back the way a scanner would, written separately from the
// encoder: both copies of the format information, the mask, the codewords,
// the error correction check, then the text. Returns the text length, or -1
// for anything that is not a sound code. *level gets 0..3 for L, M, Q, H.
static int qr_read(const bz_qr_t *qr, char *text, size_t capacity, int *level) {
    static const uint8_t totals[3] = {26, 44, 70};
    static const uint8_t ec_table[3][4] = {{7, 10, 13, 17}, {10, 16, 22, 28}, {15, 26, 36, 44}};
    static const int8_t level_of_bits[4] = {1, 0, 3, 2};
    int size = qr->size;
    if (size != 21 && size != 25 && size != 29) return -1;
    int version = (size - 17) / 4;

    unsigned first = 0, second = 0;
    for (int i = 0; i <= 5; ++i) first |= (unsigned)bz_qr_dark(qr, 8, i) << i;
    first |= (unsigned)bz_qr_dark(qr, 8, 7) << 6 | (unsigned)bz_qr_dark(qr, 8, 8) << 7 |
             (unsigned)bz_qr_dark(qr, 7, 8) << 8;
    for (int i = 9; i < 15; ++i) first |= (unsigned)bz_qr_dark(qr, 14 - i, 8) << i;
    for (int i = 0; i < 8; ++i) second |= (unsigned)bz_qr_dark(qr, size - 1 - i, 8) << i;
    for (int i = 8; i < 15; ++i) second |= (unsigned)bz_qr_dark(qr, 8, size - 15 + i) << i;
    if (first != second || !bz_qr_dark(qr, 8, size - 8)) return -1;
    unsigned format = first ^ 0x5412u, check = format;
    for (int i = 14; i >= 10; --i)
        if (check >> i & 1u) check ^= 0x537u << (i - 10);
    if (check) return -1; // Not a valid format word
    int ec_level = level_of_bits[format >> 13], mask = (int)(format >> 10 & 7u);
    int total = totals[version - 1], ec = ec_table[version - 1][ec_level], count = total - ec;

    uint8_t codewords[70] = {0};
    int bit = 0;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        bool up = ((right + 1) & 2) == 0;
        for (int step = 0; step < size; ++step)
            for (int x = right; x > right - 2; --x) {
                int y = up ? size - 1 - step : step;
                if (qr_is_fixed(size, x, y) || bit >= total * 8) continue;
                bool flip = mask == 0   ? (x + y) % 2 == 0
                            : mask == 1 ? y % 2 == 0
                            : mask == 2 ? x % 3 == 0
                            : mask == 3 ? (x + y) % 3 == 0
                            : mask == 4 ? (y / 2 + x / 3) % 2 == 0
                            : mask == 5 ? x * y % 2 + x * y % 3 == 0
                            : mask == 6 ? (x * y % 2 + x * y % 3) % 2 == 0
                                        : ((x + y) % 2 + x * y % 3) % 2 == 0;
                if (bz_qr_dark(qr, x, y) != flip) codewords[bit / 8] |= (uint8_t)(0x80 >> bit % 8);
                ++bit;
            }
    }
    if (bit != total * 8) return -1;

    // A sound codeword sequence is zero at each of the generator's roots.
    uint8_t root = 1;
    for (int i = 0; i < ec; ++i, root = qr_gf_mul(root, 2)) {
        uint8_t value = 0;
        for (int j = 0; j < total; ++j) value = qr_gf_mul(value, root) ^ codewords[j];
        if (value) return -1;
    }

    if (codewords[0] >> 4 != 4) return -1; // Byte mode
    int length = (codewords[0] & 15) << 4 | codewords[1] >> 4;
    if (length + 2 > count || (size_t)length >= capacity) return -1;
    for (int i = 0; i < length; ++i)
        text[i] = (char)((codewords[i + 1] & 15) << 4 | codewords[i + 2] >> 4);
    text[length] = '\0';
    if (level) *level = ec_level;
    return length;
}

static void test_qr(void) {
    bz_qr_t qr;
    char text[64], back[64];
    int level;

    // Every length that fits comes back as it went in, in the smallest size.
    for (int length = 1; length <= BZ_QR_MAX_TEXT; ++length) {
        for (int i = 0; i < length; ++i) text[i] = (char)(' ' + (i * 37 + length * 11) % 95);
        text[length] = '\0';
        assert(bz_qr_encode(text, &qr));
        assert(qr.size == (length <= 17 ? 21 : length <= 32 ? 25 : 29));
        assert(qr_read(&qr, back, sizeof(back), &level) == length && !strcmp(back, text));
        // Three finders, a light ring inside each.
        assert(bz_qr_dark(&qr, 0, 0) && !bz_qr_dark(&qr, 1, 1) && bz_qr_dark(&qr, 3, 3));
        assert(bz_qr_dark(&qr, qr.size - 1, 0) && bz_qr_dark(&qr, 0, qr.size - 1));
        assert(!bz_qr_dark(&qr, 7, 7) && !bz_qr_dark(&qr, qr.size, 0) && !bz_qr_dark(&qr, -1, 0));
        // Bytes above 127 too.
        for (int i = 0; i < length; ++i) text[i] = (char)(0x80 + (i * 53 + length) % 128);
        assert(bz_qr_encode(text, &qr) && qr_read(&qr, back, sizeof(back), NULL) == length);
        assert(!memcmp(back, text, (size_t)length));
    }
    memset(text, 'x', 54);
    text[54] = '\0';
    assert(!bz_qr_encode(text, &qr) && !bz_qr_encode("", &qr));

    // Spare room goes to error correction: H, Q, M, then L as the text grows.
    static const struct {
        int length, size, level;
    } cases[] = {{7, 21, 3}, {8, 21, 2}, {11, 21, 2}, {12, 21, 1}, {14, 21, 1}, {15, 21, 0},
                 {14 + 4, 25, 2}, {20, 25, 2}, {21, 25, 1}, {26, 25, 1}, {27, 25, 0},
                 {33, 29, 1}, {42, 29, 1}, {43, 29, 0}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        memset(text, 'a', (size_t)cases[i].length);
        text[cases[i].length] = '\0';
        assert(bz_qr_encode(text, &qr) && qr.size == cases[i].size);
        assert(qr_read(&qr, back, sizeof(back), &level) == cases[i].length && level == cases[i].level);
    }

    // The reader is not fooled by a damaged code.
    assert(bz_qr_encode("WIFI:S:Bitaxe_A1B2;;", &qr) && qr.size == 25);
    assert(qr_read(&qr, back, sizeof(back), &level) == 20 && level == 2);
    qr.rows[12] ^= 1u << 14;
    assert(qr_read(&qr, back, sizeof(back), NULL) < 0);
}

// ---- WiFi setup screen ------------------------------------------------------------

// The code on the WiFi setup screen, read back from the frame's pixels.
// Returns false if there is no sound code there.
static bool screen_code(char *text, size_t capacity) {
    const int field_x = BZ_W - 62, field_y = 1;
    for (int size = 21; size <= 29; size += 4) {
        bz_qr_t qr = {.size = (uint8_t)size};
        int origin = (62 - size * 2) / 2;
        bool whole = true;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const uint8_t *px = &bz_fb[(field_y + origin + y * 2) * BZ_W + field_x + origin + x * 2];
                // Each module is two pixels square, all dark or all lit.
                if (px[0] != px[1] || px[0] != px[BZ_W] || px[0] != px[BZ_W + 1] ||
                    (px[0] != 0 && px[0] != 255)) whole = false;
                if (!px[0]) qr.rows[y] |= 1u << x;
            }
        // A lit margin all round, at least a module wide.
        for (int i = 0; i < 62; ++i)
            for (int j = 0; j < 2; ++j)
                if (!bz_fb[(field_y + j) * BZ_W + field_x + i] || !bz_fb[(field_y + 61 - j) * BZ_W + field_x + i] ||
                    !bz_fb[(field_y + i) * BZ_W + field_x + j] || !bz_fb[(field_y + i) * BZ_W + field_x + 61 - j])
                    whole = false;
        if (whole && qr_read(&qr, text, capacity, NULL) > 0) return true;
    }
    return false;
}

static void test_wifi_setup(void) {
    char text[64];
    flash_valid = false;
    now = 1000;
    bz_app_init(&platform, now);

    // A host without WiFi is normal for the first seconds of a start: the
    // loading screen stays up.
    bz_data_reset(&data);
    data.link = BZ_LINK_AP_MODE;
    bz_app_set_data(&data);
    run(9500);
    assert(bz_app_mode() == BZ_MODE_BOOT && !screen_code(text, sizeof(text)));

    // Still none: the setup screen, with a code that opens the setup page,
    // since this host has not said what its network is called.
    run(1000);
    assert(bz_app_mode() == BZ_MODE_BOOT && screen_code(text, sizeof(text)));
    assert(!strcmp(text, "http://192.168.4.1"));
    assert(lit_in(0, 0, 188, 17) > 100);   // The headline
    assert(lit_in(190, 0, 193, 63) == 0);    // Nothing crowds the code
    // The headline's marker blinks.
    unsigned marker = lit_in(0, 0, 11, 15);
    run(500);
    assert(lit_in(0, 0, 11, 15) != marker);

    // A host that names its network gets a code that joins it.
    snprintf(data.ap_ssid, sizeof(data.ap_ssid), "Bitaxe_A1B2");
    bz_app_set_data(&data);
    run(100);
    assert(screen_code(text, sizeof(text)) && !strcmp(text, "WIFI:S:Bitaxe_A1B2;;"));
    // Characters with a meaning in the code are escaped.
    snprintf(data.ap_ssid, sizeof(data.ap_ssid), "a;b,c:d\"e\\f");
    bz_app_set_data(&data);
    run(100);
    assert(screen_code(text, sizeof(text)) && !strcmp(text, "WIFI:S:a\\;b\\,c\\:d\\\"e\\\\f;;"));
    // The longest name still fits; one too long to encode falls back to the page.
    snprintf(data.ap_ssid, sizeof(data.ap_ssid), "abcdefghijklmnopqrstuvwxyz012345");
    bz_app_set_data(&data);
    run(100);
    assert(screen_code(text, sizeof(text)) && !strcmp(text, "WIFI:S:abcdefghijklmnopqrstuvwxyz012345;;"));
    assert(lit_in(190, 0, 193, 63) == 0);
    snprintf(data.ap_ssid, sizeof(data.ap_ssid), ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;");
    bz_app_set_data(&data);
    run(100);
    assert(screen_code(text, sizeof(text)) && !strcmp(text, "http://192.168.4.1"));

    // The dial still opens the setup menu from here.
    click();
    assert(bz_app_mode() == BZ_MODE_MENU);
    push(900);
    assert(bz_app_mode() == BZ_MODE_BOOT && screen_code(text, sizeof(text)));

    // WiFi comes up: back to the loading screen, then on to mining.
    host_up();
    run(100);
    assert(bz_app_mode() == BZ_MODE_BOOT && !screen_code(text, sizeof(text)));
    run(6000);
    assert(bz_app_mode() == BZ_MODE_MINING);

    // WiFi drops while mining: the same wait, then the setup screen again.
    bz_data_reset(&data);
    data.link = BZ_LINK_AP_MODE;
    snprintf(data.ap_ssid, sizeof(data.ap_ssid), "Bitaxe_A1B2");
    bz_app_set_data(&data);
    run(9000);
    assert(bz_app_mode() == BZ_MODE_BOOT && !screen_code(text, sizeof(text)));
    run(1500);
    assert(screen_code(text, sizeof(text)) && !strcmp(text, "WIFI:S:Bitaxe_A1B2;;"));
    // A restart (as after saving WiFi settings) clears it straight away.
    data.link = BZ_LINK_RESTARTING;
    bz_app_set_data(&data);
    run(100);
    assert(!screen_code(text, sizeof(text)));
}

// Every page with every metric in every slot: exercises the drawing code
// under the sanitizers.
static void test_draw_everything(void) {
    for (int page = 0; page < BZ_PAGE_COUNT; ++page) {
        for (int metric = 0; metric < BZ_M_COUNT; ++metric) {
            bz_settings_t s;
            bz_settings_defaults(&s);
            s.page = (uint8_t)page;
            for (int slot = 0; slot < BZ_MAX_SLOTS; ++slot)
                s.slots[page][slot] = (uint8_t)((metric + slot) % BZ_M_COUNT);
            bz_settings_encode(&s, flash);
            flash_valid = true;
            boot_to_mining(1000);
            // Long strings and large numbers.
            snprintf(data.pool, sizeof(data.pool), "stratum.a-very-long-pool-hostname.example.org");
            snprintf(data.best_diff, sizeof(data.best_diff), "1234567890.12345T");
            data.hashrate_ghs = metric % 2 ? 4000000000u : 7;
            data.fan_rpm = 4000000000u;
            data.power_mw = 4000000000u;
            bz_app_set_data(&data);
            run(1500);
            click(); // And the menu preview for that arrangement.
            turn(1);
            click();
            turn(2);
            run(100);
        }
    }
}

int main(void) {
    test_metric_formatting();
    test_settings_blob();
    test_boot_then_mining();
    test_menu_changes_metrics(1000);
    test_menu_changes_metrics(UINT32_MAX - 20000u); // Millisecond counter wraps mid-test
    test_menu_other_settings();
    test_page_flip();
    test_u256();
    test_icons_and_hero_info();
    test_target_page();
    test_asic_page();
    test_qr();
    test_wifi_setup();
    test_draw_everything();
    puts("UI app tests passed");
    return 0;
}
