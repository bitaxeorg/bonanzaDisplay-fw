#include "bap_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void receive(bonanza_bap_client_t *c, const char *command,
                    const char *parameter, const char *value, uint32_t now) {
    char frame[256];
    size_t n = bonanza_bap_format(frame, sizeof(frame), command, parameter, value);
    assert(n);
    bonanza_bap_client_receive(c, (const uint8_t *)frame, n, now);
}

static void test_metrics_and_stream(void) {
    bonanza_bap_client_t c;
    bonanza_metrics_t m;
    bonanza_bap_client_init(&c, 0);
    assert(bonanza_bap_client_take_update(&c, &m));
    assert(m.frequency_mhz == BONANZA_METRIC_UNKNOWN);
    assert(m.hashrate_ghs == BONANZA_METRIC_UNKNOWN);
    assert(m.link == BONANZA_LINK_WAITING && m.wifi_rssi_dbm == BONANZA_RSSI_UNKNOWN);
    assert(m.power_mw == BONANZA_METRIC_UNKNOWN && m.shares_accepted == BONANZA_METRIC_UNKNOWN);
    assert(!bonanza_bap_client_take_update(&c, &m));

    // Actual host response names and units, delivered one byte at a time.
    const char *parameters[] = {"deviceModel", "asicModel", "hashrate", "chipTemp", "power", "fan_speed", "best_difficulty", "wifi_ip"};
    const char *values[] = {"Bonanza", "BM1370", "1200.55", "58.250000", "17.60", "4200", "123T", "192.168.1.234"};
    for (size_t i = 0; i < sizeof(parameters) / sizeof(*parameters); ++i) {
        char frame[256];
        size_t n = bonanza_bap_format(frame, sizeof(frame), "RES", parameters[i], values[i]);
        for (size_t j = 0; j < n; ++j) bonanza_bap_client_receive(&c, (const uint8_t *)&frame[j], 1, 2000);
    }
    assert(bonanza_bap_client_take_update(&c, &m));
    assert(c.connected && !strcmp(m.device_family, "Bonanza"));
    assert(!strcmp(m.device_model, "BM1370"));
    assert(m.hashrate_ghs == 1201 && m.temperature_c == 58 && m.power_w == 18);
    assert(m.fan_rpm == 4200 && m.frequency_mhz == BONANZA_METRIC_UNKNOWN);
    assert(!strcmp(m.best_share, "123T") && !strcmp(m.ip_address, "192.168.1.234"));
    assert(m.link == BONANZA_LINK_CONNECTED && m.power_mw == 17600);

    // Fields behind the selectable mining-screen metrics, in host units.
    const char *more[][2] = {{"vrTemp", "47.000000"}, {"voltage", "5020.00"}, {"current", "3506.25"},
                             {"shares", "1532/4"}, {"block_height", "921034"},
                             {"wifi_rssi", "-58"}, {"pool", "public-pool.io"}};
    for (size_t i = 0; i < sizeof(more) / sizeof(*more); ++i) receive(&c, "RES", more[i][0], more[i][1], 2500);
    assert(bonanza_bap_client_take_update(&c, &m));
    assert(m.vr_temp_c == 47 && m.voltage_mv == 5020 && m.current_ma == 3506);
    assert(m.shares_accepted == 1532 && m.shares_rejected == 4 && m.block_height == 921034);
    assert(m.wifi_rssi_dbm == -58 && !strcmp(m.pool, "public-pool.io"));
    // Malformed share counts and signal strengths leave the last good values.
    const char *bad_more[][2] = {{"shares", "12"}, {"shares", "1/"}, {"shares", "/2"}, {"shares", "1/2/3"},
                                 {"shares", "-1/2"}, {"shares", "99999999999/1"}, {"wifi_rssi", "--5"},
                                 {"wifi_rssi", "-"}, {"wifi_rssi", "-5.5"}, {"wifi_rssi", "-999"}};
    for (size_t i = 0; i < sizeof(bad_more) / sizeof(*bad_more); ++i)
        receive(&c, "RES", bad_more[i][0], bad_more[i][1], 2600);
    assert(!bonanza_bap_client_take_update(&c, &m));
    assert(c.metrics.shares_accepted == 1532 && c.metrics.wifi_rssi_dbm == -58);

    // USB/log noise, CRLF split across calls, and multiple frames per chunk.
    const char noise[] = "BAP UART Interface Initialized\r\npartial$garbage";
    bonanza_bap_client_receive(&c, (const uint8_t *)noise, strlen(noise), 3000);
    char frames[512];
    size_t a = bonanza_bap_format(frames, 256, "RES", "hashrate", "0");
    size_t b = bonanza_bap_format(frames + a, 256, "RES", "power", "0");
    bonanza_bap_client_receive(&c, (const uint8_t *)frames, a + b, 3000);
    assert(bonanza_bap_client_take_update(&c, &m));
    assert(m.hashrate_ghs == 0 && m.power_w == 0 && m.power_mw == 0);
    assert(m.fan_rpm == 4200); // Partial updates preserve other metrics.
}

static void test_rejection_and_recovery(void) {
    bonanza_bap_client_t c;
    bonanza_metrics_t m;
    bonanza_bap_client_init(&c, 0);
    bonanza_bap_client_take_update(&c, &m);
    char frame[256];
    size_t n = bonanza_bap_format(frame, sizeof(frame), "RES", "hashrate", "123");
    frame[12] ^= 1; // Checksum mismatch.
    bonanza_bap_client_receive(&c, (const uint8_t *)frame, n, 1);
    assert(!bonanza_bap_client_take_update(&c, &m) && !c.connected);
    const char *bad[] = {"$BAP,RES,hashrate,3\r\n", "$BAP,RES,hashrate,3*ZZ\r\n", "$BAP,RES,hashrate,3*00x\n", "$OTHER,RES,hashrate,3*00\n"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
        bonanza_bap_client_receive(&c, (const uint8_t *)bad[i], strlen(bad[i]), 2);
    const char *bad_values[] = {"-1", "+2", "nan", "inf", "1e100", "4294967295", "42junk", "", " 42", "2,3"};
    for (size_t i = 0; i < sizeof(bad_values) / sizeof(*bad_values); ++i)
        receive(&c, "RES", "hashrate", bad_values[i], 3);
    receive(&c, "ACK", "fan_speed", "66", 4); // SET ACK is percent, never telemetry RPM.
    receive(&c, "RES", "poolPort", "3333", 4); // Sent by the host, not used here.
    receive(&c, "RES", "wifi_password", "ignored", 4);
    assert(!bonanza_bap_client_take_update(&c, &m) && !c.connected);

    uint8_t overlong[600];
    memset(overlong, 'x', sizeof(overlong));
    overlong[0] = '$';
    bonanza_bap_client_receive(&c, overlong, sizeof(overlong), 5);
    receive(&c, "RES", "power", "12.5", 6);
    assert(bonanza_bap_client_take_update(&c, &m) && m.power_w == 13);
    // Truncate long text safely, and retain commas inside valid text values.
    receive(&c, "RES", "deviceModel", "A very long family name exceeding the buffer,extra", 7);
    assert(bonanza_bap_client_take_update(&c, &m));
    assert(strlen(m.device_family) == sizeof(m.device_family) - 1);
    n = bonanza_bap_format(frame, sizeof(frame), "RES", "hashrate", "123");
    bonanza_bap_client_receive(&c, (const uint8_t *)frame, n / 2, 8);
    bonanza_bap_client_reset_stream(&c); // UART overrun must invalidate partial input.
    bonanza_bap_client_receive(&c, (const uint8_t *)frame + n / 2, n - n / 2, 8);
    assert(!bonanza_bap_client_take_update(&c, &m));
    receive(&c, "RES", "hashrate", "123", 9);
    assert(bonanza_bap_client_take_update(&c, &m) && m.hashrate_ghs == 123);
}

static void test_schedule_and_reconnect(uint32_t start) {
    bonanza_bap_client_t c;
    bonanza_bap_client_init(&c, start);
    char frame[256];
    assert(!bonanza_bap_client_next_message(&c, start + 999, frame, sizeof(frame)));
    const char *bodies[] = {"REQ,systemInfo", "SUB,hashrate,1000", "SUB,temperature,1000", "SUB,power,1000", "SUB,fan_speed,1000", "SUB,best_difficulty,1000", "SUB,wifi,1000",
                            "SUB,voltage,1000", "SUB,current,1000", "SUB,shares,1000", "SUB,block_height,1000"};
    for (size_t i = 0; i < sizeof(bodies) / sizeof(*bodies); ++i) {
        assert(bonanza_bap_client_next_message(&c, start + 1000 + (uint32_t)i * 100, frame, sizeof(frame)));
        assert(strstr(frame, bodies[i]));
        assert(!bonanza_bap_client_next_message(&c, start + 1001 + (uint32_t)i * 100, frame, sizeof(frame)));
    }
    // No host on first attempt: retry even when no response ever arrived.
    assert(!bonanza_bap_client_next_message(&c, start + 11900, frame, sizeof(frame)));
    assert(bonanza_bap_client_next_message(&c, start + 12000, frame, sizeof(frame)));
    assert(strstr(frame, "REQ,systemInfo"));
    for (unsigned i = 1; i <= 10; ++i)
        assert(bonanza_bap_client_next_message(&c, start + 12000 + i * 100, frame, sizeof(frame)));
    receive(&c, "RES", "deviceModel", "Bonanza", start + 13500);
    assert(c.metrics.link == BONANZA_LINK_CONNECTED);
    // Stable subscriptions may send nothing; keepalive requests still run.
    assert(bonanza_bap_client_next_message(&c, start + 22000, frame, sizeof(frame)));
    assert(strstr(frame, "REQ,systemInfo"));
    for (unsigned t = 23000; t < 73000; t += 1000) {
        receive(&c, "RES", "deviceModel", "Bonanza", start + t);
        bonanza_bap_client_next_message(&c, start + t, frame, sizeof(frame));
    }
    assert(bonanza_bap_client_next_message(&c, start + 73200, frame, sizeof(frame)));
    assert(c.cycle_active && strstr(frame, "REQ,systemInfo")); // One-minute renewal.
    assert(bonanza_bap_client_next_message(&c, start + 73300, frame, sizeof(frame)));
    assert(strstr(frame, "SUB,hashrate"));

    // A restarted/disconnected host clears stale data and retries subscriptions.
    receive(&c, "CMD", "mode", "ap_mode", start + 74000);
    assert(!c.connected && c.metrics.power_w == BONANZA_METRIC_UNKNOWN);
    assert(!strcmp(c.metrics.device_name, "Host WiFi AP"));
    assert(c.metrics.link == BONANZA_LINK_AP_MODE);
    receive(&c, "RES", "hashrate", "90000", start + 75000);
    assert(c.connected);
    bonanza_bap_client_next_message(&c, start + 105000, frame, sizeof(frame));
    assert(!c.connected && c.metrics.hashrate_ghs == BONANZA_METRIC_UNKNOWN);
    assert(c.metrics.link == BONANZA_LINK_WAITING);
}

static void test_stream_noise(void) {
    bonanza_bap_client_t c;
    bonanza_bap_client_init(&c, 0);
    uint32_t random = 0x12345678;
    uint8_t chunk[97];
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        for (size_t j = 0; j < sizeof(chunk); ++j) {
            random = random * 1664525u + 1013904223u;
            chunk[j] = (uint8_t)(random >> 24);
        }
        bonanza_bap_client_receive(&c, chunk, sizeof(chunk), iteration);
        receive(&c, "RES", "power", "123.5", iteration);
        assert(c.metrics.power_w == 124 && c.connected);
    }
    // A CR inside a field must not become a display control character.
    receive(&c, "RES", "deviceModel", "bad\rtext", 11000);
    assert(!strcmp(c.metrics.device_family, "BITAXE"));
    char message[256];
    // Exhaust initial cycle, then a host timeout status forces renewal.
    for (unsigned i = 0; i < 11; ++i)
        assert(bonanza_bap_client_next_message(&c, 12000 + i * 100, message, sizeof(message)));
    receive(&c, "STA", "power", "subscription_timeout", 13500);
    assert(bonanza_bap_client_next_message(&c, 13500, message, sizeof(message)));
    assert(strstr(message, "REQ,systemInfo"));
    receive(&c, "STA", "status", "restarting", 14000);
    assert(!c.connected && c.metrics.power_w == BONANZA_METRIC_UNKNOWN);
    assert(c.metrics.link == BONANZA_LINK_RESTARTING && c.metrics.power_mw == BONANZA_METRIC_UNKNOWN);
}

int main(void) {
    char frame[256];
    // XOR calculated independently: BAP,REQ,systemInfo -> 0x3E.
    assert(bonanza_bap_format(frame, sizeof(frame), "REQ", "systemInfo", NULL));
    assert(!strcmp(frame, "$BAP,REQ,systemInfo*3E\r\n"));
    assert(!bonanza_bap_format(frame, 5, "REQ", "systemInfo", NULL));
    test_stream_noise();
    test_metrics_and_stream();
    test_rejection_and_recovery();
    test_schedule_and_reconnect(0);
    test_schedule_and_reconnect(UINT32_MAX - 5000u);
    puts("BAP client tests passed");
    return 0;
}
