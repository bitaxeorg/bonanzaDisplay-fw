#include "bap_client.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RETRY_MS 10000u
#define RENEW_MS 60000u // ESP-Miner subscriptions expire after five minutes.
#define RESPONSE_TIMEOUT_MS 30000u
#define COMMAND_SPACING_MS 100u

static const char *const subscriptions[] = {
    "hashrate", "temperature", "power", "fan_speed", "best_difficulty", "wifi",
    "voltage", "current", "shares", "block_height"
};
#define COMMAND_COUNT (1u + sizeof(subscriptions) / sizeof(subscriptions[0]))

static uint8_t checksum(const char *body) {
    uint8_t result = 0;
    while (*body) result ^= (uint8_t)*body++;
    return result;
}

size_t bonanza_bap_format(char *out, size_t capacity, const char *command,
                          const char *parameter, const char *value) {
    char body[BONANZA_BAP_MAX_MESSAGE];
    int n = value ? snprintf(body, sizeof(body), "BAP,%s,%s,%s", command, parameter, value)
                  : snprintf(body, sizeof(body), "BAP,%s,%s", command, parameter);
    if (n < 0 || (size_t)n >= sizeof(body) || (size_t)n + 7u > capacity ||
        (size_t)n + 6u >= BONANZA_BAP_MAX_MESSAGE) return 0;
    n = snprintf(out, capacity, "$%s*%02X\r\n", body, checksum(body));
    return n > 0 && (size_t)n < capacity ? (size_t)n : 0;
}

static void reset_metrics(bonanza_bap_client_t *client, const char *status,
                          bonanza_link_t link) {
    bonanza_metrics_t *m = &client->metrics;
    memset(m, 0, sizeof(*m));
    m->link = link;
    m->power_mw = m->vr_temp_c = m->voltage_mv = m->current_ma = m->shares_accepted =
        m->shares_rejected = m->block_height = BONANZA_METRIC_UNKNOWN;
    m->wifi_rssi_dbm = BONANZA_RSSI_UNKNOWN;
    snprintf(m->device_family, sizeof(m->device_family), "BITAXE");
    snprintf(m->device_name, sizeof(m->device_name), "%s", status);
    snprintf(m->ip_address, sizeof(m->ip_address), "--");
    snprintf(m->best_share, sizeof(m->best_share), "--");
    m->hashrate_ghs = m->temperature_c = m->power_w =
        m->frequency_mhz = m->fan_rpm = BONANZA_METRIC_UNKNOWN;
    client->connected = false;
    client->update_pending = true;
}

void bonanza_bap_client_reset_stream(bonanza_bap_client_t *client) {
    client->sentence_length = 0;
}

void bonanza_bap_client_init(bonanza_bap_client_t *client, uint32_t now_ms) {
    memset(client, 0, sizeof(*client));
    reset_metrics(client, "Waiting for BAP", BONANZA_LINK_WAITING);
    client->last_response_ms = client->last_probe_ms = now_ms;
    client->last_cycle_ms = now_ms;
    client->next_send_ms = now_ms + 1000u;
    client->cycle_active = true;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_number(const char *value, uint32_t *out) {
    // Reject signs, whitespace, non-finite values and partial conversions.
    if (*value < '0' || *value > '9') return false;
    char *end;
    errno = 0;
    double n = strtod(value, &end);
    if (errno || *end || !isfinite(n) || n < 0 || n >= UINT32_MAX - 0.5) return false;
    *out = (uint32_t)(n + 0.5);
    return true;
}

// Whole decimal digits only, e.g. a share count.
static bool read_count(const char *value, const char *end, uint32_t *out) {
    uint32_t n = 0;
    if (value == end) return false;
    for (; value != end; ++value) {
        if (*value < '0' || *value > '9' || n > (UINT32_MAX - 9u) / 10u) return false;
        n = n * 10u + (uint32_t)(*value - '0');
    }
    *out = n;
    return true;
}

// "accepted/rejected", as sent by the shares subscription.
static bool read_shares(const char *value, uint32_t *accepted, uint32_t *rejected) {
    const char *slash = strchr(value, '/');
    uint32_t a, r;
    if (!slash || !read_count(value, slash, &a) ||
        !read_count(slash + 1, slash + 1 + strlen(slash + 1), &r)) return false;
    *accepted = a;
    *rejected = r;
    return true;
}

// Signal strength in dBm: a small integer, normally negative.
static bool read_rssi(const char *value, int32_t *out) {
    bool negative = *value == '-';
    uint32_t n;
    if (negative) ++value;
    if (!read_count(value, value + strlen(value), &n) || n > 200u) return false;
    *out = negative ? -(int32_t)n : (int32_t)n;
    return true;
}

static void handle_sentence(bonanza_bap_client_t *client, uint32_t now_ms) {
    char *body = client->sentence + 1;
    char *star = strchr(body, '*');
    if (!star || strlen(star) != 3) return;
    int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    if (hi < 0 || lo < 0) return;
    *star = '\0';
    if (checksum(body) != (uint8_t)((hi << 4) | lo) || strncmp(body, "BAP,", 4)) return;

    char *command = body + 4;
    char *parameter = strchr(command, ',');
    if (!parameter) return;
    *parameter++ = '\0';
    char *value = strchr(parameter, ',');
    if (!value || !*parameter) return;
    *value++ = '\0'; // Retain commas inside values; never log received credentials.

    if ((!strcmp(command, "CMD") && !strcmp(parameter, "mode") && !strcmp(value, "ap_mode")) ||
        (!strcmp(command, "STA") && !strcmp(parameter, "status") && !strcmp(value, "restarting"))) {
        if (!strcmp(value, "ap_mode")) reset_metrics(client, "Host WiFi AP", BONANZA_LINK_AP_MODE);
        else reset_metrics(client, "Host restarting", BONANZA_LINK_RESTARTING);
        return;
    }
    if (!strcmp(command, "STA") && !strcmp(value, "subscription_timeout")) {
        client->last_cycle_ms = now_ms - RENEW_MS;
        return;
    }
    if (strcmp(command, "RES")) return;

    bonanza_metrics_t *m = &client->metrics;
    bool recognized = true;
    if (!strcmp(parameter, "deviceModel") && *value) {
        snprintf(m->device_family, sizeof(m->device_family), "%s", value);
    } else if (!strcmp(parameter, "asicModel") && *value) {
        snprintf(m->device_model, sizeof(m->device_model), "%s", value);
    } else if (!strcmp(parameter, "wifi_ip") && *value) {
        snprintf(m->ip_address, sizeof(m->ip_address), "%s", value);
    } else if (!strcmp(parameter, "best_difficulty") && *value) {
        snprintf(m->best_share, sizeof(m->best_share), "%s", value);
    } else if (!strcmp(parameter, "pool") && *value) {
        snprintf(m->pool, sizeof(m->pool), "%s", value);
    } else if (!strcmp(parameter, "shares")) {
        recognized = read_shares(value, &m->shares_accepted, &m->shares_rejected);
    } else if (!strcmp(parameter, "wifi_rssi")) {
        recognized = read_rssi(value, &m->wifi_rssi_dbm);
    } else if (!strcmp(parameter, "power")) {
        // Keep both resolutions in step: reject the pair if either is invalid.
        uint32_t watts;
        char *end;
        errno = 0;
        double n = strtod(value, &end);
        recognized = read_number(value, &watts) && !errno && n * 1000.0 < UINT32_MAX - 0.5;
        if (recognized) {
            m->power_w = watts;
            m->power_mw = (uint32_t)(n * 1000.0 + 0.5);
        }
    } else {
        uint32_t *number = NULL;
        if (!strcmp(parameter, "hashrate")) number = &m->hashrate_ghs;
        else if (!strcmp(parameter, "chipTemp")) number = &m->temperature_c;
        else if (!strcmp(parameter, "vrTemp")) number = &m->vr_temp_c;
        else if (!strcmp(parameter, "voltage")) number = &m->voltage_mv;
        else if (!strcmp(parameter, "current")) number = &m->current_ma;
        else if (!strcmp(parameter, "block_height")) number = &m->block_height;
        else if (!strcmp(parameter, "fan_speed")) number = &m->fan_rpm;
        // Supported if a future host emits it; current ESP-Miner does not.
        else if (!strcmp(parameter, "frequency")) number = &m->frequency_mhz;
        recognized = number && read_number(value, number);
    }
    if (recognized) {
        client->last_response_ms = now_ms;
        if (!client->connected) {
            snprintf(m->device_name, sizeof(m->device_name), "BAP connected");
        }
        client->connected = true;
        m->link = BONANZA_LINK_CONNECTED;
        client->update_pending = true;
    }
}

void bonanza_bap_client_receive(bonanza_bap_client_t *client,
                                const uint8_t *data, size_t size, uint32_t now_ms) {
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        if (c == '$') {
            client->sentence[0] = '$';
            client->sentence_length = 1;
        } else if (client->sentence_length) {
            if (c == '\n') {
                size_t n = client->sentence_length;
                if (client->sentence[n - 1] == '\r') --n;
                client->sentence[n] = '\0';
                handle_sentence(client, now_ms);
                bonanza_bap_client_reset_stream(client);
            } else if (client->sentence[client->sentence_length - 1] == '\r' ||
                       client->sentence_length + 1u >= sizeof(client->sentence) ||
                       (c != '\r' && (c < 0x20 || c > 0x7e))) {
                bonanza_bap_client_reset_stream(client);
            } else {
                client->sentence[client->sentence_length++] = (char)c;
            }
        }
    }
}

size_t bonanza_bap_client_next_message(bonanza_bap_client_t *client,
                                       uint32_t now_ms, char *out, size_t capacity) {
    if (client->connected && now_ms - client->last_response_ms >= RESPONSE_TIMEOUT_MS) {
        reset_metrics(client, "Waiting for BAP", BONANZA_LINK_WAITING);
    }
    if (!client->cycle_active && now_ms - client->last_cycle_ms >=
        (client->connected ? RENEW_MS : RETRY_MS)) {
        client->cycle_active = true;
        client->command_index = 0;
        client->next_send_ms = now_ms;
    }
    if ((int32_t)(now_ms - client->next_send_ms) < 0) return 0;

    size_t size = 0;
    if (client->cycle_active) {
        unsigned index = client->command_index;
        size = index == 0 ? bonanza_bap_format(out, capacity, "REQ", "systemInfo", NULL)
                         : bonanza_bap_format(out, capacity, "SUB", subscriptions[index - 1], "1000");
        if (size) {
            if (index == 0) client->last_probe_ms = now_ms;
            if (++client->command_index == COMMAND_COUNT) {
                client->cycle_active = false;
                client->last_cycle_ms = now_ms;
            }
        }
    } else if (client->connected && now_ms - client->last_probe_ms >= RETRY_MS) {
        // Subscriptions only send changed values. Probe identity for liveness,
        // so stable telemetry does not cause false disconnects.
        size = bonanza_bap_format(out, capacity, "REQ", "systemInfo", NULL);
        if (size) client->last_probe_ms = now_ms;
    }
    if (size) client->next_send_ms = now_ms + COMMAND_SPACING_MS;
    return size;
}

bool bonanza_bap_client_take_update(bonanza_bap_client_t *client,
                                     bonanza_metrics_t *metrics) {
    if (!metrics || !client->update_pending) return false;
    *metrics = client->metrics;
    client->update_pending = false;
    return true;
}
